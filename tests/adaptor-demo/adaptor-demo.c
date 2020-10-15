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
#include <gaeguli/adaptors/bandwidthadaptor.h>
#include <json-glib/json-glib.h>

#include "adaptor-demo.h"
#include "http-server.h"
#include "traffic-control.h"

struct _GaeguliAdaptorDemo
{
  GObject parent;

  GaeguliPipeline *pipeline;
  GaeguliTarget *target;
  GaeguliTrafficControl *traffic_control;
  GaeguliHttpServer *http_server;
  gchar *device;
  gchar *srt_uri;
  guint stats_timeout_id;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliAdaptorDemo, gaeguli_adaptor_demo, G_TYPE_OBJECT)
/* *INDENT-ON* */

enum
{
  PROP_DEVICE = 1,
};

GaeguliAdaptorDemo *
gaeguli_adaptor_demo_new (const gchar * v4l2_device)
{
  return GAEGULI_ADAPTOR_DEMO (g_object_new (GAEGULI_TYPE_ADAPTOR_DEMO,
          "device", v4l2_device, NULL));
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

gboolean
gaeguli_adaptor_demo_process_stats (GaeguliAdaptorDemo * self)
{
  g_autoptr (GVariant) variant = NULL;
  GVariantDict d;

  gint64 packets_sent = 0;
  gint packets_sent_lost = 0;
  gdouble bandwidth_mbps = 0;
  gdouble send_rate_mbps = 0;

  variant = gaeguli_target_get_stats (self->target);

  g_variant_dict_init (&d, variant);

  if (g_variant_dict_contains (&d, "callers")) {
    GVariant *array;
    GVariant *caller_variant;

    array = g_variant_dict_lookup_value (&d, "callers", G_VARIANT_TYPE_ARRAY);
    /* Use the last caller in the array for display. */
    caller_variant = g_variant_get_child_value (array,
        g_variant_n_children (array) - 1);

    g_variant_dict_clear (&d);
    g_variant_dict_init (&d, caller_variant);
  }

  g_variant_dict_lookup (&d, "packets-sent", "x", &packets_sent);
  g_variant_dict_lookup (&d, "packets-sent-lost", "i", &packets_sent_lost);
  g_variant_dict_lookup (&d, "send-rate-mbps", "d", &send_rate_mbps);
  g_variant_dict_lookup (&d, "bandwidth-mbps", "d", &bandwidth_mbps);

  g_variant_dict_clear (&d);

  gaeguli_http_server_send_property_uint (self->http_server,
      "srt-packets-sent", packets_sent);
  gaeguli_http_server_send_property_uint (self->http_server,
      "srt-packets-sent-lost", packets_sent_lost);
  gaeguli_http_server_send_property_uint (self->http_server,
      "srt-send-rate", send_rate_mbps * 1e6);
  gaeguli_http_server_send_property_uint (self->http_server,
      "srt-bandwidth", bandwidth_mbps * 1e6);

  return G_SOURCE_CONTINUE;
}

static void
gaeguli_adaptor_demo_on_msg_stream (GaeguliAdaptorDemo * self, JsonObject * msg)
{
  g_autoptr (GError) error = NULL;
  GValue val = G_VALUE_INIT;

  if (json_object_get_boolean_member (msg, "state")) {
    if (!self->target) {
      static const char *property_names[] = {
        "bitrate", "bitrate-actual", "quantizer", "quantizer-actual",
        "bitrate-control", "bitrate-control-actual", "adaptive-streaming"
      };
      GValue property_values[G_N_ELEMENTS (property_names)] = { 0 };
      guint notify_signal_id = g_signal_lookup ("notify", GAEGULI_TYPE_TARGET);
      GaeguliVideoCodec codec = GAEGULI_VIDEO_CODEC_H264_X264;
      const gchar *codec_str;
      gint i;

      codec_str = json_object_get_string_member (msg, "codec");
      if (!codec_str) {
        codec = GAEGULI_VIDEO_CODEC_H264_X264;
      } else if (g_str_equal (codec_str, "x265enc")) {
        codec = GAEGULI_VIDEO_CODEC_H265_X265;
      } else if (g_str_equal (codec_str, "vaapih264enc")) {
        codec = GAEGULI_VIDEO_CODEC_H264_VAAPI;
      } else if (g_str_equal (codec_str, "vaapih265enc")) {
        codec = GAEGULI_VIDEO_CODEC_H265_VAAPI;
      }

      self->target = gaeguli_pipeline_add_srt_target_full (self->pipeline,
          codec, 2048000, "srt://:7001?mode=listener", NULL, &error);

      if (error) {
        g_printerr ("Unable to add SRT target: %s\n", error->message);
        g_clear_error (&error);
      }

      gaeguli_target_start (self->target, &error);

      if (error) {
        g_printerr ("Unable to start SRT target: %s\n", error->message);
        g_clear_error (&error);
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

      self->stats_timeout_id = g_timeout_add (500,
          (GSourceFunc) gaeguli_adaptor_demo_process_stats, self);

      // TODO: select the right network interface
      self->traffic_control = gaeguli_traffic_control_new ("lo");

      g_object_get_property (G_OBJECT (self->traffic_control), "enabled", &val);
      gaeguli_http_server_send_property (self->http_server, "tc-enabled", &val);
      g_value_unset (&val);
      g_object_get_property (G_OBJECT (self->traffic_control), "bandwidth",
          &val);
      gaeguli_http_server_send_property (self->http_server,
          "tc-bandwidth", &val);
      g_value_unset (&val);
    }
  } else {
    if (self->target) {
      g_clear_handle_id (&self->stats_timeout_id, g_source_remove);
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
gaeguli_adaptor_demo_on_msg_property (GaeguliAdaptorDemo * self,
    JsonObject * msg)
{
  const gchar *name = json_object_get_string_member (msg, "name");
  GObject *receiver = NULL;
  GValue value = G_VALUE_INIT;
  g_autoptr (GError) error = NULL;

  if (g_str_equal (name, "bitrate") || g_str_equal (name, "quantizer") ||
      g_str_equal (name, "adaptive-streaming")) {
    receiver = G_OBJECT (self->target);
  } else if (g_str_equal (name, "bitrate-control")) {
    g_autoptr (GEnumClass) enum_class =
        g_type_class_ref (GAEGULI_TYPE_VIDEO_BITRATE_CONTROL);

    g_value_init (&value, GAEGULI_TYPE_VIDEO_BITRATE_CONTROL);
    g_value_set_enum (&value, g_enum_get_value_by_nick (enum_class,
            json_object_get_string_member (msg, "value"))->value);

    receiver = G_OBJECT (self->target);
  } else if (g_str_equal (name, "tc-enabled") ||
      g_str_equal (name, "tc-bandwidth")) {
    receiver = G_OBJECT (self->traffic_control);
    name += 3;
  }

  if (receiver) {
    if (!G_IS_VALUE (&value)) {
      json_node_get_value (json_object_get_member (msg, "value"), &value);
    }

    g_object_set_property (receiver, name, &value);
  }

  g_value_unset (&value);
}

static void
gaeguli_adaptor_demo_init (GaeguliAdaptorDemo * self)
{
}

static void
gaeguli_adaptor_demo_constructed (GObject * object)
{
  GaeguliAdaptorDemo *self = GAEGULI_ADAPTOR_DEMO (object);

  g_autoptr (GResolver) resolver = g_resolver_get_default ();
  g_autolist (GInetAddress) addresses = NULL;
  g_autoptr (GError) error = NULL;

  self->pipeline = gaeguli_pipeline_new_full (GAEGULI_VIDEO_SOURCE_V4L2SRC,
      self->device, GAEGULI_VIDEO_RESOLUTION_1920X1080, 24);
  self->http_server = gaeguli_http_server_new ();

  g_object_set (self->pipeline, "stream-adaptor",
      GAEGULI_TYPE_BANDWIDTH_STREAM_ADAPTOR, NULL);

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
  g_signal_connect_swapped (self->http_server, "message::property",
      G_CALLBACK (gaeguli_adaptor_demo_on_msg_property), self);
}

static void
gaeguli_adaptor_demo_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GaeguliAdaptorDemo *self = GAEGULI_ADAPTOR_DEMO (object);

  switch (prop_id) {
    case PROP_DEVICE:
      self->device = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gaeguli_adaptor_demo_dispose (GObject * object)
{
  GaeguliAdaptorDemo *self = GAEGULI_ADAPTOR_DEMO (object);

  g_clear_handle_id (&self->stats_timeout_id, g_source_remove);
  g_clear_object (&self->http_server);

  gaeguli_pipeline_stop (self->pipeline);
  g_clear_object (&self->pipeline);
  g_clear_pointer (&self->device, g_free);
  g_clear_pointer (&self->srt_uri, g_free);
}

static void
gaeguli_adaptor_demo_class_init (GaeguliAdaptorDemoClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gaeguli_adaptor_demo_constructed;
  gobject_class->set_property = gaeguli_adaptor_demo_set_property;
  gobject_class->dispose = gaeguli_adaptor_demo_dispose;

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "V4L2 device", "V4L2 device",
          "/dev/video0",
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}
