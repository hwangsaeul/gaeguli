/**
 *  tests/test-pipeline
 *
 *  Copyright 2019 SK Telecom Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *            Jakub Adam <jakub.adam@collabora.com>
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
#include "pipeline.h"

#include <gst/gst.h>

typedef struct _TestFixture
{
  GMainLoop *loop;
  guint target_id;

  GThread *thread;
  GMutex lock;
  GCond cond;

  GaeguliPipeline *pipeline;

} TestFixture;

static void
fixture_setup (TestFixture * fixture, gconstpointer unused)
{
  fixture->loop = g_main_loop_new (NULL, FALSE);
  g_mutex_init (&fixture->lock);
  g_cond_init (&fixture->cond);
}

static void
fixture_teardown (TestFixture * fixture, gconstpointer unused)
{
  g_main_loop_unref (fixture->loop);
  g_mutex_clear (&fixture->lock);
  g_cond_clear (&fixture->cond);
}

static void
_stream_started_cb (GaeguliPipeline * pipeline, guint target_id,
    TestFixture * fixture)
{
  g_autoptr (GError) error = NULL;

  gaeguli_pipeline_remove_target (pipeline, target_id, &error);
}

static gboolean
_quit_loop (TestFixture * fixture)
{
  g_main_loop_quit (fixture->loop);
  return G_SOURCE_REMOVE;
}

static void
_stream_stopped_cb (GaeguliPipeline * pipeline, guint target_id,
    TestFixture * fixture)
{
  g_debug ("got stopped signal %x", target_id);

  g_timeout_add (100, (GSourceFunc) _quit_loop, fixture);
}

static void
test_gaeguli_pipeline_instance (TestFixture * fixture, gconstpointer unused)
{
  guint target_id = 0;
  g_autoptr (GaeguliPipeline) pipeline =
      gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_ENCODING_METHOD_GENERAL);
  g_autoptr (GError) error = NULL;

  g_signal_connect (pipeline, "stream-started", G_CALLBACK (_stream_started_cb),
      fixture);
  g_signal_connect (pipeline, "stream-stopped", G_CALLBACK (_stream_stopped_cb),
      fixture);

  target_id = gaeguli_pipeline_add_fifo_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640X480,
      "/dev/null", &error);

  g_assert_cmpuint (target_id, !=, 0);
  fixture->target_id = target_id;

  g_main_loop_run (fixture->loop);

  gaeguli_pipeline_stop (pipeline);
}

static void
_add_remove_stream_started_cb (GaeguliPipeline * pipeline, guint target_id,
    TestFixture * fixture)
{
  g_mutex_lock (&fixture->lock);
  g_cond_signal (&fixture->cond);
  g_mutex_unlock (&fixture->lock);
}

static void
_add_remove_stream_stopped_cb (GaeguliPipeline * pipeline, guint target_id,
    TestFixture * fixture)
{
  g_mutex_lock (&fixture->lock);
  g_cond_signal (&fixture->cond);
  g_mutex_unlock (&fixture->lock);
}

static gpointer
_running_add_remove_test (gpointer data)
{
  g_autoptr (GError) error = NULL;
  TestFixture *fixture = data;

  g_debug ("running add/remove test");
  g_mutex_lock (&fixture->lock);
  g_cond_wait (&fixture->cond, &fixture->lock);
  g_mutex_unlock (&fixture->lock);

  g_debug ("remove (%x) target", fixture->target_id);
  gaeguli_pipeline_remove_target (fixture->pipeline, fixture->target_id,
      &error);

  g_mutex_lock (&fixture->lock);
  g_cond_wait (&fixture->cond, &fixture->lock);
  g_mutex_unlock (&fixture->lock);

  fixture->target_id = gaeguli_pipeline_add_fifo_target_full (fixture->pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_1280X720,
      "/dev/null", &error);

  g_debug ("created new target (%x)", fixture->target_id);

  g_mutex_lock (&fixture->lock);
  g_cond_wait (&fixture->cond, &fixture->lock);
  g_mutex_unlock (&fixture->lock);

  g_debug ("remove (%x) target agin", fixture->target_id);
  gaeguli_pipeline_remove_target (fixture->pipeline, fixture->target_id,
      &error);

  g_timeout_add (100, (GSourceFunc) _quit_loop, fixture);

  return NULL;
}

static void
test_gaeguli_pipeline_add_remove (TestFixture * fixture, gconstpointer unused)
{
  guint target_id = 0;
  g_autoptr (GaeguliPipeline) pipeline =
      gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_ENCODING_METHOD_GENERAL);
  g_autoptr (GError) error = NULL;

  fixture->pipeline = g_object_ref (pipeline);

  g_signal_connect (pipeline, "stream-started",
      G_CALLBACK (_add_remove_stream_started_cb), fixture);
  g_signal_connect (pipeline, "stream-stopped",
      G_CALLBACK (_add_remove_stream_stopped_cb), fixture);

  fixture->target_id = gaeguli_pipeline_add_fifo_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640X480,
      "/dev/null", &error);

  fixture->thread =
      g_thread_new ("AddRemoveTest", _running_add_remove_test, fixture);

  g_main_loop_run (fixture->loop);
  g_object_unref (fixture->pipeline);
  gaeguli_pipeline_stop (pipeline);
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/gaeguli/pipeline-instance", TestFixture, NULL, fixture_setup,
      test_gaeguli_pipeline_instance, fixture_teardown);

  g_test_add ("/gaeguli/pipeline-add-remove", TestFixture, NULL, fixture_setup,
      test_gaeguli_pipeline_add_remove, fixture_teardown);

  return g_test_run ();
}
