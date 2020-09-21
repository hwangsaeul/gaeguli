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

#ifndef __GAEGULI_ADAPTOR_DEMO_H__
#define __GAEGULI_ADAPTOR_DEMO_H__

#include <glib-object.h>

#define GAEGULI_TYPE_ADAPTOR_DEMO gaeguli_adaptor_demo_get_type()

G_DECLARE_FINAL_TYPE (GaeguliAdaptorDemo, gaeguli_adaptor_demo, GAEGULI,
    ADAPTOR_DEMO, GObject)

GaeguliAdaptorDemo *
gaeguli_adaptor_demo_new ();

gchar *
gaeguli_adaptor_demo_get_control_uri (GaeguliAdaptorDemo *self);

#endif /* __GAEGULI_ADAPTOR_DEMO_H__ */
