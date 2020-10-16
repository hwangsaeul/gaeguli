/**
 *  tests/test-target
 *
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
 *
 */

#include <gaeguli/gaeguli.h>

#include "common/receiver.h"

#define DEFAULT_BITRATE 1500000
#define CHANGED_BITRATE 3000000
#define ROUNDED_BITRATE 9999999


static void
test_gaeguli_target_encoding_params ()
{
  g_autoptr (GaeguliPipeline) pipeline = NULL;
  g_autoptr (GError) error = NULL;
  GaeguliTarget *target;
  guint val;

  pipeline = gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_VIDEO_RESOLUTION_640X480, 15);

  target = gaeguli_pipeline_add_srt_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264_X264, DEFAULT_BITRATE, "srt://127.0.0.1:1111",
      NULL, &error);
  g_assert_no_error (error);

  gaeguli_target_start (target, &error);
  g_assert_no_error (error);

  g_object_get (target, "bitrate", &val, NULL);
  g_assert_cmpuint (val, ==, DEFAULT_BITRATE);
  g_object_get (target, "bitrate-actual", &val, NULL);
  g_assert_cmpuint (val, ==, DEFAULT_BITRATE);

  g_object_set (target, "bitrate", CHANGED_BITRATE, NULL);
  g_object_get (target, "bitrate", &val, NULL);
  g_assert_cmpuint (val, ==, CHANGED_BITRATE);
  g_object_get (target, "bitrate-actual", &val, NULL);
  g_assert_cmpuint (val, ==, CHANGED_BITRATE);

  g_object_set (target, "bitrate", ROUNDED_BITRATE, NULL);
  g_object_get (target, "bitrate", &val, NULL);
  g_assert_cmpuint (val, ==, ROUNDED_BITRATE);
  g_object_get (target, "bitrate-actual", &val, NULL);
  /* Internally, x264enc accepts bitrate in kbps, so we lose the value precision
   * in the three least significant digits. */
  g_assert_cmpuint (val, ==, ROUNDED_BITRATE - (ROUNDED_BITRATE % 1000));

  gaeguli_pipeline_stop (pipeline);
}

static void
buffer_cb (GstElement * object, GstBuffer * buffer, GstPad * pad, gpointer data)
{
  g_main_loop_quit (data);
}

static void
buffer_cb_not_reached (GstElement * object, GstBuffer * buffer, GstPad * pad,
    gpointer data)
{
  g_assert_not_reached ();
}

static void
connection_error_cb (GaeguliPipeline * pipeline, GaeguliTarget * target,
    GError * error, gpointer data)
{
  g_assert_true (g_error_matches (error, GST_RESOURCE_ERROR,
          GST_RESOURCE_ERROR_NOT_AUTHORIZED));

  g_main_loop_quit (data);
}

static void
connection_error_not_reached_cb (GaeguliPipeline * pipeline,
    GaeguliTarget * target, GError * error, gpointer data)
{
  g_assert_not_reached ();
}

static void
passphrase_run (const gchar * sender_passphrase,
    const gchar * receiver_passphrase, GCallback error_cb, GCallback buffer_cb)
{
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (GaeguliPipeline) pipeline = NULL;
  g_autoptr (GstElement) receiver = NULL;
  g_autoptr (GError) error = NULL;
  GaeguliTarget *target;

  pipeline = gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_VIDEO_RESOLUTION_640X480, 15);
  g_signal_connect (pipeline, "connection-error", error_cb, loop);

  target = gaeguli_pipeline_add_srt_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264_X264, DEFAULT_BITRATE, "srt://127.0.0.1:1111",
      NULL, &error);
  g_assert_no_error (error);
  g_object_set (G_OBJECT (target), "passphrase", sender_passphrase, NULL);

  receiver = gaeguli_tests_create_receiver (GAEGULI_SRT_MODE_LISTENER, 1111);
  gaeguli_tests_receiver_set_handoff_callback (receiver, buffer_cb, loop);
  gaeguli_tests_receiver_set_passphrase (receiver, receiver_passphrase);

  gaeguli_target_start (target, &error);
  if (sender_passphrase && strlen (sender_passphrase) < 10) {
    g_assert_error (error, GAEGULI_TRANSMIT_ERROR,
        GAEGULI_TRANSMIT_ERROR_FAILED);
    goto out;
  } else {
    g_assert_no_error (error);
  }

  g_main_loop_run (loop);

out:
  gst_element_set_state (receiver, GST_STATE_NULL);
  gaeguli_pipeline_stop (pipeline);
}

static void
test_gaeguli_target_passphrase ()
{
  passphrase_run ("mysecretpassphrase", NULL,
      (GCallback) connection_error_cb, (GCallback) buffer_cb_not_reached);
  passphrase_run (NULL, "mysecretpassphrase",
      (GCallback) connection_error_cb, (GCallback) buffer_cb_not_reached);
  passphrase_run ("mysecretpassphrase", "notmatchingpassphrase",
      (GCallback) connection_error_cb, (GCallback) buffer_cb_not_reached);
  passphrase_run ("tooshort", "tooshort",
      (GCallback) connection_error_cb, (GCallback) buffer_cb_not_reached);
  passphrase_run ("mysecretpassphrase", "mysecretpassphrase",
      (GCallback) connection_error_not_reached_cb, (GCallback) buffer_cb);
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/gaeguli/target-encoding-params",
      test_gaeguli_target_encoding_params);
  g_test_add_func ("/gaeguli/target-passphrase",
      test_gaeguli_target_passphrase);

  return g_test_run ();
}
