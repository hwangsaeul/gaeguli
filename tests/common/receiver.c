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

#include "receiver.h"

GstElement *
gaeguli_tests_create_receiver (GaeguliSRTMode mode, guint port,
    GCallback handoff_callback, gpointer data)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GstElement) receiver = NULL;
  g_autoptr (GstElement) sink = NULL;
  g_autofree gchar *pipeline_str = NULL;
  gchar *mode_str = mode == GAEGULI_SRT_MODE_CALLER ? "caller" : "listener";

  pipeline_str = g_strdup_printf ("srtsrc uri=srt://127.0.0.1:%d?mode=%s ! "
      "fakesink name=sink signal-handoffs=1", port, mode_str);

  receiver = gst_parse_launch (pipeline_str, &error);
  g_assert_no_error (error);

  if (handoff_callback) {
    sink = gst_bin_get_by_name (GST_BIN (receiver), "sink");
    g_signal_connect (sink, "handoff", handoff_callback, data);
  }

  gst_element_set_state (receiver, GST_STATE_PLAYING);

  return g_steal_pointer (&receiver);
}
