/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef ANALOG_INTERFACE_
#define ANALOG_INTERFACE_
#include "analog/atv_frontend.h"
int v4l2_set_prop(int fd, const struct dtv_properties *prop);
int v4l2_get_prop(int fd, struct dtv_properties *prop);
int setAudioOutmode(int fd, int mode);
int getAudioOutmode(int fd);
#endif
