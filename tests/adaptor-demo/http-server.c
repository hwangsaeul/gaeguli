/**
 *  Copyright 2020 SK Telecom Co., Ltd.
 *    Author: Jakub Adam <jakub.adam@collabora.com>
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
 */

#include "http-server.h"
#include "gresource-adaptor-demo.h"

#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

struct _GaeguliHttpServer
{
  GObject parent;

  SoupServer *soup_server;

  GSList *websocket_connections;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliHttpServer, gaeguli_http_server, G_TYPE_OBJECT)
/* *INDENT-ON* */

enum
{
  SIGNAL_MESSAGE,
  N_SIGNALS
};

guint signals[N_SIGNALS];

GaeguliHttpServer *
gaeguli_http_server_new ()
{
  return GAEGULI_HTTP_SERVER (g_object_new (GAEGULI_TYPE_HTTP_SERVER, NULL));
}

static void
http_cb (SoupServer * server, SoupMessage * msg, const char *path,
    GHashTable * query, SoupClientContext * client, gpointer user_data)
{
  GResource *res = adaptor_demo_get_resource ();
  GBytes *bytes;

  if (g_str_equal (path, "/")) {
    path = "/index.html";
  }

  bytes =
      g_resource_lookup_data (res, path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
  if (bytes) {
    SoupBuffer *buffer;
    gconstpointer data;
    gsize size;

    data = g_bytes_get_data (bytes, &size);

    buffer = soup_buffer_new_with_owner (data, size, bytes,
        (GDestroyNotify) g_bytes_unref);

    soup_message_body_append_buffer (msg->response_body, buffer);
    soup_buffer_free (buffer);

    if (g_str_has_suffix (path, ".js")) {
      soup_message_headers_append (msg->response_headers,
          "Content-Type", "text/javascript");
    } else if (g_str_has_suffix (path, ".css")) {
      soup_message_headers_append (msg->response_headers,
          "Content-Type", "text/css");
    }

    soup_message_set_status (msg, SOUP_STATUS_OK);
  } else {
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
  }
}

static void
gaeguli_http_server_handle_message (GaeguliHttpServer * server,
    SoupWebsocketConnection * connection, GBytes * message)
{
  gsize length = 0;
  const gchar *msg_data = g_bytes_get_data (message, &length);
  g_autoptr (JsonParser) parser = json_parser_new ();
  g_autoptr (GError) error = NULL;

  if (json_parser_load_from_data (parser, msg_data, length, &error)) {
    JsonObject *msg = json_node_get_object (json_parser_get_root (parser));

    if (!json_object_has_member (msg, "msg")) {
      // Invalid message
      return;
    }

    g_signal_emit (server, signals[SIGNAL_MESSAGE],
        g_quark_from_string (json_object_get_string_member (msg, "msg")), msg);
  } else {
    g_printerr ("Error parsing message: %s\n", error->message);
  }
}

static void
message_cb (SoupWebsocketConnection * connection, gint type, GBytes * message,
    gpointer user_data)
{
  gaeguli_http_server_handle_message (GAEGULI_HTTP_SERVER (user_data),
      connection, message);
}

static void
gaeguli_http_server_remove_websocket_connection (GaeguliHttpServer * server,
    SoupWebsocketConnection * connection)
{
  gpointer client_id = g_object_get_data (G_OBJECT (connection), "client_id");

  g_print ("Connection %p closed\n", client_id);

  server->websocket_connections = g_slist_remove (server->websocket_connections,
      client_id);

  //g_signal_emit (server, signals [SIGNAL_WS_CLIENT_DISCONNECTED], 0, client_id);
}

static void
gaeguli_http_server_add_websocket_connection (GaeguliHttpServer * self,
    SoupWebsocketConnection * connection)
{
  g_signal_connect (connection, "message", (GCallback) message_cb, self);
  g_signal_connect_swapped (connection, "closed",
      (GCallback) gaeguli_http_server_remove_websocket_connection, self);

  g_object_ref (connection);

  g_object_set_data (G_OBJECT (connection), "client_id", connection);

  self->websocket_connections =
      g_slist_append (self->websocket_connections, connection);

  //g_signal_emit (server, signals [SIGNAL_WS_CLIENT_CONNECTED], 0, connection);
}

static void
websocket_cb (SoupServer * server, SoupWebsocketConnection * connection,
    const char *path, SoupClientContext * client, gpointer user_data)
{
  g_print ("New connection from %s\n", soup_client_context_get_host (client));

  gaeguli_http_server_add_websocket_connection (GAEGULI_HTTP_SERVER (user_data),
      connection);
}

gchar *
gaeguli_http_server_get_uri (GaeguliHttpServer * self)
{
  g_autoslist (SoupURI) uris = soup_server_get_uris (self->soup_server);

  return soup_uri_to_string (uris->data, FALSE);
}

static void
gaeguli_http_server_send_to_clients (GaeguliHttpServer * self, JsonNode * msg)
{
  g_autofree gchar *msg_str = json_to_string (msg, TRUE);
  GSList *it;


  for (it = self->websocket_connections; it; it = it->next) {
    SoupWebsocketConnection *connection = it->data;
    SoupWebsocketState socket_state;

    socket_state = soup_websocket_connection_get_state (connection);

    if (socket_state == SOUP_WEBSOCKET_STATE_OPEN) {
      soup_websocket_connection_send_text (connection, msg_str);
    } else {
      g_printerr ("Trying to send message using websocket that isn't open.\n");
    }
  }
}

void
gaeguli_http_server_send_property_string (GaeguliHttpServer * self,
    const gchar * name, const gchar * value)
{
  GValue val = G_VALUE_INIT;

  g_value_init (&val, G_TYPE_STRING);
  g_value_set_string (&val, value);
  gaeguli_http_server_send_property (self, name, &val);
  g_value_unset (&val);
}

void
gaeguli_http_server_send_property_uint (GaeguliHttpServer * self,
    const gchar * name, guint value)
{
  GValue val = G_VALUE_INIT;

  g_value_init (&val, G_TYPE_UINT);
  g_value_set_uint (&val, value);
  gaeguli_http_server_send_property (self, name, &val);
  g_value_unset (&val);
}

void
gaeguli_http_server_send_property (GaeguliHttpServer * self, const gchar * name,
    GValue * value)
{
  g_autoptr (JsonBuilder) builder = json_builder_new ();
  g_autoptr (JsonNode) root = NULL;

  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "msg");
  json_builder_add_string_value (builder, "property");

  json_builder_set_member_name (builder, "name");
  json_builder_add_string_value (builder, name);

  json_builder_set_member_name (builder, "value");
  if (G_VALUE_HOLDS_STRING (value)) {
    json_builder_add_string_value (builder, g_value_get_string (value));
  } else if (G_VALUE_HOLDS_UINT (value)) {
    json_builder_add_int_value (builder, g_value_get_uint (value));
  } else if (G_VALUE_HOLDS_BOOLEAN (value)) {
    json_builder_add_boolean_value (builder, g_value_get_boolean (value));
  } else if (G_VALUE_HOLDS_ENUM (value)) {
    g_autoptr (GEnumClass) enum_class = g_type_class_ref (G_VALUE_TYPE (value));

    const gchar *val = g_enum_get_value (enum_class,
        g_value_get_enum (value))->value_nick;

    json_builder_add_string_value (builder, val);
  }

  json_builder_end_object (builder);

  root = json_builder_get_root (builder);

  gaeguli_http_server_send_to_clients (self, root);
}

static void
gaeguli_http_server_init (GaeguliHttpServer * self)
{
  g_autoptr (GResolver) resolver = g_resolver_get_default ();
  g_autoptr (GSocketAddress) address = NULL;
  g_autoptr (GError) error = NULL;
  g_autolist (GInetAddress) addresses = NULL;

  self->soup_server = soup_server_new (NULL, NULL);

  soup_server_add_handler (self->soup_server, NULL, http_cb, self, NULL);
  soup_server_add_websocket_handler (self->soup_server, "/ws", NULL, NULL,
      websocket_cb, self, NULL);

  addresses = g_resolver_lookup_by_name (resolver, g_get_host_name (), NULL,
      &error);
  if (error) {
    goto on_error;
  }

  address = g_inet_socket_address_new (addresses->data, 8080);

  soup_server_listen (self->soup_server, address, 0, &error);
  if (error) {
    goto on_error;
  }

  return;

on_error:
  g_printerr ("Unable to init HTTP server: %s", error->message);
}

static void
gaeguli_http_server_dispose (GObject * object)
{
  GaeguliHttpServer *self = GAEGULI_HTTP_SERVER (object);

  soup_server_disconnect (self->soup_server);
  g_clear_object (&self->soup_server);
}

static void
gaeguli_http_server_class_init (GaeguliHttpServerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gaeguli_http_server_dispose;

  signals[SIGNAL_MESSAGE] =
      g_signal_new ("message", G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, JSON_TYPE_OBJECT);
}
