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

#include "gaeguli-internal.h"
#include "adaptors/nulladaptor.h"

#include <gio/gio.h>

/* *INDENT-OFF* */
#if !GLIB_CHECK_VERSION(2,57,1)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GEnumClass, g_type_class_unref)
#endif
/* *INDENT-ON* */

static guint gaeguli_init_refcnt = 0;

struct _GaeguliPipeline
{
  GObject parent;

  GMutex lock;

  GaeguliVideoSource source;
  gchar *device;
  GaeguliVideoResolution resolution;
  guint fps;

  GHashTable *targets;
  guint num_active_targets;

  GstElement *pipeline;
  GstElement *vsrc;

  GstElement *overlay;
  gboolean show_overlay;

  guint benchmark_interval_ms;
  guint benchmark_timeout_id;
  GHashTable *srtsocket_to_peer_addr;
  GHashTable *benchmarks;

  gboolean prefer_hw_decoding;

  GType adaptor_type;
};

static const gchar *const supported_formats[] = {
  "video/x-raw",
  "video/x-raw(memory:GLMemory)",
  "video/x-raw(memory:NVMM)",
  "image/jpeg",
  NULL
};

typedef enum
{
  PROP_SOURCE = 1,
  PROP_DEVICE,
  PROP_RESOLUTION,
  PROP_FRAMERATE,
  PROP_CLOCK_OVERLAY,
  PROP_STREAM_ADAPTOR,
  PROP_GST_PIPELINE,
  PROP_PREFER_HW_DECODING,
  PROP_BENCHMARK_INTERVAL,

  /*< private > */
  PROP_LAST
} _GaeguliPipelineProperty;

static GParamSpec *properties[PROP_LAST];

enum
{
  SIG_STREAM_STARTED,
  SIG_STREAM_STOPPED,
  SIG_CONNECTION_ERROR,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliPipeline, gaeguli_pipeline, G_TYPE_OBJECT)
/* *INDENT-ON* */

static void gaeguli_pipeline_update_vsrc_caps (GaeguliPipeline * self);

typedef struct
{
  double bw_mbps;
  double rtt_ms;
} Benchmark;

static void
gaeguli_pipeline_collect_benchmark_for_socket (GaeguliPipeline * self,
    GaeguliTarget * target, GVariantDict * d)
{
  Benchmark *benchmark;
  const gchar *peer_address = NULL;

  if (gaeguli_target_get_srt_mode (target) == GAEGULI_SRT_MODE_CALLER) {
    peer_address = gaeguli_target_get_peer_address (target);
  } else {
    int srtsocket = 0;

    g_variant_dict_lookup (d, "socket", "i", &srtsocket);

    if (srtsocket == 0) {
      return;
    }

    peer_address = g_hash_table_lookup (self->srtsocket_to_peer_addr,
        GUINT_TO_POINTER (srtsocket));
  }

  if (!peer_address) {
    g_warning ("Couldn't get peer address for target %p", target);
    return;
  }

  benchmark = g_hash_table_lookup (self->benchmarks, peer_address);
  if (!benchmark) {
    benchmark = g_new0 (Benchmark, 1);
    g_hash_table_insert (self->benchmarks, g_strdup (peer_address), benchmark);
  }

  g_variant_dict_lookup (d, "bandwidth-mbps", "d", &benchmark->bw_mbps);
  g_variant_dict_lookup (d, "rtt-ms", "d", &benchmark->rtt_ms);
}

static gboolean
gaeguli_pipeline_collect_benchmark (GaeguliPipeline * self)
{
  GHashTableIter it;
  GaeguliTarget *target;

  g_hash_table_iter_init (&it, self->targets);

  while (g_hash_table_iter_next (&it, NULL, (gpointer *) & target)) {
    g_autoptr (GVariant) stats = NULL;
    GVariantDict d;

    stats = gaeguli_target_get_stats (target);

    g_variant_dict_init (&d, stats);

    if (g_variant_dict_contains (&d, "callers")) {
      /* Target is a listener. */
      GVariant *array;
      GVariant *caller;
      GVariantIter it;

      array = g_variant_dict_lookup_value (&d, "callers", G_VARIANT_TYPE_ARRAY);

      g_variant_iter_init (&it, array);

      while ((caller = g_variant_iter_next_value (&it))) {
        g_variant_dict_clear (&d);
        g_variant_dict_init (&d, caller);

        gaeguli_pipeline_collect_benchmark_for_socket (self, target, &d);
      }
    } else {
      /* Target is a caller. */
      gaeguli_pipeline_collect_benchmark_for_socket (self, target, &d);
    }

    g_variant_dict_clear (&d);
  }

  return G_SOURCE_CONTINUE;
}

static void
gaeguli_pipeline_set_benchmark_interval (GaeguliPipeline * self, guint ms)
{
  if (self->benchmark_interval_ms != ms) {
    self->benchmark_interval_ms = ms;

    g_clear_handle_id (&self->benchmark_timeout_id, g_source_remove);

    if (self->benchmark_interval_ms > 0) {
      self->benchmark_timeout_id = g_timeout_add (self->benchmark_interval_ms,
          G_SOURCE_FUNC (gaeguli_pipeline_collect_benchmark), self);
    }
  }
}

static void
gaeguli_pipeline_finalize (GObject * object)
{
  GaeguliPipeline *self = GAEGULI_PIPELINE (object);

  if (self->pipeline) {
    g_critical ("Call gaeguli_pipeline_stop() before releasing the final "
        "GaeguliPipeline reference!");
  }

  g_clear_pointer (&self->targets, g_hash_table_unref);
  g_clear_pointer (&self->srtsocket_to_peer_addr, g_hash_table_unref);
  g_clear_pointer (&self->benchmarks, g_hash_table_unref);
  g_clear_pointer (&self->device, g_free);
  g_clear_handle_id (&self->benchmark_timeout_id, g_source_remove);

  g_mutex_clear (&self->lock);

  if (g_atomic_int_dec_and_test (&gaeguli_init_refcnt)) {
    g_debug ("Cleaning up GStreamer");
  }

  G_OBJECT_CLASS (gaeguli_pipeline_parent_class)->finalize (object);
}

static void
gaeguli_pipeline_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GaeguliPipeline *self = GAEGULI_PIPELINE (object);

  switch ((_GaeguliPipelineProperty) prop_id) {
    case PROP_SOURCE:
      g_value_set_enum (value, self->source);
      break;
    case PROP_DEVICE:
      g_value_set_string (value, self->device);
      break;
    case PROP_RESOLUTION:
      g_value_set_enum (value, self->resolution);
      break;
    case PROP_FRAMERATE:
      g_value_set_uint (value, self->fps);
      break;
    case PROP_CLOCK_OVERLAY:
      g_value_set_boolean (value, self->show_overlay);
      break;
    case PROP_STREAM_ADAPTOR:
      g_value_set_gtype (value, self->adaptor_type);
      break;
    case PROP_GST_PIPELINE:
      g_value_set_object (value, self->pipeline);
      break;
    case PROP_PREFER_HW_DECODING:
      g_value_set_boolean (value, self->prefer_hw_decoding);
      break;
    case PROP_BENCHMARK_INTERVAL:
      g_value_set_uint (value, self->benchmark_interval_ms);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gaeguli_pipeline_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GaeguliPipeline *self = GAEGULI_PIPELINE (object);

  switch ((_GaeguliPipelineProperty) prop_id) {
    case PROP_SOURCE:
      g_assert (self->source == GAEGULI_VIDEO_SOURCE_UNKNOWN);  /* construct only */
      self->source = g_value_get_enum (value);
      break;
    case PROP_DEVICE:
      g_assert_null (self->device);     /* construct only */
      self->device = g_value_dup_string (value);
      break;
    case PROP_RESOLUTION:
      self->resolution = g_value_get_enum (value);
      gaeguli_pipeline_update_vsrc_caps (self);
      break;
    case PROP_FRAMERATE:
      self->fps = g_value_get_uint (value);
      gaeguli_pipeline_update_vsrc_caps (self);
      break;
    case PROP_CLOCK_OVERLAY:
      self->show_overlay = g_value_get_boolean (value);
      if (self->overlay) {
        g_object_set (self->overlay, "silent", !self->show_overlay, NULL);
      }
      break;
    case PROP_STREAM_ADAPTOR:
      self->adaptor_type = g_value_get_gtype (value);
      break;
    case PROP_PREFER_HW_DECODING:
      self->prefer_hw_decoding = g_value_get_boolean (value);
      break;
    case PROP_BENCHMARK_INTERVAL:
      gaeguli_pipeline_set_benchmark_interval (self, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gaeguli_pipeline_class_init (GaeguliPipelineClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gaeguli_pipeline_get_property;
  object_class->set_property = gaeguli_pipeline_set_property;
  object_class->finalize = gaeguli_pipeline_finalize;

  properties[PROP_SOURCE] = g_param_spec_enum ("source", "source", "source",
      GAEGULI_TYPE_VIDEO_SOURCE, DEFAULT_VIDEO_SOURCE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_DEVICE] = g_param_spec_string ("device", "device", "device",
      DEFAULT_VIDEO_SOURCE_DEVICE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_RESOLUTION] = g_param_spec_enum ("resolution", "resolution",
      "resolution", GAEGULI_TYPE_VIDEO_RESOLUTION, DEFAULT_VIDEO_RESOLUTION,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_FRAMERATE] = g_param_spec_uint ("framerate", "framerate",
      "framerate", 1, G_MAXUINT, DEFAULT_VIDEO_FRAMERATE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_CLOCK_OVERLAY] =
      g_param_spec_boolean ("clock-overlay", "clock overlay",
      "Overlay the current time on the video stream", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_STREAM_ADAPTOR] =
      g_param_spec_gtype ("stream-adaptor", "stream adaptor",
      "Type of network stream adoption the pipeline should perform",
      GAEGULI_TYPE_STREAM_ADAPTOR, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_GST_PIPELINE] =
      g_param_spec_object ("gst-pipeline", "GStreamer pipeline",
      "The internal GStreamer pipeline", GST_TYPE_PIPELINE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_PREFER_HW_DECODING] =
      g_param_spec_boolean ("prefer-hw-decoding", "prefer hardware decoding",
      "prefer hardware decoding on availability", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_BENCHMARK_INTERVAL] =
      g_param_spec_uint ("benchmark-interval", "network benchmark interval",
      "period of benchmarking the network connections in ms", 0, G_MAXUINT, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties),
      properties);

  signals[SIG_STREAM_STARTED] =
      g_signal_new ("stream-started", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, GAEGULI_TYPE_TARGET);

  signals[SIG_STREAM_STOPPED] =
      g_signal_new ("stream-stopped", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, GAEGULI_TYPE_TARGET);

  signals[SIG_CONNECTION_ERROR] =
      g_signal_new ("connection-error", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 2, GAEGULI_TYPE_TARGET, G_TYPE_ERROR);
}


static void
gaeguli_init_once (void)
{
  gst_init (NULL, NULL);
}

static void
gaeguli_pipeline_init (GaeguliPipeline * self)
{
  if (g_atomic_int_get (&gaeguli_init_refcnt) == 0) {
    gaeguli_init_once ();
  }

  g_atomic_int_inc (&gaeguli_init_refcnt);

  g_mutex_init (&self->lock);

  /* kv: hash(fifo-path), target_pipeline */
  self->targets = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);

  self->srtsocket_to_peer_addr =
      g_hash_table_new_full (NULL, NULL, NULL, g_free);
  self->benchmarks =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  self->adaptor_type = GAEGULI_TYPE_NULL_STREAM_ADAPTOR;
}

GaeguliPipeline *
gaeguli_pipeline_new_full (GaeguliVideoSource source, const gchar * device,
    GaeguliVideoResolution resolution, guint framerate)
{
  g_autoptr (GaeguliPipeline) pipeline = NULL;

  /* TODO:
   *   1. check if source is valid
   *   2. check if source is available
   *
   * perphas, implement GInitable?
   */

  g_debug ("source: [%d / %s]", source, device);
  pipeline = g_object_new (GAEGULI_TYPE_PIPELINE, "source", source, "device",
      device, "resolution", resolution, "framerate", framerate, NULL);

  return g_steal_pointer (&pipeline);
}

GaeguliPipeline *
gaeguli_pipeline_new (void)
{
  g_autoptr (GaeguliPipeline) pipeline = NULL;

  pipeline = gaeguli_pipeline_new_full (DEFAULT_VIDEO_SOURCE,
      DEFAULT_VIDEO_SOURCE_DEVICE, DEFAULT_VIDEO_RESOLUTION,
      DEFAULT_VIDEO_FRAMERATE);

  return g_steal_pointer (&pipeline);
}

static GstPadProbeReturn
_drop_reconfigure_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  if (GST_EVENT_TYPE (event) == GST_EVENT_RECONFIGURE) {
    return GST_PAD_PROBE_DROP;
  }

  return GST_PAD_PROBE_OK;
}

static gchar *
_get_source_description (GaeguliPipeline * self)
{
  g_autoptr (GEnumClass) enum_class =
      g_type_class_ref (GAEGULI_TYPE_VIDEO_SOURCE);
  GEnumValue *enum_value = g_enum_get_value (enum_class, self->source);
  GString *result = g_string_new (enum_value->value_nick);

  switch (self->source) {
    case GAEGULI_VIDEO_SOURCE_V4L2SRC:
      g_string_append_printf (result, " device=%s", self->device);
      break;
    case GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC:
      g_string_append (result, " is-live=1");
      break;
    case GAEGULI_VIDEO_SOURCE_NVARGUSCAMERASRC:
      g_string_append_printf (result, " sensor-id=%s", self->device);
      break;
    default:
      break;
  }

  return g_string_free (result, FALSE);
}

static gchar *
_get_vsrc_pipeline_string (GaeguliPipeline * self)
{
  g_autofree gchar *source = _get_source_description (self);

  return g_strdup_printf (GAEGULI_PIPELINE_VSRC_STR, source,
      self->source == GAEGULI_VIDEO_SOURCE_NVARGUSCAMERASRC ? "" :
      GAEGULI_PIPELINE_DECODEBIN_STR);
}

static void
_decodebin_pad_added (GstElement * decodebin, GstPad * pad, gpointer user_data)
{
  if (GST_PAD_PEER (pad) == NULL) {
    GstElement *overlay = GST_ELEMENT (user_data);
    g_autoptr (GstPad) sinkpad =
        gst_element_get_static_pad (overlay, "video_sink");

    gst_pad_link (pad, sinkpad);
  }
}

static gboolean
_bus_watch (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GaeguliPipeline *self = user_data;

  switch (message->type) {
    case GST_MESSAGE_WARNING:{
      gpointer target_id;
      GaeguliTarget *target;
      g_autoptr (GError) error = NULL;

      if (!message->src) {
        break;
      }

      target_id = g_object_get_data (G_OBJECT (message->src),
          "gaeguli-target-id");

      g_mutex_lock (&self->lock);
      target = g_hash_table_lookup (self->targets, target_id);
      g_mutex_unlock (&self->lock);

      if (!target) {
        break;
      }

      gst_message_parse_warning (message, &error, NULL);
      if (error && error->domain == GST_RESOURCE_ERROR) {
        g_signal_emit (self, signals[SIG_CONNECTION_ERROR], 0, target, error);
      }
      break;
    }
    default:
      break;
  }

  return G_SOURCE_CONTINUE;
}

static gboolean
_build_vsrc_pipeline (GaeguliPipeline * self, GError ** error)
{
  g_autofree gchar *vsrc_str = NULL;
  g_autoptr (GError) internal_err = NULL;
  g_autoptr (GstBus) bus = NULL;
  g_autoptr (GstElement) decodebin = NULL;
  g_autoptr (GstElement) tee = NULL;
  g_autoptr (GstPad) tee_sink = NULL;
  g_autoptr (GstPluginFeature) feature = NULL;

  /* FIXME: what if zero-copy */
  vsrc_str = _get_vsrc_pipeline_string (self);

  g_debug ("trying to create video source pipeline (%s)", vsrc_str);
  self->vsrc = gst_parse_launch (vsrc_str, &internal_err);

  if (internal_err) {
    g_warning ("failed to build source pipeline (%s)", internal_err->message);
    g_propagate_error (error, internal_err);
    goto failed;
  }

  self->pipeline = gst_pipeline_new (NULL);
  gst_bin_add (GST_BIN (self->pipeline), g_object_ref (self->vsrc));

  bus = gst_element_get_bus (self->pipeline);
  gst_bus_add_watch (bus, _bus_watch, self);

  self->overlay = gst_bin_get_by_name (GST_BIN (self->pipeline), "overlay");
  if (self->overlay)
    g_object_set (self->overlay, "silent", !self->show_overlay, NULL);

  /* Caps of the video source are determined by the caps filter in vsrc pipeline
   * and don't need to be renegotiated when a new target pipeline links to
   * the tee. Thus, ignore reconfigure events coming from downstream. */
  tee = gst_bin_get_by_name (GST_BIN (self->vsrc), "tee");
  tee_sink = gst_element_get_static_pad (tee, "sink");
  gst_pad_add_probe (tee_sink, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      _drop_reconfigure_cb, NULL, NULL);

  decodebin = gst_bin_get_by_name (GST_BIN (self->pipeline), "decodebin");
  if (decodebin) {
    g_signal_connect (decodebin, "pad-added",
        G_CALLBACK (_decodebin_pad_added), self->overlay);
  }

  if (self->prefer_hw_decoding) {
    /* Verify whether hardware accelearted vaapijpeg decoder is available.
     * If available, make sure that the decodebin picks it up. */
    feature = gst_registry_find_feature (gst_registry_get (), "vaapijpegdec",
        GST_TYPE_ELEMENT_FACTORY);
    if (feature)
      gst_plugin_feature_set_rank (feature, GST_RANK_PRIMARY + 100);
  }

  gaeguli_pipeline_update_vsrc_caps (self);

  gst_element_set_state (self->pipeline, GST_STATE_PLAYING);

  return TRUE;

failed:
  gst_clear_object (&self->vsrc);
  gst_clear_object (&self->pipeline);

  return FALSE;
}

static void
gaeguli_pipeline_update_vsrc_caps (GaeguliPipeline * self)
{
  gint width, height, i;
  g_autoptr (GstElement) capsfilter = NULL;
  g_autoptr (GstCaps) caps = NULL;

  if (!self->vsrc) {
    return;
  }

  switch (self->resolution) {
    case GAEGULI_VIDEO_RESOLUTION_640X480:
      width = 640;
      height = 480;
      break;
    case GAEGULI_VIDEO_RESOLUTION_1280X720:
      width = 1280;
      height = 720;
      break;
    case GAEGULI_VIDEO_RESOLUTION_1920X1080:
      width = 1920;
      height = 1080;
      break;
    case GAEGULI_VIDEO_RESOLUTION_3840X2160:
      width = 3840;
      height = 2160;
      break;
    default:
      width = -1;
      height = -1;
      break;
  }

  caps = gst_caps_new_empty ();

  for (i = 0; supported_formats[i] != NULL; i++) {
    GstCaps *supported_caps = gst_caps_from_string (supported_formats[i]);
    gst_caps_set_simple (supported_caps, "width", G_TYPE_INT, width, "height",
        G_TYPE_INT, height, "framerate", GST_TYPE_FRACTION, self->fps, 1, NULL);
    gst_caps_append (caps, supported_caps);
  }

  capsfilter = gst_bin_get_by_name (GST_BIN (self->pipeline), "caps");
  g_object_set (capsfilter, "caps", caps, NULL);

  /* Cycling vsrc through READY state prods decodebin into re-discovery of input
   * stream format and rebuilding its decoding pipeline. This is needed when
   * a switch is made between two resolutions that the connected camera can only
   * produce in different output formats, e.g. a change from raw 640x480 stream
   * to MJPEG 1920x1080.
   *
   * NVARGUS Camera src doesn't support this.
   */
  if (self->source != GAEGULI_VIDEO_SOURCE_NVARGUSCAMERASRC) {
    GstState cur_state;

    gst_element_get_state (self->vsrc, &cur_state, NULL, 0);
    if (cur_state > GST_STATE_READY) {
      gst_element_set_state (self->vsrc, GST_STATE_READY);
      gst_element_set_state (self->vsrc, cur_state);
    }
  }
}

static void
gaeguli_pipeline_emit_stream_started (GaeguliPipeline * self,
    GaeguliTarget * target)
{
  g_mutex_lock (&self->lock);
  ++self->num_active_targets;
  g_mutex_unlock (&self->lock);

  g_signal_emit (self, signals[SIG_STREAM_STARTED], 0, target);
}

static void
gaeguli_pipeline_emit_stream_stopped (GaeguliPipeline * self,
    GaeguliTarget * target)
{
  g_signal_emit (self, signals[SIG_STREAM_STOPPED], 0, target);

  g_mutex_lock (&self->lock);
  if (g_hash_table_size (self->targets) == 0 && --self->num_active_targets == 0) {
    g_mutex_unlock (&self->lock);
    gaeguli_pipeline_stop (self);
    g_mutex_lock (&self->lock);
  }
  g_mutex_unlock (&self->lock);

  /* Reference acquired in gaeguli_pipeline_remove_target(). */
  g_object_unref (self);
}

static void
gaeguli_pipeline_on_caller_added (GaeguliPipeline * self, gint srtsocket,
    GInetSocketAddress * address)
{
  if (g_hash_table_contains (self->srtsocket_to_peer_addr,
          GINT_TO_POINTER (srtsocket))) {
    g_warning ("Duplicate socket %d in %s", srtsocket, __FUNCTION__);
    return;
  }

  g_hash_table_insert (self->srtsocket_to_peer_addr,
      GINT_TO_POINTER (srtsocket),
      g_inet_address_to_string (g_inet_socket_address_get_address (address)));
}

static void
gaeguli_pipeline_on_caller_removed (GaeguliPipeline * self, int srtsocket,
    GInetAddress * address)
{
  g_hash_table_remove (self->srtsocket_to_peer_addr,
      GINT_TO_POINTER (srtsocket));
}

static gint32
gaeguli_pipeline_suggest_buffer_size_for_target (GaeguliPipeline * self,
    GaeguliTarget * target)
{
  Benchmark *b = g_hash_table_lookup (self->benchmarks,
      gaeguli_target_get_peer_address (target));
  guint64 bps;
  gint latency_ms;

  if (!b) {
    return 0;
  }

  g_object_get (target, "latency", &latency_ms, NULL);

  /* Based on buffer sizes calculation from
   * https://github.com/Haivision/srt/issues/703#issuecomment-495570496 */
  bps = b->bw_mbps * 1e6;
  return (latency_ms + b->rtt_ms / 2) * bps / 1000 / 8;
}

GaeguliTarget *
gaeguli_pipeline_add_srt_target_full (GaeguliPipeline * self,
    GaeguliVideoCodec codec, guint bitrate, const gchar * uri,
    const gchar * username, GError ** error)
{
  guint target_id = 0;
  GaeguliTarget *target = NULL;

  g_return_val_if_fail (GAEGULI_IS_PIPELINE (self), 0);
  g_return_val_if_fail (uri != NULL, 0);
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

  g_mutex_lock (&self->lock);

  /* assume that it's first target */
  if (self->vsrc == NULL && !_build_vsrc_pipeline (self, error)) {
    goto failed;
  }

  target_id = g_str_hash (uri);

  target = g_hash_table_lookup (self->targets, GINT_TO_POINTER (target_id));

  if (!target) {
    g_autoptr (GstElement) tee = NULL;
    g_autoptr (GstPad) tee_srcpad = NULL;
    g_autoptr (GError) internal_err = NULL;

    g_debug ("no target pipeline mapped with [%x]", target_id);

    tee = gst_bin_get_by_name (GST_BIN (self->vsrc), "tee");
    tee_srcpad = gst_element_request_pad (tee,
        gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (tee),
            "src_%u"), NULL, NULL);

    target = gaeguli_target_new (tee_srcpad, target_id, codec, bitrate,
        self->fps, uri, username, &internal_err);

    if (target == NULL) {
      g_propagate_error (error, internal_err);
      internal_err = NULL;
      goto failed;
    }

    if (self->benchmark_interval_ms != 0 &&
        gaeguli_target_get_srt_mode (target) == GAEGULI_SRT_MODE_CALLER) {
      gint32 buffer;

      buffer = gaeguli_pipeline_suggest_buffer_size_for_target (self, target);
      if (buffer > 0) {
        g_debug ("Setting buffer sizes for [%x] to %d", target_id, buffer);

        g_object_set (target, "buffer-size", buffer, NULL);
      } else {
        g_debug ("No buffer suggestion for [%x]", target_id);
      }
    }

    g_object_set (target, "adaptor-type", self->adaptor_type, NULL);

    g_signal_connect_swapped (target, "stream-started",
        G_CALLBACK (gaeguli_pipeline_emit_stream_started), self);
    g_signal_connect_swapped (target, "stream-stopped",
        G_CALLBACK (gaeguli_pipeline_emit_stream_stopped), self);
    g_signal_connect_swapped (target, "caller-added",
        G_CALLBACK (gaeguli_pipeline_on_caller_added), self);
    g_signal_connect_swapped (target, "caller-removed",
        G_CALLBACK (gaeguli_pipeline_on_caller_removed), self);

    g_hash_table_insert (self->targets, GINT_TO_POINTER (target_id), target);
  }

  g_mutex_unlock (&self->lock);

  return target;

failed:
  g_mutex_unlock (&self->lock);

  g_debug ("failed to add target");
  return NULL;
}

GaeguliTarget *
gaeguli_pipeline_add_srt_target (GaeguliPipeline * self,
    const gchar * uri, const gchar * username, GError ** error)
{
  return gaeguli_pipeline_add_srt_target_full (self, DEFAULT_VIDEO_CODEC,
      DEFAULT_VIDEO_BITRATE, uri, username, error);
}

GaeguliReturn
gaeguli_pipeline_remove_target (GaeguliPipeline * self, GaeguliTarget * target,
    GError ** error)
{
  g_return_val_if_fail (GAEGULI_IS_PIPELINE (self), GAEGULI_RETURN_FAIL);
  g_return_val_if_fail (target != NULL, GAEGULI_RETURN_FAIL);
  g_return_val_if_fail (error == NULL || *error == NULL, GAEGULI_RETURN_FAIL);

  g_mutex_lock (&self->lock);

  if (!g_hash_table_steal (self->targets, GINT_TO_POINTER (target->id))) {
    g_debug ("no target pipeline mapped with [%x]", target->id);
    goto out;
  }

  gaeguli_target_unlink (target);
  if (gaeguli_target_get_state (target) == GAEGULI_TARGET_STATE_STOPPING) {
    /* Target removal will happen asynchronously. Keep the pipeline alive
     * until the target fires "stream-stopped". */
    g_object_ref (self);
  }
  g_object_unref (target);

out:
  g_mutex_unlock (&self->lock);

  return GAEGULI_RETURN_OK;
}

/* Must be called from the main thread. */
void
gaeguli_pipeline_stop (GaeguliPipeline * self)
{
  g_return_if_fail (GAEGULI_IS_PIPELINE (self));

  g_debug ("clear internal pipeline");

  if (self->pipeline) {
    gst_element_set_state (self->pipeline, GST_STATE_NULL);
  }

  g_mutex_lock (&self->lock);

  g_clear_pointer (&self->vsrc, gst_object_unref);
  g_clear_pointer (&self->overlay, gst_object_unref);
  gst_clear_object (&self->pipeline);

  g_mutex_unlock (&self->lock);
}

void
gaeguli_pipeline_dump_to_dot_file (GaeguliPipeline * self)
{
  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, g_get_prgname ());
}
