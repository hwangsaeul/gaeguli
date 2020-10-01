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

#include "target.h"

#include "enumtypes.h"
#include "gaeguli-internal.h"
#include "pipeline.h"
#include "streamadaptor.h"

#include <gio/gio.h>

typedef struct
{
  GObject parent;

  GstElement *encoder;
  GstElement *srtsink;
  GstPad *peer_pad;
  GstPad *sinkpad;
  gulong pending_pad_probe;
  GWeakRef gaeguli_pipeline;
  GaeguliStreamAdaptor *adaptor;

  GaeguliVideoCodec codec;
  guint bitrate;
  guint quantizer;
  guint idr_period;
  gchar *uri;
  gchar *username;
  gboolean adaptive_streaming;
} GaeguliTargetPrivate;

enum
{
  PROP_ID = 1,
  PROP_PIPELINE,
  PROP_CODEC,
  PROP_BITRATE,
  PROP_BITRATE_ACTUAL,
  PROP_QUANTIZER,
  PROP_QUANTIZER_ACTUAL,
  PROP_IDR_PERIOD,
  PROP_URI,
  PROP_USERNAME,
  PROP_ADAPTIVE_STREAMING,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST] = { 0 };

enum
{
  SIG_STREAM_STARTED,
  SIG_STREAM_STOPPED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void gaeguli_target_initable_iface_init (GInitableIface * iface);

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_CODE (GaeguliTarget, gaeguli_target, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (GaeguliTarget)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                gaeguli_target_initable_iface_init))
/* *INDENT-ON* */

static void
gaeguli_target_init (GaeguliTarget * self)
{
}

typedef gchar *(*PipelineFormatFunc) (const gchar * pipeline_str,
    guint idr_period);

struct encoding_method_params
{
  const gchar *pipeline_str;
  GaeguliEncodingMethod encoding_method;
  GaeguliVideoCodec codec;
  PipelineFormatFunc format_func;
};

static gchar *
_format_general_pipeline (const gchar * pipeline_str, guint idr_period)
{
  return g_strdup_printf (pipeline_str, idr_period);
}

static gchar *
_format_tx1_pipeline (const gchar * pipeline_str, guint idr_period)
{
  return g_strdup_printf (pipeline_str, idr_period);
}

static struct encoding_method_params enc_params[] = {
  {GAEGULI_PIPELINE_GENERAL_H264ENC_STR, GAEGULI_ENCODING_METHOD_GENERAL,
      GAEGULI_VIDEO_CODEC_H264, _format_general_pipeline},
  {GAEGULI_PIPELINE_GENERAL_H265ENC_STR, GAEGULI_ENCODING_METHOD_GENERAL,
      GAEGULI_VIDEO_CODEC_H265, _format_general_pipeline},
  {GAEGULI_PIPELINE_NVIDIA_TX1_H264ENC_STR, GAEGULI_ENCODING_METHOD_NVIDIA_TX1,
      GAEGULI_VIDEO_CODEC_H264, _format_tx1_pipeline},
  {GAEGULI_PIPELINE_NVIDIA_TX1_H265ENC_STR, GAEGULI_ENCODING_METHOD_NVIDIA_TX1,
      GAEGULI_VIDEO_CODEC_H265, _format_tx1_pipeline},
  {NULL, 0, 0},
};

static gchar *
_get_enc_string (GaeguliEncodingMethod encoding_method,
    GaeguliVideoCodec codec, guint idr_period)
{
  struct encoding_method_params *params = enc_params;

  for (; params->pipeline_str != NULL; params++) {
    if (params->encoding_method == encoding_method && params->codec == codec)
      return params->format_func (params->pipeline_str, idr_period);
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
    g_object_get (encoder, "bitrate", &result, NULL);

    if (g_str_equal (encoder_type, "x264enc") ||
        g_str_equal (encoder_type, "x265enc")) {
      result *= 1000;
    }
  } else if (g_str_equal (param, GAEGULI_ENCODING_PARAMETER_QUANTIZER)) {
    if (g_str_equal (encoder_type, "x264enc")) {
      g_object_get (encoder, "quantizer", &result, NULL);
    }
  } else {
    g_warning ("Unsupported parameter '%s'", param);
  }

  return result;
}

static GstPadProbeReturn
_change_quantizer (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstElement *encoder = GST_PAD_PARENT (GST_PAD_PEER (pad));
  GstState cur_state;

  gst_element_get_state (encoder, &cur_state, NULL, 0);
  if (cur_state == GST_STATE_PLAYING) {
    gst_element_set_state (encoder, GST_STATE_READY);
  }
  g_object_set (encoder, "quantizer", GPOINTER_TO_UINT (user_data), NULL);

  if (cur_state == GST_STATE_PLAYING) {
    gst_element_set_state (encoder, GST_STATE_PLAYING);
  }

  return GST_PAD_PROBE_REMOVE;
}

static void
_set_encoding_parameters (GstElement * encoder, GstStructure * params)
{
  guint val;
  g_autofree gchar *params_str = NULL;

  const gchar *encoder_type =
      gst_plugin_feature_get_name (gst_element_get_factory (encoder));

  params_str = gst_structure_to_string (params);
  g_debug ("Changing encoding parameters to %s", params_str);

  if (g_str_equal (encoder_type, "x264enc")) {
    if (gst_structure_get_uint (params, GAEGULI_ENCODING_PARAMETER_BITRATE,
            &val)) {
      g_object_set (encoder, "bitrate", val / 1000, NULL);
    }

    if (gst_structure_get_uint (params, GAEGULI_ENCODING_PARAMETER_QUANTIZER,
            &val)) {
      guint cur_quantizer;

      g_object_get (encoder, "quantizer", &cur_quantizer, NULL);

      if (val != cur_quantizer) {
        g_autoptr (GstPad) sinkpad =
            gst_element_get_static_pad (encoder, "sink");

        gst_pad_add_probe (GST_PAD_PEER (sinkpad), GST_PAD_PROBE_TYPE_BLOCK,
            _change_quantizer, GUINT_TO_POINTER (val), NULL);
      }
    }
  } else if (g_str_equal (encoder_type, "x265enc")) {
    if (gst_structure_get_uint (params, GAEGULI_ENCODING_PARAMETER_BITRATE,
            &val)) {
      g_object_set (encoder, "bitrate", val / 1000, NULL);
    }
  } else if (g_str_equal (encoder_type, "omxh264enc")) {
    if (gst_structure_get_uint (params, GAEGULI_ENCODING_PARAMETER_BITRATE,
            &val)) {
      g_object_set (encoder, "bitrate", val, NULL);
    }
  } else {
    g_warning ("Unsupported encoder '%s'", encoder_type);
  }
}

static void
gaeguli_target_update_baseline_parameters (GaeguliTarget * self,
    gboolean force_on_encoder)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  g_autoptr (GstStructure) params = NULL;

  if (!priv->encoder) {
    /* We're not initialized yet. */
    return;
  }

  params = gst_structure_new ("application/x-gaeguli-encoding-parameters",
      GAEGULI_ENCODING_PARAMETER_BITRATE, G_TYPE_UINT, priv->bitrate,
      GAEGULI_ENCODING_PARAMETER_QUANTIZER, G_TYPE_UINT, priv->quantizer, NULL);

  if (priv->adaptor) {
    g_object_set (priv->adaptor, "baseline-parameters", params, NULL);
  }

  if (!gaeguli_stream_adaptor_is_enabled (priv->adaptor) || force_on_encoder) {
    /* Apply directly on the encoder */
    _set_encoding_parameters (priv->encoder, params);
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

static gboolean
gaeguli_target_initable_init (GInitable * initable, GCancellable * cancellable,
    GError ** error)
{
  GaeguliTarget *self = GAEGULI_TARGET (initable);
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  g_autoptr (GaeguliPipeline) owner = NULL;
  g_autoptr (GstElement) enc_first = NULL;
  g_autoptr (GstPad) enc_sinkpad = NULL;
  g_autoptr (GstBus) bus = NULL;
  g_autofree gchar *pipeline_str = NULL;
  g_autofree gchar *uri_str = NULL;
  g_autoptr (GError) internal_err = NULL;
  GaeguliEncodingMethod encoding_method;
  GType adaptor_type;
  GstStateChangeReturn res;
  NotifyData *notify_data;

  owner = g_weak_ref_get (&priv->gaeguli_pipeline);
  if (!owner) {
    g_set_error (error, GAEGULI_RESOURCE_ERROR,
        GAEGULI_RESOURCE_ERROR_UNSUPPORTED,
        "Can't create a target without a pipeline");
    return FALSE;
  }

  g_object_get (owner, "encoding-method", &encoding_method,
      "stream-adaptor", &adaptor_type, NULL);

  pipeline_str = _get_enc_string (encoding_method, priv->codec,
      priv->idr_period);
  if (pipeline_str == NULL) {
    g_set_error (error, GAEGULI_RESOURCE_ERROR,
        GAEGULI_RESOURCE_ERROR_UNSUPPORTED, "Can't determine encoding method");
    return FALSE;
  }

  g_debug ("using encoding pipeline [%s]", pipeline_str);

  if (priv->username) {
    g_autoptr (GstUri) uri = gst_uri_from_string (priv->uri);
    g_autofree gchar *streamid = g_strdup_printf ("#!::u=%s", priv->username);

    gst_uri_set_query_value (uri, "streamid", streamid);
    uri_str = gst_uri_to_string (uri);
  }

  pipeline_str = g_strdup_printf ("%s ! " GAEGULI_PIPELINE_MUXSINK_STR,
      pipeline_str, uri_str ? uri_str : priv->uri);

  self->pipeline = gst_parse_launch (pipeline_str, &internal_err);
  if (internal_err) {
    g_warning ("failed to build muxsink pipeline (%s)", internal_err->message);
    goto failed;
  }

  gst_object_ref_sink (self->pipeline);

  priv->srtsink = gst_bin_get_by_name (GST_BIN (self->pipeline), "sink");
  g_object_set_data (G_OBJECT (priv->srtsink), "gaeguli-target-id",
      GUINT_TO_POINTER (self->id));

  bus = gst_element_get_bus (self->pipeline);
  gst_bus_set_sync_handler (bus, _bus_sync_srtsink_error_handler, &internal_err,
      NULL);

  priv->encoder = gst_bin_get_by_name (GST_BIN (self->pipeline), "enc");

  notify_data = g_new (NotifyData, 1);
  notify_data->target = G_OBJECT (self);
  notify_data->pspec = properties[PROP_BITRATE_ACTUAL];
  g_signal_connect_closure (priv->encoder, "notify::bitrate",
      g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
          (GClosureNotify) g_free), FALSE);

  notify_data = g_new (NotifyData, 1);
  notify_data->target = G_OBJECT (self);
  notify_data->pspec = properties[PROP_QUANTIZER_ACTUAL];
  g_signal_connect_closure (priv->encoder, "notify::quantizer",
      g_cclosure_new_swap (G_CALLBACK (_notify_encoder_change), notify_data,
          (GClosureNotify) g_free), FALSE);

  priv->adaptor = g_object_new (adaptor_type, "srtsink", priv->srtsink,
      "enabled", priv->adaptive_streaming, NULL);

  gaeguli_target_update_baseline_parameters (self, TRUE);

  g_signal_connect_swapped (priv->adaptor, "encoding-parameters",
      (GCallback) _set_encoding_parameters, priv->encoder);

  /* Setting READY state on srtsink check that we can bind to address and port
   * specified in srt_uri. On failure, bus handler should set internal_err. */
  res = gst_element_set_state (priv->srtsink, GST_STATE_READY);

  gst_bus_set_sync_handler (bus, NULL, NULL, NULL);

  if (res == GST_STATE_CHANGE_FAILURE) {
    goto failed;
  }

  enc_first = gst_bin_get_by_name (GST_BIN (self->pipeline), "enc_first");
  enc_sinkpad = gst_element_get_static_pad (enc_first, "sink");

  priv->sinkpad = gst_ghost_pad_new (NULL, enc_sinkpad);
  gst_object_ref_sink (priv->sinkpad);
  gst_element_add_pad (self->pipeline, priv->sinkpad);

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
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  switch (prop_id) {
    case PROP_ID:
      g_value_set_uint (value, self->id);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, priv->bitrate);
      break;
    case PROP_BITRATE_ACTUAL:
      g_value_set_uint (value, _get_encoding_parameter_uint (priv->encoder,
              GAEGULI_ENCODING_PARAMETER_BITRATE));
      break;
    case PROP_QUANTIZER:
      g_value_set_uint (value, priv->quantizer);
      break;
    case PROP_QUANTIZER_ACTUAL:
      g_value_set_uint (value, _get_encoding_parameter_uint (priv->encoder,
              GAEGULI_ENCODING_PARAMETER_QUANTIZER));
      break;
    case PROP_ADAPTIVE_STREAMING:
      if (priv->adaptor) {
        priv->adaptive_streaming =
            gaeguli_stream_adaptor_is_enabled (priv->adaptor);
      }
      g_value_set_boolean (value, priv->adaptive_streaming);
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
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  switch (prop_id) {
    case PROP_ID:
      self->id = g_value_get_uint (value);
      break;
    case PROP_PIPELINE:
      g_weak_ref_init (&priv->gaeguli_pipeline, g_value_get_object (value));
      break;
    case PROP_CODEC:
      priv->codec = g_value_get_enum (value);
      break;
    case PROP_BITRATE:{
      guint new_bitrate = g_value_get_uint (value);
      if (priv->bitrate != new_bitrate) {
        priv->bitrate = new_bitrate;
        gaeguli_target_update_baseline_parameters (self, FALSE);
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    case PROP_QUANTIZER:{
      guint new_quantizer = g_value_get_uint (value);
      if (priv->quantizer != new_quantizer) {
        priv->quantizer = new_quantizer;
        gaeguli_target_update_baseline_parameters (self, FALSE);
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    case PROP_IDR_PERIOD:
      priv->idr_period = g_value_get_uint (value);
      break;
    case PROP_URI:
      priv->uri = g_value_dup_string (value);
      break;
    case PROP_USERNAME:
      priv->username = g_value_dup_string (value);
      break;
    case PROP_ADAPTIVE_STREAMING:{
      gboolean new_adaptive_streaming = g_value_get_boolean (value);
      if (priv->adaptive_streaming != new_adaptive_streaming) {
        priv->adaptive_streaming = new_adaptive_streaming;
        if (priv->adaptor) {
          g_object_set (priv->adaptor, "enabled", priv->adaptive_streaming,
              NULL);
        }
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gaeguli_target_dispose (GObject * object)
{
  GaeguliTarget *self = GAEGULI_TARGET (object);
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  gst_clear_object (&self->pipeline);
  gst_clear_object (&priv->encoder);
  gst_clear_object (&priv->srtsink);
  gst_clear_object (&priv->peer_pad);
  gst_clear_object (&priv->sinkpad);

  g_weak_ref_clear (&priv->gaeguli_pipeline);
  g_clear_object (&priv->adaptor);

  g_clear_pointer (&priv->uri, g_free);
  g_clear_pointer (&priv->username, g_free);

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

  properties[PROP_PIPELINE] =
      g_param_spec_object ("pipeline", "owning GaeguliPipeline instance",
      "owning GaeguliPipeline instance", GAEGULI_TYPE_PIPELINE,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_CODEC] =
      g_param_spec_enum ("codec", "video codec", "video codec",
      GAEGULI_TYPE_VIDEO_CODEC, DEFAULT_VIDEO_CODEC,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

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

  properties[PROP_ADAPTIVE_STREAMING] =
      g_param_spec_boolean ("adaptive-streaming", "Use adaptive streaming",
      "Use adaptive streaming", TRUE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

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
}

static void
gaeguli_target_initable_iface_init (GInitableIface * iface)
{
  iface->init = gaeguli_target_initable_init;
}

GaeguliTarget *
gaeguli_target_new (GaeguliPipeline * pipeline, guint id,
    GaeguliVideoCodec codec, guint bitrate, guint idr_period,
    const gchar * srt_uri, const gchar * username, GError ** error)
{
  return g_initable_new (GAEGULI_TYPE_TARGET, NULL, error, "id", id,
      "pipeline", pipeline, "codec", codec, "bitrate", bitrate,
      "idr-period", idr_period, "uri", srt_uri, "username", username, NULL);
}

static GstPadProbeReturn
_link_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GaeguliTarget *self = GAEGULI_TARGET (user_data);
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  GstPad *ghost_pad = NULL;

  /*
   * GST_PAD_PROBE_TYPE_IDLE can cause infinite waiting in filesink.
   * In addition, to prevent events generated in gst_ghost_pad_new()
   * from invoking this probe callback again, we remove the probe first.
   *
   * For more details, refer to
   * https://github.com/hwangsaeul/gaeguli/pull/10#discussion_r327031325
   */
  gst_pad_remove_probe (pad, info->id);

  if (priv->pending_pad_probe == info->id) {
    priv->pending_pad_probe = 0;
  }

  g_debug ("start link target [%x]", self->id);

  ghost_pad = gst_ghost_pad_new (NULL, priv->peer_pad);
  if (ghost_pad == NULL) {
    g_error ("ghost pad is null");
  }

  gst_element_add_pad (GST_ELEMENT_PARENT (GST_PAD_PARENT (priv->peer_pad)),
      ghost_pad);

  g_debug ("created ghost pad for [%x]", self->id);

  gst_element_sync_state_with_parent (self->pipeline);
  if (gst_pad_link (ghost_pad, priv->sinkpad) != GST_PAD_LINK_OK) {
    g_error ("failed to link target to Gaeguli pipeline");
  }

  g_debug ("finished link target [%x]", self->id);

  g_signal_emit (self, signals[SIG_STREAM_STARTED], 0);

  g_debug ("emitted \"stream-started\" for [%x]", self->id);

  return GST_PAD_PROBE_REMOVE;
}

void
gaeguli_target_link_with_pad (GaeguliTarget * self, GstPad * pad)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  priv->peer_pad = gst_object_ref (pad);

  priv->pending_pad_probe = gst_pad_add_probe (priv->peer_pad,
      GST_PAD_PROBE_TYPE_BLOCK, _link_probe_cb, self, NULL);
}

guint64
gaeguli_target_get_bytes_sent (GaeguliTarget * self)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  g_autoptr (GstStructure) s = NULL;
  guint64 result = 0;

  g_return_val_if_fail (GAEGULI_IS_TARGET (self), GAEGULI_RETURN_FAIL);

  if (priv->srtsink) {
    g_object_get (priv->srtsink, "stats", &s, NULL);
    gst_structure_get_uint64 (s, "bytes-sent", &result);
  } else {
    g_warning ("SRT sink for target %d not found", self->id);
  }

  return result;
}

static gboolean
_unlink_finish_in_main_thread (GaeguliTarget * self)
{
  g_return_val_if_fail (self != NULL, G_SOURCE_REMOVE);

  gst_element_set_state (self->pipeline, GST_STATE_NULL);

  g_signal_emit (self, signals[SIG_STREAM_STOPPED], 0);

  return G_SOURCE_REMOVE;
}

static GstPadProbeReturn
_unlink_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GaeguliTarget *self = GAEGULI_TARGET (user_data);
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  g_autoptr (GstElement) topmost_pipeline = NULL;
  g_autoptr (GstPad) ghost_srcpad = NULL;

  /* Remove the probe first. See _link_probe_cb() for details. */
  gst_pad_remove_probe (pad, info->id);

  if (priv->pending_pad_probe == info->id) {
    priv->pending_pad_probe = 0;
  }

  ghost_srcpad = gst_pad_get_peer (priv->sinkpad);

  g_debug ("start unlink target [%x]", self->id);

  if (!gst_pad_unlink (ghost_srcpad, priv->sinkpad)) {
    g_error ("failed to unlink");
  }

  gst_ghost_pad_set_target (GST_GHOST_PAD (ghost_srcpad), NULL);
  gst_element_remove_pad (GST_PAD_PARENT (ghost_srcpad), ghost_srcpad);
  gst_element_release_request_pad (GST_PAD_PARENT (priv->peer_pad),
      priv->peer_pad);

  topmost_pipeline =
      GST_ELEMENT (gst_object_get_parent (GST_OBJECT (self->pipeline)));
  gst_bin_remove (GST_BIN (topmost_pipeline), self->pipeline);

  /* This probe may get called from the target's streaming thread, so let the
   * state change happen in the main thread. */
  g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
      (GSourceFunc) _unlink_finish_in_main_thread,
      g_object_ref (self), g_object_unref);

  return GST_PAD_PROBE_REMOVE;
}

void
gaeguli_target_unlink (GaeguliTarget * self)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  if (priv->pending_pad_probe != 0) {
    /* Target removed before its link pad probe got called. */
    gst_pad_remove_probe (priv->peer_pad, priv->pending_pad_probe);
    priv->pending_pad_probe = 0;
  } else {
    gst_element_set_state (priv->srtsink, GST_STATE_NULL);

    gst_pad_add_probe (priv->peer_pad, GST_PAD_PROBE_TYPE_BLOCK,
        _unlink_probe_cb, g_object_ref (self), (GDestroyNotify) g_object_unref);
  }
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

    const GVariantType *variant_type = NULL;

    v = gst_structure_get_value (s, fname);

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
      default:
        g_warning ("unsupported type was detected (%s)", G_VALUE_TYPE_NAME (v));
        goto out;
    }

    variant = g_dbus_gvalue_to_gvariant (v, variant_type);
    g_variant_dict_insert_value (dict, fname, variant);

  }

out:
  return g_variant_dict_end (dict);
}

GVariant *
gaeguli_target_get_stats (GaeguliTarget * self)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  g_autoptr (GstStructure) s = NULL;

  g_return_val_if_fail (GAEGULI_IS_TARGET (self), NULL);

  if (priv->srtsink) {
    g_object_get (priv->srtsink, "stats", &s, NULL);
    return _convert_gst_structure_to (s);
  }

  return NULL;
}
