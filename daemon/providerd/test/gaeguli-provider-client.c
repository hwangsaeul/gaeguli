/**
 *  Copyright 2019-2020 SK Telecom Co., Ltd.
 *    Author: Raghavendra Rao <raghavendra.rao@collabora.com>
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
#include "adaptors/bandwidthadaptor.h"
#include <gaeguli/gaeguli.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>

#include <glib-unix.h>
#include <gio/gio.h>

static guint signal_watch_intr_id;
struct pollfd fds[1];
int sockfd;
int numfds = 0;

static struct
{
  guint pipewire_node_id;
  gboolean overlay;
} options;

static void
_close ()
{
  close (sockfd);
}

static void
_display_menu (void)
{
  printf ("********* MENU *********\n\n");
  printf ("Enter 1 to create Source pipeline\n");
  printf ("Enter 2 to destroy Source pipeline\n");
  printf ("Enter 3 to exit\n");
  printf ("************************\n");
}

static GaeguliPipeline *
_handle_create_pipeline (guint node_id)
{
  GaeguliPipeline *pipeline =
      gaeguli_pipeline_new_full (DEFAULT_VIDEO_SOURCE, node_id,
      DEFAULT_VIDEO_RESOLUTION, DEFAULT_VIDEO_FRAMERATE);
  g_print ("Client:: Got pipeline [%p]\n", pipeline);
  return pipeline;
}

static void
_handle_destroy_pipeline (GaeguliPipeline * pipeline)
{
  if (!pipeline) {
    return;
  }
  g_print ("Invoking gaeguli_pipeline_stop () from client. pipeline [%p]\n",
      pipeline);
  gaeguli_pipeline_stop (pipeline);
  g_print ("Done with gaeguli_pipeline_stop ()\n");
}

static gboolean
intr_handler (gpointer user_data)
{
  _close ();
  return G_SOURCE_REMOVE;
}

int
main (int argc, char *argv[])
{
  GaeguliPipeline *pipeline = NULL;
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  gboolean help = FALSE;

  GOptionEntry entries[] = {
    {"node id", 'n', 0, G_OPTION_ARG_INT, &options.pipewire_node_id, NULL,
        NULL},
    {"clock-overlay", 'c', 0, G_OPTION_ARG_NONE, &options.overlay, NULL, NULL},
    {"help", '?', 0, G_OPTION_ARG_NONE, &help, NULL, NULL},
    {NULL}
  };

  options.overlay = FALSE;

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

  if (!options.pipewire_node_id) {
    g_printerr ("Invalid node id %u\n", options.pipewire_node_id);
    return -1;
  } else {
    g_print ("Got node id -> %u\n", options.pipewire_node_id);
  }

  signal_watch_intr_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, NULL);

  /* Prepare poll */
  fds[numfds].fd = sockfd;
  fds[numfds].events = POLLIN;
  fds[numfds].revents = 0;
  ++numfds;

  do {
    int cmd = 0;
    /* display the menu */
    _display_menu ();
    /* Get the command from user */
    scanf ("%d", &cmd);

    switch (cmd) {
      case 1:
        pipeline = _handle_create_pipeline (options.pipewire_node_id);
        printf ("Done with Pipeline creation\n");
        break;

      case 2:
        _handle_destroy_pipeline (pipeline);
        printf ("Done with Pipeline destruction\n");
        break;

      case 3:
        goto out;
        break;

      default:
        break;
    }
  } while (1);
out:
  _close ();

  if (signal_watch_intr_id > 0)
    g_source_remove (signal_watch_intr_id);

  exit (EXIT_SUCCESS);
}
