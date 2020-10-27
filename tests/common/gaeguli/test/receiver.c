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
gaeguli_tests_create_receiver (GaeguliSRTMode mode, guint port)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GstElement) receiver = NULL;
  g_autofree gchar *pipeline_str = NULL;
  gchar *mode_str = mode == GAEGULI_SRT_MODE_CALLER ? "caller" : "listener";

  pipeline_str =
      g_strdup_printf ("srtsrc uri=srt://127.0.0.1:%d?mode=%s name=src ! "
      "fakesink name=sink signal-handoffs=1", port, mode_str);

  receiver = gst_parse_launch (pipeline_str, &error);
  g_assert_no_error (error);

  gst_element_set_state (receiver, GST_STATE_PLAYING);

  return g_steal_pointer (&receiver);
}

void
gaeguli_tests_receiver_set_handoff_callback (GstElement * receiver,
    GCallback callback, gpointer data)
{
  g_autoptr (GstElement) sink = NULL;
  gulong handler_id;

  sink = gst_bin_get_by_name (GST_BIN (receiver), "sink");

  /* Disconnect previous handler. */
  handler_id = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (sink),
          "handoff-id"));
  if (handler_id) {
    g_signal_handler_disconnect (sink, handler_id);
  }

  if (callback) {
    handler_id = g_signal_connect (sink, "handoff", callback, data);
  } else {
    handler_id = 0;
  }

  g_object_set_data (G_OBJECT (sink), "handoff-id",
      GSIZE_TO_POINTER (handler_id));
}

void
gaeguli_tests_receiver_set_username (GstElement * receiver,
    const gchar * username, const gchar * resource)
{
  g_autoptr (GstElement) src = NULL;
  g_autofree gchar *streamid = NULL;

  gst_element_set_state (receiver, GST_STATE_READY);

  streamid = g_strdup_printf ("#!::u=%s,r=%s", username, resource);
  src = gst_bin_get_by_name (GST_BIN (receiver), "src");
  g_object_set (src, "streamid", streamid, NULL);

  gst_element_set_state (receiver, GST_STATE_PLAYING);
}

void
gaeguli_tests_receiver_set_passphrase (GstElement * receiver,
    const gchar * passphrase)
{
  g_autoptr (GstElement) src = NULL;

  gst_element_set_state (receiver, GST_STATE_READY);

  src = gst_bin_get_by_name (GST_BIN (receiver), "src");
  g_object_set (src, "passphrase", passphrase, NULL);

  gst_element_set_state (receiver, GST_STATE_PLAYING);
}
