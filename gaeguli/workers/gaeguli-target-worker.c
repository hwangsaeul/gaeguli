#include "config.h"
#include "gaeguli-internal.h"

#include <glib-unix.h>
#include <gio/gio.h>
#include <gaeguli/gaeguli.h>

static guint signal_watch_intr_id;

typedef struct _Msg
{
  GaeguliTargetMsgType type;
  GType adaptor_type;
  GaeguliVideoBitrateControl bitrate_control;
  guint bitrate;
  guint quantizer;
  gboolean adaptive_streaming;
} Msg;

typedef struct _WorkerMsg
{
  GaeguliTargetWorkerMsgType msg_type;
  gint srtsocket;
  GSocketAddress *address;
  GaeguliVideoBitrateControl bitrate_control;
  guint bitrate;
  guint quantizer;
  GaeguliSRTMode mode;
} WorkerMsg;

typedef struct _TargetWorker
{
  GstElement *pipeline;
  GstElement *srtsink;
  GstElement *encoder;
  GstElement *adaptor;
  GIOChannel *ch;
  GMainLoop *loop;
  GType adaptor_type;
  GaeguliVideoBitrateControl bitrate_control;
  GaeguliVideoCodec codec;
  guint bitrate;
  guint quantizer;
  guint node_id;
  guint idr_period;
  gboolean adaptive_streaming;
  GaeguliSRTMode mode;
  gint read_fd;
  gint write_fd;
} TargetWorker;

static gboolean
intr_handler (gpointer user_data)
{
  TargetWorker *worker = user_data;

  g_main_loop_quit (worker->loop);

  return G_SOURCE_REMOVE;
}

static WorkerMsg *
_build_message (GaeguliTargetWorkerMsgType type,
    gint srtsocket, GSocketAddress * address,
    GaeguliVideoBitrateControl bitrate_control, guint bitrate, guint quantizer,
    GaeguliSRTMode mode)
{
  WorkerMsg *msg = g_new0 (WorkerMsg, 1);

  msg->msg_type = type;
  msg->srtsocket = srtsocket;
  msg->address = address;
  msg->bitrate_control = bitrate_control;
  msg->bitrate = bitrate;
  msg->quantizer = quantizer;
  msg->mode = mode;

  return msg;
}

static gboolean
_post_message (TargetWorker * worker, WorkerMsg * msg)
{
  g_autoptr (GError) error = NULL;
  gsize size = sizeof (WorkerMsg);

  if (!worker->write_fd || !msg) {
    g_free (msg);
    return FALSE;
  }

  if (write (worker->write_fd, msg, size) < 0) {
    g_warning ("Failed to write");
  }
  g_free (msg);

  return TRUE;
}

static gboolean
_bus_watch (GstBus * bus, GstMessage * message, gpointer user_data)
{
  switch (message->type) {
    case GST_MESSAGE_WARNING:{
      gpointer target_id;
      g_autoptr (GError) error = NULL;

      if (!message->src) {
        break;
      }

      target_id = g_object_get_data (G_OBJECT (message->src),
          "gaeguli-target-id");

      gst_message_parse_warning (message, &error, NULL);
      if (error && error->domain == GST_RESOURCE_ERROR) {
        g_error ("Failed to connect target [id: %x]. Connection error: %s",
            GPOINTER_TO_UINT (target_id), error->message);
      }
      break;
    }

    default:
      break;
  }

  return G_SOURCE_CONTINUE;
}

static GstBusSyncReply
_bus_sync_srtsink_error_handler (GstBus * bus, GstMessage * message,
    gpointer user_data)
{
  switch (message->type) {
    case GST_MESSAGE_ERROR:{
      g_autoptr (GError) error = NULL;
      g_autofree gchar *debug = NULL;

      gst_message_parse_error (message, &error, &debug);
      if (g_error_matches (error, GST_RESOURCE_ERROR,
              GST_RESOURCE_ERROR_OPEN_WRITE)) {
        GError **error = user_data;

        g_clear_error (error);

        if (g_str_has_suffix (debug, "already listening on the same port")) {
          *error = g_error_new (GAEGULI_TRANSMIT_ERROR,
              GAEGULI_TRANSMIT_ERROR_ADDRINUSE, "Address already in use");
        } else {
          *error = g_error_new (GAEGULI_TRANSMIT_ERROR,
              GAEGULI_TRANSMIT_ERROR_FAILED, "Failed to open SRT socket");
        }
      }
      break;
    }
    default:
      break;
  }

  return GST_BUS_PASS;
}

static gint
_get_encoding_parameter_enum (GstElement * encoder, const gchar * param)
{
  gint result = 0;
  const gchar *encoder_type =
      gst_plugin_feature_get_name (gst_element_get_factory (encoder));

  if (g_str_equal (param, GAEGULI_ENCODING_PARAMETER_RATECTRL)) {
    if (g_str_equal (encoder_type, "x264enc")) {
      gint pass;

      g_object_get (encoder, "pass", &pass, NULL);
      switch (pass) {
        case 0:
          result = GAEGULI_VIDEO_BITRATE_CONTROL_CBR;
          break;
        case 4:
          result = GAEGULI_VIDEO_BITRATE_CONTROL_CQP;
          break;
        case 17:
        case 18:
        case 19:
          result = GAEGULI_VIDEO_BITRATE_CONTROL_VBR;
          break;
        default:
          g_warning ("Unknown x264enc pass %d", pass);
      }
    } else if (g_str_equal (encoder_type, "x265enc")) {
      gint qp;

      g_object_get (encoder, "qp", &qp, NULL);
      if (qp != -1) {
        result = GAEGULI_VIDEO_BITRATE_CONTROL_CQP;
      } else {
        const gchar *option_string;
        g_object_get (encoder, "option-string", &option_string, NULL);
        if (strstr (option_string, "strict-cbr=1")) {
          return GAEGULI_VIDEO_BITRATE_CONTROL_CBR;
        } else {
          return GAEGULI_VIDEO_BITRATE_CONTROL_VBR;
        }
      }
    } else if (g_str_equal (encoder_type, "vaapih264enc") ||
        g_str_equal (encoder_type, "vaapih265enc")) {
      gint rate_control;

      g_object_get (encoder, "rate-control", &rate_control, NULL);
      switch (rate_control) {
        case 1:
          result = GAEGULI_VIDEO_BITRATE_CONTROL_CQP;
          break;
        case 2:
          result = GAEGULI_VIDEO_BITRATE_CONTROL_CBR;
          break;
        case 4:
          result = GAEGULI_VIDEO_BITRATE_CONTROL_VBR;
          break;
        default:
          g_warning ("Unsupported vaapienc rate-control %d", rate_control);
      }
    } else if (g_str_equal (encoder_type, "omxh264enc") ||
        g_str_equal (encoder_type, "omxh265enc")) {
      gint control_rate;
      g_object_get (encoder, "control-rate", &control_rate, NULL);

      switch (control_rate) {
        case 1:
          return GAEGULI_VIDEO_BITRATE_CONTROL_VBR;
        default:
        case 2:
          return GAEGULI_VIDEO_BITRATE_CONTROL_CBR;
      }
    }
  } else {
    g_warning ("Unsupported parameter '%s'", param);
  }

  return result;
}

static guint
_ratectrl_to_pass (GaeguliVideoBitrateControl bitrate_control)
{
  switch (bitrate_control) {
    case GAEGULI_VIDEO_BITRATE_CONTROL_CQP:
      return 4;
    case GAEGULI_VIDEO_BITRATE_CONTROL_VBR:
      return 17;
    case GAEGULI_VIDEO_BITRATE_CONTROL_CBR:
    default:
      return 0;
  }
}

static void
_x264_update_in_ready_state (GstElement * encoder, GstStructure * params)
{
  const GValue *val;
  GaeguliVideoBitrateControl bitrate_control;

  val = gst_structure_get_value (params, GAEGULI_ENCODING_PARAMETER_QUANTIZER);
  if (val) {
    g_object_set_property (G_OBJECT (encoder), "quantizer", val);
  }
  if (gst_structure_get_enum (params, GAEGULI_ENCODING_PARAMETER_RATECTRL,
          GAEGULI_TYPE_VIDEO_BITRATE_CONTROL, (gint *) & bitrate_control)) {
    g_object_set (encoder, "pass", _ratectrl_to_pass (bitrate_control), NULL);
  }
}

static void
_x265_update_in_ready_state (GstElement * encoder, GstStructure * params)
{
  GaeguliVideoBitrateControl bitrate_control;

  bitrate_control = _get_encoding_parameter_enum (encoder,
      GAEGULI_ENCODING_PARAMETER_RATECTRL);
  gst_structure_get_enum (params, GAEGULI_ENCODING_PARAMETER_RATECTRL,
      GAEGULI_TYPE_VIDEO_BITRATE_CONTROL, (gint *) & bitrate_control);

  switch (bitrate_control) {
    case GAEGULI_VIDEO_BITRATE_CONTROL_CQP:{
      /* Sensible default in case quantizer isn't specified */
      guint qp = 23;

      g_object_get (encoder, "qp", &qp, NULL);
      gst_structure_get_uint (params, GAEGULI_ENCODING_PARAMETER_QUANTIZER,
          &qp);
      g_object_set (encoder, "option-string", "", "qp", qp, NULL);
      break;
    }
    case GAEGULI_VIDEO_BITRATE_CONTROL_VBR:
      g_object_set (encoder, "option-string", "", "qp", -1, NULL);
      break;
    case GAEGULI_VIDEO_BITRATE_CONTROL_CBR:
    default:{
      g_autofree gchar *option_str = NULL;
      guint bitrate;

      g_object_get (encoder, "bitrate", &bitrate, NULL);

      option_str = g_strdup_printf ("strict-cbr=1:vbv-bufsize=%d", bitrate);
      g_object_set (encoder, "option-string", option_str, "qp", -1, NULL);
    }
  }
}

static void
_vaapi_update_in_ready_state (GstElement * encoder, GstStructure * params)
{
  guint bitrate;
  const GValue *val;
  GaeguliVideoBitrateControl bitrate_control;

  if (gst_structure_get_uint (params, GAEGULI_ENCODING_PARAMETER_BITRATE,
          &bitrate)) {
    g_object_set (encoder, "bitrate", bitrate / 1000, NULL);
  }

  val = gst_structure_get_value (params, GAEGULI_ENCODING_PARAMETER_QUANTIZER);
  if (val) {
    g_object_set_property (G_OBJECT (encoder), "init-qp", val);
  }

  if (gst_structure_get_enum (params, GAEGULI_ENCODING_PARAMETER_RATECTRL,
          GAEGULI_TYPE_VIDEO_BITRATE_CONTROL, (gint *) & bitrate_control)) {
    gint value;

    switch (bitrate_control) {
      default:
      case GAEGULI_VIDEO_BITRATE_CONTROL_CBR:
        value = 2;
        break;
      case GAEGULI_VIDEO_BITRATE_CONTROL_VBR:
        value = 4;
        break;
      case GAEGULI_VIDEO_BITRATE_CONTROL_CQP:
        value = 1;
    }

    g_object_set (encoder, "rate-control", value, NULL);
  }
}

static void
_omx_update_in_ready_state (GstElement * encoder, GstStructure * params)
{
  GaeguliVideoBitrateControl bitrate_control;

  if (gst_structure_get_enum (params, GAEGULI_ENCODING_PARAMETER_RATECTRL,
          GAEGULI_TYPE_VIDEO_BITRATE_CONTROL, (gint *) & bitrate_control)) {
    gint value;

    switch (bitrate_control) {
      default:
      case GAEGULI_VIDEO_BITRATE_CONTROL_CBR:
        value = 2;
        break;
      case GAEGULI_VIDEO_BITRATE_CONTROL_VBR:
        value = 1;
        break;
    }

    g_object_set (encoder, "control-rate", value, NULL);
  }
}

typedef void (*ReadyStateCallback) (GstElement * encoder,
    GstStructure * params);

static GstPadProbeReturn
_do_in_ready_state (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstElement *encoder = GST_PAD_PARENT (GST_PAD_PEER (pad));
  GstStructure *params = user_data;
  ReadyStateCallback probe_cb;
  GstState cur_state;

  gst_structure_get (params, "probe-cb", G_TYPE_POINTER, &probe_cb, NULL);

  if (!probe_cb) {
    goto out;
  }

  gst_element_get_state (encoder, &cur_state, NULL, 0);
  if (cur_state > GST_STATE_READY) {
    gst_element_set_state (encoder, GST_STATE_READY);
  }

  probe_cb (encoder, params);

  if (cur_state > GST_STATE_READY) {
    gst_element_set_state (encoder, cur_state);
  }

out:
  return GST_PAD_PROBE_REMOVE;
}

static void
_set_encoding_parameters (GstElement * encoder, GstStructure * params)
{
  guint val;
  GaeguliVideoBitrateControl bitrate_control;
  gboolean must_go_to_ready_state = FALSE;
  ReadyStateCallback ready_state_cb = NULL;
  g_autofree gchar *params_str = NULL;

  const gchar *encoder_type =
      gst_plugin_feature_get_name (gst_element_get_factory (encoder));

  params_str = gst_structure_to_string (params);
  g_debug ("Changing encoding parameters to %s", params_str);

  if (g_str_equal (encoder_type, "x264enc")) {
    ready_state_cb = _x264_update_in_ready_state;

    if (gst_structure_get_uint (params, GAEGULI_ENCODING_PARAMETER_BITRATE,
            &val)) {
      g_object_set (encoder, "bitrate", val / 1000, NULL);
    }

    if (gst_structure_get_uint (params, GAEGULI_ENCODING_PARAMETER_QUANTIZER,
            &val)) {
      guint cur_quantizer;

      g_object_get (encoder, "quantizer", &cur_quantizer, NULL);

      if (val != cur_quantizer) {
        must_go_to_ready_state = TRUE;
      }
    }

    if (gst_structure_get_enum (params, GAEGULI_ENCODING_PARAMETER_RATECTRL,
            GAEGULI_TYPE_VIDEO_BITRATE_CONTROL, (gint *) & bitrate_control)) {
      gint cur_pass;

      g_object_get (encoder, "pass", &cur_pass, NULL);

      if (_ratectrl_to_pass (bitrate_control) != cur_pass) {
        must_go_to_ready_state = TRUE;
      }
    }
  } else if (g_str_equal (encoder_type, "x265enc")) {
    GaeguliVideoBitrateControl cur_bitrate_control;

    ready_state_cb = _x265_update_in_ready_state;

    cur_bitrate_control = _get_encoding_parameter_enum (encoder,
        GAEGULI_ENCODING_PARAMETER_RATECTRL);

    if (gst_structure_get_enum (params, GAEGULI_ENCODING_PARAMETER_RATECTRL,
            GAEGULI_TYPE_VIDEO_BITRATE_CONTROL, (gint *) & bitrate_control) &&
        bitrate_control != cur_bitrate_control) {
      must_go_to_ready_state = TRUE;
    }

    if (gst_structure_get_uint (params, GAEGULI_ENCODING_PARAMETER_BITRATE,
            &val)) {
      guint cur_bitrate;

      val /= 1000;

      g_object_get (encoder, "bitrate", &cur_bitrate, NULL);

      if (val != cur_bitrate) {
        g_object_set (encoder, "bitrate", val, NULL);

        if (cur_bitrate_control == GAEGULI_VIDEO_BITRATE_CONTROL_CBR) {
          must_go_to_ready_state = TRUE;
        }
      }
    }

    if (cur_bitrate_control == GAEGULI_VIDEO_BITRATE_CONTROL_CQP &&
        gst_structure_get_uint (params, GAEGULI_ENCODING_PARAMETER_QUANTIZER,
            &val)) {
      guint cur_quantizer;

      g_object_get (encoder, "qp", &cur_quantizer, NULL);

      if (val != cur_quantizer) {
        must_go_to_ready_state = TRUE;
      }
    }
  } else if (g_str_equal (encoder_type, "vaapih264enc") ||
      g_str_equal (encoder_type, "vaapih265enc")) {
    ready_state_cb = _vaapi_update_in_ready_state;
    must_go_to_ready_state = TRUE;
  } else if (g_str_equal (encoder_type, "omxh264enc") ||
      g_str_equal (encoder_type, "omxh265enc")) {
    ready_state_cb = _omx_update_in_ready_state;

    if (gst_structure_get_uint (params, GAEGULI_ENCODING_PARAMETER_BITRATE,
            &val)) {
      g_object_set (encoder, "bitrate", val, NULL);
    }

    if (gst_structure_get_enum (params, GAEGULI_ENCODING_PARAMETER_RATECTRL,
            GAEGULI_TYPE_VIDEO_BITRATE_CONTROL, (gint *) & bitrate_control)) {
      GaeguliVideoBitrateControl cur_bitrate_control;

      cur_bitrate_control = _get_encoding_parameter_enum (encoder,
          GAEGULI_ENCODING_PARAMETER_RATECTRL);

      if (bitrate_control != cur_bitrate_control) {
        must_go_to_ready_state = TRUE;
      }
    }
  } else {
    g_warning ("Unsupported encoder '%s'", encoder_type);
  }

  if (must_go_to_ready_state && ready_state_cb) {
    g_autoptr (GstPad) sinkpad = gst_element_get_static_pad (encoder, "sink");

    GstStructure *probe_data = gst_structure_copy (params);

    gst_structure_set (probe_data, "probe-cb", G_TYPE_POINTER, ready_state_cb,
        NULL);

    gst_pad_add_probe (GST_PAD_PEER (sinkpad), GST_PAD_PROBE_TYPE_BLOCK,
        _do_in_ready_state, probe_data, (GDestroyNotify) gst_structure_free);
  }
}

static void
_update_baseline_parameters (TargetWorker * worker)
{
  g_autoptr (GstStructure) params = NULL;

  if (!worker->encoder) {
    /* We're not initialized yet. */
    return;
  }

  params = gst_structure_new ("application/x-gaeguli-encoding-parameters",
      GAEGULI_ENCODING_PARAMETER_RATECTRL,
      GAEGULI_TYPE_VIDEO_BITRATE_CONTROL, worker->bitrate_control,
      GAEGULI_ENCODING_PARAMETER_BITRATE, G_TYPE_UINT, worker->bitrate,
      GAEGULI_ENCODING_PARAMETER_QUANTIZER, G_TYPE_UINT, worker->quantizer,
      NULL);

  if (worker->adaptor) {
    g_object_set (worker->adaptor, "baseline-parameters", params, NULL);
  }

  _set_encoding_parameters (worker->encoder, params);
}

static gchar *
_target_create_streamid (gchar * username, gint32 buffer_size)
{
  GString *str = g_string_new (NULL);

  if (username) {
    g_string_append_printf (str, "u=%s", username);
  }

  if (buffer_size > 0) {
    g_string_append_printf (str, "%sh8l_bufsize=%d",
        (str->len > 0) ? "," : "", buffer_size);
  }

  if (str->len > 0) {
    g_string_prepend (str, "#!::");
  }

  return g_string_free (str, FALSE);
}

void
free_resources (TargetWorker * worker)
{
  if (!worker) {
    return;
  }

  if (worker->write_fd > 0) {
    close (worker->write_fd);
    worker->write_fd = 0;
  }

  if (worker->ch) {
    g_io_channel_shutdown (worker->ch, TRUE, NULL);
  }

  if (worker->loop) {
    g_main_loop_unref (worker->loop);
  }

  if (worker->srtsink) {
    gst_object_unref (worker->srtsink);
  }

  if (worker->encoder) {
    gst_object_unref (worker->encoder);
  }

  if (worker->adaptor) {
    gst_object_unref (worker->adaptor);
  }

  if (worker->pipeline) {
    gst_object_unref (worker->pipeline);
  }

  g_free (worker);
}

static void
process_msg (TargetWorker * worker, Msg * msg)
{
  if (!worker || !msg) {
    return;
  }

  switch (msg->type) {
    default:
      break;

    case GAEGULI_SET_TARGET_STOP_MSG:{
      g_main_loop_quit (worker->loop);
    }
      break;

    case GAEGULI_SET_TARGET_BITRATE_MSG:{
      worker->bitrate = msg->bitrate;
      _update_baseline_parameters (worker);
    }
      break;

    case GAEGULI_SET_TARGET_BITRATE_CONTROL_MSG:{
      worker->bitrate_control = msg->bitrate_control;
      _update_baseline_parameters (worker);
    }
      break;

    case GAEGULI_SET_TARGET_QUANTIZER_MSG:{
      worker->quantizer = msg->quantizer;
      _update_baseline_parameters (worker);
    }
      break;

    case GAEGULI_SET_TARGET_ADAPTOR_TYPE_MSG:{
      worker->adaptor_type = msg->adaptor_type;
      worker->adaptor =
          g_object_new (worker->adaptor_type, "srtsink", worker->srtsink,
          "enabled", worker->adaptive_streaming, NULL);
      g_signal_connect_swapped (worker->adaptor, "encoding-parameters",
          (GCallback) _set_encoding_parameters, worker->encoder);
    }
      break;

    case GAEGULI_SET_TARGET_ADAPTIVE_STREAMING_MSG:{
      worker->adaptive_streaming = msg->adaptive_streaming;
      if (worker->adaptor) {
        g_object_set (worker->adaptor, "enabled", worker->adaptive_streaming,
            NULL);
      }
    }
      break;
  }
}

static gboolean
cb_read_watch (GIOChannel * channel, GIOCondition cond, gpointer data)
{
  Msg msg;
  gsize size = sizeof (Msg);
  TargetWorker *worker = data;

  if ((cond == G_IO_HUP) || (cond == G_IO_ERR)) {
    g_io_channel_unref (channel);
    return FALSE;
  }

  if (read (worker->read_fd, (void *) &msg, size) > 0) {
    process_msg (worker, &msg);
  }

  return TRUE;
}

typedef struct
{
  TargetWorker *worker;
  GaeguliTargetWorkerMsgType msg_type;
} NotifyData;

static void
_notify_encoder_change (NotifyData * data)
{
  if (!_post_message (data->worker, _build_message (data->msg_type, 0, NULL,
              data->worker->bitrate_control, data->worker->bitrate,
              data->worker->quantizer, data->worker->mode))) {
    g_warning ("Failed to post message");
  }
}

static void
_target_on_caller_added (TargetWorker * worker, gint srtsocket,
    GSocketAddress * address)
{
  if (!_post_message (worker, _build_message (GAEGULI_TARGET_CALLER_ADDED_MSG,
              srtsocket, address, worker->bitrate_control, worker->bitrate,
              worker->quantizer, worker->mode))) {
    g_warning ("Failed to post message");
  }
}

static void
_target_on_caller_removed (TargetWorker * worker, gint srtsocket,
    GSocketAddress * address)
{
  if (!_post_message (worker, _build_message (GAEGULI_TARGET_CALLER_REMOVED_MSG,
              srtsocket, address, worker->bitrate_control, worker->bitrate,
              worker->quantizer, worker->mode))) {
    g_warning ("Failed to post message");
  }
}

typedef gchar *(*PipelineFormatFunc) (const gchar * pipeline_str,
    guint idr_period, guint node_id);

struct encoding_method_params
{
  const gchar *pipeline_str;
  GaeguliVideoCodec codec;
  PipelineFormatFunc format_func;
};

static gchar *
_format_general_pipeline (const gchar * pipeline_str, guint idr_period,
    guint node_id)
{
  return g_strdup_printf (pipeline_str, node_id, idr_period);
}

static gchar *
_format_tx1_pipeline (const gchar * pipeline_str, guint idr_period,
    guint node_id)
{
  return g_strdup_printf (pipeline_str, node_id, idr_period);
}

static struct encoding_method_params enc_params[] = {
  {GAEGULI_PIPELINE_GENERAL_H264ENC_STR, GAEGULI_VIDEO_CODEC_H264_X264,
      _format_general_pipeline},
  {GAEGULI_PIPELINE_GENERAL_H265ENC_STR, GAEGULI_VIDEO_CODEC_H265_X265,
      _format_general_pipeline},
  {GAEGULI_PIPELINE_VAAPI_H264_STR, GAEGULI_VIDEO_CODEC_H264_VAAPI,
      _format_general_pipeline},
  {GAEGULI_PIPELINE_VAAPI_H265_STR, GAEGULI_VIDEO_CODEC_H265_VAAPI,
      _format_general_pipeline},
  {GAEGULI_PIPELINE_NVIDIA_TX1_H264ENC_STR, GAEGULI_VIDEO_CODEC_H264_OMX,
      _format_tx1_pipeline},
  {GAEGULI_PIPELINE_NVIDIA_TX1_H265ENC_STR, GAEGULI_VIDEO_CODEC_H265_OMX,
      _format_tx1_pipeline},
  {NULL, 0, 0},
};

static gchar *
_get_enc_string (GaeguliVideoCodec codec, guint idr_period, guint node_id)
{
  struct encoding_method_params *params = enc_params;

  for (; params->pipeline_str != NULL; params++) {
    if (params->codec == codec)
      return params->format_func (params->pipeline_str, idr_period, node_id);
  }

  return NULL;
}

int
main (int argc, char *argv[])
{
  TargetWorker *worker = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GstBus) bus = NULL;
  g_autoptr (GstBus) read_bus = NULL;
  g_autoptr (GstElement) srtsink = NULL;
  g_autoptr (GstElement) enc_first = NULL;
  g_autoptr (GstElement) muxsink_first = NULL;
  g_autoptr (GIOChannel) read_ch = NULL;
  g_autofree gchar *pipeline_str = NULL;
  g_autofree gchar *srt_uri = NULL;
  g_autofree gchar *peer_address = NULL;
  g_autofree gchar *username = NULL;
  g_autofree gchar *passphrase = NULL;
  g_autofree gchar *streamid = NULL;
  NotifyData *notify_data;
  GaeguliSRTMode mode = GAEGULI_SRT_MODE_UNKNOWN;
  GaeguliSRTKeyLength pbkeylen = GAEGULI_SRT_KEY_LENGTH_0;
  GstStateChangeReturn res = GST_FLOW_OK;
  gboolean is_recording = FALSE;
  gint32 buffer_size = 0;
  gint ret = 0;
  guint id = 0;

  worker = g_new0 (TargetWorker, 1);

  id = atol (argv[1]);
  is_recording = atoi (argv[2]);
  srt_uri = g_strdup (argv[3]);
  mode = atoi (argv[4]);
  buffer_size = atoi (argv[5]);
  pbkeylen = atoi (argv[6]);
  worker->read_fd = atoi (argv[7]);
  /* Close the write side of the pipe: Target -> Worker */
  close (atoi (argv[8]));
  /* Close the read side of the pipe: Worker -> Target */
  close (atoi (argv[9]));
  worker->write_fd = atoi (argv[10]);
  worker->bitrate_control = atoi (argv[11]);
  worker->bitrate = atoi (argv[12]);
  worker->quantizer = atoi (argv[13]);
  worker->codec = atoi (argv[14]);
  worker->idr_period = atoi (argv[15]);
  worker->node_id = atoi (argv[16]);

  if (fcntl (worker->read_fd, F_SETFL, O_NONBLOCK) < 0) {
    g_error ("failed to set non blocking flag on read fd");
    ret = -1;
    goto failed;
  }

  worker->ch = g_io_channel_unix_new (worker->read_fd);
  g_io_channel_set_flags (worker->ch, G_IO_FLAG_NONBLOCK, NULL);
  g_io_channel_set_encoding (worker->ch, NULL, NULL);
  g_io_channel_set_buffered (worker->ch, FALSE);

  /* Add watches to read channel */
  g_io_add_watch (worker->ch, G_IO_IN | G_IO_ERR | G_IO_HUP,
      (GIOFunc) cb_read_watch, worker);

  /* arg list is prepared with the provided values till NULL is encountered.
   * If username & passphrase are NULL then argc would be less than expected.
   * In this case, argv[17] and argv[18] will be invalid. */
  if (argc > (GAEGULI_TARGET_WORKER_ARGS_NUM - 2)) {
    username = g_strdup (argv[17]);
    passphrase = g_strdup (argv[18]);
  }

  pipeline_str =
      _get_enc_string (worker->codec, worker->idr_period, worker->node_id);
  if (pipeline_str == NULL) {
    g_set_error (&error, GAEGULI_RESOURCE_ERROR,
        GAEGULI_RESOURCE_ERROR_UNSUPPORTED, "Can't determine encoding method");
    ret = -1;
    goto failed;
  }

  if (!is_recording) {
    pipeline_str = g_strdup_printf ("%s ! " GAEGULI_PIPELINE_MUXSINK_STR,
        pipeline_str, srt_uri);
  } else {
    pipeline_str = g_strdup_printf ("%s ! " GAEGULI_RECORD_PIPELINE_MUXSINK_STR,
        pipeline_str, srt_uri);
  }

  worker->loop = g_main_loop_new (NULL, FALSE);

  signal_watch_intr_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, worker);

  /* Initialise the gst env */
  gst_init (&argc, &argv);

  g_debug ("using encoding pipeline [%s]", pipeline_str);

  worker->pipeline = gst_parse_launch (pipeline_str, &error);
  if (error) {
    g_warning ("failed to build muxsink pipeline (%s)", error->message);
    ret = -1;
    goto failed;
  }

  gst_object_ref_sink (worker->pipeline);

  muxsink_first =
      gst_bin_get_by_name (GST_BIN (worker->pipeline), "muxsink_first");
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (muxsink_first),
          "pcr-interval")) {
    g_info ("set pcr-interval to 360");
    g_object_set (G_OBJECT (muxsink_first), "pcr-interval", 360, NULL);
  }

  if (!is_recording) {
    worker->srtsink = gst_bin_get_by_name (GST_BIN (worker->pipeline), "sink");
    g_object_set_data (G_OBJECT (worker->srtsink), "gaeguli-target-id",
        GUINT_TO_POINTER (id));
    g_object_set_data (G_OBJECT (worker->srtsink), "gaeguli-target-id",
        GUINT_TO_POINTER (id));
    g_signal_connect_swapped (worker->srtsink, "caller-added",
        G_CALLBACK (_target_on_caller_added), worker);
    g_signal_connect_swapped (worker->srtsink, "caller-removed",
        G_CALLBACK (_target_on_caller_removed), worker);

    g_object_get (worker->srtsink, "mode", &worker->mode, NULL);

    /* Post mode to the Parent */
    if (!_post_message (worker, _build_message (GAEGULI_TARGET_SRT_MODE_MSG, 0,
                NULL, worker->bitrate_control, worker->bitrate,
                worker->quantizer, worker->mode))) {
      g_warning ("Failed to post message");
    }

    if (mode == GAEGULI_SRT_MODE_CALLER) {
      g_autoptr (GstUri) uri = gst_uri_from_string (srt_uri);

      peer_address = g_strdup (gst_uri_get_host (uri));
    }
  } else {
    worker->srtsink =
        gst_bin_get_by_name (GST_BIN (worker->pipeline), "recsink");
  }

  worker->encoder = gst_bin_get_by_name (GST_BIN (worker->pipeline), "enc");

  notify_data = g_new (NotifyData, 1);
  notify_data->worker = worker;
  notify_data->msg_type = GAEGULI_TARGET_NOTIFY_ENCODER_BITRATE_CHANGE_MSG;
  g_signal_connect_closure (worker->encoder, "notify::bitrate",
      g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
          (GClosureNotify) g_free), FALSE);

  notify_data = g_new (NotifyData, 1);
  notify_data->worker = worker;
  notify_data->msg_type = GAEGULI_TARGET_NOTIFY_ENCODER_QUANTIZER_CHANGE_MSG;
  g_signal_connect_closure (worker->encoder, "notify::quantizer",
      g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
          (GClosureNotify) g_free), FALSE);

  /* vaapienc */
  g_signal_connect_closure (worker->encoder, "notify::init-qp",
      g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
          NULL), FALSE);

  notify_data = g_new (NotifyData, 1);
  notify_data->worker = worker;
  notify_data->msg_type =
      GAEGULI_TARGET_NOTIFY_ENCODER_BITRATE_CONTROL_CHANGE_MSG;
  /* x264enc */
  g_signal_connect_closure (worker->encoder, "notify::pass",
      g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
          (GClosureNotify) g_free), FALSE);

  /* x265enc */
  g_signal_connect_closure (worker->encoder, "notify::qp",
      g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
          NULL), FALSE);
  g_signal_connect_closure (worker->encoder, "notify::option-string",
      g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
          NULL), FALSE);

  /* vaapienc */
  g_signal_connect_closure (worker->encoder, "notify::rate-control",
      g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
          NULL), FALSE);

  if (!is_recording) {
    /* Changing srtsink URI must happen first because it will clear parameters
     * like streamid. */
    if (buffer_size > 0) {
      g_autoptr (GstUri) uri = NULL;
      g_autofree gchar *uri_str = NULL;
      g_autofree gchar *buffer_size_str = NULL;

      buffer_size_str = g_strdup_printf ("%d", buffer_size);

      g_object_get (worker->srtsink, "uri", &uri_str, NULL);
      uri = gst_uri_from_string (uri_str);
      g_clear_pointer (&uri_str, g_free);

      gst_uri_set_query_value (uri, "sndbuf", buffer_size_str);

      uri_str = gst_uri_to_string (uri);
      g_object_set (worker->srtsink, "uri", uri_str, NULL);
    }

    switch (pbkeylen) {
      default:
      case GAEGULI_SRT_KEY_LENGTH_0:
        pbkeylen = 0;
        break;
      case GAEGULI_SRT_KEY_LENGTH_16:
        pbkeylen = 16;
        break;
      case GAEGULI_SRT_KEY_LENGTH_24:
        pbkeylen = 24;
        break;
      case GAEGULI_SRT_KEY_LENGTH_32:
        pbkeylen = 32;
        break;
    }

    streamid = _target_create_streamid (username, buffer_size);

    g_object_set (worker->srtsink, "passphrase", passphrase, "pbkeylen",
        pbkeylen, "streamid", streamid, NULL);

    _update_baseline_parameters (worker);

    bus = gst_element_get_bus (worker->pipeline);
    gst_bus_add_watch (bus, _bus_watch, worker);

    gst_bus_set_sync_handler (bus, _bus_sync_srtsink_error_handler,
        &error, NULL);
    /* Setting READY state on srtsink check that we can bind to address and port
     * specified in srt_uri. On failure, bus handler should set internal_err. */
    res = gst_element_set_state (worker->srtsink, GST_STATE_READY);

    gst_bus_set_sync_handler (bus, NULL, NULL, NULL);
  } else {
    res = gst_element_set_state (worker->srtsink, GST_STATE_READY);
    if (res == GST_STATE_CHANGE_FAILURE) {
      g_warning ("failed to set READY state on sink. (%s)", error->message);
      ret = -1;
      goto failed;
    }
  }

  if (res == GST_STATE_CHANGE_FAILURE) {
    g_warning ("failed to set READY state on sink");
    ret = -1;
    goto failed;
  }

  g_debug ("Setting PLAYING on target");
  gst_element_set_state (worker->pipeline, GST_STATE_PLAYING);

  g_main_loop_run (worker->loop);

  g_debug ("Setting NULL on target");
  gst_element_set_state (worker->pipeline, GST_STATE_NULL);

failed:

  free_resources (worker);
  return ret;
}
