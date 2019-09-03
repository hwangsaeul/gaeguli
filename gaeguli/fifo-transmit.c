/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#include "config.h"

#include "fifo-transmit.h"
#include <glib/gstdio.h>
#include <unistd.h>

struct _GaeguliFifoTransmit
{
  GObject parent;

  gchar *fifo_dir;
  gchar *fifo_path;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliFifoTransmit, gaeguli_fifo_transmit, G_TYPE_OBJECT)
/* *INDENT-ON* */

static void
_delete_fifo_path (GaeguliFifoTransmit * self)
{
  g_autoptr (GDir) tmpd = NULL;
  const gchar *f;

  if (access (self->fifo_path, F_OK) == 0) {
    unlink (self->fifo_path);
  }

  tmpd = g_dir_open (self->fifo_dir, 0, NULL);

  while ((f = g_dir_read_name (tmpd)) != NULL) {
    g_autofree gchar *fname = g_build_filename (self->fifo_dir, f, NULL);
    if (g_remove (fname) != 0) {
      g_debug ("Failed to remove (%s)", fname);
    }
  }

  if (g_rmdir (self->fifo_dir) != 0) {
    g_debug ("Failed to remove dir (%s)", self->fifo_dir);
  }
}

static void
gaeguli_fifo_transmit_dispose (GObject * object)
{
  GaeguliFifoTransmit *self = GAEGULI_FIFO_TRANSMIT (object);

  _delete_fifo_path (self);

  g_clear_pointer (&self->fifo_dir, g_free);
  g_clear_pointer (&self->fifo_path, g_free);

  G_OBJECT_CLASS (gaeguli_fifo_transmit_parent_class)->dispose (object);
}

static void
gaeguli_fifo_transmit_class_init (GaeguliFifoTransmitClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gaeguli_fifo_transmit_dispose;
}

static void
gaeguli_fifo_transmit_init (GaeguliFifoTransmit * self)
{
}

GaeguliFifoTransmit *
gaeguli_fifo_transmit_new (void)
{
  g_autoptr (GaeguliFifoTransmit) self = NULL;

  self = g_object_new (GAEGULI_TYPE_FIFO_TRANSMIT, NULL);

  return g_steal_pointer (&self);
}

const gchar *
gaeguli_fifo_transmit_get_fifo (GaeguliFifoTransmit * self)
{
  g_return_val_if_fail (GAEGULI_IS_FIFO_TRANSMIT (self), NULL);

  if (self->fifo_dir == NULL) {
    /* create fifo path */
    const gchar *tmpdir = g_get_tmp_dir ();
    self->fifo_dir = g_build_filename (tmpdir, "gaeguli-fifo-XXXXXX", NULL);
    g_mkdtemp (self->fifo_dir);
    g_free (self->fifo_path);
    self->fifo_path = g_build_filename (self->fifo_dir, "fifo", NULL);
    if (access (self->fifo_path, F_OK) == -1) {
      /* TODO: error handling */
      mkfifo (self->fifo_path, 0666);
    }
    g_debug ("created fifo (%s)", self->fifo_path);
  }
  return self->fifo_path;
}
