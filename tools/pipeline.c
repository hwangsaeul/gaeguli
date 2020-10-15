/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#include "config.h"

#include "pipeline.h"
#include "target.h"

#include <glib-unix.h>
#include <gio/gio.h>
#include <gst/gst.h>

static guint signal_watch_intr_id;
static GaeguliTarget *target;

static struct
{
  const gchar *device;
  const gchar *uri;
  const gchar *username;
  gboolean overlay;
} options;

static void
activate (GApplication * app, gpointer user_data)
{
  g_autoptr (GError) error = NULL;

  GaeguliPipeline *pipeline = user_data;

  g_application_hold (app);

  g_print ("Streaming to %s\n", options.uri);
  target = gaeguli_pipeline_add_srt_target (pipeline, options.uri,
      options.username, &error);
  gaeguli_target_start (target, &error);
}

static gboolean
intr_handler (gpointer user_data)
{
  GaeguliPipeline *pipeline = user_data;
  g_autoptr (GError) error = NULL;

  gaeguli_pipeline_remove_target (pipeline, target, &error);

  g_debug ("target removed");

  return G_SOURCE_REMOVE;
}

static void
stream_stopped_cb (GaeguliPipeline * pipeline, guint target_id,
    gpointer user_data)
{
  GApplication *app = user_data;

  g_debug ("stream stopped");
  g_application_release (app);
}

static gboolean
uri_arg_cb (const gchar * option_name, const gchar * value, gpointer data,
    GError ** error)
{
  if (!options.uri) {
    options.uri = g_strdup (value);
  }
  return TRUE;
}

int
main (int argc, char *argv[])
{
  gboolean help = FALSE;
  int result;

  g_autoptr (GError) error = NULL;
  g_autoptr (GApplication) app = g_application_new (NULL, 0);

  g_autoptr (GOptionContext) context = NULL;
  GOptionEntry entries[] = {
    {"device", 'd', 0, G_OPTION_ARG_FILENAME, &options.device, NULL, NULL},
    {"username", 'u', 0, G_OPTION_ARG_STRING, &options.username, NULL, NULL},
    {"clock-overlay", 'c', 0, G_OPTION_ARG_NONE, &options.overlay, NULL, NULL},
    {"help", '?', 0, G_OPTION_ARG_NONE, &help, NULL, NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_CALLBACK, uri_arg_cb, NULL, NULL},
    {NULL}
  };

  g_autoptr (GaeguliPipeline) pipeline = NULL;

  options.device = DEFAULT_VIDEO_SOURCE_DEVICE;
  options.uri = NULL;
  options.username = NULL;
  options.overlay = FALSE;

  gst_init (&argc, &argv);

  context = g_option_context_new (NULL);
  g_option_context_set_help_enabled (context, FALSE);
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("%s\n", error->message);
    return -1;
  }

  if (help) {
    g_autofree gchar *text = g_option_context_get_help (context, FALSE, NULL);
    g_printerr ("%s\n", text);
    return -1;
  }

  if (!options.uri) {
    g_printerr ("SRT uri not specified\n");
    return -1;
  }
  if (!g_str_has_prefix (options.uri, "srt://")) {
    g_printerr ("Invalid SRT uri %s\n", options.uri);
    return -1;
  }

  pipeline = gaeguli_pipeline_new_full (DEFAULT_VIDEO_SOURCE, options.device,
      DEFAULT_VIDEO_RESOLUTION, DEFAULT_VIDEO_FRAMERATE);
  g_object_set (pipeline, "clock-overlay", options.overlay, NULL);

  signal_watch_intr_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);

  g_signal_connect (pipeline, "stream-stopped", G_CALLBACK (stream_stopped_cb),
      app);

  g_signal_connect (app, "activate", G_CALLBACK (activate), pipeline);

  result = g_application_run (app, argc, argv);

  gaeguli_pipeline_stop (pipeline);

  if (signal_watch_intr_id > 0)
    g_source_remove (signal_watch_intr_id);

  return result;
}
