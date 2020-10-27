/**
 *  Copyright 2020 SK Telecom Co., Ltd.
 *    Author: Jakub Adam <jakub.adam@collabora.com>
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
 */

#include "gaeguli/gaeguli.h"

GstElement       *gaeguli_tests_create_receiver (GaeguliSRTMode mode,
                                                 guint port);

void              gaeguli_tests_receiver_set_handoff_callback
                                                (GstElement *receiver,
                                                 GCallback handoff_callback,
                                                 gpointer data);

void              gaeguli_tests_receiver_set_username
                                                (GstElement *receiver,
                                                 const gchar *username,
                                                 const gchar *resource);

void              gaeguli_tests_receiver_set_passphrase
                                                (GstElement *receiver,
                                                 const gchar *passphrase);
