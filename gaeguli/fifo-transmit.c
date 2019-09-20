/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#include "config.h"

#include "fifo-transmit.h"
#include "gaeguli-internal.h"

#include <gio/gio.h>
#include <gio/gnetworking.h>
#include <glib/gstdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <srt.h>

static gint srt_init_refcount = 0;

typedef struct _SRTInfo
{
  gint refcount;

  gchar *hostinfo;
  GSocketAddress *sockaddr;
  GaeguliSRTMode mode;

  SRTSOCKET sock;
  gint poll_id;

} SRTInfo;

static SRTInfo *
srt_info_new (const gchar * host, guint port, GaeguliSRTMode mode,
    const gchar * hostinfo_json)
{
  SRTInfo *info = g_new0 (SRTInfo, 1);

  info->hostinfo = g_strdup (hostinfo_json);
  info->refcount = 1;
  info->sock = SRT_INVALID_SOCK;
  info->poll_id = SRT_ERROR;
  info->sockaddr = g_inet_socket_address_new_from_string (host, port);
  info->mode = mode;

  return info;
}

#if 0
static SRTInfo *
srt_info_ref (SRTInfo * info)
{
  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->hostinfo != NULL, NULL);
  g_return_val_if_fail (info->refcount >= 1, NULL);

  g_atomic_int_inc (&info->refcount);

  return info;
}
#endif

static void
srt_info_unref (SRTInfo * info)
{
  g_return_if_fail (info != NULL);
  g_return_if_fail (info->hostinfo != NULL);
  g_return_if_fail (info->refcount >= 1);

  if (g_atomic_int_dec_and_test (&info->refcount)) {
    if (info->sock != SRT_INVALID_SOCK) {
      srt_close (info->sock);
    }
    g_free (info->hostinfo);
    g_clear_object (&info->sockaddr);
    g_free (info);
  }
}

/* *INDENT-OFF* */
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SRTInfo, srt_info_unref)
/* *INDENT-ON* */

struct srt_constant_params
{
  const gchar *name;
  gint param;
  gint val;
};

static struct srt_constant_params srt_params[] = {
  {"SRTO_SENDER", SRTO_SENDER, 1},      /* 1: sender */
  {"SRTO_SNDSYN", SRTO_SNDSYN, 0},      /* 0: non-blocking */
  {"SRTO_RCVSYN", SRTO_RCVSYN, 0},      /* 0: non-blocking */
  {"SRTO_LINGER", SRTO_LINGER, 0},
  {"SRTO_TSBPMODE", SRTO_TSBPDMODE, 1}, /* Timestamp-based Packet Delivery mode must be enabled */
  {"SRTO_RENDEZVOUS", SRTO_RENDEZVOUS, 0},      /* 0: not for rendezvous */
  {NULL, -1, -1},
};

#define BUFSIZE         8192

struct _GaeguliFifoTransmit
{
  GObject parent;

  gchar *fifo_dir;
  gchar *fifo_path;

  GCancellable *cancellable;
  guint fifo_read_event_source_id;
  GIOStatus fifo_read_status;

  GHashTable *sockets;

  gchar buf[BUFSIZE];

  GaeguliFifoTransmitStats stats;
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
  if (self->fifo_read_event_source_id != 0) {
    g_source_remove (self->fifo_read_event_source_id);
    self->fifo_read_event_source_id = 0;
  }

  _delete_fifo_path (self);

  g_clear_pointer (&self->fifo_dir, g_free);
  g_clear_pointer (&self->fifo_path, g_free);

  g_clear_object (&self->cancellable);

  if (g_atomic_int_dec_and_test (&srt_init_refcount)) {
    srt_cleanup ();
    g_debug ("Cleaning up SRT");
  }

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
  if (g_atomic_int_get (&srt_init_refcount) == 0) {
    if (srt_startup () != 0) {
      g_error ("%s", srt_getlasterror_str ());
    }
  }

  g_atomic_int_inc (&srt_init_refcount);

  self->cancellable = g_cancellable_new ();
  self->sockets = g_hash_table_new_full (g_str_hash, g_str_equal,
      (GDestroyNotify) g_free, (GDestroyNotify) srt_info_unref);
  self->fifo_read_status = G_IO_STATUS_NORMAL;
}

GaeguliFifoTransmit *
gaeguli_fifo_transmit_new_full (const gchar * tmpdir)
{

  g_autoptr (GaeguliFifoTransmit) self = NULL;
  g_autofree gchar *fifo_path = NULL;

  fifo_path = g_build_filename (tmpdir, "fifo", NULL);

  if (access (fifo_path, F_OK) == 0) {
    g_debug ("%s already exists!", fifo_path);
    return NULL;
  } else if (mkfifo (fifo_path, 0666) == -1) {
    g_debug ("Could not create %s: %s", fifo_path, strerror (errno));
    return NULL;
  }

  self = g_object_new (GAEGULI_TYPE_FIFO_TRANSMIT, NULL);

  self->fifo_dir = g_strdup (tmpdir);
  self->fifo_path = g_steal_pointer (&fifo_path);

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
  if (!self) {
    g_remove (tmpdir);
  }

  return g_steal_pointer (&self);
}

const gchar *
gaeguli_fifo_transmit_get_fifo (GaeguliFifoTransmit * self)
{
  g_return_val_if_fail (GAEGULI_IS_FIFO_TRANSMIT (self), NULL);

  return self->fifo_path;
}

GIOStatus
gaeguli_fifo_transmit_get_read_status (GaeguliFifoTransmit * self)
{
  GIOStatus status;

  g_return_val_if_fail (GAEGULI_IS_FIFO_TRANSMIT (self), G_IO_STATUS_ERROR);

  status = self->fifo_read_status;
  if (status == G_IO_STATUS_AGAIN) {
    status = G_IO_STATUS_NORMAL;
  }

  return status;
}

GaeguliFifoTransmitStats *
gaeguli_fifo_transmit_get_stats (GaeguliFifoTransmit * self)
{
  g_return_val_if_fail (GAEGULI_IS_FIFO_TRANSMIT (self), NULL);

  return &self->stats;
}

static gboolean
_srt_open (SRTInfo * info)
{
  g_autoptr (GError) error = NULL;

  gint sock_flags = SRT_EPOLL_ERR | SRT_EPOLL_OUT;
  struct srt_constant_params *params = srt_params;

  gpointer sa;
  size_t sa_len;

  sa_len = g_socket_address_get_native_size (info->sockaddr);
  sa = g_alloca (sa_len);
  if (!g_socket_address_to_native (info->sockaddr, sa, sa_len, &error)) {
    g_warning ("%s", error->message);
  }

  info->sock = srt_socket (AF_INET, SOCK_DGRAM, 0);

  if (info->poll_id != SRT_ERROR) {
    srt_epoll_release (info->poll_id);
  }

  info->poll_id = srt_epoll_create ();

  for (; params->name != NULL; params++) {
    if (srt_setsockopt (info->sock, 0, params->param, &params->val,
            sizeof (gint))) {
      g_error ("%s", srt_getlasterror_str ());
    }

  }

  if (srt_epoll_add_usock (info->poll_id, info->sock, &sock_flags)) {
    g_warning ("%s", srt_getlasterror_str ());
    goto failed;
  }

  if (srt_connect (info->sock, sa, sa_len) == SRT_ERROR) {
    g_debug ("%s", srt_getlasterror_str ());
    goto failed;
  }

  g_debug ("opened srt socket successfully");
  return TRUE;

failed:
  g_debug ("Failed to open srt socket");

  if (info->poll_id != SRT_ERROR) {
    srt_epoll_release (info->poll_id);
  }

  if (info->sock != SRT_INVALID_SOCK) {
    srt_close (info->sock);
  }

  info->poll_id = SRT_ERROR;
  info->sock = SRT_INVALID_SOCK;
  return FALSE;
}

static void
_send_to_listener (GaeguliFifoTransmit * self, SRTInfo * info,
    gconstpointer buf, gsize buf_len)
{
  gssize len = 0;
  gint poll_timeout = 100;      /* FIXME: does it work? */

  if (info->sock == SRT_INVALID_SOCK) {
    g_warning ("Trying to re-connnect");
    _srt_open (info);
  }

  while (len < buf_len) {
    SRTSOCKET wsock;
    gint wsocklen = 1;

    gint sent;
    gint rest = MIN (buf_len - len, 1316);      /* FIXME: https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/merge_requests/657 */

    if (srt_epoll_wait (info->poll_id, 0, 0, &wsock,
            &wsocklen, poll_timeout, NULL, 0, NULL, 0) < 0) {
      g_debug ("Failed to do poll wait, skip data");
      return;
    }

    switch (srt_getsockstate (wsock)) {
      case SRTS_BROKEN:
      case SRTS_NONEXIST:
      case SRTS_CLOSED:
        g_warning ("Invalidate SRT socket");
        srt_epoll_remove_usock (info->poll_id, info->sock);
        srt_close (info->sock);
        info->sock = SRT_INVALID_SOCK;
        return;
      case SRTS_CONNECTED:
        /* good to go */
        break;
      default:
        /* not-ready */
        return;
    }

    sent = srt_sendmsg2 (wsock, (char *) (buf + len), rest, 0);
    g_debug ("sent buffer %d (size: %ld/%lu)", sent, len, buf_len);

    if (sent <= 0) {
      g_warning ("%s", srt_getlasterror_str ());
      return;
    }

    len += sent;
  }
}

static void
_send_to (GaeguliFifoTransmit * self, gconstpointer buf, gsize len)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, self->sockets);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    /* TODO: support to be listener */
    _send_to_listener (self, (SRTInfo *) value, buf, len);
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
    self->fifo_read_status =
        g_io_channel_read_chars (channel, self->buf, BUFSIZE, &read_len,
        &error);

    if (self->fifo_read_status != G_IO_STATUS_NORMAL && error != NULL) {
      g_error ("%s", error->message);
    }

    self->stats.bytes_read += read_len;

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
  g_debug ("hostinfo[%x]: %s", transmit_id, hostinfo);

  if (g_hash_table_lookup (self->sockets, hostinfo) != NULL) {
    g_debug ("SRT has already started. (host: %s, port: %d, mode: %d)",
        host, port, mode);
    goto out;
  }

  srtinfo = srt_info_new (host, port, mode, hostinfo);
  if (!g_hash_table_insert (self->sockets, g_steal_pointer (&hostinfo),
          g_steal_pointer (&srtinfo))) {
    /* TODO: set errors and return zero id */
    g_error ("Failed to add new srt connection information.");
  }

  g_debug ("Created SRT connection (n: %d)", g_hash_table_size (self->sockets));

  if (self->fifo_read_event_source_id == 0) {
    g_autoptr (GIOChannel) io_channel = NULL;
    gint fd = open (self->fifo_path, O_NONBLOCK | O_RDONLY);

    g_debug ("opening io channel (%s)", self->fifo_path);

    /* It's time to read bytes from fifo */
    io_channel = g_io_channel_unix_new (fd);
    g_io_channel_set_close_on_unref (io_channel, TRUE);
    g_io_channel_set_encoding (io_channel, NULL, NULL);
    g_io_channel_set_buffered (io_channel, FALSE);
    g_io_channel_set_flags (io_channel, G_IO_FLAG_NONBLOCK, NULL);

    self->fifo_read_event_source_id = g_io_add_watch (io_channel,
        G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP, _recv_stream, self);
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

  if (g_hash_table_size (self->sockets) == 0 && self->fifo_read_event_source_id) {
    g_source_remove (self->fifo_read_event_source_id);
    self->fifo_read_event_source_id = 0;
  }
  return ret;
}
