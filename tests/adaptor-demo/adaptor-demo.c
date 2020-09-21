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
gaeguli_adaptor_demo_on_msg_stream (GaeguliAdaptorDemo * self, JsonObject * msg)
{
  g_autoptr (GError) error = NULL;

  if (json_object_get_boolean_member (msg, "state")) {
    if (!self->target) {
      self->target = gaeguli_pipeline_add_srt_target_full (self->pipeline,
          GAEGULI_VIDEO_CODEC_H264, GAEGULI_VIDEO_RESOLUTION_1920X1080, 30,
          2048000, "srt://:7001?mode=listener", NULL, &error);

      if (error) {
        g_printerr ("Unable to add SRT target: %s\n", error->message);
      }
    }
  } else {
    if (self->target) {
      gaeguli_pipeline_remove_target (self->pipeline, self->target, &error);
      if (error) {
        g_printerr ("Unable to remove SRT target: %s\n", error->message);
      }
      self->target = NULL;
    }
  }

  gaeguli_pipeline_dump_to_dot_file (self->pipeline);
}

static void
gaeguli_adaptor_demo_init (GaeguliAdaptorDemo * self)
{
  self->pipeline = gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_V4L2SRC,
      "/dev/video4", GAEGULI_ENCODING_METHOD_GENERAL);
  self->http_server = gaeguli_http_server_new ();

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
}

static void
gaeguli_adaptor_demo_class_init (GaeguliAdaptorDemoClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gaeguli_adaptor_demo_dispose;
}
