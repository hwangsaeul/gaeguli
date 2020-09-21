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

#include <gaeguli/gaeguli.h>
#include <glib-unix.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>

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
  g_autoptr (GaeguliPipeline) pipeline =
      gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_V4L2SRC, "/dev/video4",
      GAEGULI_ENCODING_METHOD_GENERAL);
  g_autoptr (GError) error = NULL;

  //http_server = mss_http_server_new ();

  g_unix_signal_add (SIGINT, sigint_handler, loop);

  gaeguli_pipeline_add_srt_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_1920X1080, 30, 2048000,
      "srt://127.0.0.1:7001?mode=listener", NULL, &error);

  if (error) {
    g_printerr ("Unable to initiate SRT stream: %s\n", error->message);
    return 1;
  }

  g_print ("Control panel URI: %s\n", "");

  g_main_loop_run (loop);

  gaeguli_pipeline_stop (pipeline);
}
