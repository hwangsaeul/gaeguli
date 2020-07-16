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

#include <adaptors/nulladaptor.h>
#include <gst/gstelement.h>

struct _GaeguliNullStreamAdaptor
{
  GaeguliStreamAdaptor parent;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliNullStreamAdaptor, gaeguli_null_stream_adaptor,
    GAEGULI_TYPE_STREAM_ADAPTOR)
/* *INDENT-ON* */

GaeguliStreamAdaptor *
gaeguli_null_stream_adaptor_new (GstElement * srtsink)
{
  g_return_val_if_fail (srtsink != NULL, NULL);

  return g_object_new (GAEGULI_TYPE_NULL_STREAM_ADAPTOR, "srtsink", srtsink,
      NULL);
}

static void
gaeguli_null_stream_adaptor_init (GaeguliNullStreamAdaptor * self)
{
}

static void
gaeguli_null_stream_adaptor_class_init (GaeguliNullStreamAdaptorClass * klass)
{
}
