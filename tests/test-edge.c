/**
 * tests/test-edge
 *
 * Copyright 2019 SK Telecom, Co., Ltd.
 *   Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 *
 */

#include <gaeguli/gaeguli.h>

static void
test_gaeguli_edge_instance (void)
{
  GaeguliReturn ret;
  guint stream_id;
  g_autoptr (GaeguliEdge) edge = gaeguli_edge_new ();
  g_autoptr (GError) error = NULL;

  stream_id =
      gaeguli_edge_start_stream (edge, NULL, 8888, GAEGULI_SRT_MODE_LISTENER,
      &error);
  g_assert_cmpstr (stream_id, !=, 0);

  ret = gaeguli_edge_stop_stream (edge, stream_id);
  g_assert (ret == GAEGULI_RETURN_OK);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/gaeguli/edge-instance", test_gaeguli_edge_instance);

  return g_test_run ();
}
