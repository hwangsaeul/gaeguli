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

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/gaeguli/fifo-transmit-instance",
      test_gaeguli_fifo_transmit_instance);

  g_test_add ("/gaeguli/fifo-transmit-start",
      TestFixture, NULL, fixture_setup,
      test_gaeguli_fifo_transmit_start, fixture_teardown);

  return g_test_run ();
}
