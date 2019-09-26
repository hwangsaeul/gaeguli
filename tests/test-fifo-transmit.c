/** 
 *  tests/test-fifo-transmit
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
#include <gaeguli/fifo-transmit.h>
#include <glib/gstdio.h>
#include <gst/gst.h>

#define FIFO_READ_LIMIT_BYTES 10000

typedef struct _TestFixture
{
  GMainLoop *loop;
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
test_gaeguli_fifo_transmit_instance (void)
{
  const gchar *fifo_path = NULL;
  g_autoptr (GaeguliFifoTransmit) fifo_transmit = gaeguli_fifo_transmit_new ();

  g_assert_nonnull (fifo_transmit);

  fifo_path = gaeguli_fifo_transmit_get_fifo (fifo_transmit);
  g_assert_nonnull (fifo_path);
}

static void
test_gaeguli_fifo_transmit_start (TestFixture * fixture, gconstpointer unused)
{
  guint transmit_id = 0;
  g_autoptr (GError) error = NULL;
  g_autoptr (GaeguliFifoTransmit) fifo_transmit = gaeguli_fifo_transmit_new ();

  g_assert_nonnull (fifo_transmit);

  transmit_id = gaeguli_fifo_transmit_start (fifo_transmit,
      "127.0.0.1", 8888, GAEGULI_SRT_MODE_CALLER, &error);

  g_assert_cmpuint (transmit_id, !=, 0);

  g_clear_error (&error);
  g_assert_true (gaeguli_fifo_transmit_stop (fifo_transmit, transmit_id,
          &error));
}

static void
test_gaeguli_fifo_transmit_same_fifo_path ()
{
  g_autoptr (GaeguliFifoTransmit) fifo_transmit_1 = NULL;
  g_autoptr (GaeguliFifoTransmit) fifo_transmit_2 = NULL;
  g_autofree gchar *tmpdir = NULL;

  tmpdir =
      g_build_filename (g_get_tmp_dir (), "test-gaeguli-fifo-XXXXXX", NULL);
  g_mkdtemp (tmpdir);

  fifo_transmit_1 = gaeguli_fifo_transmit_new_full (tmpdir);
  g_assert_nonnull (fifo_transmit_1);

  fifo_transmit_2 = gaeguli_fifo_transmit_new_full (tmpdir);
  g_assert_null (fifo_transmit_2);
}

typedef struct
{
  GaeguliFifoTransmit *transmit;
  guint transmit_id;
  guint target_id;
  gboolean closing;
} FifoTestData;

typedef struct
{
  GMutex lock;
  TestFixture *fixture;
  GaeguliPipeline *pipeline;
  FifoTestData fifos[5];
  guint fifos_to_create;
} AddRemoveTestData;

static gboolean
add_remove_fifo_cb (AddRemoveTestData * data)
{
  gint i;

  g_mutex_lock (&data->lock);

  /* If there's a free slot, create a new fifo transmit. */
  for (i = 0; data->fifos_to_create && i != G_N_ELEMENTS (data->fifos); ++i) {
    FifoTestData *fifo = &data->fifos[i];

    if (fifo->transmit == NULL && !fifo->closing) {
      g_autoptr (GError) error = NULL;

      fifo->transmit = gaeguli_fifo_transmit_new ();
      g_assert_nonnull (fifo->transmit);

      fifo->transmit_id = gaeguli_fifo_transmit_start (fifo->transmit,
          "127.0.0.1", 8888, GAEGULI_SRT_MODE_CALLER, &error);
      g_assert_no_error (error);

      fifo->target_id = gaeguli_pipeline_add_fifo_target_full (data->pipeline,
          GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640x480,
          gaeguli_fifo_transmit_get_fifo (fifo->transmit), &error);
      g_assert_no_error (error);

      g_debug ("Added fifo %u", fifo->transmit_id);

      --data->fifos_to_create;
      break;
    }
  }

  /* Check that all fifos are in NORMAL state and data are being read. */
  for (i = 0; i != G_N_ELEMENTS (data->fifos); ++i) {
    FifoTestData *fifo = &data->fifos[i];
    g_autoptr (GVariantDict) stats = NULL;
    g_autoptr (GVariant) bytes_read = NULL;
    g_autoptr (GError) error = NULL;

    if (fifo->transmit == NULL || fifo->closing) {
      continue;
    }

    g_assert_cmpint (gaeguli_fifo_transmit_get_read_status (fifo->transmit), ==,
        G_IO_STATUS_NORMAL);

    stats = gaeguli_fifo_transmit_get_stats (fifo->transmit);
    g_assert_nonnull (stats);
    bytes_read = g_variant_dict_lookup_value (stats,
        "bytes-read", G_VARIANT_TYPE_UINT64);
    g_assert_nonnull (bytes_read);

    g_debug ("Fifo %u has read %lu B", fifo->transmit_id,
        g_variant_get_uint64 (bytes_read));

    /* Remove fifos that have read at least FIFO_READ_LIMIT_BYTES. */
    if (g_variant_get_uint64 (bytes_read) >= FIFO_READ_LIMIT_BYTES) {
      /* First stop the pipeline. */
      gaeguli_pipeline_remove_target (data->pipeline, fifo->target_id, &error);
      g_assert_no_error (error);

      fifo->closing = TRUE;
      /* The removal gets finished in stream_stopped_cb(). */
    }
  }

  g_mutex_unlock (&data->lock);

  return G_SOURCE_CONTINUE;
}

static void
stream_stopped_cb (GaeguliPipeline * pipeline, guint target_id,
    AddRemoveTestData * data)
{
  gint i;
  gboolean have_active_fifos = FALSE;

  g_mutex_lock (&data->lock);

  for (i = 0; i != G_N_ELEMENTS (data->fifos); ++i) {
    FifoTestData *fifo = &data->fifos[i];
    g_autoptr (GError) error = NULL;

    if (fifo->target_id == target_id) {
      g_assert_true (fifo->closing);
      gaeguli_fifo_transmit_stop (fifo->transmit, fifo->transmit_id, &error);
      g_assert_no_error (error);
      g_clear_object (&fifo->transmit);
      fifo->closing = FALSE;

      g_debug ("Removed fifo %u", fifo->target_id);
    } else if (fifo->transmit != NULL) {
      have_active_fifos = TRUE;
    }
  }

  if (!have_active_fifos && data->fifos_to_create == 0) {
    /* All fifos finished receiving, quit the test. */
    g_main_loop_quit (data->fixture->loop);
  }

  g_mutex_unlock (&data->lock);
}

static void
test_gaeguli_fifo_transmit_add_remove_random (TestFixture * fixture,
    gconstpointer unused)
{
  AddRemoveTestData data = { 0 };
  guint idle_source;
  gint i;

  g_mutex_init (&data.lock);
  data.fixture = fixture;
  data.pipeline = gaeguli_pipeline_new ();
  data.fifos_to_create = 10;

  g_signal_connect (data.pipeline, "stream-stopped",
      (GCallback) stream_stopped_cb, &data);

  idle_source = g_timeout_add (20, (GSourceFunc) add_remove_fifo_cb, &data);
  g_main_loop_run (fixture->loop);
  g_source_remove (idle_source);

  gaeguli_pipeline_stop (data.pipeline);
  g_clear_object (&data.pipeline);
  for (i = 0; i != G_N_ELEMENTS (data.fifos); ++i) {
    g_clear_object (&data.fifos[i].transmit);
  }
}

static void
reattach_stream_stream_stopped_cb (GaeguliPipeline * pipeline, guint target_id,
    gboolean * flag)
{
  *flag = TRUE;
}

static gsize
get_bytes_read (GaeguliFifoTransmit * transmit)
{
  g_autoptr (GVariantDict) stats = NULL;
  g_autoptr (GVariant) bytes_read = NULL;

  stats = gaeguli_fifo_transmit_get_stats (transmit);
  g_assert_nonnull (stats);
  bytes_read = g_variant_dict_lookup_value (stats, "bytes-read",
      G_VARIANT_TYPE_UINT64);
  g_assert_nonnull (bytes_read);

  return g_variant_get_uint64 (bytes_read);
}

static gsize
read_from_pipeline (GaeguliFifoTransmit * transmit, gsize bytes_read_limit)
{
  g_autoptr (GaeguliPipeline) pipeline = NULL;
  g_autoptr (GError) error = NULL;
  gboolean stream_stopped = FALSE;
  guint target_id;
  gsize bytes_read;

  /* Set up a pipeline. */
  pipeline = gaeguli_pipeline_new ();
  target_id = gaeguli_pipeline_add_fifo_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640x480,
      gaeguli_fifo_transmit_get_fifo (transmit), &error);
  g_assert_no_error (error);

  g_debug ("Started reading from pipeline %p", pipeline);

  /* Read from the fifo until we reach the desired limit. */
  do {
    g_main_context_iteration (g_main_context_get_thread_default (), FALSE);

    bytes_read = get_bytes_read (transmit);
    g_debug ("Fifo has read %lu B", bytes_read);
  } while (bytes_read < bytes_read_limit);

  /* Detach the pipeline from our fifo transmit. */
  gaeguli_pipeline_remove_target (pipeline, target_id, &error);
  g_assert_no_error (error);

  /* Wait until the pipeline stops. */
  g_signal_connect (pipeline, "stream-stopped",
      (GCallback) reattach_stream_stream_stopped_cb, &stream_stopped);
  while (!stream_stopped) {
    g_main_context_iteration (g_main_context_get_thread_default (), FALSE);
    g_debug ("Status: %d", gaeguli_fifo_transmit_get_read_status (transmit));
  }

  gaeguli_pipeline_stop (pipeline);

  /* Read any data remaining in the pipe before attaching the next pipeline. */
  while (gaeguli_fifo_transmit_get_available_bytes (transmit) > 0) {
    g_main_context_iteration (g_main_context_get_thread_default (), FALSE);
  }

  return get_bytes_read (transmit);
}

static void
test_gaeguli_fifo_transmit_reattach_stream (TestFixture * fixture,
    gconstpointer unused)
{
  g_autoptr (GaeguliFifoTransmit) transmit = gaeguli_fifo_transmit_new ();
  g_autoptr (GError) error = NULL;
  gssize bytes_read;

  g_main_context_push_thread_default (g_main_loop_get_context (fixture->loop));

  gaeguli_fifo_transmit_start (transmit, "127.0.0.1", 8888,
      GAEGULI_SRT_MODE_CALLER, &error);
  g_assert_no_error (error);

  /* Let two different pipelines write consecutively into one fifo. */
  bytes_read = read_from_pipeline (transmit, FIFO_READ_LIMIT_BYTES);
  read_from_pipeline (transmit, bytes_read + FIFO_READ_LIMIT_BYTES);

  g_main_context_pop_thread_default (g_main_loop_get_context (fixture->loop));
}

static void
test_gaeguli_fifo_transmit_address_in_use (void)
{
  g_autoptr (GaeguliFifoTransmit) transmit = gaeguli_fifo_transmit_new ();
  g_autoptr (GError) error = NULL;
  guint transmit_id;

  transmit_id = gaeguli_fifo_transmit_start (transmit, "127.0.0.1", 8888,
      GAEGULI_SRT_MODE_LISTENER, &error);
  g_assert_no_error (error);
  g_assert_cmpint (transmit_id, !=, 0);

  transmit_id = gaeguli_fifo_transmit_start (transmit, "127.0.0.2", 8888,
      GAEGULI_SRT_MODE_LISTENER, &error);
  g_assert_error (error, GAEGULI_TRANSMIT_ERROR,
      GAEGULI_TRANSMIT_ERROR_ADDRINUSE);
  g_assert_cmpint (transmit_id, ==, 0);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  /* Don't treat warnings as fatal, which is GTest default. */
  g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

  gst_init (&argc, &argv);

  g_test_add_func ("/gaeguli/fifo-transmit-instance",
      test_gaeguli_fifo_transmit_instance);

  g_test_add ("/gaeguli/fifo-transmit-start",
      TestFixture, NULL, fixture_setup,
      test_gaeguli_fifo_transmit_start, fixture_teardown);

  g_test_add_func ("/gaeguli/fifo-transmit-same-fifo-path",
      test_gaeguli_fifo_transmit_same_fifo_path);

  g_test_add ("/gaeguli/fifo-transmit-add-remove-random",
      TestFixture, NULL, fixture_setup,
      test_gaeguli_fifo_transmit_add_remove_random, fixture_teardown);

  g_test_add ("/gaeguli/fifo-transmit-reattach-stream",
      TestFixture, NULL, fixture_setup,
      test_gaeguli_fifo_transmit_reattach_stream, fixture_teardown);

  g_test_add_func ("/gaeguli/fifo-transmit-address-in-use",
      test_gaeguli_fifo_transmit_address_in_use);

  return g_test_run ();
}
