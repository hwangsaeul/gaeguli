/**
 *  Copyright 2019-2020 SK Telecom Co., Ltd.
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

#ifndef __GAEGULI_TARGET_H__
#define __GAEGULI_TARGET_H__

#if !defined(__GAEGULI_INSIDE__) && !defined(GAEGULI_COMPILATION)
#error "Only <gaeguli/gaeguli.h> can be included directly."
#endif

#include <gst/gst.h>
#include <gaeguli/types.h>
#include <gaeguli/streamadaptor.h>

typedef struct _GaeguliPipeline GaeguliPipeline;

/**
 * SECTION: target
 * @Title: GaeguliTarget
 * @Short_description: A SRT stream source
 *
 * A #GaeguliTarget represents an encoded video stream available on a defined
 * UDP port for consumption by clients connecting using SRT protocol.
 */

G_BEGIN_DECLS

#define GAEGULI_TYPE_TARGET   (gaeguli_target_get_type ())
G_DECLARE_FINAL_TYPE          (GaeguliTarget, gaeguli_target, GAEGULI, TARGET, GObject)

struct _GaeguliTarget
{
  GObject parent;

  guint id;
  GstElement *pipeline;
};


GaeguliTarget          *gaeguli_target_new           (guint                  id,
                                                      GaeguliVideoCodec      codec,
                                                      guint                  bitrate,
                                                      guint                  idr_period,
                                                      const gchar           *srt_uri,
                                                      const gchar           *username,
                                                      gboolean              is_record_target,
                                                      const gchar           *location,
                                                      guint                 node_id,
                                                      GError               **error);

void                    gaeguli_target_start         (GaeguliTarget        *self,
                                                      GError              **error);

void                    gaeguli_target_stop          (GaeguliTarget        *self);

GaeguliTargetState      gaeguli_target_get_state     (GaeguliTarget        *self);

GaeguliSRTMode          gaeguli_target_get_srt_mode  (GaeguliTarget        *self);

const gchar            *gaeguli_target_get_peer_address
                                                     (GaeguliTarget        *self);

GVariant               *gaeguli_target_get_stats      (GaeguliTarget       *self);

GaeguliStreamAdaptor   *gaeguli_target_get_stream_adaptor
                                                     (GaeguliTarget *self);

G_END_DECLS

#endif // __GAEGULI_TARGET_H__
