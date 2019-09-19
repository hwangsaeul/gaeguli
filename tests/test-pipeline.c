/**
 * tests/test-edge
 *
 * Copyright 2019 SK Telecom, Co., Ltd.
 *   Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 *
 */

#include <gaeguli/gaeguli.h>
#include "pipeline.h"

#include <gst/gst.h>

typedef struct _TestFixture
{
  GMainLoop *loop;
  guint target_id;
} TestFixture;

static void
fixture_setup (TestFixture * fixture, gconstpointer unused)
{
  fixture->loop = g_main_loop_new (NULL, FALSE);
}

static void
fixture_teardown (TestFixture * fixture, gconstpointer unused)
{
  g_main_loop_unref (fixture->loop);
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
  g_autoptr (GaeguliPipeline) pipeline = gaeguli_pipeline_new ();
  g_autoptr (GError) error = NULL;

  g_signal_connect (pipeline, "stream-started", G_CALLBACK (_stream_started_cb),
      fixture);
  g_signal_connect (pipeline, "stream-stopped", G_CALLBACK (_stream_stopped_cb),
      fixture);
  target_id = gaeguli_pipeline_add_fifo_target (pipeline, "/dev/null", &error);

  g_assert_cmpuint (target_id, !=, 0);
  fixture->target_id = target_id;

  g_main_loop_run (fixture->loop);
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/gaeguli/pipeline-instance", TestFixture, NULL, fixture_setup,
      test_gaeguli_pipeline_instance, fixture_teardown);

  return g_test_run ();
}
