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

#ifndef __GAEGULI_STREAM_ADAPTOR_H__
#define __GAEGULI_STREAM_ADAPTOR_H__

#if !defined(__GAEGULI_INSIDE__) && !defined(GAEGULI_COMPILATION)
#error "Only <gaeguli/gaeguli.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

#define GAEGULI_TYPE_STREAM_ADAPTOR   (gaeguli_stream_adaptor_get_type ())
G_DECLARE_DERIVABLE_TYPE (GaeguliStreamAdaptor, gaeguli_stream_adaptor, GAEGULI,
    STREAM_ADAPTOR, GObject)

struct _GaeguliStreamAdaptorClass
{
  GObjectClass parent_class;
};

G_END_DECLS

#endif // __GAEGULI_STREAM_ADAPTOR_H__
