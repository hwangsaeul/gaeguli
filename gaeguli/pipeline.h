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

/**
 * SECTION: pipeline
 * @Title: GaeguliPipeline
 * @Short_description: Object to read video and send it to fifo
 *
 * A #GaeguliPipeline is an object capable of receive video from different type of sources and send it to a fifo for further processing.
 */

G_BEGIN_DECLS

#define GAEGULI_TYPE_PIPELINE   (gaeguli_pipeline_get_type ())
G_DECLARE_FINAL_TYPE            (GaeguliPipeline, gaeguli_pipeline, GAEGULI, PIPELINE, GObject)

/**
 * gaeguli_pipeline_new:
 *
 * Creates a new #GaeguliPipeline object
 *
 * Returns: the newly created object
 */
GaeguliPipeline        *gaeguli_pipeline_new    (void);

/**
 * gaeguli_pipeline_new_full:
 * @source: the source of the video
 * @device: the device used as source in case of V4L
 * @encoding_method: the codec use for encoding
 *
 * Creates a new #GaeguliPipeline object using specific parameters.
 *
 * Returns: the newly created object
 */
GaeguliPipeline        *gaeguli_pipeline_new_full
                                                (GaeguliVideoSource     source,
                                                 const gchar           *device,
                                                 GaeguliEncodingMethod  encoding_method);
/**
 * gaeguli_pipeline_add_fifo_target:
 * @self: a #GaeguliPipeline object
 * @fifo_path: path to fifo file
 * @error: a #GError
 *
 * Adds a fifo target to the pipeline.
 *
 * Returns: an identifier for the fifo connection known as target_id
 */
guint                   gaeguli_pipeline_add_fifo_target
                                                (GaeguliPipeline       *self,
                                                 const gchar           *fifo_path,
                                                 GError               **error);

/**
 * gaeguli_pipeline_add_fifo_target_full:
 * @self: a #GaeguliPipeline object
 * @codec: codec to use for streaming
 * @resolution: resolution to use for streaming
 * @framerate: framerate to use for streaming
 * @bitrate: bitrate use for streaming
 * @fifo_path: path to fifo file
 * @error: a #GError
 *
 * Adds a fifo target to the pipeline using specific parameters.
 *
 * Returns: an identifier for the fifo connection known as target_id
 */
guint                   gaeguli_pipeline_add_fifo_target_full
                                                (GaeguliPipeline       *self,
                                                 GaeguliVideoCodec      codec,
                                                 GaeguliVideoResolution resolution,
                                                 guint                  framerate,
                                                 guint                  bitrate,
                                                 const gchar           *fifo_path,
                                                 GError               **error);

/**
 * gaeguli_pipeline_remove_target:
 * @self: a #GaeguliPipeline object
 * @target_id: identifier as returned by #gaeguli_pipeline_add_fifo_target
 * @error: a #GError
 *
 * Removes a specific fifo target.
 *
 * Returns: an #GaeguliReturn
 */
GaeguliReturn           gaeguli_pipeline_remove_target
                                                (GaeguliPipeline       *self,
                                                 guint                  target_id,
                                                 GError               **error);

/**
 * gaeguli_pipeline_stop:
 * @self: a #GaeguliPipeline object
 *
 * Stops the pipeline.
 */
void                    gaeguli_pipeline_stop   (GaeguliPipeline       *self);

/**
 * gaeguli_pipeline_dump_to_dot_file:
 * @self: a #GaeguliPipeline object
 *
 * Dumps debug info to file.
 */
void                    gaeguli_pipeline_dump_to_dot_file
                                                (GaeguliPipeline       *self);

G_END_DECLS

#endif // __GAEGULI_PIPELINE_H__
