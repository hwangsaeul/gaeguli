/**
 *  Copyright 2019 SK Telecom Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *            Jakub Adam <jakub.adam@collabora.com>
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

#ifndef __GAEGULI_FIFO_TRANSMIT_H__
#define __GAEGULI_FIFO_TRANSMIT_H__

#if !defined(__GAEGULI_INSIDE__) && !defined(GAEGULI_COMPILATION)
#error "Only <gaeguli/gaeguli.h> can be included directly."
#endif

#include <glib-object.h>
#include <gaeguli/types.h>

G_BEGIN_DECLS

#define GAEGULI_TYPE_FIFO_TRANSMIT     (gaeguli_fifo_transmit_get_type ())
G_DECLARE_FINAL_TYPE                   (GaeguliFifoTransmit, gaeguli_fifo_transmit,
                                        GAEGULI, FIFO_TRANSMIT, GObject)

GaeguliFifoTransmit    *gaeguli_fifo_transmit_new      (void);

GaeguliFifoTransmit    *gaeguli_fifo_transmit_new_full (const gchar            *tmpdir);

const gchar            *gaeguli_fifo_transmit_get_fifo (GaeguliFifoTransmit    *self);

GIOStatus               gaeguli_fifo_transmit_get_read_status
                                                       (GaeguliFifoTransmit *self);

gssize                  gaeguli_fifo_transmit_get_available_bytes
                                                       (GaeguliFifoTransmit *self);

GVariantDict           *gaeguli_fifo_transmit_get_stats
                                                       (GaeguliFifoTransmit    *self);

guint                   gaeguli_fifo_transmit_start    (GaeguliFifoTransmit    *self,
                                                        const gchar            *host,
                                                        guint                   port,
                                                        GaeguliSRTMode          mode,
                                                        GError                **error);

guint                   gaeguli_fifo_transmit_start_full
                                                       (GaeguliFifoTransmit    *self,
                                                        const gchar            *host,
                                                        guint                   port,
                                                        GaeguliSRTMode          mode,
                                                        const gchar            *username,
                                                        GError                **error);

gboolean                gaeguli_fifo_transmit_stop     (GaeguliFifoTransmit    *self,
                                                        guint                   transmit_id,
                                                        GError                **error); 

G_END_DECLS

#endif // __GAEGULI_FIFO_TRANSMIT_H__
