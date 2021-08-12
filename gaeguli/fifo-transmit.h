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

/**
 * SECTION: fifo-transmit
 * @Title: GaeguliFifoTransmit
 * @Short_description: Object to read fifo and send SRT streaming
 *
 * A #GaeguliFifoTransmit is an object capable of receive video from a fifo file and transmit it using SRT protocol.
 */
G_BEGIN_DECLS

#define GAEGULI_TYPE_FIFO_TRANSMIT     (gaeguli_fifo_transmit_get_type ())
G_DECLARE_FINAL_TYPE                   (GaeguliFifoTransmit, gaeguli_fifo_transmit,
                                        GAEGULI, FIFO_TRANSMIT, GObject)

/**
 * gaeguli_fifo_transmit_new:
 *
 * Creates a new #GaeguliFifoTransmit object
 *
 * Returns: the newly created object
 */
GaeguliFifoTransmit    *gaeguli_fifo_transmit_new      (void);

/**
 * gaeguli_fifo_transmit_new_full:
 * @tmpdir: temporary folder to use to create fifo file
 * @fifoname: a fifo file name
 *
 * Creates a new #GaeguliFifoTransmit object with specific parameters.
 *
 * Returns: the newly created object
 */
GaeguliFifoTransmit    *gaeguli_fifo_transmit_new_full (const gchar            *tmpdir,
                                                        const gchar            *fifoname);

/**
 * gaeguli_fifo_transmit_get_fifo:
 * @self: a #GaeguliFifoTransmit object
 *
 * Gets the path to the fifo file
 *
 * Returns: The fifo transmit file path
 */
const gchar            *gaeguli_fifo_transmit_get_fifo (GaeguliFifoTransmit    *self);

/**
 * gaeguli_fifo_transmit_get_read_status:
 * @self: a #GaeguliFifoTransmit object
 *
 * Gets the read  status of the fifo object.
 *
 * Returns: a #GIOStatus object
 */
GIOStatus               gaeguli_fifo_transmit_get_read_status
                                                       (GaeguliFifoTransmit *self);

/**
 * gaeguli_fifo_transmit_get_available_bytes:
 * @self: a #GaeguliFifoTransmit object
 *
 * Gets the availabe bytes in the fifo.
 *
 * Returns: availabe bytes to read.
 */
gssize                  gaeguli_fifo_transmit_get_available_bytes
                                                       (GaeguliFifoTransmit *self);

/**
 * gaeguli_fifo_transmit_get_available_bytes:
 * @self: a #GaeguliFifoTransmit object
 *
 * Gets stats about the fifo transmited bytes.
 *
 * Returns: stats about fifo transmited bytes
 */
GVariantDict           *gaeguli_fifo_transmit_get_stats
                                                       (GaeguliFifoTransmit    *self);

/**
 * gaeguli_fifo_transmit_start:
 * @self: a #GaeguliFifoTransmit object
 * @host: destination host
 * @port: destination port
 * @mode: SRT mode, wheter to act as listener or caller
 * @error: a #GError
 *
 * Stars the fifo transmist to desired `host` and `port`.
 *
 * Returns: an identifier for the transmision known as transmit_id
 */
guint                   gaeguli_fifo_transmit_start    (GaeguliFifoTransmit    *self,
                                                        const gchar            *host,
                                                        guint                   port,
                                                        GaeguliSRTMode          mode,
                                                        GError                **error);

/**
 * gaeguli_fifo_transmit_start_full:
 * @self: a #GaeguliFifoTransmit object
 * @host: destination host
 * @port: destination port
 * @mode: SRT mode, wheter to act as listener or caller
 * @username: string to identify the sender
 * @latency: SRT latency in ms
 * @error: a #GError
 *
 * Stars the fifo transmist to desired `host` and `port` with addtional parameters.
 *
 * Returns: an identifier for the transmision known as transmit_id
 */
guint                   gaeguli_fifo_transmit_start_full
                                                       (GaeguliFifoTransmit    *self,
                                                        const gchar            *host,
                                                        guint                   port,
                                                        GaeguliSRTMode          mode,
                                                        const gchar            *username,
                                                        guint                   latency,
                                                        GError                **error);

/**
 * gaeguli_fifo_transmit_stop:
 * @self: a #GaeguliFifoTransmit object
 * @transmit_id: identifier as returned by #gaeguli_fifo_transmit_start
 * @error: a #GError
 *
 * Stops the transmision of specific target_id
 *
 * Returns: an identifier for the transmision known as transmit_id
 */
gboolean                gaeguli_fifo_transmit_stop     (GaeguliFifoTransmit    *self,
                                                        guint                   transmit_id,
                                                        GError                **error); 

G_END_DECLS

#endif // __GAEGULI_FIFO_TRANSMIT_H__
