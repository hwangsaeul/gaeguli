/**
 *  Copyright 2021 SK Telecom Co., Ltd.
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

#include "messenger.h"

#include <fcntl.h>
#include <json-glib/json-glib.h>

enum
{
  SIGNAL_MESSAGE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliMessenger, gaeguli_messenger, G_TYPE_OBJECT)
/* *INDENT-ON* */

static gboolean
_channel_readable (GIOChannel * source, GIOCondition condition, gpointer data)
{
  GaeguliMessenger *self = data;
  g_autoptr (JsonParser) parser = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *str = NULL;
  GIOStatus status;
  gsize length;

  status = g_io_channel_read_line (self->read_channel, &str, &length,
      NULL, &error);
  if (status != G_IO_STATUS_NORMAL) {
    g_error ("Error reading to IO channel: %s", error->message);
    return G_SOURCE_REMOVE;
  }

  parser = json_parser_new ();

  if (json_parser_load_from_data (parser, str, length, &error)) {
    JsonObject *msg = json_node_get_object (json_parser_get_root (parser));

    {
      g_autofree gchar *msg_str =
          json_to_string (json_parser_get_root (parser), TRUE);
      g_debug ("Message received %s", msg_str);
    }

    if (json_object_has_member (msg, "request")) {
      const gchar *request = json_object_get_string_member (msg, "request");
      g_signal_emit (self, signals[SIGNAL_MESSAGE],
          g_quark_from_string (request), msg);
    }
  } else {
    g_error ("Error parsing message: %s", error->message);
  }

  return G_SOURCE_CONTINUE;
}

GaeguliMessenger *
gaeguli_messenger_new (guint readfd, guint writefd)
{
  GaeguliMessenger *self = g_object_new (GAEGULI_TYPE_MESSENGER, NULL);
  GIOStatus status;

  self->read_channel = g_io_channel_unix_new (readfd);
  status = g_io_channel_set_flags (self->read_channel, G_IO_FLAG_NONBLOCK,
      NULL);
  if (status != G_IO_STATUS_NORMAL) {
    g_error ("Failed to make read file descriptor non-blocking");
  }

  g_io_channel_set_close_on_unref (self->read_channel, TRUE);
  g_io_channel_set_encoding (self->read_channel, NULL, NULL);

  g_io_add_watch (self->read_channel, G_IO_IN, _channel_readable, self);

  self->write_channel = g_io_channel_unix_new (writefd);

  g_io_channel_set_close_on_unref (self->read_channel, TRUE);

  return self;
}

static void
gaeguli_messenger_send (GaeguliMessenger * self, JsonNode * msg)
{
  g_autofree gchar *msgstr = json_to_string (msg, FALSE);
  g_autoptr (GError) error = NULL;

  if (g_io_channel_write_chars (self->write_channel, msgstr, -1,
          NULL, &error) != G_IO_STATUS_NORMAL) {
    g_error ("Error writing to IO channel: %s", error->message);
    return;
  }
  if (g_io_channel_write_chars (self->write_channel, "\n", -1,
          NULL, &error) != G_IO_STATUS_NORMAL) {
    g_error ("Error writing to IO channel: %s", error->message);
    return;
  }
}

void
gaeguli_messenger_send_terminate (GaeguliMessenger * self)
{
  JsonBuilder *builder;
  JsonNode *root;

  builder = json_builder_new ();
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "request");
  json_builder_add_string_value (builder, "terminate");
  json_builder_end_object (builder);

  root = json_builder_get_root (builder);

  gaeguli_messenger_send (self, root);

  json_node_unref (root);
  g_object_unref (builder);
}

static void
gaeguli_messenger_init (GaeguliMessenger * self)
{
}

static void
gaeguli_messenger_dispose (GObject * object)
{
  GaeguliMessenger *self = GAEGULI_MESSENGER (object);

  g_clear_pointer (&self->read_channel, g_io_channel_unref);
  g_clear_pointer (&self->write_channel, g_io_channel_unref);

  G_OBJECT_CLASS (gaeguli_messenger_parent_class)->dispose (object);
}

static void
gaeguli_messenger_class_init (GaeguliMessengerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gaeguli_messenger_dispose;

  signals[SIGNAL_MESSAGE] =
      g_signal_new ("message", G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_DETAILED | G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, JSON_TYPE_OBJECT);
}
