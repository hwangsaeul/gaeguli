/**
 *  Copyright 2019-2020 SK Telecom Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *            Jakub Adam <jakub.adam@collabora.com>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "config.h"

/* GValueArray is deprecated since GLib 2.32 but srtsink returns it in "stats"
 * structure. */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "target.h"

#include "enumtypes.h"
#include "gaeguli-internal.h"
#include "pipeline.h"
#include "adaptors/nulladaptor.h"

#include <gio/gio.h>

#include <syslog.h>

struct _GaeguliTarget
{
  GObject parent;

  guint id;
  GstElement *pipeline;

  GstElement *snapshot_valve;
  GstElement *snapshot_jpegenc;
  GstElement *snapshot_jifmux;
  GQueue *snapshot_tasks;
  guint num_snapshots_to_encode;
  guint snapshot_quality;
  GaeguliIDCTMethod snapshot_idct_method;

  GMutex lock;

  GaeguliTargetState state;

  GstElement *encoder;
  GstElement *srtsink;
  GstPad *sinkpad;
  GaeguliStreamAdaptor *adaptor;

  GaeguliVideoCodec codec;
  GaeguliVideoBitrateControl bitrate_control;
  guint bitrate;
  guint quantizer;
  guint idr_period;
  guint node_id;
  gint client_fd;
  gchar *uri;
  gchar *peer_address;
  gchar *username;
  gchar *passphrase;
  GaeguliSRTKeyLength pbkeylen;
  GType adaptor_type;
  gboolean adaptive_streaming;
  GaeguliTargetType target_type;
  gint32 buffer_size;
  GstStructure *video_params;
  gchar *location;
};

enum
{
  PROP_ID = 1,
  PROP_TARGET_TYPE,
  PROP_NODE_ID,
  PROP_CODEC,
  PROP_BITRATE_CONTROL,
  PROP_BITRATE_CONTROL_ACTUAL,
  PROP_BITRATE,
  PROP_BITRATE_ACTUAL,
  PROP_QUANTIZER,
  PROP_QUANTIZER_ACTUAL,
  PROP_IDR_PERIOD,
  PROP_URI,
  PROP_USERNAME,
  PROP_PASSPHRASE,
  PROP_PBKEYLEN,
  PROP_ADAPTOR_TYPE,
  PROP_ADAPTIVE_STREAMING,
  PROP_BUFFER_SIZE,
  PROP_LATENCY,
  PROP_VIDEO_PARAMS,
  PROP_LOCATION,
  PROP_SNAPSHOT_QUALITY,
  PROP_SNAPSHOT_IDCT_METHOD,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST] = { 0 };

enum
{
  SIG_STREAM_STARTED,
  SIG_STREAM_STOPPED,
  SIG_CALLER_ADDED,
  SIG_CALLER_REMOVED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void gaeguli_target_initable_iface_init (GInitableIface * iface);

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_CODE (GaeguliTarget, gaeguli_target, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                gaeguli_target_initable_iface_init))
/* *INDENT-ON* */

static void
gaeguli_target_init (GaeguliTarget * self)
{
  self->state = GAEGULI_TARGET_STATE_NEW;
  self->adaptor_type = GAEGULI_TYPE_NULL_STREAM_ADAPTOR;

  self->snapshot_tasks = g_queue_new ();
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

static guint
_get_encoding_parameter_uint (GstElement * encoder, const gchar * param)
{
  guint result = 0;

  const gchar *encoder_type =
      gst_plugin_feature_get_name (gst_element_get_factory (encoder));

  if (g_str_equal (param, GAEGULI_ENCODING_PARAMETER_BITRATE)) {
    if (g_str_equal (encoder_type, "x264enc") ||
        g_str_equal (encoder_type, "x265enc") ||
        g_str_equal (encoder_type, "vaapih264enc") ||
        g_str_equal (encoder_type, "vaapih265enc")) {
      g_object_get (encoder, "bitrate", &result, NULL);
      result *= 1000;
    } else if (g_str_equal (encoder_type, "omxh264enc") ||
        g_str_equal (encoder_type, "omxh265enc")) {
      g_object_get (encoder, "target-bitrate", &result, NULL);
    }
  } else if (g_str_equal (param, GAEGULI_ENCODING_PARAMETER_QUANTIZER)) {
    if (g_str_equal (encoder_type, "x264enc")) {
      g_object_get (encoder, "quantizer", &result, NULL);
    } else if (g_str_equal (encoder_type, "x265enc")) {
      g_object_get (encoder, "qp", &result, NULL);
    } else if (g_str_equal (encoder_type, "vaapih264enc") ||
        g_str_equal (encoder_type, "vaapih265enc")) {
      g_object_get (encoder, "init-qp", &result, NULL);
    }
  } else {
    g_warning ("Unsupported parameter '%s'", param);
  }

  return result;
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
gaeguli_target_update_baseline_parameters (GaeguliTarget * self,
    gboolean force_on_encoder)
{
  g_autoptr (GstStructure) params = NULL;

  if (!self->encoder) {
    /* We're not initialized yet. */
    return;
  }

  params = gst_structure_new ("application/x-gaeguli-encoding-parameters",
      GAEGULI_ENCODING_PARAMETER_RATECTRL,
      GAEGULI_TYPE_VIDEO_BITRATE_CONTROL, self->bitrate_control,
      GAEGULI_ENCODING_PARAMETER_BITRATE, G_TYPE_UINT, self->bitrate,
      GAEGULI_ENCODING_PARAMETER_QUANTIZER, G_TYPE_UINT, self->quantizer, NULL);

  g_object_set (self, "video-params", params, NULL);

  if (self->adaptor) {
    g_object_set (self->adaptor, "baseline-parameters", params, NULL);
  }

  if (!gaeguli_stream_adaptor_is_enabled (self->adaptor) || force_on_encoder) {
    /* Apply directly on the encoder */
    _set_encoding_parameters (self->encoder, params);
  }
}

typedef struct
{
  GObject *target;
  GParamSpec *pspec;
} NotifyData;

static void
_notify_encoder_change (NotifyData * data)
{
  g_object_notify_by_pspec (data->target, data->pspec);
}

static void
gaeguli_target_on_caller_added (GaeguliTarget * self, gint srtsocket,
    GSocketAddress * address)
{
  g_signal_emit (self, signals[SIG_CALLER_ADDED], 0, srtsocket, address);
}

static void
gaeguli_target_on_caller_removed (GaeguliTarget * self, gint srtsocket,
    GSocketAddress * address)
{
  g_signal_emit (self, signals[SIG_CALLER_REMOVED], 0, srtsocket, address);
}

static void
gaeguli_target_set_snapshot_tags (GaeguliTarget * self, GVariant * tags)
{
  GVariantIter it;
  gchar *tag_name;
  GVariant *val_variant;
  GstTagSetter *tag_setter = GST_TAG_SETTER (self->snapshot_jifmux);

  g_return_if_fail (g_variant_is_of_type (tags, G_VARIANT_TYPE_VARDICT));

  gst_tag_setter_reset_tags (tag_setter);

  g_variant_iter_init (&it, tags);
  while (g_variant_iter_next (&it, "{sv}", &tag_name, &val_variant)) {
    GValue val = G_VALUE_INIT;

    if (!gst_tag_exists (tag_name)) {
      g_warning ("Unknown tag %s", tag_name);
      goto next;
    }

    g_dbus_gvariant_to_gvalue (val_variant, &val);
    gst_tag_setter_add_tag_value (tag_setter, GST_TAG_MERGE_REPLACE, tag_name,
        &val);
    g_value_unset (&val);

  next:
    g_free (tag_name);
    g_variant_unref (val_variant);
  }
}

static GstPadProbeReturn
_on_valve_buffer (GstPad * pad, GstPadProbeInfo * info, GaeguliTarget * self)
{
  GTask *task;

  /* FIXME - Check whether locking is required? */
  task = g_queue_peek_head (self->snapshot_tasks);
  if (task) {
    GVariant *tags = g_task_get_task_data (task);
    if (tags) {
      gaeguli_target_set_snapshot_tags (self, tags);
    }
  }

  if (--self->num_snapshots_to_encode == 0) {
    /* No pending snapshot requests, close the valve. */
    g_object_set (GST_PAD_PARENT (pad), "drop", TRUE, NULL);
  }

  return GST_PAD_PROBE_OK;
}

static void
gaeguli_target_create_snapshot (GaeguliTarget * self, GstBuffer * buffer)
{
  g_autoptr (GTask) task = NULL;
  GstMapInfo info;

  g_return_if_fail (!g_queue_is_empty (self->snapshot_tasks));

  {
    /* FIXME - Check whether locking is required? */
    task = g_queue_pop_head (self->snapshot_tasks);
  }

  if (g_task_return_error_if_cancelled (task)) {
    return;
  }

  gst_buffer_map (buffer, &info, GST_MAP_READ);

  g_task_return_pointer (task, g_bytes_new (info.data, info.size),
      (GDestroyNotify) g_bytes_unref);

  gst_buffer_unmap (buffer, &info);
}

GBytes *
gaeguli_target_create_snapshot_finish (GaeguliTarget * self,
    GAsyncResult * result, GError ** error)
{
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static gboolean
gaeguli_target_initable_init (GInitable * initable, GCancellable * cancellable,
    GError ** error)
{
  GaeguliTarget *self = GAEGULI_TARGET (initable);

  g_autoptr (GaeguliPipeline) owner = NULL;
  g_autoptr (GstElement) enc_first = NULL;
  g_autoptr (GstElement) muxsink_first = NULL;
  g_autoptr (GstPad) enc_sinkpad = NULL;
  g_autofree gchar *pipeline_str = NULL;
  g_autoptr (GError) internal_err = NULL;
  NotifyData *notify_data;

  if ((GAEGULI_TARGET_TYPE_SRT == self->target_type) ||
      (GAEGULI_TARGET_TYPE_RECORDING == self->target_type)) {
    pipeline_str =
        _get_enc_string (self->codec, self->idr_period, self->node_id);
    if (pipeline_str == NULL) {
      g_set_error (error, GAEGULI_RESOURCE_ERROR,
          GAEGULI_RESOURCE_ERROR_UNSUPPORTED,
          "Can't determine encoding method");
      return FALSE;
    }
    g_debug ("using encoding pipeline [%s]", pipeline_str);
  }

  if (GAEGULI_TARGET_TYPE_SRT == self->target_type) {
    pipeline_str = g_strdup_printf ("%s ! " GAEGULI_PIPELINE_MUXSINK_STR,
        pipeline_str, self->uri);
  } else if (GAEGULI_TARGET_TYPE_RECORDING == self->target_type) {
    pipeline_str = g_strdup_printf ("%s ! " GAEGULI_RECORD_PIPELINE_MUXSINK_STR,
        pipeline_str, self->location);
  } else if (GAEGULI_TARGET_TYPE_IMAGE_CAPTURE == self->target_type) {
    pipeline_str = g_strdup_printf (GAEGULI_PIPELINE_IMAGE_STR, self->node_id);
  }

  syslog (LOG_INFO, "Encoding pipeline [%s]", pipeline_str);
  self->pipeline = gst_parse_launch (pipeline_str, &internal_err);
  if (internal_err) {
    g_warning ("failed to build muxsink pipeline (%s)", internal_err->message);
    syslog (LOG_ERR, "Failed to build muxsink pipeline. Error [%s]",
        internal_err->message);
    goto failed;
  }

  gst_object_ref_sink (self->pipeline);

  if ((GAEGULI_TARGET_TYPE_SRT == self->target_type) ||
      (GAEGULI_TARGET_TYPE_RECORDING == self->target_type)) {
    muxsink_first =
        gst_bin_get_by_name (GST_BIN (self->pipeline), "muxsink_first");
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (muxsink_first),
            "pcr-interval")) {
      g_info ("set pcr-interval to 360");
      g_object_set (G_OBJECT (muxsink_first), "pcr-interval", 360, NULL);
    }
  }

  if (GAEGULI_TARGET_TYPE_SRT == self->target_type) {
    self->srtsink = gst_bin_get_by_name (GST_BIN (self->pipeline), "sink");
    g_object_set_data (G_OBJECT (self->srtsink), "gaeguli-target-id",
        GUINT_TO_POINTER (self->id));
    g_signal_connect_swapped (self->srtsink, "caller-added",
        G_CALLBACK (gaeguli_target_on_caller_added), self);
    g_signal_connect_swapped (self->srtsink, "caller-removed",
        G_CALLBACK (gaeguli_target_on_caller_removed), self);
    if (gaeguli_target_get_srt_mode (self) == GAEGULI_SRT_MODE_CALLER) {
      g_autoptr (GstUri) uri = gst_uri_from_string (self->uri);

      self->peer_address = g_strdup (gst_uri_get_host (uri));
    }
  } else if (GAEGULI_TARGET_TYPE_RECORDING == self->target_type) {
    self->srtsink = gst_bin_get_by_name (GST_BIN (self->pipeline), "recsink");
  } else if (GAEGULI_TARGET_TYPE_IMAGE_CAPTURE == self->target_type) {
    /* Handle image capture type */
    g_autoptr (GstPad) valve_src = NULL;
    g_autoptr (GstElement) fakesink = NULL;

    self->snapshot_valve = gst_bin_get_by_name (GST_BIN (self->pipeline),
        "valve");
    valve_src = gst_element_get_static_pad (self->snapshot_valve, "src");
    gst_pad_add_probe (valve_src, GST_PAD_PROBE_TYPE_BUFFER,
        (GstPadProbeCallback) _on_valve_buffer, self, NULL);

    self->snapshot_jpegenc = gst_bin_get_by_name (GST_BIN (self->pipeline),
        "jpegenc");
    g_object_set (self->snapshot_jpegenc, "quality", self->snapshot_quality,
        "idct-method", self->snapshot_idct_method, NULL);

    self->snapshot_jifmux = gst_bin_get_by_name (GST_BIN (self->pipeline),
        "jifmux");

    fakesink = gst_bin_get_by_name (GST_BIN (self->pipeline), "fakesink");
    g_object_set (fakesink, "signal-handoffs", TRUE, NULL);
    g_signal_connect_swapped (fakesink, "handoff",
        G_CALLBACK (gaeguli_target_create_snapshot), self);
  }

  if ((GAEGULI_TARGET_TYPE_SRT == self->target_type) ||
      (GAEGULI_TARGET_TYPE_RECORDING == self->target_type)) {
    self->encoder = gst_bin_get_by_name (GST_BIN (self->pipeline), "enc");

    notify_data = g_new (NotifyData, 1);
    notify_data->target = G_OBJECT (self);
    notify_data->pspec = properties[PROP_BITRATE_ACTUAL];
    g_signal_connect_closure (self->encoder, "notify::bitrate",
        g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
            (GClosureNotify) g_free), FALSE);

    notify_data = g_new (NotifyData, 1);
    notify_data->target = G_OBJECT (self);
    notify_data->pspec = properties[PROP_QUANTIZER_ACTUAL];
    g_signal_connect_closure (self->encoder, "notify::quantizer",
        g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
            (GClosureNotify) g_free), FALSE);
    /* vaapienc */
    g_signal_connect_closure (self->encoder, "notify::init-qp",
        g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
            NULL), FALSE);

    notify_data = g_new (NotifyData, 1);
    notify_data->target = G_OBJECT (self);
    notify_data->pspec = properties[PROP_BITRATE_CONTROL_ACTUAL];
    /* x264enc */
    g_signal_connect_closure (self->encoder, "notify::pass",
        g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
            (GClosureNotify) g_free), FALSE);
    /* x265enc */
    g_signal_connect_closure (self->encoder, "notify::qp",
        g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
            NULL), FALSE);
    g_signal_connect_closure (self->encoder, "notify::option-string",
        g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
            NULL), FALSE);
    /* vaapienc */
    g_signal_connect_closure (self->encoder, "notify::rate-control",
        g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
            NULL), FALSE);
  }

  return TRUE;

failed:
  if (internal_err) {
    g_propagate_error (error, internal_err);
    internal_err = NULL;
  }

  return FALSE;
}

static void
gaeguli_target_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GaeguliTarget *self = GAEGULI_TARGET (object);

  switch (prop_id) {
    case PROP_ID:
      g_value_set_uint (value, self->id);
      break;
    case PROP_BITRATE_CONTROL:
      g_value_set_enum (value, self->bitrate_control);
      break;
    case PROP_BITRATE_CONTROL_ACTUAL:
      g_value_set_enum (value, _get_encoding_parameter_enum (self->encoder,
              GAEGULI_ENCODING_PARAMETER_RATECTRL));
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;
    case PROP_BITRATE_ACTUAL:
      g_value_set_uint (value, _get_encoding_parameter_uint (self->encoder,
              GAEGULI_ENCODING_PARAMETER_BITRATE));
      break;
    case PROP_QUANTIZER:
      g_value_set_uint (value, self->quantizer);
      break;
    case PROP_QUANTIZER_ACTUAL:
      g_value_set_uint (value, _get_encoding_parameter_uint (self->encoder,
              GAEGULI_ENCODING_PARAMETER_QUANTIZER));
      break;
    case PROP_ADAPTIVE_STREAMING:
      if (self->adaptor) {
        self->adaptive_streaming =
            gaeguli_stream_adaptor_is_enabled (self->adaptor);
      }
      g_value_set_boolean (value, self->adaptive_streaming);
      break;
    case PROP_BUFFER_SIZE:
      g_value_set_int (value, self->buffer_size);
      break;
    case PROP_LATENCY:{
      g_object_get_property (G_OBJECT (self->srtsink), "latency", value);
      break;
    }
    case PROP_VIDEO_PARAMS:{
      g_value_set_boxed (value, self->video_params);
    }
      break;
    case PROP_NODE_ID:
      g_value_set_uint (value, self->node_id);
      break;
    case PROP_SNAPSHOT_QUALITY:
      g_value_set_uint (value, self->snapshot_quality);
      break;
    case PROP_SNAPSHOT_IDCT_METHOD:
      g_value_set_enum (value, self->snapshot_idct_method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gaeguli_target_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GaeguliTarget *self = GAEGULI_TARGET (object);

  switch (prop_id) {
    case PROP_ID:
      self->id = g_value_get_uint (value);
      break;
    case PROP_CODEC:
      self->codec = g_value_get_enum (value);
      break;
    case PROP_BITRATE:{
      guint new_bitrate = g_value_get_uint (value);
      if (self->bitrate != new_bitrate) {
        self->bitrate = new_bitrate;
        gaeguli_target_update_baseline_parameters (self, FALSE);
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    case PROP_BITRATE_CONTROL:{
      GaeguliVideoBitrateControl new_rate_control = g_value_get_enum (value);
      if (self->bitrate_control != new_rate_control) {
        self->bitrate_control = new_rate_control;
        gaeguli_target_update_baseline_parameters (self, FALSE);
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    case PROP_QUANTIZER:{
      guint new_quantizer = g_value_get_uint (value);
      if (self->quantizer != new_quantizer) {
        self->quantizer = new_quantizer;
        gaeguli_target_update_baseline_parameters (self, FALSE);
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    case PROP_IDR_PERIOD:
      self->idr_period = g_value_get_uint (value);
      break;
    case PROP_URI:
      self->uri = g_value_dup_string (value);
      break;
    case PROP_USERNAME:
      g_clear_pointer (&self->username, g_free);
      self->username = g_value_dup_string (value);
      break;
    case PROP_PASSPHRASE:
      g_clear_pointer (&self->passphrase, g_free);
      self->passphrase = g_value_dup_string (value);
      break;
    case PROP_PBKEYLEN:
      self->pbkeylen = g_value_get_enum (value);
      break;
    case PROP_ADAPTOR_TYPE:
      self->adaptor_type = g_value_get_gtype (value);
      break;
    case PROP_ADAPTIVE_STREAMING:{
      gboolean new_adaptive_streaming = g_value_get_boolean (value);
      if (self->adaptive_streaming != new_adaptive_streaming) {
        self->adaptive_streaming = new_adaptive_streaming;
        if (self->adaptor) {
          g_object_set (self->adaptor, "enabled", self->adaptive_streaming,
              NULL);
        }
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    case PROP_BUFFER_SIZE:
      self->buffer_size = g_value_get_int (value);
      break;
    case PROP_VIDEO_PARAMS:
      self->video_params = g_value_dup_boxed (value);
      break;
    case PROP_LOCATION:
      g_clear_pointer (&self->location, g_free);
      self->location = g_value_dup_string (value);
      break;
    case PROP_NODE_ID:
      self->node_id = g_value_get_uint (value);
      break;
    case PROP_TARGET_TYPE:
      self->target_type = g_value_get_enum (value);
      break;
    case PROP_SNAPSHOT_QUALITY:
      self->snapshot_quality = g_value_get_uint (value);
      if (self->snapshot_jpegenc) {
        g_object_set (self->snapshot_jpegenc, "quality", self->snapshot_quality,
            NULL);
      }
      break;
    case PROP_SNAPSHOT_IDCT_METHOD:
      self->snapshot_idct_method = g_value_get_enum (value);
      if (self->snapshot_jpegenc) {
        g_object_set (self->snapshot_jpegenc, "idct-method",
            self->snapshot_idct_method, NULL);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gaeguli_target_dispose (GObject * object)
{
  GaeguliTarget *self = GAEGULI_TARGET (object);

  gst_clear_object (&self->pipeline);
  gst_clear_object (&self->encoder);
  gst_clear_object (&self->srtsink);
  gst_clear_object (&self->sinkpad);

  g_clear_object (&self->adaptor);

  g_clear_pointer (&self->uri, g_free);
  g_clear_pointer (&self->peer_address, g_free);
  g_clear_pointer (&self->username, g_free);
  g_clear_pointer (&self->passphrase, g_free);
  g_clear_pointer (&self->location, g_free);
  gst_clear_structure (&self->video_params);

  gst_clear_object (&self->snapshot_valve);
  gst_clear_object (&self->snapshot_jpegenc);
  gst_clear_object (&self->snapshot_jifmux);
  g_clear_pointer (&self->snapshot_tasks, g_queue_free);

  G_OBJECT_CLASS (gaeguli_target_parent_class)->dispose (object);
}

static void
gaeguli_target_class_init (GaeguliTargetClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gaeguli_target_get_property;
  gobject_class->set_property = gaeguli_target_set_property;
  gobject_class->dispose = gaeguli_target_dispose;

  properties[PROP_ID] =
      g_param_spec_uint ("id", "target ID", "target ID",
      0, G_MAXUINT, 0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_CODEC] =
      g_param_spec_enum ("codec", "video codec", "video codec",
      GAEGULI_TYPE_VIDEO_CODEC, DEFAULT_VIDEO_CODEC,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_BITRATE_CONTROL] =
      g_param_spec_enum ("bitrate-control", "bitrate control",
      "bitrate control", GAEGULI_TYPE_VIDEO_BITRATE_CONTROL,
      GAEGULI_VIDEO_BITRATE_CONTROL_CBR,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

  properties[PROP_BITRATE_CONTROL_ACTUAL] =
      g_param_spec_enum ("bitrate-control-actual", "actual rate control",
      "actual encoding type", GAEGULI_TYPE_VIDEO_BITRATE_CONTROL,
      GAEGULI_VIDEO_BITRATE_CONTROL_CBR,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_BITRATE] =
      g_param_spec_uint ("bitrate", "requested video bitrate",
      "requested video bitrate", 1, G_MAXUINT, DEFAULT_VIDEO_BITRATE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

  properties[PROP_BITRATE_ACTUAL] =
      g_param_spec_uint ("bitrate-actual", "actual video bitrate",
      "actual video bitrate", 1, G_MAXUINT, DEFAULT_VIDEO_BITRATE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_QUANTIZER] =
      g_param_spec_uint ("quantizer", "Constant quantizer or quality to apply",
      "Constant quantizer or quality to apply",
      0, 50, 21,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

  properties[PROP_QUANTIZER_ACTUAL] =
      g_param_spec_uint ("quantizer-actual",
      "Actual constant quantizer or quality used",
      "Actual constant quantizer or quality used", 0, G_MAXUINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_IDR_PERIOD] =
      g_param_spec_uint ("idr-period", "keyframe interval",
      "Maximal distance between two key-frames (0 for automatic)",
      0, G_MAXUINT, 0,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_URI] =
      g_param_spec_string ("uri", "SRT URI", "SRT URI",
      NULL, G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_USERNAME] =
      g_param_spec_string ("username", "username", "username",
      NULL, G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_PASSPHRASE] =
      g_param_spec_string ("passphrase", "passphrase",
      "Password for the encrypted transmission. Must be 10 to 80 "
      "characters long. Pass NULL to unset.",
      NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_PBKEYLEN] =
      g_param_spec_enum ("pbkeylen", "Cryptographic key length in bytes",
      "Cryptographic key length in bytes",
      GAEGULI_TYPE_SRT_KEY_LENGTH, GAEGULI_SRT_KEY_LENGTH_0,
      G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_ADAPTOR_TYPE] =
      g_param_spec_gtype ("adaptor-type", "stream adaptor type",
      "Type of network stream adoption the target should perform",
      GAEGULI_TYPE_STREAM_ADAPTOR, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_ADAPTIVE_STREAMING] =
      g_param_spec_boolean ("adaptive-streaming", "Use adaptive streaming",
      "Use adaptive streaming", TRUE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

  properties[PROP_BUFFER_SIZE] =
      g_param_spec_int ("buffer-size", "Send buffer size",
      "Send buffer size in bytes (0 = library default)", 0, G_MAXINT32, 0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  properties[PROP_LATENCY] =
      g_param_spec_int ("latency", "SRT latency",
      "SRT latency in milliseconds", 0, G_MAXINT32, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_VIDEO_PARAMS] =
      g_param_spec_boxed ("video-params",
      "Video encoding parameters from the original configuration",
      "Video encoding parameters from the original configuration",
      GST_TYPE_STRUCTURE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  properties[PROP_NODE_ID] =
      g_param_spec_uint ("node-id", "pipewire ouput node ID",
      "pipewire ouput node ID", 0, G_MAXUINT, 0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

  properties[PROP_TARGET_TYPE] =
      g_param_spec_enum ("target-type", "Target Type",
      "Type of the target to create", GAEGULI_TYPE_TARGET_TYPE,
      GAEGULI_TARGET_TYPE_SRT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_LOCATION] =
      g_param_spec_string ("location",
      "Location to store the recorded stream",
      "Location to store the recorded stream", NULL,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_SNAPSHOT_QUALITY] =
      g_param_spec_uint ("snapshot-quality",
      "JPEG encoding quality of stream snapshots",
      "JPEG encoding quality of stream snapshots", 0, 100, 85,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  properties[PROP_SNAPSHOT_IDCT_METHOD] =
      g_param_spec_enum ("snapshot-idct-method",
      "The IDCT algorithm to use to encode stream snapshots",
      "The IDCT algorithm to use to encode stream snapshots",
      GAEGULI_TYPE_IDCT_METHOD, GAEGULI_IDCT_METHOD_IFAST,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, G_N_ELEMENTS (properties),
      properties);

  signals[SIG_STREAM_STARTED] =
      g_signal_new ("stream-started", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 0);

  signals[SIG_STREAM_STOPPED] =
      g_signal_new ("stream-stopped", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 0);

  signals[SIG_CALLER_ADDED] =
      g_signal_new ("caller-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);

  signals[SIG_CALLER_REMOVED] =
      g_signal_new ("caller-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);
}

static void
gaeguli_target_initable_iface_init (GInitableIface * iface)
{
  iface->init = gaeguli_target_initable_init;
}

GaeguliTarget *
gaeguli_target_new (guint id,
    GaeguliVideoCodec codec, guint bitrate, guint idr_period,
    const gchar * srt_uri, const gchar * username,
    GaeguliTargetType target_type, const gchar * location, guint node_id,
    GError ** error)
{
  return g_initable_new (GAEGULI_TYPE_TARGET, NULL, error, "id", id,
      "target-type", target_type, "node-id", node_id,
      "codec", codec, "bitrate", bitrate,
      "idr-period", idr_period, "uri", srt_uri, "username", username,
      "location", location, "adaptor_type", GAEGULI_TYPE_NULL_STREAM_ADAPTOR,
      NULL);
}

static gchar *
gaeguli_target_create_streamid (GaeguliTarget * self)
{
  GString *str = g_string_new (NULL);

  if (self->username) {
    g_string_append_printf (str, "u=%s", self->username);
  }

  if (self->buffer_size > 0) {
    g_string_append_printf (str, "%sh8l_bufsize=%d",
        (str->len > 0) ? "," : "", self->buffer_size);
  }

  if (str->len > 0) {
    g_string_prepend (str, "#!::");
  }

  return g_string_free (str, FALSE);
}

GaeguliConsumerRspType
gaeguli_start_consumer (GaeguliTarget * self, GError ** error)
{
  g_autoptr (GstBus) bus = NULL;
  g_autoptr (GError) internal_err = NULL;
  g_autofree gchar *streamid = NULL;
  GstStateChangeReturn res;
  gint pbkeylen;

  if (self->state != GAEGULI_TARGET_STATE_NEW) {
    syslog (LOG_INFO, "Target %p already running", self);
    return GAEGULI_CONSUMER_RSP_START_TARGET_SUCCESS;
  }

  self->state = GAEGULI_TARGET_STATE_STARTING;

  if (GAEGULI_TARGET_TYPE_SRT == self->target_type) {
    /* Changing srtsink URI must happen first because it will clear parameters
     * like streamid. */
    if (self->buffer_size > 0) {
      g_autoptr (GstUri) uri = NULL;
      g_autofree gchar *uri_str = NULL;
      g_autofree gchar *buffer_size_str = NULL;

      buffer_size_str = g_strdup_printf ("%d", self->buffer_size);

      g_object_get (self->srtsink, "uri", &uri_str, NULL);
      uri = gst_uri_from_string (uri_str);
      g_clear_pointer (&uri_str, g_free);

      gst_uri_set_query_value (uri, "sndbuf", buffer_size_str);

      uri_str = gst_uri_to_string (uri);
      g_object_set (self->srtsink, "uri", uri_str, NULL);
    }

    switch (self->pbkeylen) {
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

    streamid = gaeguli_target_create_streamid (self);

    g_object_set (self->srtsink, "passphrase", self->passphrase, "pbkeylen",
        pbkeylen, "streamid", streamid, NULL);

    self->adaptor = g_object_new (self->adaptor_type, "srtsink", self->srtsink,
        "enabled", self->adaptive_streaming, NULL);

    gaeguli_target_update_baseline_parameters (self, TRUE);

    g_signal_connect_swapped (self->adaptor, "encoding-parameters",
        (GCallback) _set_encoding_parameters, self->encoder);

    bus = gst_element_get_bus (self->pipeline);
    gst_bus_set_sync_handler (bus, _bus_sync_srtsink_error_handler,
        &internal_err, NULL);
    /* Setting READY state on srtsink check that we can bind to address and port
     * specified in srt_uri. On failure, bus handler should set internal_err. */
    syslog (LOG_INFO, "Setting READY state on srtsink");
    res = gst_element_set_state (self->srtsink, GST_STATE_READY);

    gst_bus_set_sync_handler (bus, NULL, NULL, NULL);

    if (res == GST_STATE_CHANGE_FAILURE) {
      goto failed;
    }
  } else if (GAEGULI_TARGET_TYPE_RECORDING == self->target_type) {
    syslog (LOG_INFO, "Setting READY state on filesink");
    res = gst_element_set_state (self->srtsink, GST_STATE_READY);
    if (res == GST_STATE_CHANGE_FAILURE) {
      goto failed;
    }
  }

  syslog (LOG_INFO, "Setting PLAYING state on target");
  gst_element_set_state (self->pipeline, GST_STATE_PLAYING);

  self->state = GAEGULI_TARGET_STATE_RUNNING;

  syslog (LOG_INFO, "Emitting SIG_STREAM_STARTED");
  g_signal_emit (self, signals[SIG_STREAM_STARTED], 0);

  g_print ("emitted \"stream-started\" for [%x]\n", self->id);

  return GAEGULI_CONSUMER_RSP_START_TARGET_SUCCESS;

failed:
  if (internal_err) {
    g_propagate_error (error, internal_err);
    internal_err = NULL;
  }

  self->state = GAEGULI_TARGET_STATE_ERROR;
  return GAEGULI_CONSUMER_RSP_FAIL;
}

GaeguliConsumerRspType
gaeguli_target_start (GaeguliTarget * self, GError ** error)
{
  /* send IPC msg to consumer daemon */
  if (gaeguli_send_socket_consumer_msg ((void *)
          gaeguli_build_consumer_msg (GAEGULI_CONSUMER_MSG_START_TARGET, NULL,
              0, 0, NULL, NULL, 0, self->node_id, self->id),
          self->client_fd) > 0) {
    /* poll and wait for the response */

    while (1) {
      GaeguliConsumerRspType rsp =
          gaeguli_get_consumer_daemon_response (self->client_fd);
      if (GAEGULI_CONSUMER_RSP_START_TARGET_SUCCESS == rsp) {
        syslog (LOG_INFO, "Successfully started the target");
        return rsp;
      } else if (GAEGULI_CONSUMER_RSP_FAIL == rsp) {
        syslog (LOG_ERR,
            "Got the fail IPC responce from daemon. Failed to start the target");
        return rsp;
      } else {
        continue;
      }
    }
  } else {
    syslog (LOG_ERR, "Failed to send IPC msg to consumer daemon\n");
  }
  return GAEGULI_CONSUMER_RSP_FAIL;
}

void
gaeguli_target_stop (GaeguliTarget * self)
{
  self->state = GAEGULI_TARGET_STATE_STOPPING;

  gst_element_set_state (self->pipeline, GST_STATE_NULL);

  while (!g_queue_is_empty (self->snapshot_tasks)) {
    g_autoptr (GTask) task = g_queue_pop_head (self->snapshot_tasks);
    g_task_return_new_error (task, GAEGULI_RESOURCE_ERROR,
        GAEGULI_RESOURCE_ERROR_STOPPED,
        "The image capture pipeline has been stopped");
  }

  self->state = GAEGULI_TARGET_STATE_STOPPED;

  g_signal_emit (self, signals[SIG_STREAM_STOPPED], 0);
}

static GVariant *_convert_gst_structure_to (GstStructure * s);

static GVariant *
_convert_value_array_to (GValueArray * a)
{
  g_autofree GVariant **children = g_new0 (GVariant *, a->n_values);
  guint i;

  for (i = 0; i != a->n_values; ++i) {
    children[i] = _convert_gst_structure_to
        (g_value_get_boxed (g_value_array_get_nth (a, i)));
  }

  return g_variant_ref_sink
      (g_variant_new_array (G_VARIANT_TYPE_VARDICT, children, a->n_values));
}

static GVariant *
_convert_gst_structure_to (GstStructure * s)
{
  g_autoptr (GVariantDict) dict = g_variant_dict_new (NULL);
  gint i = 0;

  for (i = 0; i < gst_structure_n_fields (s); i++) {
    const gchar *fname = gst_structure_nth_field_name (s, i);
    const GValue *v = NULL;
    g_autoptr (GVariant) variant = NULL;

    v = gst_structure_get_value (s, fname);

    if (G_TYPE_IS_FUNDAMENTAL (G_VALUE_TYPE (v))) {
      const GVariantType *variant_type = NULL;

      switch (G_VALUE_TYPE (v)) {
        case G_TYPE_INT:
          variant_type = G_VARIANT_TYPE_INT32;
          break;
        case G_TYPE_UINT:
          variant_type = G_VARIANT_TYPE_UINT32;
          break;
        case G_TYPE_UINT64:
          variant_type = G_VARIANT_TYPE_UINT64;
          break;
        case G_TYPE_INT64:
          variant_type = G_VARIANT_TYPE_INT64;
          break;
        case G_TYPE_DOUBLE:
          variant_type = G_VARIANT_TYPE_DOUBLE;
          break;
      }

      variant = g_dbus_gvalue_to_gvariant (v, variant_type);
    } else if (G_VALUE_HOLDS (v, G_TYPE_VALUE_ARRAY)) {
      variant = _convert_value_array_to (g_value_get_boxed (v));
    }

    if (!variant) {
      g_warning ("unsupported type was detected (%s)", G_VALUE_TYPE_NAME (v));
      goto out;
    }

    g_variant_dict_insert_value (dict, fname, variant);
  }

out:
  return g_variant_dict_end (dict);
}

GaeguliSRTMode
gaeguli_target_get_srt_mode (GaeguliTarget * self)
{
  GaeguliSRTMode mode;

  g_object_get (self->srtsink, "mode", &mode, NULL);

  return mode;
}

const gchar *
gaeguli_target_get_peer_address (GaeguliTarget * self)
{
  return self->peer_address;
}

GaeguliTargetState
gaeguli_target_get_state (GaeguliTarget * self)
{
  return self->state;
}

GVariant *
gaeguli_target_get_stats (GaeguliTarget * self)
{
  g_autoptr (GstStructure) s = NULL;

  g_return_val_if_fail (GAEGULI_IS_TARGET (self), NULL);

  if (self->srtsink) {
    g_object_get (self->srtsink, "stats", &s, NULL);
    return _convert_gst_structure_to (s);
  }

  return NULL;
}

GaeguliStreamAdaptor *
gaeguli_target_get_stream_adaptor (GaeguliTarget * self)
{
  return self->adaptor;
}

void
gaeguli_target_set_clientfd (GaeguliTarget * self, gint fd)
{
  self->client_fd = fd;
}

void
gaeguli_target_deep_copy (GaeguliTarget * src, GaeguliTarget * dst)
{
  dst->id = src->id;
  dst->pipeline = src->pipeline;
  dst->node_id = src->node_id;
  dst->parent = src->parent;
  dst->state = src->state;
  dst->encoder = src->encoder;
  dst->srtsink = src->srtsink;
  dst->sinkpad = src->sinkpad;
  dst->adaptor = src->adaptor;
  dst->codec = src->codec;
  dst->bitrate_control = src->bitrate_control;
  dst->bitrate = src->bitrate;
  dst->quantizer = src->quantizer;
  dst->idr_period = src->idr_period;
  dst->node_id = src->node_id;
  dst->client_fd = src->client_fd;
  dst->pbkeylen = src->pbkeylen;
  dst->adaptor_type = src->adaptor_type;
  dst->adaptive_streaming = src->adaptive_streaming;
  dst->target_type = src->target_type;

  dst->buffer_size = src->buffer_size;
  dst->video_params = src->video_params;
  dst->uri = g_strdup (src->uri);
  dst->peer_address = g_strdup (src->peer_address);
  dst->username = g_strdup (src->username);
  dst->passphrase = g_strdup (src->passphrase);
  dst->location = g_strdup (src->location);
}

void
gaeguli_target_free_srt_resources (GaeguliTarget * self)
{
  if (self->uri) {
    g_free (self->uri);
  }
  if (self->peer_address) {
    g_free (self->peer_address);
  }
  if (self->username) {
    g_free (self->username);
  }
  if (self->passphrase) {
    g_free (self->passphrase);
  }
  if (self->location) {
    g_free (self->location);
  }
}

int
gaeguli_target_get_size ()
{
  return sizeof (GaeguliTarget);
}

guint
gaeguli_target_get_id (GaeguliTarget * self)
{
  return self->id;
}

guint
gaeguli_target_get_node_id (GaeguliTarget * self)
{
  return self->node_id;
}

gint
gaeguli_target_get_clientfd (GaeguliTarget * self)
{
  return self->client_fd;
}
