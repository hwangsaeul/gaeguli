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

#include <adaptors/bandwidthadaptor.h>

struct _GaeguliBandwidthStreamAdaptor
{
  GaeguliStreamAdaptor parent;

  guint current_bitrate;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliBandwidthStreamAdaptor, gaeguli_bandwidth_stream_adaptor,
    GAEGULI_TYPE_STREAM_ADAPTOR)
/* *INDENT-ON* */

GaeguliStreamAdaptor *
gaeguli_bandwidth_stream_adaptor_new (GstElement * srtsink,
    GstStructure * baseline_parameters)
{
  g_return_val_if_fail (srtsink != NULL, NULL);

  return g_object_new (GAEGULI_TYPE_BANDWIDTH_STREAM_ADAPTOR,
      "srtsink", srtsink, "baseline-parameters", baseline_parameters, NULL);
}

static void
gaeguli_bandwidth_adaptor_on_stats (GaeguliStreamAdaptor * adaptor,
    GstStructure * stats)
{
  GaeguliBandwidthStreamAdaptor *self =
      GAEGULI_BANDWIDTH_STREAM_ADAPTOR (adaptor);

  gdouble srt_bandwidth;

  if (gst_structure_get_double (stats, "bandwidth-mbps", &srt_bandwidth)) {
    gint new_bitrate = self->current_bitrate;

    if (srt_bandwidth < self->current_bitrate) {
      new_bitrate = srt_bandwidth;
    } else if (srt_bandwidth > self->current_bitrate) {
      guint baseline_bitrate = G_MAXUINT;

      if (!gaeguli_stream_adaptor_get_baseline_parameter_uint
          (GAEGULI_STREAM_ADAPTOR (self), GAEGULI_ENCODING_PARAMETER_BITRATE,
              &baseline_bitrate)) {
        g_warning ("Couldn't read baseline bitrate");
      }

      new_bitrate = MIN (srt_bandwidth, baseline_bitrate);
    }

    if (ABS (new_bitrate - (gint) self->current_bitrate) >
        (self->current_bitrate / 10)) {
      g_debug ("Changing bitrate from %u to %u", self->current_bitrate,
          new_bitrate);

      self->current_bitrate = new_bitrate;

      gaeguli_stream_adaptor_signal_encoding_parameters (adaptor,
          GAEGULI_ENCODING_PARAMETER_BITRATE, G_TYPE_UINT,
          self->current_bitrate, NULL);
    }
  }
}

static void
gaeguli_bandwidth_stream_adaptor_init (GaeguliBandwidthStreamAdaptor * self)
{
}

static void
gaeguli_bandwidth_stream_adaptor_constructed (GObject * object)
{
  GaeguliBandwidthStreamAdaptor *self =
      GAEGULI_BANDWIDTH_STREAM_ADAPTOR (object);

  if (!gaeguli_stream_adaptor_get_baseline_parameter_uint
      (GAEGULI_STREAM_ADAPTOR (self), GAEGULI_ENCODING_PARAMETER_BITRATE,
          &self->current_bitrate)) {
    g_warning ("Couldn't read initial bitrate");
  }
}

static void
gaeguli_bandwidth_stream_adaptor_class_init (GaeguliBandwidthStreamAdaptorClass
    * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GaeguliStreamAdaptorClass *streamadaptor_class =
      GAEGULI_STREAM_ADAPTOR_CLASS (klass);

  gobject_class->constructed = gaeguli_bandwidth_stream_adaptor_constructed;
  streamadaptor_class->on_stats = gaeguli_bandwidth_adaptor_on_stats;
}