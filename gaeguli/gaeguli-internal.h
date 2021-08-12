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

#ifndef __GAEGULI_INTERNAL_H__
#define __GAEGULI_INTERNAL_H__

#define GAEGULI_PIPELINE_VSRC_STR       "\
        %s ! videorate ! capsfilter name=caps ! %s ! tee name=tee allow-not-linked=1 "

#define GAEGULI_PIPELINE_IMAGE_STR    "\
        valve name=valve drop=1 ! jpegenc name=jpegenc ! jifmux name=jifmux ! fakesink name=fakesink async=0"

#define GAEGULI_PIPELINE_GENERAL_H264ENC_STR    "\
        queue name=enc_first ! videoconvert ! x264enc name=enc tune=zerolatency key-int-max=%d ! \
        video/x-h264, profile=baseline ! h264parse ! queue "

#define GAEGULI_PIPELINE_GENERAL_H265ENC_STR    "\
        queue name=enc_first ! videoconvert ! x265enc name=enc tune=zerolatency key-int-max=%d ! \
        h265parse ! queue "

#define GAEGULI_PIPELINE_DECODEBIN_STR    "\
        decodebin name=decodebin ! videoconvert ! clockoverlay name=overlay "

#define GAEGULI_PIPELINE_OMXH264ENC_STR     "\
        omxh264enc name=enc insert-sps-pps=true insert-vui=true control-rate=1 periodicity-idr=%d ! queue "

#define GAEGULI_PIPELINE_OMXH265ENC_STR     "\
        omxh265enc name=enc insert-sps-pps=true insert-vui=true control-rate=1 periodicity-idr=%d ! queue "

#define GAEGULI_PIPELINE_NVIDIA_TX1_H264ENC_STR    "\
        queue name=enc_first ! nvvidconv ! video/x-raw(memory:NVMM),format=I420 ! " \
        GAEGULI_PIPELINE_OMXH264ENC_STR

#define GAEGULI_PIPELINE_NVIDIA_TX1_H265ENC_STR    "\
        queue name=enc_first ! nvvidconv ! video/x-raw(memory:NVMM),format=I420 ! " \
        GAEGULI_PIPELINE_OMXH265ENC_STR

#define GAEGULI_PIPELINE_VAAPI_H264_STR    "\
        queue name=enc_first ! videoconvert ! vaapih264enc name=enc keyframe-period=%d ! \
        h264parse ! queue "

#define GAEGULI_PIPELINE_VAAPI_H265_STR    "\
        queue name=enc_first ! videoconvert ! vaapih265enc name=enc keyframe-period=%d ! \
        h265parse ! queue "

#define GAEGULI_PIPELINE_MPEGTSMUX_SINK_STR    "\
        mpegtsmux name=muxsink_first ! tsparse set-timestamps=1 smoothing-latency=1000 ! \
        srtsink name=sink uri=%s wait-for-connection=false"

#define GAEGULI_PIPELINE_RTPMUX_SINK_STR    "\
        rtpmux name=muxsink_first ! queue ! \
        srtsink name=sink uri=%s wait-for-connection=false"

#define GAEGULI_RECORD_PIPELINE_MPEGTSMUX_SINK_STR    "\
        mpegtsmux name=muxsink_first ! tsparse set-timestamps=1 smoothing-latency=1000 ! \
        filesink name=recsink location=%s "

#endif // __GAEGULI_INTERNAL_H__
