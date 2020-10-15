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

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/gaeguli/target-encoding-params",
      test_gaeguli_target_encoding_params);

  return g_test_run ();
}
