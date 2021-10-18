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
#include "messenger.h"
#include "adaptors/nulladaptor.h"

#include <gio/gio.h>

#define GAEGULI_PIPELINE_WORKER_ARGS_NUM 4

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

  GstElement *snapshot_valve;
  GstElement *snapshot_jpegenc;
  GstElement *snapshot_jifmux;
  GQueue *snapshot_tasks;
  guint num_snapshots_to_encode;
  guint snapshot_quality;
  GaeguliIDCTMethod snapshot_idct_method;

  GstElement *overlay;
  gboolean show_overlay;

  guint benchmark_interval_ms;
  guint benchmark_timeout_id;
  GHashTable *srtsocket_to_peer_addr;
  GHashTable *benchmarks;

  gboolean prefer_hw_decoding;

  GMainLoop *loop;
  guint pw_node_id;
  GPid worker_pid;
  GaeguliMessenger *messenger;

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
  PROP_SNAPSHOT_QUALITY,
  PROP_SNAPSHOT_IDCT_METHOD,

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

#define LOCK_PIPELINE \
  g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&self->lock)

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
  g_clear_pointer (&self->snapshot_tasks, g_queue_free);
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

  self->snapshot_tasks = g_queue_new ();
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

static void
gaeguli_pipeline_set_snapshot_tags (GaeguliPipeline * self, GVariant * tags)
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
_on_valve_buffer (GstPad * pad, GstPadProbeInfo * info, GaeguliPipeline * self)
{
  GTask *task;

  LOCK_PIPELINE;

  task = g_queue_peek_head (self->snapshot_tasks);
  if (task) {
    GVariant *tags = g_task_get_task_data (task);
    if (tags) {
      gaeguli_pipeline_set_snapshot_tags (self, tags);
    }
  }

  if (--self->num_snapshots_to_encode == 0) {
    /* No pending snapshot requests, close the valve. */
    g_object_set (GST_PAD_PARENT (pad), "drop", TRUE, NULL);
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
_get_pipeline_id_description (GaeguliPipeline * self)
{
  GString *desc = g_string_new (NULL);

  g_string_append_printf (desc, "%d_%p", getpid (), self);

  return g_string_free (desc, FALSE);
}

static gchar *
_get_stream_props_description (GaeguliPipeline * self)
{
  GString *stream_props = g_string_new (PIPEWIRE_NODE_STREAM_PROPERTIES_STR);

  g_string_append_printf (stream_props, ",%s=%s",
      GAEGULI_PIPELINE_TAG, _get_pipeline_id_description (self));

  return g_string_free (stream_props, FALSE);
}

static gchar *
_get_vsrc_pipeline_string (GaeguliPipeline * self)
{
  g_autofree gchar *source = _get_source_description (self);
  g_autofree gchar *props = _get_stream_props_description (self);

  return g_strdup_printf (GAEGULI_PIPELINE_VSRC_STR, source,
      self->source == GAEGULI_VIDEO_SOURCE_NVARGUSCAMERASRC ? "" :
      GAEGULI_PIPELINE_DECODEBIN_STR, props);
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
_is_target_device (GstDevice * target_device)
{
  gchar *device_class;
  gboolean res = FALSE;

  device_class = gst_device_get_device_class (target_device);

  if (g_strrstr (device_class, PIPEWIRE_MEDIA_CLASS_STR)) {
    res = TRUE;
  }
  g_free (device_class);

  return res;
}

static guint
_get_pw_nodeid (GstDevice * target_device, gchar * device_id)
{
  GstStructure *props;
  guint node_id = 0;

  props = gst_device_get_properties (target_device);
  if (props) {
    const gchar *node_str, *node_id_str;

    if (gst_structure_has_field (props, GAEGULI_PIPELINE_TAG)
        && gst_structure_has_field (props, PIPEWIRE_NODE_ID_STR)) {
      node_str = gst_structure_get_string (props, GAEGULI_PIPELINE_TAG);
      if (!g_strcmp0 (node_str, device_id)) {
        node_id_str = gst_structure_get_string (props, PIPEWIRE_NODE_ID_STR);
        node_id = atoi (node_id_str);
        g_debug ("Got node_id = %d", node_id);
      }
    }
    gst_structure_free (props);
  }

  return node_id;
}

static gboolean
_bus_device_monitor (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GaeguliPipeline *self = user_data;

  GstDevice *device;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_DEVICE_ADDED:{
      gst_message_parse_device_added (message, &device);
      if (_is_target_device (device)) {
        g_autofree gchar *pipeline_id = _get_pipeline_id_description (self);
        self->pw_node_id = _get_pw_nodeid (device, pipeline_id);
        if (self->pw_node_id > 0) {
          g_main_loop_quit (self->loop);
        }
      }
      gst_object_unref (device);
    }
      break;

    default:
      break;
  }

  return G_SOURCE_CONTINUE;
}

static void
gaeguli_pipeline_create_snapshot (GaeguliPipeline * self, GstBuffer * buffer)
{
  g_autoptr (GTask) task = NULL;
  GstMapInfo info;

  g_return_if_fail (!g_queue_is_empty (self->snapshot_tasks));

  {
    LOCK_PIPELINE;
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

static void
gaeguli_pipeline_create_device_monitor (GaeguliPipeline * self)
{
  GstBus *bus;
  GstCaps *caps;
  GstDeviceMonitor *device_monitor;

  device_monitor = gst_device_monitor_new ();

  bus = gst_device_monitor_get_bus (device_monitor);
  gst_bus_add_watch (bus, _bus_device_monitor, self);
  gst_object_unref (bus);

  caps = gst_caps_new_empty_simple ("video/x-raw");
  gst_device_monitor_add_filter (device_monitor, "Video", caps);
  gst_caps_unref (caps);

  self->loop = g_main_loop_new (NULL, FALSE);
  gst_device_monitor_start (device_monitor);
  g_main_loop_run (self->loop);
  g_main_loop_unref (self->loop);
  self->loop = NULL;

  gst_device_monitor_stop (device_monitor);
  gst_object_unref (device_monitor);
}

static void
_cb_child_watch (GPid pid, gint status, gpointer * data)
{
  /* Close pid */
  g_debug ("closing process with pid: %d\n", pid);
  g_spawn_close_pid (pid);
}

static gchar **
_gaeguli_build_pipeline_worker_args (int child_readfd, int child_writefd)
{
  gsize n_bytes = (GAEGULI_PIPELINE_WORKER_ARGS_NUM * sizeof (gchar *));
  gchar **args = g_malloc (n_bytes);
  if (args) {
    gint i = 0;

    if (g_file_test (GAEGULI_PIPELINE_WORKER, G_FILE_TEST_EXISTS)) {
      args[i++] = g_strdup (GAEGULI_PIPELINE_WORKER);
    } else {
      args[i++] = g_strdup (GAEGULI_PIPELINE_WORKER_ALT);
    }
    args[i++] = g_strdup_printf ("%d", child_readfd);
    args[i++] = g_strdup_printf ("%d", child_writefd);

    args[i++] = NULL;
  }
  return args;
}

static void
_child_setup (int *closefds)
{
  close (*closefds++);
  close (*closefds);
}

static gboolean
_build_vsrc_pipeline (GaeguliPipeline * self, GError ** error)
{
  g_autofree gchar *vsrc_str = NULL;
  g_autoptr (GError) internal_err = NULL;
  g_autoptr (GstBus) bus = NULL;
  g_autoptr (GstElement) decodebin = NULL;
  g_autoptr (GstElement) tee = NULL;
  g_autoptr (GstElement) fakesink = NULL;
  g_autoptr (GstPad) tee_sink = NULL;
  g_autoptr (GstPad) valve_src = NULL;
  g_autoptr (GstPluginFeature) feature = NULL;
  int readpipe[2] = { -1, -1 };
  int writepipe[2] = { -1, -1 };
  int closefds[2];

  g_autoptr (GError) gerr = NULL;
  gboolean ret = TRUE;
  GPid pid;
  gchar **args = NULL;

  /* Create pipes for IPC */
  if (pipe (readpipe) < 0 || pipe (writepipe) < 0) {
    g_error ("Failed to initialize IPC");
    goto failed;
  }

  args = _gaeguli_build_pipeline_worker_args (writepipe[0], readpipe[1]);
  if (!args) {
    goto failed;
  }

  /* File descriptors to close before the child's exec() is run. */
  closefds[0] = readpipe[0];
  closefds[1] = writepipe[1];

  self->messenger = gaeguli_messenger_new (readpipe[0], writepipe[1]);

  if (!g_spawn_async (NULL, args, NULL,
          G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
          (GSpawnChildSetupFunc) _child_setup, &closefds, &pid, &gerr)) {
    g_error ("spawning pipeline worker failed. %s", gerr->message);
    goto failed;
  } else {
    g_debug ("A new child process spawned with pid %d\n", pid);
    self->worker_pid = pid;
    g_child_watch_add (pid, (GChildWatchFunc) _cb_child_watch, NULL);
  }

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

  self->snapshot_valve = gst_bin_get_by_name (GST_BIN (self->pipeline),
      "valve");
  if (self->snapshot_valve) {
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
        G_CALLBACK (gaeguli_pipeline_create_snapshot), self);
  }

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

  gaeguli_pipeline_create_device_monitor (self);

  goto out;

failed:
  gst_clear_object (&self->vsrc);
  gst_clear_object (&self->pipeline);
  if (readpipe[0] != -1)
    close (readpipe[0]);
  if (writepipe[1] != -1)
    close (writepipe[1]);

  ret = FALSE;

out:
  if (readpipe[1] != -1)
    close (readpipe[1]);
  if (writepipe[0] != -1)
    close (writepipe[0]);

  g_strfreev (args);

  return ret;
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

static GaeguliTarget *
gaeguli_pipeline_add_target (GaeguliPipeline * self,
    GaeguliVideoCodec codec, guint bitrate,
    const gchar * uri, const gchar * username,
    const gchar * location, gboolean is_record_target, GError ** error)
{
  guint target_id = 0;
  GaeguliTarget *target = NULL;

  g_return_val_if_fail (GAEGULI_IS_PIPELINE (self), 0);
  if (!is_record_target) {
    g_return_val_if_fail (uri != NULL, 0);
  } else {
    g_return_val_if_fail (location != NULL, 0);
  }
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

  g_mutex_lock (&self->lock);

  /* assume that it's first target */
  if (self->vsrc == NULL && !is_record_target
      && !_build_vsrc_pipeline (self, error)) {
    goto failed;
  }

  if (!is_record_target) {
    target_id = g_str_hash (uri);
  } else {
    target_id = g_str_hash (location);
  }

  target = g_hash_table_lookup (self->targets, GINT_TO_POINTER (target_id));

  if (!target) {
    g_autoptr (GError) internal_err = NULL;

    g_debug ("no target pipeline mapped with [%x]", target_id);

    target = gaeguli_target_new (target_id, codec, bitrate,
        self->fps, uri, username, is_record_target, location,
        self->pw_node_id, &internal_err);

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

    if (!is_record_target) {
      g_signal_connect_swapped (target, "stream-started",
          G_CALLBACK (gaeguli_pipeline_emit_stream_started), self);
      g_signal_connect_swapped (target, "stream-stopped",
          G_CALLBACK (gaeguli_pipeline_emit_stream_stopped), self);
      g_signal_connect_swapped (target, "caller-added",
          G_CALLBACK (gaeguli_pipeline_on_caller_added), self);
      g_signal_connect_swapped (target, "caller-removed",
          G_CALLBACK (gaeguli_pipeline_on_caller_removed), self);
    }
    g_hash_table_insert (self->targets, GINT_TO_POINTER (target_id), target);
  } else {
    if (is_record_target) {
      g_warning ("Record target already exists for given location %s",
          location);
    }
  }

  g_mutex_unlock (&self->lock);

  return target;

failed:
  g_mutex_unlock (&self->lock);

  g_debug ("failed to add target");
  return NULL;
}

GaeguliTarget *
gaeguli_pipeline_add_recording_target_full (GaeguliPipeline * self,
    GaeguliVideoCodec codec, guint bitrate,
    const gchar * location, GError ** error)
{
  return gaeguli_pipeline_add_target (self, codec,
      bitrate, NULL, NULL, location, TRUE, error);
}

GaeguliTarget *
gaeguli_pipeline_add_recording_target (GaeguliPipeline * self,
    const gchar * location, GError ** error)
{
  return gaeguli_pipeline_add_recording_target_full (self, DEFAULT_VIDEO_CODEC,
      DEFAULT_VIDEO_BITRATE, location, error);
}

GaeguliTarget *
gaeguli_pipeline_add_srt_target_full (GaeguliPipeline * self,
    GaeguliVideoCodec codec, guint bitrate, const gchar * uri,
    const gchar * username, GError ** error)
{
  return gaeguli_pipeline_add_target (self, codec,
      bitrate, uri, username, NULL, FALSE, error);
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

  g_mutex_unlock (&self->lock);

  gaeguli_target_stop (target);
  g_object_unref (target);

out:

  return GAEGULI_RETURN_OK;
}

void
gaeguli_pipeline_create_snapshot_async (GaeguliPipeline * self, GVariant * tags,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data)
{
  g_autoptr (GVariant) tags_autoptr = tags;
  g_autoptr (GTask) task = NULL;
  GError *error = NULL;

  LOCK_PIPELINE;

  task = g_task_new (self, cancellable, callback, user_data);

  if (self->vsrc == NULL && !_build_vsrc_pipeline (self, &error)) {
    g_task_return_error (task, error);
    return;
  }

  if (tags && !g_variant_is_of_type (tags, G_VARIANT_TYPE_VARDICT)) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
        "Tags must be NULL or of variant type 'a{sv}'");
  }

  if (tags) {
    g_task_set_task_data (task, g_steal_pointer (&tags_autoptr),
        (GDestroyNotify) g_variant_unref);
  }

  g_queue_push_tail (self->snapshot_tasks, g_steal_pointer (&task));
  if (self->num_snapshots_to_encode++ == 0) {
    g_object_set (self->snapshot_valve, "drop", FALSE, NULL);
  }
}

GBytes *
gaeguli_pipeline_create_snapshot_finish (GaeguliPipeline * self,
    GAsyncResult * result, GError ** error)
{
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/* Must be called from the main thread. */
void
gaeguli_pipeline_stop (GaeguliPipeline * self)
{
  g_return_if_fail (GAEGULI_IS_PIPELINE (self));

  g_debug ("clear internal pipeline");

  g_mutex_lock (&self->lock);

  if (self->messenger) {
    gaeguli_messenger_send_terminate (self->messenger);
  }

  while (!g_queue_is_empty (self->snapshot_tasks)) {
    g_autoptr (GTask) task = g_queue_pop_head (self->snapshot_tasks);
    g_task_return_new_error (task, GAEGULI_RESOURCE_ERROR,
        GAEGULI_RESOURCE_ERROR_STOPPED, "The pipeline has been stopped");
  }

  g_clear_pointer (&self->vsrc, gst_object_unref);
  g_clear_pointer (&self->overlay, gst_object_unref);
  gst_clear_object (&self->snapshot_valve);
  gst_clear_object (&self->snapshot_jpegenc);
  gst_clear_object (&self->snapshot_jifmux);
  gst_clear_object (&self->pipeline);
  g_clear_object (&self->messenger);

  g_mutex_unlock (&self->lock);
}

void
gaeguli_pipeline_dump_to_dot_file (GaeguliPipeline * self)
{
  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, g_get_prgname ());
}
