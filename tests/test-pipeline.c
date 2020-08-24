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
#include "common/receiver.h"

#define TARGET_BYTES_SENT_LIMIT 10000

typedef struct _TestFixture
{
  GMainLoop *loop;
  GaeguliPipeline *pipeline;
  guint target_id;
  guint port_base;
} TestFixture;

static void
fixture_setup (TestFixture * fixture, gconstpointer unused)
{
  fixture->loop = g_main_loop_new (NULL, FALSE);
  fixture->port_base = g_random_int_range (15000, 40000);
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
  g_autoptr (GaeguliPipeline) pipeline =
      gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_ENCODING_METHOD_GENERAL);
  g_autoptr (GError) error = NULL;

  g_signal_connect (pipeline, "stream-started", G_CALLBACK (_stream_started_cb),
      fixture);
  g_signal_connect (pipeline, "stream-stopped", G_CALLBACK (_stream_stopped_cb),
      fixture);

  target_id = gaeguli_pipeline_add_srt_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640X480, 30, 2048000,
      "srt://127.0.0.1:1111", NULL, &error);

  g_assert_cmpuint (target_id, !=, 0);
  fixture->target_id = target_id;

  g_main_loop_run (fixture->loop);

  gaeguli_pipeline_stop (pipeline);
}

typedef struct
{
  guint id;
  gboolean closing;
  GstElement *receiver_pipeline;
} TargetTestData;

typedef struct
{
  GMutex lock;
  TestFixture *fixture;
  GaeguliPipeline *pipeline;
  TargetTestData targets[5];
  guint targets_to_create;
} AddRemoveTestData;

static gboolean
add_remove_target_cb (AddRemoveTestData * data)
{
  gint i;

  g_mutex_lock (&data->lock);

  /* If there's a free slot, create a new fifo transmit. */
  for (i = 0; data->targets_to_create && i != G_N_ELEMENTS (data->targets); ++i) {
    TargetTestData *target = &data->targets[i];

    if (target->id == 0 && !target->closing) {
      g_autoptr (GError) error = NULL;
      g_autofree gchar *uri = NULL;

      target->receiver_pipeline =
          gaeguli_tests_create_receiver (GAEGULI_SRT_MODE_LISTENER,
          data->fixture->port_base + i, NULL, NULL);
      g_assert_nonnull (target->receiver_pipeline);

      uri = g_strdup_printf ("srt://127.0.0.1:%d",
          data->fixture->port_base + i);

      target->id = gaeguli_pipeline_add_srt_target_full (data->pipeline,
          GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640X480, 30,
          2048000, uri, NULL, &error);
      g_assert_no_error (error);

      g_debug ("Added target %u", target->id);

      --data->targets_to_create;
      break;
    }
  }

  /* Check that all targets are in NORMAL state and data are being read. */
  for (i = 0; i != G_N_ELEMENTS (data->targets); ++i) {
    TargetTestData *target = &data->targets[i];
    guint64 bytes_sent;
    g_autoptr (GError) error = NULL;

    if (target->id == 0 || target->closing) {
      continue;
    }

    bytes_sent = gaeguli_pipeline_target_get_bytes_sent (data->pipeline,
        target->id);

    g_debug ("Target %u has sent %lu B", target->id, bytes_sent);

    /* Remove fifos that have read at least FIFO_READ_LIMIT_BYTES. */
    if (bytes_sent >= TARGET_BYTES_SENT_LIMIT) {
      /* First stop the pipeline. */
      gaeguli_pipeline_remove_target (data->pipeline, target->id, &error);
      g_assert_no_error (error);

      target->closing = TRUE;
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
  gboolean have_active_targets = FALSE;

  g_mutex_lock (&data->lock);

  for (i = 0; i != G_N_ELEMENTS (data->targets); ++i) {
    TargetTestData *target = &data->targets[i];
    g_autoptr (GError) error = NULL;

    if (target->id == target_id) {
      g_assert_true (target->closing);

      gst_element_set_state (target->receiver_pipeline, GST_STATE_NULL);
      gst_clear_object (&target->receiver_pipeline);

      g_debug ("Removed target %u", target->id);

      target->id = 0;
      target->closing = FALSE;
    } else if (target->id != 0) {
      have_active_targets = TRUE;
    }
  }

  if (!have_active_targets && data->targets_to_create == 0) {
    /* All fifos finished receiving, quit the test. */
    g_main_loop_quit (data->fixture->loop);
  }

  g_mutex_unlock (&data->lock);
}

static void
test_gaeguli_pipeline_add_remove_target_random (TestFixture * fixture,
    gconstpointer unused)
{
  AddRemoveTestData data = { 0 };
  guint idle_source;

  g_mutex_init (&data.lock);
  data.fixture = fixture;
  data.pipeline =
      gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_ENCODING_METHOD_GENERAL);
  data.targets_to_create = 10;

  g_signal_connect (data.pipeline, "stream-stopped",
      (GCallback) stream_stopped_cb, &data);

  idle_source = g_timeout_add (20, (GSourceFunc) add_remove_target_cb, &data);
  g_main_loop_run (fixture->loop);
  g_source_remove (idle_source);

  gaeguli_pipeline_stop (data.pipeline);
  g_clear_object (&data.pipeline);
}

static void
test_gaeguli_pipeline_address_in_use (void)
{
  g_autoptr (GaeguliPipeline) pipeline =
      gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_ENCODING_METHOD_GENERAL);
  g_autoptr (GError) error = NULL;
  guint target_id;

  target_id = gaeguli_pipeline_add_srt_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640X480, 30, 2048000,
      "srt://127.0.0.1:1111?mode=listener", NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpint (target_id, !=, 0);

  target_id = gaeguli_pipeline_add_srt_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640X480, 30, 2048000,
      "srt://127.0.0.2:1111?mode=listener", NULL, &error);
  g_assert_error (error, GAEGULI_TRANSMIT_ERROR,
      GAEGULI_TRANSMIT_ERROR_ADDRINUSE);
  g_assert_cmpint (target_id, ==, 0);

  gaeguli_pipeline_stop (pipeline);
}

typedef struct
{
  TestFixture *fixture;
  GaeguliPipeline *pipeline;

  guint watchdog_id;

  GstElement *receiver1;
  GstElement *receiver2;

  gsize receiver1_buffer_cnt;
  gsize receiver2_buffer_cnt;
  gsize receiver3_buffer_cnt;

  gsize receiver1_buffer_cnt_last;

} ClientTestData;

static gboolean
receiver_watchdog_cb (ClientTestData * data)
{
  g_debug ("Watchdog timeout");

  /* Check that receiver 1 haven't stopped receiving. */
  g_assert_cmpuint (data->receiver1_buffer_cnt, >,
      data->receiver1_buffer_cnt_last);

  data->receiver1_buffer_cnt_last = data->receiver1_buffer_cnt;

  return G_SOURCE_CONTINUE;
}

static void
receiver2_buffer_cb (GstElement * object, GstBuffer * buffer, GstPad * pad,
    ClientTestData * data)
{
  ++data->receiver2_buffer_cnt;

  if (data->receiver2_buffer_cnt == 100) {
    g_debug ("Receiver 2 started receiving; exiting the main loop");

    g_main_loop_quit (data->fixture->loop);
  }
}

static void
receiver1_buffer_cb (GstElement * object, GstBuffer * buffer, GstPad * pad,
    ClientTestData * data)
{
  ++data->receiver1_buffer_cnt;

  if (data->receiver1_buffer_cnt == 1) {
    data->watchdog_id = g_timeout_add (100, (GSourceFunc) receiver_watchdog_cb,
        data);
  } else if (data->receiver1_buffer_cnt == 100) {
    guint target_id;
    g_autoptr (GError) error = NULL;
    g_autofree gchar *uri_str = NULL;

    g_debug ("Receiver 1 started receiving; spawning receiver 2");

    uri_str = g_strdup_printf ("srt://127.0.0.1:%d?mode=listener",
        data->fixture->port_base + 1);

    target_id = gaeguli_pipeline_add_srt_target_full (data->pipeline,
        GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640X480, 30, 2048000,
        uri_str, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpint (target_id, !=, 0);

    data->receiver2 = gaeguli_tests_create_receiver (GAEGULI_SRT_MODE_CALLER,
        data->fixture->port_base + 1, (GCallback) receiver2_buffer_cb, data);
  }
}

static void
test_gaeguli_pipeline_listener (TestFixture * fixture, gconstpointer unused)
{
  g_autoptr (GaeguliPipeline) pipeline =
      gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_ENCODING_METHOD_GENERAL);
  g_autoptr (GError) error = NULL;
  g_autofree gchar *uri_str = NULL;
  ClientTestData data = { 0 };
  guint target_id;

  uri_str = g_strdup_printf ("srt://127.0.0.1:%d?mode=caller",
      fixture->port_base);
  target_id = gaeguli_pipeline_add_srt_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640X480, 30, 2048000,
      uri_str, NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpint (target_id, !=, 0);

  data.fixture = fixture;
  data.pipeline = pipeline;
  data.receiver1 = gaeguli_tests_create_receiver (GAEGULI_SRT_MODE_LISTENER,
      data.fixture->port_base, (GCallback) receiver1_buffer_cb, &data);

  g_main_loop_run (fixture->loop);

  g_source_remove (data.watchdog_id);
  gst_element_set_state (data.receiver1, GST_STATE_NULL);
  g_clear_pointer (&data.receiver1, gst_object_unref);
  gst_element_set_state (data.receiver2, GST_STATE_NULL);
  g_clear_pointer (&data.receiver2, gst_object_unref);

  gaeguli_pipeline_stop (pipeline);
}

typedef struct
{
  TestFixture *fixture;
  GaeguliPipeline *pipeline;
  guint listeners_to_create;
  gint listeners[5];
} ListenerRandomTestData;

static gboolean
listener_random_cb (ListenerRandomTestData * data)
{
  gint i = g_random_int_range (0, G_N_ELEMENTS (data->listeners));
  g_autoptr (GError) error = NULL;

  if (data->listeners[i] == 0) {
    g_autofree gchar *uri = NULL;

    if (data->listeners_to_create == 0) {
      return G_SOURCE_CONTINUE;
    }

    uri = g_strdup_printf ("srt://127.0.0.1:%d?mode=listener",
        data->fixture->port_base + i);

    data->listeners[i] = gaeguli_pipeline_add_srt_target_full (data->pipeline,
        GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640X480, 30, 2048000,
        uri, NULL, &error);
    g_assert_no_error (error);

    --data->listeners_to_create;
    g_debug ("Added a listener. %d more to go.(random seed: %d)",
        data->listeners_to_create, i);
  } else {
    gaeguli_pipeline_remove_target (data->pipeline, data->listeners[i], &error);
    g_assert_no_error (error);
    data->listeners[i] = 0;

    g_debug ("Removed a listener.");

    if (data->listeners_to_create == 0) {
      for (i = 0; i != G_N_ELEMENTS (data->listeners); ++i) {
        if (data->listeners[i] != 0) {
          return G_SOURCE_CONTINUE;
        }
      }
      /* All listeners have gone through their lifecycle; stop the test. */
      g_main_loop_quit (data->fixture->loop);
    }
  }

  return G_SOURCE_CONTINUE;
}

static void
test_gaeguli_pipeline_listener_random (TestFixture * fixture,
    gconstpointer unused)
{
  g_autoptr (GaeguliPipeline) pipeline =
      gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_ENCODING_METHOD_GENERAL);

  ListenerRandomTestData data = { 0 };
  guint timeout_source;

  data.fixture = fixture;
  data.pipeline = pipeline;
  data.listeners_to_create = 10;

  timeout_source = g_timeout_add (50, (GSourceFunc) listener_random_cb, &data);
  g_main_loop_run (fixture->loop);
  g_source_remove (timeout_source);

  gaeguli_pipeline_stop (pipeline);
}

static gboolean
_stop_pipeline (TestFixture * fixture)
{
  gaeguli_pipeline_stop (fixture->pipeline);
  g_main_loop_quit (fixture->loop);
  return G_SOURCE_REMOVE;
}

static void
_schedule_pipeline_stop (GaeguliPipeline * pipeline, guint target_id,
    TestFixture * fixture)
{
  g_timeout_add (100, (GSourceFunc) _stop_pipeline, fixture);
}

static void
do_pipeline_cycle (TestFixture * fixture, GaeguliEncodingMethod encoding_method)
{
  g_autoptr (GaeguliPipeline) pipeline =
      gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      encoding_method);
  g_autoptr (GError) error = NULL;

  gaeguli_pipeline_add_srt_target_full (pipeline,
      GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_640X480, 30, 2048000,
      "srt://127.0.0.1:1111", NULL, &error);

  fixture->pipeline = pipeline;

  g_signal_connect (pipeline, "stream-started",
      G_CALLBACK (_schedule_pipeline_stop), fixture);

  g_main_loop_run (fixture->loop);
}

static void
test_gaeguli_pipeline_debug_tx1 (TestFixture * fixture, gconstpointer unused)
{
  GaeguliEncodingMethod encoding_method = GAEGULI_ENCODING_METHOD_GENERAL;
  g_autoptr (GstPluginFeature) feature = NULL;
  guint i;

  feature = gst_registry_lookup_feature (gst_registry_get (), "nvvidconv");
  if (feature) {
    encoding_method = GAEGULI_ENCODING_METHOD_NVIDIA_TX1;
  }

  for (i = 0; i != 3; ++i) {
    do_pipeline_cycle (fixture, encoding_method);
  }
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/gaeguli/pipeline-instance", TestFixture, NULL, fixture_setup,
      test_gaeguli_pipeline_instance, fixture_teardown);

  g_test_add ("/gaeguli/pipeline-add-remove-target-random",
      TestFixture, NULL, fixture_setup,
      test_gaeguli_pipeline_add_remove_target_random, fixture_teardown);

  g_test_add_func ("/gaeguli/pipeline-address-in-use",
      test_gaeguli_pipeline_address_in_use);

  g_test_add ("/gaeguli/pipeline-listener",
      TestFixture, NULL, fixture_setup,
      test_gaeguli_pipeline_listener, fixture_teardown);

  g_test_add ("/gaeguli/pipeline-listener-random",
      TestFixture, NULL, fixture_setup,
      test_gaeguli_pipeline_listener_random, fixture_teardown);

  g_test_add ("/gaeguli/pipeline-debug-tx1", TestFixture, NULL, fixture_setup,
      test_gaeguli_pipeline_debug_tx1, fixture_teardown);

  return g_test_run ();
}
