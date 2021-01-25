#include "config.h"

#include <glib-unix.h>
#include <gio/gio.h>

static guint signal_watch_intr_id;

static gboolean
intr_handler (gpointer user_data)
{
  GMainLoop *loop = user_data;

  g_main_loop_quit (loop);
  return G_SOURCE_REMOVE;
}

int
main (int argc, char *argv[])
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GMainLoop) loop = NULL;

  loop = g_main_loop_new (NULL, FALSE);

  signal_watch_intr_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, loop);

  g_main_loop_run (loop);

  return 0;
}
