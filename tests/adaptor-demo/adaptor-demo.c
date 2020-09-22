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

#include <gaeguli/gaeguli.h>
#include <json-glib/json-glib.h>

#include "adaptor-demo.h"
#include "http-server.h"

struct _GaeguliAdaptorDemo
{
  GObject parent;

  GaeguliPipeline *pipeline;
  GaeguliTarget *target;
  GaeguliHttpServer *http_server;
  gchar *srt_uri;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliAdaptorDemo, gaeguli_adaptor_demo, G_TYPE_OBJECT)
/* *INDENT-ON* */

GaeguliAdaptorDemo *
gaeguli_adaptor_demo_new ()
{
  return GAEGULI_ADAPTOR_DEMO (g_object_new (GAEGULI_TYPE_ADAPTOR_DEMO, NULL));
}

gchar *
gaeguli_adaptor_demo_get_control_uri (GaeguliAdaptorDemo * self)
{
  return gaeguli_http_server_get_uri (self->http_server);
}

static void
gaeguli_adaptor_demo_on_property_changed (GaeguliAdaptorDemo * self,
    GParamSpec * pspec)
{
  GValue value = G_VALUE_INIT;

  g_object_get_property (G_OBJECT (self->target), pspec->name, &value);

  gaeguli_http_server_send_property (self->http_server, pspec->name, &value);

  g_value_unset (&value);
}

static void
gaeguli_adaptor_demo_on_msg_stream (GaeguliAdaptorDemo * self, JsonObject * msg)
{
  g_autoptr (GError) error = NULL;

  if (json_object_get_boolean_member (msg, "state")) {
    if (!self->target) {
      static const char *property_names[] = {
        "bitrate", "bitrate-actual", "quantizer", "quantizer-actual"
      };
      GValue property_values[G_N_ELEMENTS (property_names)] = { 0 };
      guint notify_signal_id = g_signal_lookup ("notify", GAEGULI_TYPE_TARGET);
      gint i;

      self->target = gaeguli_pipeline_add_srt_target_full (self->pipeline,
          GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_1920X1080, 30,
          2048000, "srt://:7001?mode=listener", NULL, &error);

      if (error) {
        g_printerr ("Unable to add SRT target: %s\n", error->message);
      }

      gaeguli_http_server_send_property_string (self->http_server, "srt-uri",
          self->srt_uri);

      g_object_getv (G_OBJECT (self->target), G_N_ELEMENTS (property_names),
          property_names, property_values);

      for (i = 0; i != G_N_ELEMENTS (property_names); ++i) {
        gaeguli_http_server_send_property (self->http_server, property_names[i],
            &property_values[i]);

        g_signal_connect_closure_by_id (self->target, notify_signal_id,
            g_quark_from_static_string (property_names[i]),
            g_cclosure_new_swap
            (G_CALLBACK (gaeguli_adaptor_demo_on_property_changed), self,
                NULL), FALSE);
      }
    }
  } else {
    if (self->target) {
      gaeguli_pipeline_remove_target (self->pipeline, self->target, &error);
      if (error) {
        g_printerr ("Unable to remove SRT target: %s\n", error->message);
      }
      self->target = NULL;

      gaeguli_http_server_send_property_string (self->http_server, "srt-uri",
          "");
    }
  }
}

static void
gaeguli_adaptor_demo_init (GaeguliAdaptorDemo * self)
{
  g_autoptr (GResolver) resolver = g_resolver_get_default ();
  g_autolist (GInetAddress) addresses = NULL;
  g_autoptr (GError) error = NULL;

  self->pipeline = gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_V4L2SRC,
      "/dev/video4", GAEGULI_ENCODING_METHOD_GENERAL);
  self->http_server = gaeguli_http_server_new ();

  addresses = g_resolver_lookup_by_name (resolver, g_get_host_name (), NULL,
      &error);
  if (error) {
    g_printerr ("Unable to resolve local host IP");
  } else {
    g_autofree gchar *ip = g_inet_address_to_string (addresses->data);
    self->srt_uri = g_strdup_printf ("srt://%s:7001", ip);
  }

  g_signal_connect_swapped (self->http_server, "message::stream",
      G_CALLBACK (gaeguli_adaptor_demo_on_msg_stream), self);
}

static void
gaeguli_adaptor_demo_dispose (GObject * object)
{
  GaeguliAdaptorDemo *self = GAEGULI_ADAPTOR_DEMO (object);

  g_clear_object (&self->http_server);

  gaeguli_pipeline_stop (self->pipeline);
  g_clear_object (&self->pipeline);
  g_clear_pointer (&self->srt_uri, g_free);
}

static void
gaeguli_adaptor_demo_class_init (GaeguliAdaptorDemoClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gaeguli_adaptor_demo_dispose;
}
