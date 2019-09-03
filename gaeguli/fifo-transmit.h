/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
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

const gchar            *gaeguli_fifo_transmit_get_fifo (GaeguliFifoTransmit    *self);

guint                   gaeguli_fifo_transmit_start    (GaeguliFifoTransmit    *self,
                                                        const gchar            *host,
                                                        guint                   port,
                                                        GaeguliSRTMode          mode,
                                                        GError                **error);

gboolean                gaeguli_fifo_transmit_stop     (GaeguliFifoTransmit    *self,
                                                        guint                   transmit_id,
                                                        GError                **error); 

G_END_DECLS

#endif // __GAEGULI_FIFO_TRANSMIT_H__
