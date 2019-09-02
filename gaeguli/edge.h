/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#ifndef __GAEGULI_EDGE_H__
#define __GAEGULI_EDGE_H__

#if !defined(__GAEGULI_INSIDE__) && !defined(GAEGULI_COMPILATION)
#error "Only <gaeguli/gaeguli.h> can be included directly."
#endif

#include <glib-object.h>
#include <gaeguli/types.h>

G_BEGIN_DECLS

#define GAEGULI_TYPE_EDGE       (gaeguli_edge_get_type ())
GAEGULI_API_EXPORT
G_DECLARE_FINAL_TYPE            (GaeguliEdge, gaeguli_edge, GAEGULI, EDGE, GObject)


GAEGULI_API_EXPORT
GaeguliEdge    *gaeguli_edge_new        (void);

GAEGULI_API_EXPORT
guint           gaeguli_start_stream    (GaeguliEdge   *self,
                                         const gchar   *host, guint port, GaeguliSRTMode mode,
                                         GError       **error);

GAEGULI_API_EXPORT
GaeguliReturn   gaeguli_stop_stream     (GaeguliEdge   *self, guint stream_id);

G_END_DECLS

#endif // __GAEGULI_EDGE_H__
