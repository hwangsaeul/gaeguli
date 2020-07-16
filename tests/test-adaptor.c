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

#include <adaptors/nulladaptor.h>

static void
test_gaeguli_adaptor_instance ()
{
  g_autoptr (GstElement) srtsink = gst_element_factory_make ("srtsink", NULL);
  g_autoptr (GaeguliStreamAdaptor) adaptor = NULL;

  adaptor = gaeguli_null_stream_adaptor_new (srtsink);
  g_assert_nonnull (adaptor);
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/gaeguli/adaptor-instance", test_gaeguli_adaptor_instance);

  return g_test_run ();
}
