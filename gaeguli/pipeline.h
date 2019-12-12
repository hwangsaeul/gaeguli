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


GaeguliPipeline        *gaeguli_pipeline_new    (void);

GaeguliPipeline        *gaeguli_pipeline_new_full
                                                (GaeguliVideoSource     source,
                                                 const gchar           *device,
                                                 GaeguliEncodingMethod  encoding_method);

guint                   gaeguli_pipeline_add_fifo_target
                                                (GaeguliPipeline       *self,
                                                 const gchar           *fifo_path,
                                                 GError               **error);

guint                   gaeguli_pipeline_add_fifo_target_full
                                                (GaeguliPipeline       *self,
                                                 GaeguliVideoCodec      codec,
                                                 GaeguliVideoResolution resolution,
                                                 guint                  framerate,
                                                 guint                  bitrate,
                                                 const gchar           *fifo_path,
                                                 GError               **error);

GaeguliReturn           gaeguli_pipeline_remove_target
                                                (GaeguliPipeline       *self,
                                                 guint                  target_id,
                                                 GError               **error);

void                    gaeguli_pipeline_stop   (GaeguliPipeline       *self);

void                    gaeguli_pipeline_dump_to_dot_file
                                                (GaeguliPipeline       *self);

G_END_DECLS

#endif // __GAEGULI_PIPELINE_H__
