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

#include <gio/gio.h>
#include <gaeguli/types.h>

typedef struct _GaeguliTarget GaeguliTarget;

/**
 * SECTION: pipeline
 * @Title: GaeguliPipeline
 * @Short_description: Object to read video and send it using SRT
 *
 * A #GaeguliPipeline is an object capable of receiving video from different
 * types of sources and streaming it using SRT protocol.
 */

G_BEGIN_DECLS

#define GAEGULI_TYPE_PIPELINE   (gaeguli_pipeline_get_type ())
G_DECLARE_FINAL_TYPE            (GaeguliPipeline, gaeguli_pipeline, GAEGULI, PIPELINE, GObject)

/**
 * gaeguli_pipeline_new:
 * @attributes: unified parameters

 * Creates a new #GaeguliPipeline object
 *
 * Returns: the newly created object
 */
GaeguliPipeline        *gaeguli_pipeline_new    (GVariant              *attributes);

/**
 * gaeguli_pipeline_new_full:
 * @source: the source of the video
 * @device: the device used as source in case of V4L
 * @resolution: source stream resolution
 * @framerate: source stream frame rate
 *
 * Creates a new #GaeguliPipeline object using specific parameters.
 *
 * Returns: the newly created object
 */
GaeguliPipeline        *gaeguli_pipeline_new_full
                                                (GaeguliVideoSource     source,
                                                 const gchar           *device,
                                                 GaeguliVideoResolution resolution,
                                                 guint                  framerate);
/**
 *
 */
GaeguliTarget          *gaeguli_pipeline_add_target_full
                                                (GaeguliPipeline       *self,
                                                 GVariant              *attributes,
                                                 GError               **error);
 
/**
 * gaeguli_pipeline_add_srt_target:
 * @self: a #GaeguliPipeline object
 * @uri: SRT URI
 * @username: SRT Stream ID User Name identifying this target
 * @error: a #GError
 *
 * Adds a SRT target to the pipeline.
 *
 * Returns: A #GageuliTarget. The object is owned by #GaeguliPipeline.
 * You should g_object_ref() it to keep the reference.
 */
GaeguliTarget          *gaeguli_pipeline_add_srt_target
                                                (GaeguliPipeline       *self,
                                                 const gchar           *uri,
                                                 const gchar           *username,
                                                 GError               **error);

/**
 * gaeguli_pipeline_add_srt_target_full:
 * @self: a #GaeguliPipeline object
 * @codec: codec to use for streaming
 * @stream_type: video transfer method
 * @bitrate: bitrate use for streaming
 * @username: SRT Stream ID User Name identifying this target
 * @uri: SRT URI
 * @error: a #GError
 *
 * Adds a SRT target to the pipeline using specific parameters.
 *
 * Returns: A #GageuliTarget. The object is owned by #GaeguliPipeline.
 * You should g_object_ref() it to keep the reference.
 */
GaeguliTarget          *gaeguli_pipeline_add_srt_target_full
                                                (GaeguliPipeline       *self,
                                                 GaeguliVideoCodec      codec,
                                                 GaeguliVideoStreamType
                                                                        stream_type,
                                                 guint                  bitrate,
                                                 const gchar           *uri,
                                                 const gchar           *username,
                                                 GError               **error);

/**
 * gaeguli_pipeline_add_recording_target:
 * @self: a #GaeguliPipeline object
 * @location: Recording location
 * @error: a #GError
 *
 * Adds a Recording target to the pipeline.
 *
 * Returns: A #GageuliTarget. The object is owned by #GaeguliPipeline.
 * You should g_object_ref() it to keep the reference.
 */
GaeguliTarget          *gaeguli_pipeline_add_recording_target
                                                (GaeguliPipeline       *self,
                                                 const gchar           *location,
                                                 GError               **error);

/**
 * gaeguli_pipeline_add_recording_target_full:
 * @self: a #GaeguliPipeline object
 * @codec: codec to use for streaming
 * @bitrate: bitrate use for streaming
 * @location: Recording location
 * @error: a #GError
 *
 * Adds a Recording target to the pipeline using specific parameters.
 *
 * Returns: A #GageuliTarget. The object is owned by #GaeguliPipeline.
 * You should g_object_ref() it to keep the reference.
 */
GaeguliTarget          *gaeguli_pipeline_add_recording_target_full
                                                (GaeguliPipeline       *self,
                                                 GaeguliVideoCodec      codec,
                                                 guint                  bitrate,
                                                 const gchar           *location,
                                                 GError               **error);

/**
 * gaeguli_pipeline_remove_target:
 * @self: a #GaeguliPipeline object
 * @target: the #GaeguliTarget to remove
 * @error: a #GError
 *
 * Removes a specific SRT target.
 *
 * Returns: an #GaeguliReturn
 */
GaeguliReturn           gaeguli_pipeline_remove_target
                                                (GaeguliPipeline       *self,
                                                 GaeguliTarget         *target,
                                                 GError               **error);

/**
 * gaeguli_pipeline_create_snapshot_async:
 * @self: a #GaeguliPipeline object
 * @tags: a #GVariant of type #G_VARIANT_TYPE_VARDICT with tags to insert
 * into the snapshot in EXIF format.
 * @cancellable: a #GCancellable object
 * @callback: a #GAsyncReadyCallback to call when the request is fulfilled
 * @user_data: arbitrary data passed to @callback
 *
 * Asynchronously saves a single video frame as JPEG image and passes it to
 * @callback.
 */
void                    gaeguli_pipeline_create_snapshot_async
                                                (GaeguliPipeline       *self,
                                                 GVariant              *tags,
                                                 GCancellable          *cancellable,
                                                 GAsyncReadyCallback    callback,
                                                 gpointer               user_data);

/**
 * gaeguli_pipeline_create_snapshot_finish:
 * @self: a #GaeguliPipeline object
 * @result: a #GAsyncResult obtained from the GAsyncReadyCallback passed to gaeguli_pipeline_create_snapshot_async()
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with gaeguli_pipeline_create_snapshot_async().
 *
 * Returns: #GBytes with JPEG image data. On error returns %NULL and sets @error.
 */
GBytes                 *gaeguli_pipeline_create_snapshot_finish
                                                (GaeguliPipeline       *self,
                                                 GAsyncResult          *result,
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
