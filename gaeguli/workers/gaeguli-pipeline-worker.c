#include "config.h"

#include <glib.h>
#include <json-glib/json-glib.h>

#include "messenger.h"

typedef struct _PipelineWorker
{
  GMainLoop *loop;
  GaeguliMessenger *messenger;
} PipelineWorker;

void
free_resources (PipelineWorker * worker)
{
  if (!worker) {
    return;
  }

  g_clear_object (&worker->messenger);
  g_clear_pointer (&worker->loop, g_main_loop_unref);

  g_free (worker);
}

static void
_on_msg_terminate (PipelineWorker * worker, JsonObject * msg)
{
  g_main_loop_quit (worker->loop);
}

int
main (int argc, char *argv[])
{
  PipelineWorker *worker = NULL;
  g_autoptr (GError) error = NULL;

  worker = g_new0 (PipelineWorker, 1);

  worker->messenger = gaeguli_messenger_new (atoi (argv[1]), atoi (argv[2]));

  g_signal_connect_swapped (worker->messenger, "message::terminate",
      G_CALLBACK (_on_msg_terminate), worker);

  worker->loop = g_main_loop_new (NULL, FALSE);

  g_main_loop_run (worker->loop);

  free_resources (worker);

  return 0;
}
