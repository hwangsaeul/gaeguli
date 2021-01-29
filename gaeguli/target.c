/**
 *  Copyright 2019-2020 SK Telecom Co., Ltd.
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

#include "config.h"

/* GValueArray is deprecated since GLib 2.32 but srtsink returns it in "stats"
 * structure. */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "target.h"

#include "enumtypes.h"
#include "gaeguli-internal.h"
#include "pipeline.h"
#include "adaptors/nulladaptor.h"

#include <gio/gio.h>
#include <fcntl.h>

typedef struct
{
  GObject parent;

  GMutex lock;

  GaeguliTargetState state;

  GaeguliStreamAdaptor *adaptor;

  GaeguliVideoCodec codec;
  GaeguliVideoBitrateControl bitrate_control;
  guint bitrate;
  guint quantizer;
  guint idr_period;
  gchar *uri;
  gchar *peer_address;
  gchar *username;
  gchar *passphrase;
  GaeguliSRTKeyLength pbkeylen;
  GType adaptor_type;
  gboolean adaptive_streaming;
  gboolean is_recording;
  gint32 buffer_size;
  GstStructure *video_params;
  gchar *location;
  GaeguliSRTMode mode;

  guint node_id;
  GPid worker_pid;
  gint target_fds[2];
  gint worker_fds[2];
  GIOChannel *read_ch;
} GaeguliTargetPrivate;

enum
{
  PROP_ID = 1,
  PROP_CODEC,
  PROP_BITRATE_CONTROL,
  PROP_BITRATE_CONTROL_ACTUAL,
  PROP_BITRATE,
  PROP_BITRATE_ACTUAL,
  PROP_QUANTIZER,
  PROP_QUANTIZER_ACTUAL,
  PROP_IDR_PERIOD,
  PROP_URI,
  PROP_USERNAME,
  PROP_PASSPHRASE,
  PROP_PBKEYLEN,
  PROP_ADAPTOR_TYPE,
  PROP_ADAPTIVE_STREAMING,
  PROP_BUFFER_SIZE,
  PROP_LATENCY,
  PROP_VIDEO_PARAMS,
  PROP_TARGET_IS_RECORDING,
  PROP_LOCATION,
  PROP_NODE_ID,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST] = { 0 };

enum
{
  SIG_STREAM_STARTED,
  SIG_STREAM_STOPPED,
  SIG_CALLER_ADDED,
  SIG_CALLER_REMOVED,
  LAST_SIGNAL
};

typedef struct _GaeguliTargetMsg
{
  GaeguliTargetMsgType type;
  GaeguliVideoBitrateControl bitrate_control;
  guint bitrate;
  guint quantizer;
  gboolean adaptive_streaming;
  GType adaptor_type;
} GaeguliTargetMsg;

typedef struct _GaeguliTargetWorkerMsg
{
  GaeguliTargetWorkerMsgType msg_type;
  gint srtsocket;
  GSocketAddress *address;
  GaeguliVideoBitrateControl bitrate_control;
  guint bitrate;
  guint quantizer;
  GaeguliSRTMode mode;
} GaeguliTargetWorkerMsg;

static guint signals[LAST_SIGNAL] = { 0 };

static void gaeguli_target_initable_iface_init (GInitableIface * iface);

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_CODE (GaeguliTarget, gaeguli_target, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (GaeguliTarget)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                gaeguli_target_initable_iface_init))
/* *INDENT-ON* */

#define LOCK_TARGET \
  g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&priv->lock)

static void
gaeguli_target_init (GaeguliTarget * self)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  g_mutex_init (&priv->lock);
  priv->state = GAEGULI_TARGET_STATE_NEW;
  priv->adaptor_type = GAEGULI_TYPE_NULL_STREAM_ADAPTOR;
}

static GaeguliTargetMsg *
gaeguli_target_build_message (GaeguliTargetMsgType type,
    GaeguliVideoBitrateControl bitrate_control, guint bitrate, guint quantizer,
    gboolean adaptive_streaming, GType adaptor_type)
{
  GaeguliTargetMsg *msg = g_new0 (GaeguliTargetMsg, 1);

  msg->type = type;
  msg->bitrate_control = bitrate_control;
  msg->bitrate = bitrate;
  msg->quantizer = quantizer;
  msg->adaptive_streaming = adaptive_streaming;
  msg->adaptor_type = adaptor_type;

  return msg;
}

static gboolean
gaeguli_target_post_message (GaeguliTarget * self, GaeguliTargetMsg * msg)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);
  g_autoptr (GError) error = NULL;
  gsize size = sizeof (GaeguliTargetMsg);

  if (!priv->target_fds[1]) {
    g_free (msg);
    return FALSE;
  }

  if (write (priv->target_fds[1], msg, size) < 0) {
    g_warning ("Failed to post message");
  }
  g_free (msg);

  return TRUE;
}

static void
process_msg (GaeguliTarget * self, GaeguliTargetWorkerMsg * msg)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);
  if (!self || !msg) {
    return;
  }

  switch (msg->msg_type) {
    default:
      break;
    case GAEGULI_TARGET_CALLER_ADDED_MSG:{
      g_signal_emit (self, signals[SIG_CALLER_ADDED], 0, msg->srtsocket,
          msg->address);
    }
      break;

    case GAEGULI_TARGET_CALLER_REMOVED_MSG:{
      g_signal_emit (self, signals[SIG_CALLER_REMOVED], 0, msg->srtsocket,
          msg->address);
    }
      break;

    case GAEGULI_TARGET_SRT_MODE_MSG:{
      priv->mode = msg->mode;
    }
      break;

    case GAEGULI_TARGET_NOTIFY_ENCODER_BITRATE_CONTROL_CHANGE_MSG:{
      g_object_notify_by_pspec (G_OBJECT (self),
          properties[PROP_BITRATE_CONTROL_ACTUAL]);
    }
      break;

    case GAEGULI_TARGET_NOTIFY_ENCODER_BITRATE_CHANGE_MSG:{
      g_object_notify_by_pspec (G_OBJECT (self),
          properties[PROP_BITRATE_ACTUAL]);
    }
      break;

    case GAEGULI_TARGET_NOTIFY_ENCODER_QUANTIZER_CHANGE_MSG:{
      g_object_notify_by_pspec (G_OBJECT (self),
          properties[PROP_QUANTIZER_ACTUAL]);
    }
      break;
  }
}

static gboolean
cb_read_watch (GIOChannel * channel, GIOCondition cond, gpointer data)
{
  GaeguliTargetWorkerMsg msg;
  gsize size = sizeof (GaeguliTargetWorkerMsg);
  GaeguliTarget *self = data;
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  if ((cond == G_IO_HUP) || (cond == G_IO_ERR)) {
    g_io_channel_unref (channel);
    return FALSE;
  }

  if (read (priv->worker_fds[0], (void *) &msg, size) > 0) {
    process_msg (self, &msg);
  }

  return TRUE;
}

static void
_cb_child_watch (GPid pid, gint status, gpointer data)
{
  /* Close pid */
  g_debug ("closing process with pid: %d\n", pid);
  g_spawn_close_pid (pid);
}

static gchar **
_gaeguli_build_target_worker_args (GaeguliTarget * self)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);
  gsize n_bytes = (GAEGULI_TARGET_WORKER_ARGS_NUM * sizeof (gchar *));
  gchar **args = g_malloc (n_bytes);
  if (args) {
    gint i = 0;

    if (g_file_test (GAEGULI_TARGET_WORKER, G_FILE_TEST_EXISTS)) {
      args[i++] = g_strdup (GAEGULI_TARGET_WORKER);
    } else {
      args[i++] = g_strdup (GAEGULI_TARGET_WORKER_ALT);
    }
    args[i++] = g_strdup_printf ("%d", self->id);
    args[i++] = g_strdup_printf ("%d", priv->is_recording);
    if (!priv->is_recording) {
      args[i++] = g_strdup (priv->uri);
    } else {
      args[i++] = g_strdup (priv->location);
    }
    args[i++] = g_strdup_printf ("%d", GAEGULI_SRT_MODE_UNKNOWN);
    args[i++] = g_strdup_printf ("%d", priv->buffer_size);
    args[i++] = g_strdup_printf ("%d", priv->pbkeylen);
    args[i++] = g_strdup_printf ("%d", priv->target_fds[0]);
    args[i++] = g_strdup_printf ("%d", priv->target_fds[1]);
    args[i++] = g_strdup_printf ("%d", priv->worker_fds[0]);
    args[i++] = g_strdup_printf ("%d", priv->worker_fds[1]);
    args[i++] = g_strdup_printf ("%d", priv->bitrate_control);
    args[i++] = g_strdup_printf ("%d", priv->bitrate);
    args[i++] = g_strdup_printf ("%d", priv->quantizer);
    args[i++] = g_strdup_printf ("%d", priv->codec);
    args[i++] = g_strdup_printf ("%u", priv->idr_period);
    args[i++] = g_strdup_printf ("%u", priv->node_id);
    args[i++] = g_strdup (priv->username);
    args[i++] = g_strdup (priv->passphrase);

    args[i++] = NULL;
  }
  return args;
}

static gboolean
gaeguli_target_initable_init (GInitable * initable, GCancellable * cancellable,
    GError ** error)
{
  GaeguliTarget *self = GAEGULI_TARGET (initable);
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  g_autoptr (GaeguliPipeline) owner = NULL;
  g_autoptr (GstElement) enc_first = NULL;
  g_autoptr (GstElement) muxsink_first = NULL;
  g_autoptr (GError) internal_err = NULL;

  g_autoptr (GError) gerr = NULL;
  gboolean ret = TRUE;
  GPid pid;
  gchar **args = NULL;

  /* Create pipes for IPC: Target -> Worker direction */
  if (pipe (priv->target_fds) < 0) {
    g_error ("Failed to create target fds");
    ret = FALSE;
    goto failed;
  }

  /* Create pipes for IPC: Worker -> Target direction */
  if (pipe (priv->worker_fds) < 0) {
    g_error ("Failed to create worker fds");
    ret = FALSE;
    goto failed;
  }

  if (!(args = _gaeguli_build_target_worker_args (self))) {
    ret = FALSE;
    goto failed;
  }

  if (!g_spawn_async (NULL, args, NULL,
          G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_LEAVE_DESCRIPTORS_OPEN, NULL,
          NULL, &pid, &gerr)) {
    g_error ("spawning pipeline worker failed. %s", gerr->message);
    ret = FALSE;
    goto failed;
  } else {
    g_debug ("A new child process spawned with pid %d", pid);
    priv->worker_pid = pid;
    g_child_watch_add (pid, (GChildWatchFunc) _cb_child_watch, self);

    /* Close the read side of the target pipe */
    close (priv->target_fds[0]);
    priv->target_fds[0] = 0;

    /* Make the write fd of pipeline pipe non blocking */
    if (fcntl (priv->target_fds[1], F_SETFL, O_NONBLOCK) < 0) {
      g_error ("failed to set non blocking flag on write fd of target pipe");
      ret = FALSE;
      goto failed;
    }

    /* Close the write side of the worker pipe */
    close (priv->worker_fds[1]);
    priv->worker_fds[1] = 0;

    /* Create channel to read data on worker pipe */
    priv->read_ch = g_io_channel_unix_new (priv->worker_fds[0]);

    g_io_channel_set_flags (priv->read_ch, G_IO_FLAG_NONBLOCK, NULL);
    g_io_channel_set_encoding (priv->read_ch, NULL, NULL);
    g_io_channel_set_buffered (priv->read_ch, FALSE);

    /* Add watches to read channel */
    g_io_add_watch (priv->read_ch, G_IO_IN | G_IO_ERR | G_IO_HUP,
        (GIOFunc) cb_read_watch, self);
  }

failed:
  g_strfreev (args);
  if (internal_err) {
    g_propagate_error (error, internal_err);
    internal_err = NULL;
  }

  return ret;
}

static void
gaeguli_target_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GaeguliTarget *self = GAEGULI_TARGET (object);
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  switch (prop_id) {
    case PROP_ID:
      g_value_set_uint (value, self->id);
      break;
    case PROP_BITRATE_CONTROL:
      g_value_set_enum (value, priv->bitrate_control);
      break;
#ifndef USE_TARGET_WORKER_PROCESS
    case PROP_BITRATE_CONTROL_ACTUAL:
      g_value_set_enum (value, _get_encoding_parameter_enum (priv->encoder,
              GAEGULI_ENCODING_PARAMETER_RATECTRL));
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, priv->bitrate);
      break;
    case PROP_BITRATE_ACTUAL:
      g_value_set_uint (value, _get_encoding_parameter_uint (priv->encoder,
              GAEGULI_ENCODING_PARAMETER_BITRATE));
      break;
    case PROP_QUANTIZER:
      g_value_set_uint (value, priv->quantizer);
      break;
    case PROP_QUANTIZER_ACTUAL:
      g_value_set_uint (value, _get_encoding_parameter_uint (priv->encoder,
              GAEGULI_ENCODING_PARAMETER_QUANTIZER));
      break;
#endif
    case PROP_ADAPTIVE_STREAMING:
      if (priv->adaptor) {
        priv->adaptive_streaming =
            gaeguli_stream_adaptor_is_enabled (priv->adaptor);
      }
      g_value_set_boolean (value, priv->adaptive_streaming);
      break;
    case PROP_BUFFER_SIZE:
      g_value_set_int (value, priv->buffer_size);
      break;
#ifndef USE_TARGET_WORKER_PROCESS
    case PROP_LATENCY:{
      g_object_get_property (G_OBJECT (priv->srtsink), "latency", value);
      break;
    }
#endif
    case PROP_VIDEO_PARAMS:{
      g_value_set_boxed (value, priv->video_params);
    }
      break;
    case PROP_TARGET_IS_RECORDING:
      g_value_set_boolean (value, priv->is_recording);
      break;
    case PROP_NODE_ID:
      g_value_set_uint (value, priv->node_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gaeguli_target_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GaeguliTarget *self = GAEGULI_TARGET (object);
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  switch (prop_id) {
    case PROP_ID:
      self->id = g_value_get_uint (value);
      break;
    case PROP_CODEC:
      priv->codec = g_value_get_enum (value);
      break;
    case PROP_BITRATE:{
      guint new_bitrate = g_value_get_uint (value);
      if (priv->bitrate != new_bitrate) {
        priv->bitrate = new_bitrate;
        if (priv->worker_pid > 0) {
          if (!gaeguli_target_post_message (self,
                  gaeguli_target_build_message (GAEGULI_SET_TARGET_BITRATE_MSG,
                      priv->bitrate_control, priv->bitrate, priv->quantizer,
                      priv->adaptive_streaming, priv->adaptor_type))) {
            g_warning ("Failed to send message to worker");
          }
        }
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    case PROP_BITRATE_CONTROL:{
      GaeguliVideoBitrateControl new_rate_control = g_value_get_enum (value);
      if (priv->bitrate_control != new_rate_control) {
        priv->bitrate_control = new_rate_control;

        if (priv->worker_pid > 0) {
          if (!gaeguli_target_post_message (self,
                  gaeguli_target_build_message
                  (GAEGULI_SET_TARGET_BITRATE_CONTROL_MSG,
                      priv->bitrate_control, priv->bitrate, priv->quantizer,
                      priv->adaptive_streaming, priv->adaptor_type))) {
            g_warning ("Failed to send message to worker");
          }
        }
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    case PROP_QUANTIZER:{
      guint new_quantizer = g_value_get_uint (value);
      if (priv->quantizer != new_quantizer) {
        priv->quantizer = new_quantizer;

        if (priv->worker_pid > 0) {
          if (!gaeguli_target_post_message (self,
                  gaeguli_target_build_message
                  (GAEGULI_SET_TARGET_QUANTIZER_MSG, priv->bitrate_control,
                      priv->bitrate, priv->quantizer, priv->adaptive_streaming,
                      priv->adaptor_type))) {
            g_warning ("Failed to send message to worker");
          }
        }
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    case PROP_IDR_PERIOD:
      priv->idr_period = g_value_get_uint (value);
      break;
    case PROP_URI:
      priv->uri = g_value_dup_string (value);
      break;
    case PROP_USERNAME:
      g_clear_pointer (&priv->username, g_free);
      priv->username = g_value_dup_string (value);
      break;
    case PROP_PASSPHRASE:
      g_clear_pointer (&priv->passphrase, g_free);
      priv->passphrase = g_value_dup_string (value);
      break;
    case PROP_PBKEYLEN:
      priv->pbkeylen = g_value_get_enum (value);
      break;
    case PROP_ADAPTOR_TYPE:
      priv->adaptor_type = g_value_get_gtype (value);
      break;
    case PROP_ADAPTIVE_STREAMING:{
      gboolean new_adaptive_streaming = g_value_get_boolean (value);
      if (priv->adaptive_streaming != new_adaptive_streaming) {
        priv->adaptive_streaming = new_adaptive_streaming;
        if (priv->worker_pid > 0) {
          if (!gaeguli_target_post_message (self,
                  gaeguli_target_build_message
                  (GAEGULI_SET_TARGET_ADAPTIVE_STREAMING_MSG,
                      priv->bitrate_control, priv->bitrate, priv->quantizer,
                      priv->adaptive_streaming, priv->adaptor_type))) {
            g_warning ("Failed to send message to worker");
          }
        }
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    case PROP_BUFFER_SIZE:
      priv->buffer_size = g_value_get_int (value);
      break;
    case PROP_VIDEO_PARAMS:
      priv->video_params = g_value_dup_boxed (value);
      break;
    case PROP_TARGET_IS_RECORDING:
      priv->is_recording = g_value_get_boolean (value);
      break;
    case PROP_LOCATION:
      g_clear_pointer (&priv->location, g_free);
      priv->location = g_value_dup_string (value);
      break;
    case PROP_NODE_ID:
      priv->node_id = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gaeguli_target_dispose (GObject * object)
{
  GaeguliTarget *self = GAEGULI_TARGET (object);
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  g_clear_object (&priv->adaptor);

  g_clear_pointer (&priv->uri, g_free);
  g_clear_pointer (&priv->peer_address, g_free);
  g_clear_pointer (&priv->username, g_free);
  g_clear_pointer (&priv->passphrase, g_free);
  g_clear_pointer (&priv->location, g_free);
  gst_clear_structure (&priv->video_params);
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gaeguli_target_parent_class)->dispose (object);
}

static void
gaeguli_target_class_init (GaeguliTargetClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gaeguli_target_get_property;
  gobject_class->set_property = gaeguli_target_set_property;
  gobject_class->dispose = gaeguli_target_dispose;

  properties[PROP_ID] =
      g_param_spec_uint ("id", "target ID", "target ID",
      0, G_MAXUINT, 0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_CODEC] =
      g_param_spec_enum ("codec", "video codec", "video codec",
      GAEGULI_TYPE_VIDEO_CODEC, DEFAULT_VIDEO_CODEC,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_BITRATE_CONTROL] =
      g_param_spec_enum ("bitrate-control", "bitrate control",
      "bitrate control", GAEGULI_TYPE_VIDEO_BITRATE_CONTROL,
      GAEGULI_VIDEO_BITRATE_CONTROL_CBR,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

  properties[PROP_BITRATE_CONTROL_ACTUAL] =
      g_param_spec_enum ("bitrate-control-actual", "actual rate control",
      "actual encoding type", GAEGULI_TYPE_VIDEO_BITRATE_CONTROL,
      GAEGULI_VIDEO_BITRATE_CONTROL_CBR,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_BITRATE] =
      g_param_spec_uint ("bitrate", "requested video bitrate",
      "requested video bitrate", 1, G_MAXUINT, DEFAULT_VIDEO_BITRATE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

  properties[PROP_BITRATE_ACTUAL] =
      g_param_spec_uint ("bitrate-actual", "actual video bitrate",
      "actual video bitrate", 1, G_MAXUINT, DEFAULT_VIDEO_BITRATE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_QUANTIZER] =
      g_param_spec_uint ("quantizer", "Constant quantizer or quality to apply",
      "Constant quantizer or quality to apply",
      0, 50, 21,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

  properties[PROP_QUANTIZER_ACTUAL] =
      g_param_spec_uint ("quantizer-actual",
      "Actual constant quantizer or quality used",
      "Actual constant quantizer or quality used", 0, G_MAXUINT, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_IDR_PERIOD] =
      g_param_spec_uint ("idr-period", "keyframe interval",
      "Maximal distance between two key-frames (0 for automatic)",
      0, G_MAXUINT, 0,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_URI] =
      g_param_spec_string ("uri", "SRT URI", "SRT URI",
      NULL, G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_USERNAME] =
      g_param_spec_string ("username", "username", "username",
      NULL, G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_PASSPHRASE] =
      g_param_spec_string ("passphrase", "passphrase",
      "Password for the encrypted transmission. Must be 10 to 80 "
      "characters long. Pass NULL to unset.",
      NULL, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_PBKEYLEN] =
      g_param_spec_enum ("pbkeylen", "Cryptographic key length in bytes",
      "Cryptographic key length in bytes",
      GAEGULI_TYPE_SRT_KEY_LENGTH, GAEGULI_SRT_KEY_LENGTH_0,
      G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_ADAPTOR_TYPE] =
      g_param_spec_gtype ("adaptor-type", "stream adaptor type",
      "Type of network stream adoption the target should perform",
      GAEGULI_TYPE_STREAM_ADAPTOR, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_ADAPTIVE_STREAMING] =
      g_param_spec_boolean ("adaptive-streaming", "Use adaptive streaming",
      "Use adaptive streaming", TRUE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

  properties[PROP_BUFFER_SIZE] =
      g_param_spec_int ("buffer-size", "Send buffer size",
      "Send buffer size in bytes (0 = library default)", 0, G_MAXINT32, 0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  properties[PROP_LATENCY] =
      g_param_spec_int ("latency", "SRT latency",
      "SRT latency in milliseconds", 0, G_MAXINT32, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_VIDEO_PARAMS] =
      g_param_spec_boxed ("video-params",
      "Video encoding parameters from the original configuration",
      "Video encoding parameters from the original configuration",
      GST_TYPE_STRUCTURE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  properties[PROP_TARGET_IS_RECORDING] =
      g_param_spec_boolean ("is-recording", "Is Recording target",
      "Is Recording target", FALSE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

  properties[PROP_LOCATION] =
      g_param_spec_string ("location",
      "Location to store the recorded stream",
      "Location to store the recorded stream", NULL,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_NODE_ID] =
      g_param_spec_uint ("node-id", "pipewire node ID",
      "pipewire node ID", 0, G_MAXUINT, 0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
      G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, G_N_ELEMENTS (properties),
      properties);

  signals[SIG_STREAM_STARTED] =
      g_signal_new ("stream-started", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 0);

  signals[SIG_STREAM_STOPPED] =
      g_signal_new ("stream-stopped", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 0);

  signals[SIG_CALLER_ADDED] =
      g_signal_new ("caller-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);

  signals[SIG_CALLER_REMOVED] =
      g_signal_new ("caller-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);
}

static void
gaeguli_target_initable_iface_init (GInitableIface * iface)
{
  iface->init = gaeguli_target_initable_init;
}

GaeguliTarget *
gaeguli_target_new (guint id,
    GaeguliVideoCodec codec, guint bitrate, guint idr_period,
    const gchar * srt_uri, const gchar * username, gboolean is_record_target,
    const gchar * location, guint node_id, GError ** error)
{
  return g_initable_new (GAEGULI_TYPE_TARGET, NULL, error, "id", id,
      "codec", codec, "bitrate", bitrate,
      "idr-period", idr_period, "uri", srt_uri, "username", username,
      "is-recording", is_record_target, "location", location, "node-id",
      node_id, NULL);
}

void
gaeguli_target_start (GaeguliTarget * self, GError ** error)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  g_autoptr (GstBus) bus = NULL;
  g_autoptr (GError) internal_err = NULL;
  g_autofree gchar *streamid = NULL;

  if (priv->state != GAEGULI_TARGET_STATE_NEW) {
    g_warning ("Target %u is already running", self->id);
    return;
  }

  priv->state = GAEGULI_TARGET_STATE_STARTING;

  g_signal_emit (self, signals[SIG_STREAM_STARTED], 0);
  g_print ("emitted \"stream-started\" for [%x]\n", self->id);

  return;
}

void
gaeguli_target_stop (GaeguliTarget * self)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  LOCK_TARGET;

  /* Send stop message to worker */
  if (priv->target_fds[1] > 0) {
    if (!gaeguli_target_post_message (self,
            gaeguli_target_build_message (GAEGULI_SET_TARGET_STOP_MSG,
                priv->bitrate_control, priv->bitrate, priv->quantizer,
                priv->adaptive_streaming, priv->adaptor_type))) {
      g_warning
          ("Failed to send message with msg type GAEGULI_SET_TARGET_STOP_MSG to worker");
    }

    close (priv->target_fds[1]);
    priv->target_fds[1] = 0;
  }

  if (priv->read_ch) {
    g_io_channel_shutdown (priv->read_ch, TRUE, NULL);
    g_clear_pointer (&priv->read_ch, g_io_channel_unref);
    priv->read_ch = NULL;
  }

  priv->state = GAEGULI_TARGET_STATE_STOPPED;

  g_signal_emit (self, signals[SIG_STREAM_STOPPED], 0);
}

static GVariant *_convert_gst_structure_to (GstStructure * s);

static GVariant *
_convert_value_array_to (GValueArray * a)
{
  g_autofree GVariant **children = g_new0 (GVariant *, a->n_values);
  guint i;

  for (i = 0; i != a->n_values; ++i) {
    children[i] = _convert_gst_structure_to
        (g_value_get_boxed (g_value_array_get_nth (a, i)));
  }

  return g_variant_ref_sink
      (g_variant_new_array (G_VARIANT_TYPE_VARDICT, children, a->n_values));
}

static GVariant *
_convert_gst_structure_to (GstStructure * s)
{
  g_autoptr (GVariantDict) dict = g_variant_dict_new (NULL);
  gint i = 0;

  for (i = 0; i < gst_structure_n_fields (s); i++) {
    const gchar *fname = gst_structure_nth_field_name (s, i);
    const GValue *v = NULL;
    g_autoptr (GVariant) variant = NULL;

    v = gst_structure_get_value (s, fname);

    if (G_TYPE_IS_FUNDAMENTAL (G_VALUE_TYPE (v))) {
      const GVariantType *variant_type = NULL;

      switch (G_VALUE_TYPE (v)) {
        case G_TYPE_INT:
          variant_type = G_VARIANT_TYPE_INT32;
          break;
        case G_TYPE_UINT:
          variant_type = G_VARIANT_TYPE_UINT32;
          break;
        case G_TYPE_UINT64:
          variant_type = G_VARIANT_TYPE_UINT64;
          break;
        case G_TYPE_INT64:
          variant_type = G_VARIANT_TYPE_INT64;
          break;
        case G_TYPE_DOUBLE:
          variant_type = G_VARIANT_TYPE_DOUBLE;
          break;
      }

      variant = g_dbus_gvalue_to_gvariant (v, variant_type);
    } else if (G_VALUE_HOLDS (v, G_TYPE_VALUE_ARRAY)) {
      variant = _convert_value_array_to (g_value_get_boxed (v));
    }

    if (!variant) {
      g_warning ("unsupported type was detected (%s)", G_VALUE_TYPE_NAME (v));
      goto out;
    }

    g_variant_dict_insert_value (dict, fname, variant);
  }

out:
  return g_variant_dict_end (dict);
}

GaeguliSRTMode
gaeguli_target_get_srt_mode (GaeguliTarget * self)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  return priv->mode;
}

const gchar *
gaeguli_target_get_peer_address (GaeguliTarget * self)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  return priv->peer_address;
}

GaeguliTargetState
gaeguli_target_get_state (GaeguliTarget * self)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  return priv->state;
}

GVariant *
gaeguli_target_get_stats (GaeguliTarget * self)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  g_autoptr (GstStructure) s = NULL;

  g_return_val_if_fail (GAEGULI_IS_TARGET (self), NULL);
#ifndef USE_TARGET_WORKER_PROCESS
  if (priv->srtsink) {
    g_object_get (priv->srtsink, "stats", &s, NULL);
    return _convert_gst_structure_to (s);
  }
#endif
  return NULL;
}

GaeguliStreamAdaptor *
gaeguli_target_get_stream_adaptor (GaeguliTarget * self)
{
  GaeguliTargetPrivate *priv = gaeguli_target_get_instance_private (self);

  return priv->adaptor;
}
