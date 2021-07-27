/**
 *  Copyright 2019 SK Telecom Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
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

#ifndef __GAEGULI_TYPES_H__
#define __GAEGULI_TYPES_H__

#if !defined(__GAEGULI_INSIDE__) && !defined(GAEGULI_COMPILATION)
#error "Only <gaeguli/gaeguli.h> can be included directly."
#endif

#include <gmodule.h>

#ifndef _GAEGULI_EXTERN
#define _GAEGULI_EXTERN         extern
#endif

/**
 * SECTION: types
 * @Title: Gaeguli Types
 * @Short_description:  Several types used to export APIs
 */
#define GAEGULI_API_EXPORT     _GAEGULI_EXTERN

typedef enum {
  GAEGULI_RETURN_FAIL = -1,
  GAEGULI_RETURN_OK,
} GaeguliReturn;

typedef enum {
  GAEGULI_SRT_MODE_UNKNOWN = 0,
  GAEGULI_SRT_MODE_CALLER,
  GAEGULI_SRT_MODE_LISTENER,
  GAEGULI_SRT_MODE_RENDEZVOUS,
} GaeguliSRTMode;

typedef enum {
  GAEGULI_VIDEO_SOURCE_UNKNOWN = 0,
  GAEGULI_VIDEO_SOURCE_V4L2SRC,
  GAEGULI_VIDEO_SOURCE_AVFVIDEOSRC,
  GAEGULI_VIDEO_SOURCE_VIDEOTESTSRC,
  GAEGULI_VIDEO_SOURCE_NVARGUSCAMERASRC,
} GaeguliVideoSource;

typedef enum {
  GAEGULI_VIDEO_STREAM_TYPE_UNKNOWN = 0,
  GAEGULI_VIDEO_STREAM_TYPE_MPEG_TS,
  GAEGULI_VIDEO_STREAM_TYPE_MPEG_TS_OVER_SRT = GAEGULI_VIDEO_STREAM_TYPE_MPEG_TS,
  GAEGULI_VIDEO_STREAM_TYPE_RTP,
  GAEGULI_VIDEO_STREAM_TYPE_RTP_OVER_SRT = GAEGULI_VIDEO_STREAM_TYPE_RTP,
} GaeguliVideoStreamType;

typedef enum {
  GAEGULI_VIDEO_CODEC_UNKNOWN = 0,
  GAEGULI_VIDEO_CODEC_H264_X264,
  GAEGULI_VIDEO_CODEC_H264_VAAPI,
  GAEGULI_VIDEO_CODEC_H264_OMX,
  GAEGULI_VIDEO_CODEC_H265_X265,
  GAEGULI_VIDEO_CODEC_H265_VAAPI,
  GAEGULI_VIDEO_CODEC_H265_OMX,
} GaeguliVideoCodec;

typedef enum {
  GAEGULI_VIDEO_BITRATE_CONTROL_CBR = 1,
  GAEGULI_VIDEO_BITRATE_CONTROL_CQP,
  GAEGULI_VIDEO_BITRATE_CONTROL_VBR,
} GaeguliVideoBitrateControl;

typedef enum {
  GAEGULI_VIDEO_RESOLUTION_UNKNOWN = 0,
  GAEGULI_VIDEO_RESOLUTION_640X480,
  GAEGULI_VIDEO_RESOLUTION_1280X720,
  GAEGULI_VIDEO_RESOLUTION_1920X1080,
  GAEGULI_VIDEO_RESOLUTION_3840X2160,
} GaeguliVideoResolution;

typedef enum {
  GAEGULI_SRT_KEY_LENGTH_0 = 0,
  GAEGULI_SRT_KEY_LENGTH_16 = 16,
  GAEGULI_SRT_KEY_LENGTH_24 = 24,
  GAEGULI_SRT_KEY_LENGTH_32 = 32
} GaeguliSRTKeyLength;

typedef enum {
  GAEGULI_TARGET_STATE_NEW,
  GAEGULI_TARGET_STATE_STARTING,
  GAEGULI_TARGET_STATE_RUNNING,
  GAEGULI_TARGET_STATE_STOPPING,
  GAEGULI_TARGET_STATE_STOPPED,
  GAEGULI_TARGET_STATE_ERROR
} GaeguliTargetState;

typedef enum {
  GAEGULI_IDCT_METHOD_ISLOW = 0,
  GAEGULI_IDCT_METHOD_IFAST = 1,
  GAEGULI_IDCT_METHOD_FLOAT = 2
} GaeguliIDCTMethod;

#define GAEGULI_RESOURCE_ERROR          (gaeguli_resource_error_quark ())
GQuark gaeguli_resource_error_quark     (void);

typedef enum {
  GAEGULI_RESOURCE_ERROR_UNSUPPORTED,
  GAEGULI_RESOURCE_ERROR_READ,
  GAEGULI_RESOURCE_ERROR_WRITE,
  GAEGULI_RESOURCE_ERROR_RW,
  GAEGULI_RESOURCE_ERROR_STOPPED,
} GaeguliResourceError;

#define GAEGULI_TRANSMIT_ERROR          (gaeguli_transmit_error_quark ())
GQuark gaeguli_transmit_error_quark     (void);

typedef enum {
  GAEGULI_TRANSMIT_ERROR_FAILED,
  GAEGULI_TRANSMIT_ERROR_ADDRINUSE,
  GAEGULI_TRANSMIT_ERROR_MISMATCHED_CODEC
} GaeguliTransmitError;

#endif // __GAEGULI_TYPES_H__
