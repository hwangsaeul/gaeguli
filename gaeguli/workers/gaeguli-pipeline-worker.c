#include "config.h"
#include "gaeguli-internal.h"

#include <glib-unix.h>
#include <gio/gio.h>
#include <gaeguli/gaeguli.h>

static const gchar *const supported_formats[] = {
  "video/x-raw",
  "video/x-raw(memory:GLMemory)",
  "video/x-raw(memory:NVMM)",
  "image/jpeg",
  NULL
};

typedef struct _Msg
{
  GaeguliPipelineMsgType type;
  guint value;
} Msg;

typedef struct _PipelineWorker
{
  GstElement *pipeline;
  GstElement *vsrc;
  GIOChannel *ch;
  GMainLoop *loop;
  gint read_fd;
  gint write_fd;
  guint fps;
  GaeguliVideoSource source;
  gboolean show_overlay;
  gboolean prefer_hw_decoding;
  GaeguliVideoResolution resolution;
} PipelineWorker;

static guint signal_watch_intr_id;

static gboolean
intr_handler (gpointer user_data)
{
  PipelineWorker *worker = user_data;

  g_main_loop_quit (worker->loop);
  return G_SOURCE_REMOVE;
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

static void
_pipeline_update_vsrc_caps (PipelineWorker * worker)
{
  gint width, height, i;
  g_autoptr (GstElement) capsfilter = NULL;
  g_autoptr (GstCaps) caps = NULL;

  if (!worker->pipeline) {
    return;
  }

  switch (worker->resolution) {
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
        G_TYPE_INT, height, "framerate", GST_TYPE_FRACTION, worker->fps, 1,
        NULL);
    gst_caps_append (caps, supported_caps);
  }

  capsfilter = gst_bin_get_by_name (GST_BIN (worker->pipeline), "caps");
  g_object_set (capsfilter, "caps", caps, NULL);

  /* Cycling vsrc through READY state prods decodebin into re-discovery of input
   * stream format and rebuilding its decoding pipeline. This is needed when
   * a switch is made between two resolutions that the connected camera can only
   * produce in different output formats, e.g. a change from raw 640x480 stream
   * to MJPEG 1920x1080.
   *
   * NVARGUS Camera src doesn't support this.
   */
  if (worker->source != GAEGULI_VIDEO_SOURCE_NVARGUSCAMERASRC) {
    GstState cur_state;

    gst_element_get_state (worker->pipeline, &cur_state, NULL, 0);
    if (cur_state > GST_STATE_READY) {
      gst_element_set_state (worker->vsrc, GST_STATE_READY);
      gst_element_set_state (worker->vsrc, cur_state);
    }
  }
}

void
free_resources (PipelineWorker * worker)
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

  if (worker->vsrc) {
    gst_object_unref (worker->vsrc);
  }

  if (worker->pipeline) {
    gst_object_unref (worker->pipeline);
  }

  g_free (worker);
}

static void
process_msg (PipelineWorker * worker, Msg * msg)
{
  if (!worker || !msg) {
    return;
  }

  switch (msg->type) {
    default:
      break;

    case GAEGULI_SET_RESOLUTION_MSG:{
      worker->resolution = msg->value;
      _pipeline_update_vsrc_caps (worker);
    }
      break;

    case GAEGULI_SET_FPS_MSG:{
      worker->fps = msg->value;
      _pipeline_update_vsrc_caps (worker);
    }
      break;

    case GAEGULI_PIPELINE_TERMINATE_WORKER_MSG:{
      g_main_loop_quit (worker->loop);
    }
      break;
  }
}

static gboolean
cb_read_watch (GIOChannel * channel, GIOCondition cond, gpointer data)
{
  Msg msg;
  gsize size = sizeof (Msg);
  PipelineWorker *worker = data;

  if ((cond == G_IO_HUP) || (cond == G_IO_ERR)) {
    g_io_channel_unref (channel);
    return FALSE;
  }

  if (read (worker->read_fd, (void *) &msg, size) > 0) {
    process_msg (worker, &msg);
  }

  return TRUE;
}

static gchar *
_get_source_description (GaeguliVideoSource vsrc_type, gchar * device)
{
  g_autoptr (GEnumClass) enum_class =
      g_type_class_ref (GAEGULI_TYPE_VIDEO_SOURCE);
  GEnumValue *enum_value = g_enum_get_value (enum_class, vsrc_type);
  GString *result = g_string_new (enum_value->value_nick);

  switch (vsrc_type) {
    case GAEGULI_VIDEO_SOURCE_V4L2SRC:
      g_string_append_printf (result, " device=%s name=vsrc ", device);
      break;
    case GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC:
      g_string_append (result, " is-live=1");
      break;
    case GAEGULI_VIDEO_SOURCE_NVARGUSCAMERASRC:
      g_string_append_printf (result, " sensor-id=%s", device);
      break;
    default:
      break;
  }

  return g_string_free (result, FALSE);
}

static gchar *
_get_pipeline_id_description (gchar * pipeline_instance)
{
  GString *desc = g_string_new (NULL);

  g_string_append_printf (desc, "%d_%s", getppid (), pipeline_instance);

  return g_string_free (desc, FALSE);
}

static gchar *
_get_stream_props_description (gchar * pipeline_instance)
{
  GString *stream_props = g_string_new (PIPEWIRE_NODE_STREAM_PROPERTIES_STR);

  g_string_append_printf (stream_props, ",%s=%s",
      GAEGULI_PIPELINE_TAG, _get_pipeline_id_description (pipeline_instance));

  return g_string_free (stream_props, FALSE);
}

static gchar *
_get_vsrc_pipeline_string (GaeguliVideoSource vsrc_type,
    gchar * pipeline_instance, gchar * device)
{
  /* FIXME: what if zero-copy */
  g_autofree gchar *source = _get_source_description (vsrc_type, device);
  g_autofree gchar *props = _get_stream_props_description (pipeline_instance);

  /* FIXME - pipewiresink along with another sink in the pipeline   *
   * do not provide the captured video to the consumer pipeline.    *
   * Hence its inclusion is diabled.                                *
   * This should be a sepearate target.                             */
  return g_strdup_printf
      (GAEGULI_PIPELINE_VSRC_STR, source,
      vsrc_type == GAEGULI_VIDEO_SOURCE_NVARGUSCAMERASRC ? "" :
      GAEGULI_PIPELINE_DECODEBIN_STR, props);
}

int
main (int argc, char *argv[])
{
  PipelineWorker *worker = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GstElement) overlay = NULL;
  g_autoptr (GstElement) decodebin = NULL;
  g_autoptr (GstPluginFeature) feature = NULL;
  g_autofree gchar *pipeline_str = NULL;
  g_autofree gchar *device = NULL;
  gint ret = 0;

  worker = g_new0 (PipelineWorker, 1);

  device = g_strdup (argv[2]);
  worker->source = atoi (argv[3]);
  worker->show_overlay = atoi (argv[4]);
  worker->prefer_hw_decoding = atoi (argv[5]);
  worker->resolution = atoi (argv[6]);
  worker->fps = atoi (argv[7]);
  worker->read_fd = atoi (argv[8]);
  /* Close the write side of the pipe: Pipeline -> Worker */
  close (atoi (argv[9]));
  /* Close the read side of the pipe: Worker -> Pipeline */
  close (atoi (argv[10]));
  worker->write_fd = atoi (argv[11]);

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

  pipeline_str = _get_vsrc_pipeline_string (worker->source, argv[1], device);

  worker->loop = g_main_loop_new (NULL, FALSE);

  signal_watch_intr_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, worker);

  /* Initialise the gst env */
  gst_init (&argc, &argv);

  g_debug ("using source pipeline [%s]", pipeline_str);
  worker->pipeline = gst_parse_launch (pipeline_str, &error);
  if (error) {
    g_warning ("failed to build source pipeline (%s)", error->message);
    ret = -1;
    goto failed;
  }

  overlay = gst_bin_get_by_name (GST_BIN (worker->pipeline), "overlay");
  if (overlay) {
    g_object_set (overlay, "silent", !worker->show_overlay, NULL);
  }

  decodebin = gst_bin_get_by_name (GST_BIN (worker->pipeline), "decodebin");
  if (decodebin) {
    g_signal_connect (decodebin, "pad-added",
        G_CALLBACK (_decodebin_pad_added), overlay);
  }

  worker->vsrc = gst_bin_get_by_name (GST_BIN (worker->pipeline), "vsrc");

  if (worker->prefer_hw_decoding) {
    /* Verify whether hardware accelearted vaapijpeg decoder is available.
     * If available, make sure that the decodebin picks it up. */
    feature = gst_registry_find_feature (gst_registry_get (), "vaapijpegdec",
        GST_TYPE_ELEMENT_FACTORY);
    if (feature)
      gst_plugin_feature_set_rank (feature, GST_RANK_PRIMARY + 100);
  }

  _pipeline_update_vsrc_caps (worker);

  g_debug ("Setting PLAYING on pipeline");
  gst_element_set_state (worker->pipeline, GST_STATE_PLAYING);

  g_main_loop_run (worker->loop);

  g_debug ("Setting NULL on pipeline");
  gst_element_set_state (worker->pipeline, GST_STATE_NULL);

failed:

  free_resources (worker);
  return ret;
}
