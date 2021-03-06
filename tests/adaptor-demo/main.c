/**
 *  Copyright 2020 SK Telecom Co., Ltd.
 *    Author: Jakub Adam <jakub.adam@collabora.com>
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
 */

#include <glib-unix.h>

#include "config.h"

#include "adaptor-demo.h"

static gboolean
sigint_handler (gpointer user_data)
{
  g_main_loop_quit (user_data);
  return G_SOURCE_REMOVE;
}

int
main (int argc, char *argv[])
{
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (GaeguliAdaptorDemo) app = NULL;
  g_autoptr (GError) error = NULL;

  struct
  {
    const gchar *device;
  } options;
  g_autoptr (GOptionContext) context = NULL;
  GOptionEntry entries[] = {
    {"device", 'd', 0, G_OPTION_ARG_FILENAME, &options.device, NULL, NULL},
    {NULL}
  };

  options.device = DEFAULT_VIDEO_SOURCE_DEVICE;

  context = g_option_context_new (NULL);
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("%s\n", error->message);
    return -1;
  }

  g_unix_signal_add (SIGINT, sigint_handler, loop);

  app = gaeguli_adaptor_demo_new (options.device);

  {
    g_autofree gchar *http_uri = gaeguli_adaptor_demo_get_control_uri (app);
    g_print ("Control panel URI: %s\n", http_uri);
  }

  g_main_loop_run (loop);

  return 0;
}
