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
      g_strdup_printf ("srt://127.0.0.1:%" G_GUINT32_FORMAT, fixture->port);
}

static void
fixture_teardown (TestFixture * fixture, gconstpointer unused)
{
  g_main_loop_unref (fixture->loop);
}

static void
_stream_started_cb (GaeguliPipeline * pipeline, GaeguliTarget * target,
    TestFixture * fixture)
{
  g_autoptr (GError) error = NULL;

  gaeguli_pipeline_remove_target (pipeline, target, &error);
}

static gboolean
_quit_loop (TestFixture * fixture)
{
  g_main_loop_quit (fixture->loop);
  return G_SOURCE_REMOVE;
}

static void
_stream_stopped_cb (GaeguliPipeline * pipeline, GaeguliTarget * target,
    TestFixture * fixture)
{
  g_debug ("got stopped signal %x", target->id);

  g_timeout_add (100, (GSourceFunc) _quit_loop, fixture);
}

static void
test_gaeguli_pipeline_rtp_instance (TestFixture * fixture, gconstpointer unused)
{
  GaeguliTarget *target;
  g_autoptr (GaeguliPipeline) pipeline =
      gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_VIDEO_RESOLUTION_640X480, 30);
  g_autoptr (GError) error = NULL;

  g_signal_connect (pipeline, "stream-started", G_CALLBACK (_stream_started_cb),
      fixture);
  g_signal_connect (pipeline, "stream-stopped", G_CALLBACK (_stream_stopped_cb),
      fixture);

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
