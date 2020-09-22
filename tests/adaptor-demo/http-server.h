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

#ifndef __GAEGULI_HTTP_SERVER_H__
#define __GAEGULI_HTTP_SERVER_H__

#include <glib-object.h>

#define GAEGULI_TYPE_HTTP_SERVER gaeguli_http_server_get_type()

G_DECLARE_FINAL_TYPE (GaeguliHttpServer, gaeguli_http_server, GAEGULI,
    HTTP_SERVER, GObject)

GaeguliHttpServer *
gaeguli_http_server_new ();

gchar *
gaeguli_http_server_get_uri (GaeguliHttpServer * self);

void
gaeguli_http_server_send_property (GaeguliHttpServer * self, const gchar * name,
    GValue * value);

void
gaeguli_http_server_send_property_string (GaeguliHttpServer * self,
    const gchar * name, const gchar * value);

void
gaeguli_http_server_send_property_uint (GaeguliHttpServer * self,
    const gchar * name, guint value);

#endif /* __GAEGULI_HTTP_SERVER_H__ */
