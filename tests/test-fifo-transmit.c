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
#include <glib/gstdio.h>

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

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/gaeguli/fifo-transmit-instance",
      test_gaeguli_fifo_transmit_instance);

  g_test_add ("/gaeguli/fifo-transmit-start",
      TestFixture, NULL, fixture_setup,
      test_gaeguli_fifo_transmit_start, fixture_teardown);

  g_test_add_func ("/gaeguli/fifo-transmit-same-fifo-path",
      test_gaeguli_fifo_transmit_same_fifo_path);

  return g_test_run ();
}
