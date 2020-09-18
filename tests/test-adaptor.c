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

#include "common/receiver.h"

#include <adaptors/nulladaptor.h>
#include <adaptors/bandwidthadaptor.h>

GMainLoop *loop = NULL;

/* GaeguliTestAdaptor class */

#define STATS_INTERVAL_MS 10
#define TEST_BITRATE1 12345678
#define TEST_BITRATE2 999888
#define TEST_QUANTIZATION 37

#define GAEGULI_TYPE_TEST_ADAPTOR   (gaeguli_test_adaptor_get_type ())

/* *INDENT-OFF* */
G_DECLARE_FINAL_TYPE (GaeguliTestAdaptor, gaeguli_test_adaptor, GAEGULI,
    TEST_ADAPTOR, GaeguliStreamAdaptor)
/* *INDENT-ON* */

struct _GaeguliTestAdaptor
{
  GaeguliStreamAdaptor parent;

  gint64 last_callback;
  guint callbacks_left;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliTestAdaptor, gaeguli_test_adaptor,
    GAEGULI_TYPE_STREAM_ADAPTOR)
/* *INDENT-ON* */

static void
gaeguli_test_adaptor_on_stats (GaeguliStreamAdaptor * self,
    GstStructure * stats)
{
  GaeguliTestAdaptor *test_adaptor = GAEGULI_TEST_ADAPTOR (self);
  guint64 now = g_get_monotonic_time ();
  g_autofree gchar *stats_str = gst_structure_to_string (stats);

  g_debug ("Stats callback invoked with stats structure: %s", stats_str);

  g_assert_nonnull (stats);
  g_assert_cmpstr (gst_structure_get_name (stats), ==,
      "application/x-srt-statistics");

  g_assert_cmpint (gst_structure_n_fields (stats), >, 0);

  if (!gst_structure_has_field (stats, "packets-sent")) {
    g_debug ("Socket not connected yet; keep on waiting.");
    return;
  }

  g_assert_true (gst_structure_has_field (stats, "packets-sent-lost"));
  g_assert_true (gst_structure_has_field (stats, "packets-retransmitted"));
  g_assert_true (gst_structure_has_field (stats, "send-rate-mbps"));
  g_assert_true (gst_structure_has_field (stats, "bandwidth-mbps"));

  /* Check callback invocation irregularity lies within 1/5 of STATS_INTERVAL */
  if (test_adaptor->last_callback != 0) {
    g_assert_cmpint ((now - test_adaptor->last_callback) / 1000, >=,
        STATS_INTERVAL_MS - STATS_INTERVAL_MS / 5);
    g_assert_cmpint ((now - test_adaptor->last_callback) / 1000, <,
        STATS_INTERVAL_MS + STATS_INTERVAL_MS / 5);
  }

  test_adaptor->last_callback = now;

  if (--test_adaptor->callbacks_left == 0) {
    g_autoptr (GstElement) srtsink = NULL;
    g_autoptr (GstElement) encoder = NULL;
    guint bitrate, quantizer;

    g_debug ("Invoking change of encoding parameters");

    gaeguli_stream_adaptor_signal_encoding_parameters (self,
        GAEGULI_ENCODING_PARAMETER_BITRATE, G_TYPE_UINT, TEST_BITRATE1,
        GAEGULI_ENCODING_PARAMETER_QUANTIZER, G_TYPE_UINT, TEST_QUANTIZATION,
        NULL);

    g_object_get (self, "srtsink", &srtsink, NULL);

    encoder = gst_bin_get_by_name (GST_BIN (GST_ELEMENT_PARENT (srtsink)),
        "enc");
    g_assert_nonnull (encoder);

    g_object_get (encoder, "bitrate", &bitrate, "quantizer", &quantizer, NULL);
    /* x264enc takes bitrate in kbps */
    g_assert_cmpint (bitrate, ==, TEST_BITRATE1 / 1000);
    g_assert_cmpint (quantizer, ==, TEST_QUANTIZATION);
  }
}

static void
_on_encoding_parameters (GaeguliTestAdaptor * adaptor, GstStructure * params,
    gpointer data)
{
  guint val = 0;

  g_assert_cmpstr (gst_structure_get_name (params), ==,
      "application/x-gaeguli-encoding-parameters");

  g_assert_cmpint (gst_structure_n_fields (params), ==, 2);

  g_assert_true (gst_structure_has_field (params,
          GAEGULI_ENCODING_PARAMETER_BITRATE));
  g_assert_true (gst_structure_get_uint (params,
          GAEGULI_ENCODING_PARAMETER_BITRATE, &val));
  g_assert_cmpint (val, ==, TEST_BITRATE1);

  g_assert_true (gst_structure_has_field (params,
          GAEGULI_ENCODING_PARAMETER_QUANTIZER));
  g_assert_true (gst_structure_get_uint (params,
          GAEGULI_ENCODING_PARAMETER_QUANTIZER, &val));
  g_assert_cmpint (val, ==, TEST_QUANTIZATION);

  g_debug ("Stopping the main loop");
  g_main_loop_quit (loop);
}

static void
gaeguli_test_adaptor_init (GaeguliTestAdaptor * self)
{
  self->callbacks_left = 3;

  g_signal_connect (self, "encoding-parameters",
      G_CALLBACK (_on_encoding_parameters), NULL);
}

static void
gaeguli_test_adaptor_constructed (GObject * self)
{
  const GstStructure *initial_params = NULL;
  guint val;

  g_object_set (self, "stats-interval", STATS_INTERVAL_MS, NULL);

  initial_params = gaeguli_stream_adaptor_get_initial_encoding_parameters
      (GAEGULI_STREAM_ADAPTOR (self));

  g_assert_cmpstr (gst_structure_get_name (initial_params), ==,
      "application/x-gaeguli-encoding-parameters");

  g_assert_true (gst_structure_has_field (initial_params,
          GAEGULI_ENCODING_PARAMETER_BITRATE));
  g_assert_true (gst_structure_get_uint (initial_params,
          GAEGULI_ENCODING_PARAMETER_BITRATE, &val));
  g_assert_cmpint (val, ==, TEST_BITRATE2 - (TEST_BITRATE2 % 1000));

  g_assert_true (gst_structure_has_field (initial_params,
          GAEGULI_ENCODING_PARAMETER_QUANTIZER));
}

static void
gaeguli_test_adaptor_class_init (GaeguliTestAdaptorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GaeguliStreamAdaptorClass *streamadaptor_class =
      GAEGULI_STREAM_ADAPTOR_CLASS (klass);

  gobject_class->constructed = gaeguli_test_adaptor_constructed;
  streamadaptor_class->on_stats = gaeguli_test_adaptor_on_stats;
}

/* *** */

#define GAEGULI_TYPE_DUMMY_SRTSINK   (gaeguli_dummy_srtsink_get_type ())

/* *INDENT-OFF* */
G_DECLARE_FINAL_TYPE (GaeguliDummySrtSink, gaeguli_dummy_srtsink, GAEGULI,
    DUMMY_SRTSINK, GstElement)
/* *INDENT-ON* */

enum
{
  PROP_STATS = 1
};

struct _GaeguliDummySrtSink
{
  GstElement parent;

  GstStructure *stats;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliDummySrtSink, gaeguli_dummy_srtsink, GST_TYPE_ELEMENT)
/* *INDENT-ON* */

static GaeguliDummySrtSink *
gaeguli_dummy_srtsink_new ()
{
  return g_object_new (GAEGULI_TYPE_DUMMY_SRTSINK, NULL);
}

static void G_GNUC_NULL_TERMINATED
gaeguli_dummy_srtsink_set_stats (GaeguliDummySrtSink * self, const gchar * name,
    ...)
{
  va_list varargs;

  va_start (varargs, name);
  gst_structure_set_valist (self->stats, name, varargs);
  va_end (varargs);
}

static void
gaeguli_dummy_srtsink_init (GaeguliDummySrtSink * self)
{
  self->stats = gst_structure_new_empty ("application/x-srt-statistics");
}

static void
gaeguli_dummy_srtsink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GaeguliDummySrtSink *self = GAEGULI_DUMMY_SRTSINK (object);

  switch (property_id) {
    case PROP_STATS:
      g_value_set_boxed (value, self->stats);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gaeguli_dummy_srtsink_dispose (GObject * object)
{
  GaeguliDummySrtSink *self = GAEGULI_DUMMY_SRTSINK (object);

  gst_clear_structure (&self->stats);
}

static void
gaeguli_dummy_srtsink_class_init (GaeguliDummySrtSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gaeguli_dummy_srtsink_get_property;
  gobject_class->dispose = gaeguli_dummy_srtsink_dispose;

  g_object_class_install_property (gobject_class, PROP_STATS,
      g_param_spec_boxed ("stats", "Statistics",
          "SRT Statistics", GST_TYPE_STRUCTURE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

/* *** */

static void
test_gaeguli_adaptor_instance ()
{
  g_autoptr (GstElement) srtsink = gst_element_factory_make ("srtsink", NULL);
  g_autoptr (GaeguliStreamAdaptor) adaptor = NULL;

  adaptor = gaeguli_null_stream_adaptor_new (srtsink);
  g_assert_nonnull (adaptor);
}

static void
test_gaeguli_adaptor_stats ()
{
  g_autoptr (GaeguliPipeline) pipeline = NULL;
  g_autoptr (GstElement) receiver = NULL;
  g_autoptr (GError) error = NULL;

  pipeline = gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC, NULL,
      GAEGULI_ENCODING_METHOD_GENERAL);
  g_object_set (pipeline, "stream-adaptor", GAEGULI_TYPE_TEST_ADAPTOR, NULL);

  gaeguli_pipeline_add_srt_target_full (pipeline, GAEGULI_VIDEO_CODEC_H264,
      GAEGULI_VIDEO_RESOLUTION_640X480, 15, TEST_BITRATE2,
      "srt://127.0.0.1:1111", NULL, &error);
  g_assert_no_error (error);

  receiver = gaeguli_tests_create_receiver (GAEGULI_SRT_MODE_LISTENER, 1111,
      NULL, NULL);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);
  g_clear_pointer (&loop, g_main_loop_unref);

  gaeguli_pipeline_stop (pipeline);
  gst_element_set_state (receiver, GST_STATE_NULL);
}

typedef struct
{
  GMainLoop *loop;
  guint expected_bitrate;
  gboolean params_change_triggered;
} BandwidthTestData;

static gboolean
_quit_main_loop (GMainLoop * loop)
{
  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static void
_bandwidth_on_encoding_parameters (BandwidthTestData * data,
    GstStructure * params)
{
  guint val;

  g_assert_true (gst_structure_get_uint (params,
          GAEGULI_ENCODING_PARAMETER_BITRATE, &val));

  g_assert_cmpuint (val, ==, data->expected_bitrate);

  data->params_change_triggered = TRUE;
  g_main_loop_quit (data->loop);
}

static void
test_gaeguli_adaptor_bandwidth ()
{
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (GaeguliStreamAdaptor) adaptor = NULL;
  g_autoptr (GaeguliDummySrtSink) dummysrt = NULL;
  g_autoptr (GstStructure) initial_params = NULL;
  BandwidthTestData data = { 0 };

  data.loop = loop;

  dummysrt = gaeguli_dummy_srtsink_new ();
  initial_params =
      gst_structure_new ("application/x-gaeguli-encoding-parameters",
      GAEGULI_ENCODING_PARAMETER_BITRATE, G_TYPE_UINT, 1000, NULL);

  adaptor = gaeguli_bandwidth_stream_adaptor_new (GST_ELEMENT (dummysrt),
      initial_params);
  g_object_set (adaptor, "stats-interval", STATS_INTERVAL_MS, NULL);
  g_signal_connect_swapped (adaptor, "encoding-parameters",
      (GCallback) _bandwidth_on_encoding_parameters, &data);

  /* Bandwidth equals default - parameter change should not trigger. */

  gaeguli_dummy_srtsink_set_stats (dummysrt, "bandwidth-mbps", G_TYPE_DOUBLE,
      1000.0, NULL);

  g_timeout_add (3 * STATS_INTERVAL_MS, (GSourceFunc) _quit_main_loop, loop);

  g_main_loop_run (loop);

  g_assert_false (data.params_change_triggered);

  /* Bandwidth higher than default - parameter change should not trigger. */

  gaeguli_dummy_srtsink_set_stats (dummysrt, "bandwidth-mbps", G_TYPE_DOUBLE,
      5000.0, NULL);

  g_timeout_add (3 * STATS_INTERVAL_MS, (GSourceFunc) _quit_main_loop, loop);

  g_main_loop_run (loop);

  g_assert_false (data.params_change_triggered);

  /* Bandwidth lower than default by less than 10% - parameter change should
   * not trigger. */

  gaeguli_dummy_srtsink_set_stats (dummysrt, "bandwidth-mbps", G_TYPE_DOUBLE,
      950.0, NULL);

  g_timeout_add (3 * STATS_INTERVAL_MS, (GSourceFunc) _quit_main_loop, loop);

  g_main_loop_run (loop);

  g_assert_false (data.params_change_triggered);

  /* Bandwidth lower than default by more than 10% - bitrate should change to
   * 800 Mpbs. */

  gaeguli_dummy_srtsink_set_stats (dummysrt, "bandwidth-mbps", G_TYPE_DOUBLE,
      800.0, NULL);
  data.expected_bitrate = 800;

  g_main_loop_run (loop);

  g_assert_true (data.params_change_triggered);

  /* Bandwidth going back up over default bitrate - bitrate should change to
   * default. */

  gaeguli_dummy_srtsink_set_stats (dummysrt, "bandwidth-mbps", G_TYPE_DOUBLE,
      2000.0, NULL);
  data.expected_bitrate = 1000;

  g_main_loop_run (loop);

  g_assert_true (data.params_change_triggered);
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/gaeguli/adaptor-instance", test_gaeguli_adaptor_instance);
  g_test_add_func ("/gaeguli/adaptor-stats", test_gaeguli_adaptor_stats);
  g_test_add_func ("/gaeguli/adaptor-bandwidth",
      test_gaeguli_adaptor_bandwidth);

  return g_test_run ();
}
