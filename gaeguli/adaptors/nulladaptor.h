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

#ifndef __GAEGULI_NULL_STREAM_ADAPTOR_H__
#define __GAEGULI_NULL_STREAM_ADAPTOR_H__

#include "gaeguli/gaeguli.h"

#include <gst/gst.h>

G_BEGIN_DECLS

#define GAEGULI_TYPE_NULL_STREAM_ADAPTOR   (gaeguli_null_stream_adaptor_get_type ())
G_DECLARE_FINAL_TYPE (GaeguliNullStreamAdaptor, gaeguli_null_stream_adaptor, GAEGULI,
    NULL_STREAM_ADAPTOR, GaeguliStreamAdaptor)

/**
 * gaeguli_null_stream_adaptor_new:
 * @srtsink: a #GstSrtSink element to collect data from
 *
 * Creates a null stream adaptor that doesn't perform any adaption.
 *
 * Returns: a #GaeguliStreamAdaptor instance
 */
GaeguliStreamAdaptor     *gaeguli_null_stream_adaptor_new
                                                (GstElement            *srtsink);

G_END_DECLS

#endif // __GAEGULI_NULL_STREAM_ADAPTOR_H__
