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
  const gchar *uri;
  const gchar *username;
  guint pipewire_output_node_id;
  guint pipewire_input_node_id;
  GaeguliTargetType target_type;
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
  printf ("Enter 1 to create Target pipeline\n");
  printf ("Enter 2 to destroy Target pipeline\n");
  printf ("Enter 3 to exit\n");
  printf ("************************\n");
}

static GaeguliTarget *
_handle_create_target (GaeguliPipeline * pipeline,
    const gchar * uri, const gchar * username, guint input_node_id,
    guint output_node_id, GaeguliTargetType type)
{
  GaeguliTarget *target = NULL;
  g_autoptr (GError) error = NULL;

  switch (type) {
    case GAEGULI_TARGET_TYPE_SRT:{
      g_print ("Client:: Invoking gaeguli_pipeline_add_srt_target() "
          "pipeline = %p, uri = %s input_node_id = %d output_node_id = %d\n",
          pipeline, uri, input_node_id, output_node_id);
      target = gaeguli_pipeline_add_srt_target (pipeline, uri, username,
          input_node_id, output_node_id, &error);
      g_print ("Client:: Got target [%p]. Starting the target\n", target);
      gaeguli_target_start (target, &error);
      g_print ("Done Starting the target [%p]\n", target);
    }
      break;

    case GAEGULI_TARGET_TYPE_RECORDING:{
      g_print ("Client:: Invoking gaeguli_pipeline_add_recording_target() "
          "pipeline = %p, uri = %s input_node_id = %d output_node_id = %d\n",
          pipeline, uri, input_node_id, output_node_id);
      target = gaeguli_pipeline_add_recording_target (pipeline,
          uri, input_node_id, output_node_id, &error);
      g_print ("Client:: Got target [%p]. Starting the target\n", target);
      gaeguli_target_start (target, &error);
      g_print ("Done Starting the target [%p]\n", target);
    }
      break;

    case GAEGULI_TARGET_TYPE_IMAGE_CAPTURE:{
      target = gaeguli_pipeline_add_image_capture_target (pipeline,
          input_node_id, output_node_id, &error);
      g_print ("Client:: Got target [%p]. Starting the target\n", target);
      gaeguli_target_start (target, &error);
      g_print ("Done Starting the target [%p]\n", target);
    }
      break;

    default:
      break;
  }
  return target;
}

static void
_handle_destroy_target (GaeguliPipeline * pipeline, GaeguliTarget * target)
{
  g_autoptr (GError) error = NULL;

  if (!pipeline || !target) {
    return;
  }
  g_print ("Invoking gaeguli_remove_target () from client. target [%p]\n",
      target);
  gaeguli_pipeline_remove_target (pipeline, target, &error);
  g_print ("Done with gaeguli_remove_target ()\n");
}

static gboolean
intr_handler (gpointer user_data)
{
  _close ();
  return G_SOURCE_REMOVE;
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
  GaeguliPipeline *pipeline = NULL;
  GaeguliTarget *target = NULL;
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  gboolean help = FALSE;

  GOptionEntry entries[] = {
    {"username", 'u', 0, G_OPTION_ARG_STRING, &options.username, NULL, NULL},
    {"input node id", 'i', 0, G_OPTION_ARG_INT, &options.pipewire_input_node_id,
        NULL, NULL},
    {"output node id", 'o', 0, G_OPTION_ARG_INT,
        &options.pipewire_output_node_id, NULL, NULL},
    {"target type", 't', 0, G_OPTION_ARG_INT, &options.target_type, NULL, NULL},
    {"help", '?', 0, G_OPTION_ARG_NONE, &help, NULL, NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_CALLBACK, uri_arg_cb, NULL, NULL},
    {NULL}
  };

  options.uri = NULL;
  options.username = NULL;

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

  if (!options.uri &&
      (options.target_type == GAEGULI_CONSUMER_MSG_CREATE_SRT_TARGET)) {
    g_printerr ("SRT uri not specified\n");
    return -1;
  }

  if (options.uri && !g_str_has_prefix (options.uri, "srt://") &&
      (options.target_type == GAEGULI_CONSUMER_MSG_CREATE_SRT_TARGET)) {
    g_printerr ("Invalid SRT uri %s\n", options.uri);
    return -1;
  }

  if (!options.pipewire_output_node_id || !options.pipewire_input_node_id) {
    g_printerr ("Invalid node id %u %u \n",
        options.pipewire_output_node_id, options.pipewire_input_node_id);
    return -1;
  } else {
    g_print ("Got node id -> %u\n", options.pipewire_output_node_id);
  }

  signal_watch_intr_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, NULL);

  /* Prepare poll */
  fds[numfds].fd = sockfd;
  fds[numfds].events = POLLIN;
  fds[numfds].revents = 0;
  ++numfds;

  /* Get the Source Provider Pipeline */
  pipeline = gaeguli_get_pipeline (options.pipewire_input_node_id);
  printf ("Client: Got pipeline [%p]\n", pipeline);

  do {
    int cmd = 0;
    /* display the menu */
    _display_menu ();
    /* Get the command from user */
    scanf ("%d", &cmd);

    switch (cmd) {
      case 1:
        target = _handle_create_target (pipeline,
            options.uri, options.username, options.pipewire_input_node_id,
            options.pipewire_output_node_id, options.target_type);
        printf ("Done with target creation. target = %p\n", target);
        break;

      case 2:
        _handle_destroy_target (pipeline, target);
        printf ("Done with target destruction\n");
        break;

      case 3:
        goto out;
        break;

      default:
        break;
    }
  } while (1);
out:
  gaeguli_unmap_pipeline (pipeline);
  _close ();

  if (signal_watch_intr_id > 0)
    g_source_remove (signal_watch_intr_id);

  exit (EXIT_SUCCESS);
}
