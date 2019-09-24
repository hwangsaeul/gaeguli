/**
 *  Copyright 2019 SK Telecom Co., Ltd.
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

#include "pipeline.h"

#include "types.h"
#include "enumtypes.h"
#include "gaeguli-internal.h"

#include <unistd.h>

#include <gst/gst.h>

/* *INDENT-OFF* */
#if !GLIB_CHECK_VERSION(2,57,1)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GEnumClass, g_type_class_unref)
#endif
/* *INDENT-ON* */

#define GAEGULI_PIPELINE_VSRC_STR       "%s %s%s ! video/x-raw,width=%d,height=%d ! tee name=tee allow-not-linked=1 "

#define GAEGULI_PIPELINE_H264ENC_STR    "\
        queue name=enc_first ! videoconvert ! x264enc tune=zerolatency ! \
        h264parse "

// ! video/x-h264,alignment=au ! identity name=enc_last "

#define GAEGULI_PIPELINE_MUXSINK_STR    "\
        mpegtsmux name=muxsink_first ! tsparse set-timestamps=1 smoothing-latency=1000 ! \
        filesink name=muxink_last location=%s buffer-mode=unbuffered"

struct _GaeguliPipeline
{
  GObject parent;

  GMutex lock;

  GaeguliVideoSource source;
  gchar *device;

  GHashTable *targets;

  GstElement *pipeline;
  GstElement *vsrc;

  guint stop_pipeline_event_id;
};

typedef struct _LinkTarget
{
  gint refcount;

  GaeguliPipeline *self;
  gboolean link;
  guint target_id;

  GstElement *src;
  GstElement *target;
} LinkTarget;

static LinkTarget *
link_target_new (GaeguliPipeline * self, GstElement * src, guint target_id,
    GstElement * target, gboolean link)
{
  LinkTarget *t = g_new0 (LinkTarget, 1);

  t->refcount = 1;

  t->self = g_object_ref (self);
  t->src = g_object_ref (src);
  t->target = g_object_ref (target);
  t->target_id = target_id;
  t->link = link;

  return t;
}

#if 0
static LinkTarget *
link_target_ref (LinkTarget * link_target)
{
  g_return_val_if_fail (link_target != NULL, NULL);
  g_return_val_if_fail (link_target->self != NULL, NULL);
  g_return_val_if_fail (link_target->src != NULL, NULL);
  g_return_val_if_fail (link_target->target != NULL, NULL);

  g_atomic_int_inc (&link_target->refcount);

  return link_target;
}
#endif

static void
link_target_unref (LinkTarget * link_target)
{
  g_return_if_fail (link_target != NULL);
  g_return_if_fail (link_target->self != NULL);
  g_return_if_fail (link_target->src != NULL);
  g_return_if_fail (link_target->target != NULL);

  if (g_atomic_int_dec_and_test (&link_target->refcount)) {
    g_clear_object (&link_target->self);
    g_clear_object (&link_target->src);
    g_clear_object (&link_target->target);
    g_free (link_target);
  }
}

/* *INDENT-OFF* */
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LinkTarget, link_target_unref)
/* *INDENT-ON* */

typedef enum
{
  PROP_SOURCE = 1,
  PROP_DEVICE,

  /*< private > */
  PROP_LAST = PROP_DEVICE,
} _GaeguliPipelineProperty;

static GParamSpec *properties[PROP_LAST + 1];

enum
{
  SIG_STREAM_STARTED,
  SIG_STREAM_STOPPED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliPipeline, gaeguli_pipeline, G_TYPE_OBJECT)
/* *INDENT-ON* */

static void
gaeguli_pipeline_dispose (GObject * object)
{
  GaeguliPipeline *self = GAEGULI_PIPELINE (object);

  if (self->pipeline) {
    g_critical ("Call gaeguli_pipeline_stop() before releasing the final "
        "GaeguliPipeline reference!");
  }

  g_clear_pointer (&self->targets, g_hash_table_unref);
  g_clear_pointer (&self->device, g_free);

  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (gaeguli_pipeline_parent_class)->dispose (object);
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
  object_class->dispose = gaeguli_pipeline_dispose;

  properties[PROP_SOURCE] = g_param_spec_enum ("source", "source", "source",
      GAEGULI_TYPE_VIDEO_SOURCE, DEFAULT_VIDEO_SOURCE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_DEVICE] = g_param_spec_string ("device", "device", "device",
      DEFAULT_VIDEO_SOURCE_DEVICE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties),
      properties);

  signals[SIG_STREAM_STARTED] =
      g_signal_new ("stream-started", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[SIG_STREAM_STOPPED] =
      g_signal_new ("stream-stopped", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
gaeguli_pipeline_init (GaeguliPipeline * self)
{
  g_mutex_init (&self->lock);

  /* kv: hash(fifo-path), target_pipeline */
  self->targets = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) g_object_unref);
}

static GaeguliPipeline *
gaeguli_pipeline_new_full (GaeguliVideoSource source, const gchar * device)
{
  g_autoptr (GaeguliPipeline) pipeline = NULL;

  /* TODO:
   *   1. check if source is valid
   *   2. check if source is available
   *
   * perphas, implement GInitable?
   */

  g_debug ("source: [%d / %s]", source, device);
  pipeline =
      g_object_new (GAEGULI_TYPE_PIPELINE, "source", source, "device", device,
      NULL);

  return g_steal_pointer (&pipeline);
}

GaeguliPipeline *
gaeguli_pipeline_new (void)
{
  g_autoptr (GaeguliPipeline) pipeline = NULL;

  pipeline =
      gaeguli_pipeline_new_full (DEFAULT_VIDEO_SOURCE,
      DEFAULT_VIDEO_SOURCE_DEVICE);

  return g_steal_pointer (&pipeline);
}

static GstElement *
_build_target_pipeline (GaeguliVideoCodec codec, const gchar * fifo_path,
    GError ** error)
{
  g_autoptr (GstElement) target_pipeline = NULL;
  g_autoptr (GstElement) enc_first = NULL;
  g_autoptr (GstPad) enc_sinkpad = NULL;
  GstPad *ghost_pad = NULL;
  g_autofree gchar *pipeline_str = NULL;
  g_autoptr (GError) internal_err = NULL;

  switch (codec) {
    case GAEGULI_VIDEO_CODEC_H264:
      pipeline_str = g_strdup_printf (GAEGULI_PIPELINE_H264ENC_STR " ! "
          GAEGULI_PIPELINE_MUXSINK_STR, fifo_path);
      break;
    default:
      g_error ("requested unsupported codec");
      break;
  }

  target_pipeline = gst_parse_launch (pipeline_str, &internal_err);
  if (target_pipeline == NULL) {
    g_error ("failed to build muxsink pipeline (%s)", internal_err->message);
    g_propagate_error (error, internal_err);
    goto failed;
  }

  enc_first = gst_bin_get_by_name (GST_BIN (target_pipeline), "enc_first");
  enc_sinkpad = gst_element_get_static_pad (enc_first, "sink");
  ghost_pad = gst_ghost_pad_new ("ghost_sink", enc_sinkpad);
  gst_element_add_pad (target_pipeline, ghost_pad);

  return g_steal_pointer (&target_pipeline);

failed:
  return NULL;
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

static gboolean
_build_vsrc_pipeline (GaeguliPipeline * self, GaeguliVideoResolution resolution,
    GError ** error)
{
  g_autofree gchar *vsrc_str = NULL;
  gint width, height;
  g_autoptr (GError) internal_err = NULL;
  g_autoptr (GEnumClass) enum_class =
      g_type_class_ref (GAEGULI_TYPE_VIDEO_SOURCE);
  GEnumValue *enum_value = g_enum_get_value (enum_class, self->source);
  g_autoptr (GstElement) tee = NULL;
  g_autoptr (GstPad) tee_sink = NULL;

  switch (resolution) {
    case GAEGULI_VIDEO_RESOLUTION_640x480:
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

  /* FIXME: what if zero-copy */
  vsrc_str =
      g_strdup_printf (GAEGULI_PIPELINE_VSRC_STR, enum_value->value_nick,
      self->source == GAEGULI_VIDEO_SOURCE_V4L2SRC ? "device=" : "",
      self->device, width, height);

  g_debug ("trying to create video source pipeline (%s)", vsrc_str);
  self->vsrc = gst_parse_launch (vsrc_str, &internal_err);

  if (self->vsrc == NULL) {
    g_error ("failed to build source pipeline (%s)", internal_err->message);
    g_propagate_error (error, internal_err);
    goto failed;
  }

  self->pipeline = gst_pipeline_new (NULL);
  gst_bin_add (GST_BIN (self->pipeline), g_object_ref (self->vsrc));

  /* Caps of the video source are determined by the caps filter in vsrc pipeline
   * and don't need to be renegotiated when a new target pipeline links to
   * the tee. Thus, ignore reconfigure events coming from downstream. */
  tee = gst_bin_get_by_name (GST_BIN (self->vsrc), "tee");
  tee_sink = gst_element_get_static_pad (tee, "sink");
  gst_pad_add_probe (tee_sink, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      _drop_reconfigure_cb, NULL, NULL);

  gst_element_set_state (self->pipeline, GST_STATE_PLAYING);

  return TRUE;

failed:
  g_clear_object (&self->vsrc);
  g_clear_object (&self->pipeline);

  return FALSE;
}

static gboolean
_stop_pipeline (GaeguliPipeline * self)
{
  gaeguli_pipeline_stop (self);

  return G_SOURCE_REMOVE;
}

static GstPadProbeReturn
_link_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  LinkTarget *link_target = user_data;
  g_autoptr (GstPad) sink_pad = NULL;
  g_autoptr (GaeguliPipeline) self = g_object_ref (link_target->self);

  /* 
   * GST_PAD_PROBE_TYPE_IDLE can cause infinite waiting in filesink.
   * In addition, to prevent events generated in gst_ghost_pad_new()
   * from invoking this probe callback again, we remove the probe first.
   *
   * For more details, refer to
   * https://github.com/hwangsaeul/gaeguli/pull/10#discussion_r327031325
   */
  gst_pad_remove_probe (pad, info->id);

  if (link_target->link) {
    GstPad *tee_ghost_pad = NULL;

    /* linking */
    g_debug ("start link target [%x]", link_target->target_id);

    tee_ghost_pad = gst_ghost_pad_new (NULL, pad);
    gst_element_add_pad (link_target->self->vsrc, tee_ghost_pad);
    sink_pad = gst_element_get_static_pad (link_target->target, "ghost_sink");

    if (sink_pad == NULL) {
      g_error ("sink pad is null");
    }
    if (tee_ghost_pad == NULL) {
      g_error ("ghost pad is null");
    }
    gst_element_sync_state_with_parent (link_target->target);
    if (gst_pad_link (tee_ghost_pad, sink_pad) != GST_PAD_LINK_OK) {
      g_error ("failed to link tee src to target sink");
    }
    g_signal_emit (link_target->self, signals[SIG_STREAM_STARTED], 0,
        link_target->target_id);
  } else {
    /* unlinking */

    g_autoptr (GstPad) ghost_sinkpad =
        gst_element_get_static_pad (link_target->target, "ghost_sink");
    g_autoptr (GstPad) ghost_srcpad = gst_pad_get_peer (ghost_sinkpad);
    g_autoptr (GstPad) srcpad =
        gst_ghost_pad_get_target (GST_GHOST_PAD (ghost_srcpad));
    g_autoptr (GstElement) tee =
        gst_bin_get_by_name (GST_BIN (link_target->self->vsrc), "tee");

    g_debug ("start unlink target [%x]", link_target->target_id);

    if (!gst_pad_unlink (ghost_srcpad, ghost_sinkpad)) {
      g_error ("failed to unlink");
    }

    gst_ghost_pad_set_target (GST_GHOST_PAD (ghost_srcpad), NULL);
    gst_element_remove_pad (link_target->self->vsrc, ghost_srcpad);
    gst_element_release_request_pad (tee, srcpad);
    gst_bin_remove (GST_BIN (link_target->self->pipeline), link_target->target);
    gst_element_set_state (link_target->target, GST_STATE_NULL);

    g_signal_emit (self, signals[SIG_STREAM_STOPPED], 0,
        link_target->target_id);

    g_mutex_lock (&link_target->self->lock);

    g_hash_table_remove (link_target->self->targets,
        GINT_TO_POINTER (link_target->target_id));
    if (g_hash_table_size (self->targets) == 0 &&
        self->stop_pipeline_event_id == 0) {
      self->stop_pipeline_event_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
          (GSourceFunc) _stop_pipeline,
          gst_object_ref (self), gst_object_unref);
    }

    g_mutex_unlock (&link_target->self->lock);
  }

  return GST_PAD_PROBE_REMOVE;
}

guint
gaeguli_pipeline_add_fifo_target_full (GaeguliPipeline * self,
    GaeguliVideoCodec codec,
    GaeguliVideoResolution resolution, const gchar * fifo_path, GError ** error)
{
  guint target_id = 0;
  g_autoptr (GstElement) target_pipeline = NULL;

  g_return_val_if_fail (GAEGULI_IS_PIPELINE (self), 0);
  g_return_val_if_fail (fifo_path != NULL, 0);
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

  g_mutex_lock (&self->lock);

  if (access (fifo_path, W_OK) != 0) {
    g_error ("Can't write to fifo (%s)", fifo_path);
    g_set_error (error, GAEGULI_RESOURCE_ERROR, GAEGULI_RESOURCE_ERROR_WRITE,
        "Can't access to fifo: %s", fifo_path);
    goto failed;
  }

  /* Halt vsrc pipeline removal if planned. */
  if (self->stop_pipeline_event_id != 0) {
    g_source_remove (self->stop_pipeline_event_id);
    self->stop_pipeline_event_id = 0;
  }

  /* assume that it's first target */
  if (self->vsrc == NULL && !_build_vsrc_pipeline (self, resolution, error)) {
    goto failed;
  }

  target_id = g_str_hash (fifo_path);
  target_pipeline =
      g_hash_table_lookup (self->targets, GINT_TO_POINTER (target_id));

  if (target_pipeline == NULL) {
    GstPadTemplate *templ = NULL;

    g_autoptr (GstElement) tee = NULL;
    g_autoptr (GstPad) tee_srcpad = NULL;

    g_autoptr (LinkTarget) link_target = NULL;
    g_autoptr (GError) internal_err = NULL;

    g_debug ("no target pipeline mapped with [%x]", target_id);

    target_pipeline = _build_target_pipeline (codec, fifo_path, &internal_err);

    /* linking target pipeline with vsrc */
    if (target_pipeline == NULL) {
      g_propagate_error (error, internal_err);
      goto failed;
    }

    gst_bin_add (GST_BIN (self->pipeline), g_object_ref (target_pipeline));
    g_hash_table_insert (self->targets, GINT_TO_POINTER (target_id),
        g_object_ref (target_pipeline));

    tee = gst_bin_get_by_name (GST_BIN (self->vsrc), "tee");
    templ =
        gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (tee),
        "src_%u");
    tee_srcpad = gst_element_request_pad (tee, templ, NULL, NULL);

    link_target =
        link_target_new (self, self->vsrc, target_id, target_pipeline, TRUE);

    gst_pad_add_probe (tee_srcpad, GST_PAD_PROBE_TYPE_BLOCK, _link_probe_cb,
        g_steal_pointer (&link_target), (GDestroyNotify) link_target_unref);
  }

  g_mutex_unlock (&self->lock);

  return target_id;

failed:
  g_mutex_unlock (&self->lock);

  g_debug ("failed to add target");
  return 0;
}

guint
gaeguli_pipeline_add_fifo_target (GaeguliPipeline * self,
    const gchar * fifo_path, GError ** error)
{
  return gaeguli_pipeline_add_fifo_target_full (self, DEFAULT_VIDEO_CODEC,
      DEFAULT_VIDEO_RESOLUTION, fifo_path, error);
}

GaeguliReturn
gaeguli_pipeline_remove_target (GaeguliPipeline * self, guint target_id,
    GError ** error)
{
  GstElement *target_pipeline;

  g_autoptr (GstPad) ghost_sinkpad = NULL;
  g_autoptr (GstPad) ghost_srcpad = NULL;
  g_autoptr (GstPad) tee_srcpad = NULL;

  g_autoptr (LinkTarget) link_target = NULL;

  g_return_val_if_fail (GAEGULI_IS_PIPELINE (self), GAEGULI_RETURN_FAIL);
  g_return_val_if_fail (target_id != 0, GAEGULI_RETURN_FAIL);
  g_return_val_if_fail (error == NULL || *error == NULL, GAEGULI_RETURN_FAIL);

  g_mutex_lock (&self->lock);

  target_pipeline =
      g_hash_table_lookup (self->targets, GINT_TO_POINTER (target_id));
  if (target_pipeline == NULL) {
    g_debug ("no target pipeline mapped with [%x]", target_id);
    goto out;
  }

  ghost_sinkpad = gst_element_get_static_pad (target_pipeline, "ghost_sink");
  ghost_srcpad = gst_pad_get_peer (ghost_sinkpad);
  tee_srcpad = gst_ghost_pad_get_target (GST_GHOST_PAD (ghost_srcpad));

  link_target =
      link_target_new (self, self->vsrc, target_id, target_pipeline, FALSE);

  gst_pad_add_probe (tee_srcpad, GST_PAD_PROBE_TYPE_BLOCK, _link_probe_cb,
      g_steal_pointer (&link_target), (GDestroyNotify) link_target_unref);


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

  g_mutex_lock (&self->lock);

  if (self->stop_pipeline_event_id) {
    g_source_remove (self->stop_pipeline_event_id);
    self->stop_pipeline_event_id = 0;
  }

  g_clear_pointer (&self->vsrc, gst_object_unref);
  if (self->pipeline) {
    gst_element_set_state (self->pipeline, GST_STATE_NULL);
  }
  g_clear_pointer (&self->pipeline, gst_object_unref);

  g_mutex_unlock (&self->lock);
}
