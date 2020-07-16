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

} GaeguliStreamAdaptorPrivate;

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_PRIVATE (GaeguliStreamAdaptor, gaeguli_stream_adaptor,
    G_TYPE_OBJECT)
/* *INDENT-ON* */

enum
{
  PROP_SRTSINK = 1,
  PROP_LAST
};

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

  G_OBJECT_CLASS (gaeguli_stream_adaptor_parent_class)->dispose (object);
}

static void
gaeguli_stream_adaptor_class_init (GaeguliStreamAdaptorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gaeguli_stream_adaptor_set_property;
  gobject_class->dispose = gaeguli_stream_adaptor_dispose;

  g_object_class_install_property (gobject_class, PROP_SRTSINK,
      g_param_spec_object ("srtsink", "SRT sink",
          "SRT sink", GST_TYPE_ELEMENT,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}
