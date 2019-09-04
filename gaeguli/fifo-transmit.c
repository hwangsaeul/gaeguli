/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#include "config.h"

#include "fifo-transmit.h"

#include <gio/gio.h>
#include <gio/gnetworking.h>
#include <glib/gstdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <srt.h>

/* *INDENT-OFF* */
#define HOSTINFO_JSON_FORMAT \
"{ \
   \"host\": \"%s\", \
   \"port\": %" G_GUINT32_FORMAT ", \
   \"mode\": %" G_GINT32_FORMAT " \
}"
/* *INDENT-ON* */
typedef struct _SRTInfo
{
  gint refcount;

  gchar *hostinfo;
  GSocketAddress *sockaddr;

  SRTSOCKET sock;
  gint poll_id;

} SRTInfo;

static SRTInfo *
srt_info_new (const gchar * hostinfo_json)
{
  SRTInfo *info = g_new0 (SRTInfo, 1);
  info->hostinfo = g_strdup (hostinfo_json);
  info->refcount = 1;
  info->sock = SRT_INVALID_SOCK;

  return info;
}

static SRTInfo *
srt_info_ref (SRTInfo * info)
{
  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->hostinfo != NULL, NULL);
  g_return_val_if_fail (info->refcount >= 1, NULL);

  g_atomic_int_inc (&info->refcount);

  return info;
}

static void
srt_info_unref (SRTInfo * info)
{
  g_return_if_fail (info != NULL);
  g_return_if_fail (info->hostinfo != NULL);
  g_return_if_fail (info->refcount >= 1);

  if (g_atomic_int_dec_and_test (&info->refcount)) {
    g_free (info->hostinfo);
    g_clear_object (&info->sockaddr);
    g_free (info);
  }
}

/* *INDENT-OFF* */
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SRTInfo, srt_info_unref)
/* *INDENT-ON* */

#define BUFSIZE         8192

struct _GaeguliFifoTransmit
{
  GObject parent;

  gchar *fifo_dir;
  gchar *fifo_path;

  GCancellable *cancellable;
  GIOChannel *io_channel;

  GHashTable *sockets;

  gchar buf[BUFSIZE];
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliFifoTransmit, gaeguli_fifo_transmit, G_TYPE_OBJECT)
/* *INDENT-ON* */

static void
_delete_fifo_path (GaeguliFifoTransmit * self)
{
  g_autoptr (GDir) tmpd = NULL;
  const gchar *f;

  if (self->fifo_path != NULL && access (self->fifo_path, F_OK) == 0) {
    unlink (self->fifo_path);
  }

  if (self->fifo_dir == NULL) {
    return;
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

  g_cancellable_cancel (self->cancellable);

  g_clear_pointer (&self->sockets, g_hash_table_unref);
  g_clear_object (&self->io_channel);

  _delete_fifo_path (self);

  g_clear_pointer (&self->fifo_dir, g_free);
  g_clear_pointer (&self->fifo_path, g_free);

  g_clear_object (&self->cancellable);

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
  self->cancellable = g_cancellable_new ();
  self->sockets = g_hash_table_new_full (g_str_hash, g_str_equal,
      (GDestroyNotify) g_free, (GDestroyNotify) srt_info_unref);
}

static GaeguliFifoTransmit *
gaeguli_fifo_transmit_new_full (const gchar * tmpdir)
{

  g_autoptr (GaeguliFifoTransmit) self = NULL;

  self = g_object_new (GAEGULI_TYPE_FIFO_TRANSMIT, NULL);

  self->fifo_dir = g_strdup (tmpdir);
  self->fifo_path = g_build_filename (self->fifo_dir, "fifo", NULL);

  if (access (self->fifo_path, F_OK) == -1) {
    /* TODO: error handling */
    mkfifo (self->fifo_path, 0666);
  }

  return g_steal_pointer (&self);
}

GaeguliFifoTransmit *
gaeguli_fifo_transmit_new (void)
{
  const gchar *sys_tmpdir = g_get_tmp_dir ();
  g_autofree gchar *tmpdir = NULL;
  g_autoptr (GaeguliFifoTransmit) self = NULL;

  tmpdir = g_build_filename (sys_tmpdir, "gaeguli-fifo-XXXXXX", NULL);
  g_mkdtemp (tmpdir);

  self = gaeguli_fifo_transmit_new_full (tmpdir);

  return g_steal_pointer (&self);
}

const gchar *
gaeguli_fifo_transmit_get_fifo (GaeguliFifoTransmit * self)
{
  g_return_val_if_fail (GAEGULI_IS_FIFO_TRANSMIT (self), NULL);

  return self->fifo_path;
}

static void
_send_to (GaeguliFifoTransmit * self, gconstpointer buf, gsize len)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, self->sockets);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
  }
}

static gboolean
_recv_stream (GIOChannel * channel, GIOCondition cond, gpointer user_data)
{
  GaeguliFifoTransmit *self = GAEGULI_FIFO_TRANSMIT (user_data);

  g_debug ("(%s):%s%s%s%s", self->fifo_path,
      (cond & G_IO_ERR) ? " ERR" : "",
      (cond & G_IO_HUP) ? " HUP" : "",
      (cond & G_IO_IN) ? " IN" : "", (cond & G_IO_PRI) ? " PRI" : "");

  if ((cond & G_IO_IN)) {
    g_autoptr (GError) error = NULL;

    gsize read_len = 0;
    GIOStatus io_status =
        g_io_channel_read_chars (channel, self->buf, BUFSIZE, &read_len,
        &error);

    if (io_status != G_IO_STATUS_NORMAL && error != NULL) {
      g_error ("%s", error->message);
    }

    _send_to (self, self->buf, read_len);
  }

  return TRUE;                  /* should keep continuing */
}

guint
gaeguli_fifo_transmit_start (GaeguliFifoTransmit * self,
    const gchar * host, guint port, GaeguliSRTMode mode, GError ** error)
{
  guint transmit_id = 0;
  g_autoptr (SRTInfo) srtinfo = NULL;
  g_autofree gchar *hostinfo = NULL;

  g_return_val_if_fail (GAEGULI_IS_FIFO_TRANSMIT (self), 0);
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

  hostinfo = g_strdup_printf (HOSTINFO_JSON_FORMAT, host, port, mode);
  transmit_id = g_str_hash (hostinfo);
  g_debug ("hostinfo[%d]: %s", transmit_id, hostinfo);

  if (g_hash_table_lookup (self->sockets, hostinfo) != NULL) {
    g_debug ("SRT has already started. (host: %s, port: %d, mode: %d)",
        host, port, mode);
    goto out;
  }

  srtinfo = srt_info_new (hostinfo);
  if (!g_hash_table_insert (self->sockets, g_steal_pointer (&hostinfo),
          g_steal_pointer (&srtinfo))) {
    /* TODO: set errors and return zero id */
    g_error ("Failed to add new srt connection information.");
  }

  g_debug ("Created SRT connection (n: %d)", g_hash_table_size (self->sockets));

  if (self->io_channel == NULL) {
    gint fd = open (self->fifo_path, O_NONBLOCK | O_RDONLY);

    g_debug ("opening io channel (%s)", self->fifo_path);

    /* It's time to read bytes from fifo */
    self->io_channel = g_io_channel_unix_new (fd);
    g_io_channel_set_close_on_unref (self->io_channel, TRUE);
    g_io_channel_set_encoding (self->io_channel, NULL, NULL);
    g_io_channel_set_buffered (self->io_channel, FALSE);
    g_io_channel_set_flags (self->io_channel, G_IO_FLAG_NONBLOCK, NULL);

    g_io_add_watch (self->io_channel, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
        _recv_stream, self);
  }

out:
  return transmit_id;
}

static gboolean
_find_by_transmit_id (gpointer key, gpointer value, gpointer user_data)
{
  guint id = 0;
  SRTInfo *info = value;
  id = g_str_hash (info->hostinfo);

  return id == GPOINTER_TO_UINT (user_data);
}

gboolean
gaeguli_fifo_transmit_stop (GaeguliFifoTransmit * self,
    guint transmit_id, GError ** error)
{
  SRTInfo *info = NULL;
  gboolean ret = FALSE;

  g_return_val_if_fail (GAEGULI_IS_FIFO_TRANSMIT (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  info =
      g_hash_table_find (self->sockets, _find_by_transmit_id,
      GUINT_TO_POINTER (transmit_id));
  if (info != NULL) {
    ret = g_hash_table_remove (self->sockets, info->hostinfo);
  }
  g_debug ("Removed SRT connection (n: %d)", g_hash_table_size (self->sockets));

  if (g_hash_table_size (self->sockets) == 0) {
    g_autoptr (GError) err = NULL;
    GIOStatus io_status = g_io_channel_shutdown (self->io_channel, TRUE, &err);

    if (io_status != G_IO_STATUS_NORMAL) {
      g_error ("Failed to close io channel (reason: %s)", err->message);
    }

    g_clear_object (&self->io_channel);
  }
  return ret;
}
