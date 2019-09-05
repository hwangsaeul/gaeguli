/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#ifndef __GAEGULI_PIPELINE_H__
#define __GAEGULI_PIPELINE_H__

#if !defined(__GAEGULI_INSIDE__) && !defined(GAEGULI_COMPILATION)
#error "Only <gaeguli/gaeguli.h> can be included directly."
#endif

#include <glib-object.h>
#include <gaeguli/types.h>

G_BEGIN_DECLS

#define GAEGULI_TYPE_PIPELINE   (gaeguli_pipeline_get_type ())
G_DECLARE_FINAL_TYPE            (GaeguliPipeline, gaeguli_pipeline, GAEGULI, PIPELINE, GObject)


GaeguliPipeline        *gaeguli_pipeline_new    (const gchar *source);

guint                   gaeguli_pipeline_add_fifo_target
                                                (GaeguliPipeline       *self,
                                                 const gchar           *fifo_path,
                                                 GError               **error);

GaeguliReturn           gaeguli_pipeline_remove_target
                                                (GaeguliPipeline       *self,
                                                 guint                  target_id,
                                                 GError               **error);
G_END_DECLS

#endif // __GAEGULI_PIPELINE_H__
