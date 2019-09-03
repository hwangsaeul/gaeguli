/**
 * tests/test-fifo-transmit
 *
 * Copyright 2019 SK Telecom, Co., Ltd.
 *   Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 *
 */

#include <gaeguli/gaeguli.h>
#include <gaeguli/fifo-transmit.h>

static void
test_gaeguli_fifo_transmit_instance (void)
{
  const gchar *fifo_path = NULL;
  g_autoptr (GaeguliFifoTransmit) fifo_transmit = gaeguli_fifo_transmit_new ();

  g_assert_nonnull (fifo_transmit);

  fifo_path = gaeguli_fifo_transmit_get_fifo (fifo_transmit);
  g_assert_nonnull (fifo_path);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/gaeguli/fifo-transmit-instance",
      test_gaeguli_fifo_transmit_instance);

  return g_test_run ();
}
