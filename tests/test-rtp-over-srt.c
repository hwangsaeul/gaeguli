/**
 *  tests/test-rtp-over-srt
 *
 *  Copyright 2021 SK Telecom Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
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
#include "gaeguli/test/receiver.h"

typedef struct _TestFixture
{
  GMainLoop *loop;
  GaeguliPipeline *pipeline;
  GaeguliTarget *target;
  guint port;
  gchar *srt_uri;
} TestFixture;

static void
fixture_setup (TestFixture * fixture, gconstpointer unused)
{
  fixture->loop = g_main_loop_new (NULL, FALSE);
  fixture->port = g_random_int_range (39000, 40000);
  fixture->srt_uri =
      g_strdup_printf ("srt://127.0.0.1:%" G_GUINT32_FORMAT "?mode=caller",
      fixture->port);
}

static void
fixture_teardown (TestFixture * fixture, gconstpointer unused)
{
  g_main_loop_unref (fixture->loop);
}

typedef struct
{
  TestFixture *fixture;
  gsize buffer_cnt;
} ClientTestData;

static void
_test1_buffer_cb (GstElement * object, GstBuffer * buffer, GstPad * pad,
    ClientTestData * data)
{
  ++data->buffer_cnt;
  if (++data->buffer_cnt == 50) {
    g_debug ("reached 50 received buffer count; exiting the main loop");
    g_main_loop_quit (data->fixture->loop);
  }
}

static void
test_gaeguli_pipeline_rtp_instance (TestFixture * fixture, gconstpointer unused)
{
  GaeguliTarget *target;
  g_autoptr (GaeguliPipeline) pipeline =
      gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_VIDEO_RESOLUTION_640X480, 30);
  g_autoptr (GError) error = NULL;
  ClientTestData data = { 0 };
  g_autoptr (GstElement) listener =
      gaeguli_tests_create_receiver (GAEGULI_SRT_MODE_LISTENER, fixture->port);

  data.fixture = fixture;

  gaeguli_tests_receiver_set_handoff_callback (listener,
      (GCallback) _test1_buffer_cb, &data);

  target = gaeguli_pipeline_add_srt_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264_X264, GAEGULI_VIDEO_STREAM_TYPE_RTP_OVER_SRT,
      2048000, fixture->srt_uri, NULL, &error);

  g_assert_no_error (error);
  g_assert_nonnull (target);
  g_assert_cmpuint (target->id, !=, 0);
  fixture->target = target;

  gaeguli_target_start (target, &error);
  g_assert_no_error (error);

  g_main_loop_run (fixture->loop);

  gst_element_set_state (listener, GST_STATE_NULL);
  gaeguli_pipeline_stop (pipeline);
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/gaeguli/pipeline-rtp-instance", TestFixture, NULL,
      fixture_setup, test_gaeguli_pipeline_rtp_instance, fixture_teardown);

  return g_test_run ();
}
