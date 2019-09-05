/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#include "config.h"

#include "fifo-transmit.h"

#include <gio/gio.h>

static void
activate (GApplication * app, gpointer user_data)
{
  g_autoptr (GError) error = NULL;
  const gchar *host = NULL;
  guint port, transmit_id;
  GaeguliFifoTransmit *fifo_transmit = user_data;

  host = g_object_get_data (G_OBJECT (app), "host");
  port = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (app), "port"));

  transmit_id =
      gaeguli_fifo_transmit_start (fifo_transmit, host, port,
      GAEGULI_SRT_MODE_LISTENER, &error);

  g_object_set_data (G_OBJECT (app), "transmit-id",
      GINT_TO_POINTER (transmit_id));

  g_application_hold (app);
}

int
main (int argc, char *argv[])
{
  gboolean help;
  const gchar *host = NULL;
  guint port;
  const gchar *tmpdir = NULL;

  g_autoptr (GError) error = NULL;
  g_autoptr (GApplication) app =
      g_application_new ("org.hwangsaeul.Gaeguli1.FifoTransmitApp", 0);

  g_autoptr (GOptionContext) context = NULL;
  GOptionEntry entries[] = {
    {"host", 'h', 0, G_OPTION_ARG_STRING, &host, NULL, NULL},
    {"port", 'p', 0, G_OPTION_ARG_INT, &port, NULL, NULL},
    {"tmpdir", 't', 0, G_OPTION_ARG_FILENAME, &tmpdir, NULL, NULL},
    {"help", '?', 0, G_OPTION_ARG_NONE, &help, NULL, NULL},
    {NULL}
  };

  g_autoptr (GaeguliFifoTransmit) fifo_transmit = NULL;

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

  if (tmpdir) {
    /* TODO: set tmpdir for fifo */
  }

  fifo_transmit = gaeguli_fifo_transmit_new ();
  g_printerr ("Send bytestream to: %s",
      gaeguli_fifo_transmit_get_fifo (fifo_transmit));

  g_signal_connect (app, "activate", G_CALLBACK (activate), fifo_transmit);

  g_object_set_data_full (G_OBJECT (app), "host", g_strdup (host), g_free);
  g_object_set_data (G_OBJECT (app), "port", GINT_TO_POINTER (port));

  return g_application_run (app, argc, argv);
}
