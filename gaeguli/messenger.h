/**
 *  Copyright 2021 SK Telecom Co., Ltd.
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

#ifndef __GAEGULI_MESSENGER_H__
#define __GAEGULI_MESSENGER_H__

#include <glib-object.h>

typedef struct _GaeguliMessenger GaeguliMessenger;

G_BEGIN_DECLS

#define GAEGULI_TYPE_MESSENGER (gaeguli_messenger_get_type ())
G_DECLARE_FINAL_TYPE           (GaeguliMessenger, gaeguli_messenger, GAEGULI, MESSENGER, GObject)

struct _GaeguliMessenger
{
  GObject parent;

  GIOChannel *read_channel;
  GIOChannel *write_channel;
};


GaeguliMessenger       *gaeguli_messenger_new            (guint              readfd,
                                                          guint              writefd);

void                    gaeguli_messenger_send_terminate (GaeguliMessenger  *self);

G_END_DECLS

#endif // __GAEGULI_MESSENGER_H__
