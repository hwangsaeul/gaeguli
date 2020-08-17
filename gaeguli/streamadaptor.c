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
 *
 */

#include "streamadaptor.h"

#include <gst/gstelement.h>

typedef struct
{
  GstElement *srtsink;
  guint stats_interval;
  guint stats_timeout_id;
} GaeguliStreamAdaptorPrivate;

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_PRIVATE (GaeguliStreamAdaptor, gaeguli_stream_adaptor,
    G_TYPE_OBJECT)
/* *INDENT-ON* */

enum
{
  PROP_SRTSINK = 1,
  PROP_STATS_INTERVAL,
  PROP_LAST
};

enum
{
  SIG_ENCODING_PARAMETERS,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
gaeguli_stream_adaptor_collect_stats (GaeguliStreamAdaptor * self)
{
  GaeguliStreamAdaptorPrivate *priv =
      gaeguli_stream_adaptor_get_instance_private (self);
  GaeguliStreamAdaptorClass *klass = GAEGULI_STREAM_ADAPTOR_GET_CLASS (self);

  g_autoptr (GstStructure) s = NULL;

  g_object_get (priv->srtsink, "stats", &s, NULL);

  if (gst_structure_n_fields (s) != 0) {
    klass->on_stats (self, s);
  }
}

gboolean
_stats_collection_timeout (gpointer user_data)
{
  gaeguli_stream_adaptor_collect_stats (user_data);

  return G_SOURCE_CONTINUE;
}

static void
gaeguli_stream_adaptor_start_timer (GaeguliStreamAdaptor * self)
{
  GaeguliStreamAdaptorPrivate *priv =
      gaeguli_stream_adaptor_get_instance_private (self);

  priv->stats_timeout_id =
      g_timeout_add (priv->stats_interval, _stats_collection_timeout, self);
}

static void
gaeguli_stream_adaptor_set_srtsink (GaeguliStreamAdaptor * self,
    GstElement * srtsink)
{
  GaeguliStreamAdaptorPrivate *priv =
      gaeguli_stream_adaptor_get_instance_private (self);
  GaeguliStreamAdaptorClass *klass = GAEGULI_STREAM_ADAPTOR_GET_CLASS (self);

  priv->srtsink = gst_object_ref (srtsink);

  if (klass->on_stats) {
    gaeguli_stream_adaptor_start_timer (self);
  }
}

static void
gaeguli_stream_adaptor_set_stats_interval (GaeguliStreamAdaptor * self,
    guint ms)
{
  GaeguliStreamAdaptorPrivate *priv =
      gaeguli_stream_adaptor_get_instance_private (self);

  priv->stats_interval = ms;

  if (priv->stats_timeout_id != 0) {
    g_clear_handle_id (&priv->stats_timeout_id, g_source_remove);
    gaeguli_stream_adaptor_start_timer (self);
  }
}

void
gaeguli_stream_adaptor_signal_encoding_parameters (GaeguliStreamAdaptor * self,
    const gchar * param, ...)
{
  g_autoptr (GstStructure) s = NULL;
  va_list varargs;

  va_start (varargs, param);
  s = gst_structure_new_valist ("application/x-gaeguli-encoding-parameters",
      param, varargs);
  va_end (varargs);

  g_signal_emit (self, signals[SIG_ENCODING_PARAMETERS], 0, s);
}

static void
gaeguli_stream_adaptor_init (GaeguliStreamAdaptor * self)
{
}

static void
gaeguli_stream_adaptor_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GaeguliStreamAdaptor *self = GAEGULI_STREAM_ADAPTOR (object);

  switch (property_id) {
    case PROP_SRTSINK:
      gaeguli_stream_adaptor_set_srtsink (self, g_value_get_object (value));
      break;
    case PROP_STATS_INTERVAL:
      gaeguli_stream_adaptor_set_stats_interval (self,
          g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gaeguli_stream_adaptor_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GaeguliStreamAdaptor *self = GAEGULI_STREAM_ADAPTOR (object);
  GaeguliStreamAdaptorPrivate *priv =
      gaeguli_stream_adaptor_get_instance_private (self);

  switch (property_id) {
    case PROP_STATS_INTERVAL:
      g_value_set_uint (value, priv->stats_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gaeguli_stream_adaptor_dispose (GObject * object)
{
  GaeguliStreamAdaptor *self = GAEGULI_STREAM_ADAPTOR (object);
  GaeguliStreamAdaptorPrivate *priv =
      gaeguli_stream_adaptor_get_instance_private (self);

  gst_clear_object (&priv->srtsink);
  g_clear_handle_id (&priv->stats_timeout_id, g_source_remove);

  G_OBJECT_CLASS (gaeguli_stream_adaptor_parent_class)->dispose (object);
}

static void
gaeguli_stream_adaptor_class_init (GaeguliStreamAdaptorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gaeguli_stream_adaptor_set_property;
  gobject_class->get_property = gaeguli_stream_adaptor_get_property;
  gobject_class->dispose = gaeguli_stream_adaptor_dispose;

  g_object_class_install_property (gobject_class, PROP_SRTSINK,
      g_param_spec_object ("srtsink", "SRT sink",
          "SRT sink", GST_TYPE_ELEMENT,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STATS_INTERVAL,
      g_param_spec_uint ("stats-interval", "Statistics collection interval",
          "Statistics collection interval in milliseconds", 1, G_MAXUINT, 10,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  signals[SIG_ENCODING_PARAMETERS] =
      g_signal_new ("encoding-parameters", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_STRUCTURE);
}
