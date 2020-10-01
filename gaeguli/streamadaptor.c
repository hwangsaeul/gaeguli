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
  GstStructure *baseline_parameters;
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
  PROP_BASELINE_PARAMETERS,
  PROP_STATS_INTERVAL,
  PROP_ENABLED,
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

  if (GAEGULI_STREAM_ADAPTOR_GET_CLASS (self)->on_stats) {
    priv->stats_timeout_id =
        g_timeout_add (priv->stats_interval, _stats_collection_timeout, self);
  }
}

static void
gaeguli_stream_adaptor_stop_timer (GaeguliStreamAdaptor * self)
{
  GaeguliStreamAdaptorPrivate *priv =
      gaeguli_stream_adaptor_get_instance_private (self);

  g_clear_handle_id (&priv->stats_timeout_id, g_source_remove);
}

static void
gaeguli_stream_adaptor_signal_encoding_parameters_internal (GaeguliStreamAdaptor
    * self, const GstStructure * params)
{
  g_signal_emit (self, signals[SIG_ENCODING_PARAMETERS], 0, params);
}

static void
gaeguli_stream_adaptor_set_stats_interval (GaeguliStreamAdaptor * self,
    guint ms)
{
  GaeguliStreamAdaptorPrivate *priv =
      gaeguli_stream_adaptor_get_instance_private (self);

  priv->stats_interval = ms;

  if (priv->stats_timeout_id != 0) {
    gaeguli_stream_adaptor_stop_timer (self);
    gaeguli_stream_adaptor_start_timer (self);
  }
}

gboolean
gaeguli_stream_adaptor_is_enabled (GaeguliStreamAdaptor * self)
{
  GaeguliStreamAdaptorPrivate *priv =
      gaeguli_stream_adaptor_get_instance_private (self);

  return priv->stats_timeout_id != 0;
}

const GstStructure *
gaeguli_stream_adaptor_get_baseline_parameters (GaeguliStreamAdaptor * self)
{
  GaeguliStreamAdaptorPrivate *priv;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (GAEGULI_IS_STREAM_ADAPTOR (self), NULL);

  priv = gaeguli_stream_adaptor_get_instance_private (self);

  return priv->baseline_parameters;
}

gboolean
gaeguli_stream_adaptor_get_baseline_parameter_uint (GaeguliStreamAdaptor * self,
    const gchar * name, guint * value)
{
  const GstStructure *s = gaeguli_stream_adaptor_get_baseline_parameters (self);

  return s ? gst_structure_get_uint (s, name, value) : FALSE;
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

  gaeguli_stream_adaptor_signal_encoding_parameters_internal (self, s);
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
  GaeguliStreamAdaptorPrivate *priv =
      gaeguli_stream_adaptor_get_instance_private (self);

  switch (property_id) {
    case PROP_SRTSINK:
      priv->srtsink = g_value_dup_object (value);
      break;
    case PROP_BASELINE_PARAMETERS:
      priv->baseline_parameters = g_value_dup_boxed (value);
      break;
    case PROP_STATS_INTERVAL:
      gaeguli_stream_adaptor_set_stats_interval (self,
          g_value_get_uint (value));
      break;
    case PROP_ENABLED:
      if (g_value_get_boolean (value)) {
        gaeguli_stream_adaptor_start_timer (self);
      } else if (priv->stats_timeout_id) {
        gaeguli_stream_adaptor_stop_timer (self);

        /* Revert encoder settings into their initial state. */
        gaeguli_stream_adaptor_signal_encoding_parameters_internal (self,
            priv->baseline_parameters);
      }
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
    case PROP_SRTSINK:
      g_value_set_object (value, priv->srtsink);
      break;
    case PROP_BASELINE_PARAMETERS:
      g_value_set_boxed (value, priv->baseline_parameters);
      break;
    case PROP_STATS_INTERVAL:
      g_value_set_uint (value, priv->stats_interval);
      break;
    case PROP_ENABLED:
      g_value_set_boolean (value, gaeguli_stream_adaptor_is_enabled (self));
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

  gaeguli_stream_adaptor_stop_timer (self);
  gst_clear_object (&priv->srtsink);
  gst_clear_structure (&priv->baseline_parameters);

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
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BASELINE_PARAMETERS,
      g_param_spec_boxed ("baseline-parameters",
          "Baseline encoding parameters", "Baseline encoding parameters the "
          "adaptor derives its proposed modifications from", GST_TYPE_STRUCTURE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STATS_INTERVAL,
      g_param_spec_uint ("stats-interval", "Statistics collection interval",
          "Statistics collection interval in milliseconds", 1, G_MAXUINT, 10,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENABLED,
      g_param_spec_boolean ("enabled", "Turns stream adaptor on or off",
          "Turns stream adaptor on or off", TRUE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  signals[SIG_ENCODING_PARAMETERS] =
      g_signal_new ("encoding-parameters", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_STRUCTURE);
}
