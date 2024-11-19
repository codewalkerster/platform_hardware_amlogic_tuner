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

#define LOG_TAG "tunerhal2.0-Frontend"
#include <sys/ioctl.h>
#include <stdlib.h>
#include <utils/Log.h>
#include <errno.h>
#include <string.h>
#include "utils/frontend.h"
#include "analogInterface.h"

int v4l2_set_prop(int fd, const struct dtv_properties *prop)
{

    struct v4l2_properties v4l2_prop;
    struct v4l2_property *property = NULL;
    int i = 0;

    property = (struct v4l2_property *) malloc(prop->num * sizeof(struct v4l2_property));

    if (property == NULL)
    {
        ALOGE("malloc failed, error:%s", strerror(errno));
        return 1;
    }

    memset(&v4l2_prop, 0, sizeof(struct v4l2_properties));

    v4l2_prop.num = prop->num;
    v4l2_prop.props = property;

    for (i = 0; i < prop->num; ++i)
    {
        (v4l2_prop.props + i)->cmd = (prop->props + i)->cmd;
        (v4l2_prop.props + i)->data = (prop->props + i)->u.data;
    }

    ALOGD("V4L2_SET_PROPERTY cmd = 0x%x", prop->props->cmd);

    if (ioctl(fd, V4L2_SET_PROPERTY, &v4l2_prop) == -1)
    {
        ALOGE("ioctl V4L2_SET_PROPERTY failed, error:%s", strerror(errno));
        return 1;
    }

    for (i = 0; i < prop->num; ++i)
    {
        (prop->props + i)->result = (v4l2_prop.props + i)->result;
    }

    if (property != NULL)
    {
        free(property);
    }

    return 0;
}

int v4l2_get_prop(int fd, struct dtv_properties *prop)
{
    struct v4l2_properties v4l2_prop;
    struct v4l2_property *property = NULL;
    int i = 0;

    property = (struct v4l2_property *)malloc(prop->num * sizeof(struct v4l2_property));

    if (property == NULL)
    {
        ALOGE("malloc failed, error:%s", strerror(errno));
        return 1;
    }

    memset(&v4l2_prop, 0, sizeof(struct v4l2_properties));

    v4l2_prop.num = prop->num;
    v4l2_prop.props = property;

    for (i = 0; i < prop->num; ++i)
    {
        (v4l2_prop.props + i)->cmd = (prop->props + i)->cmd;
        (v4l2_prop.props + i)->data = (prop->props + i)->u.data;
    }

    ALOGD("V4L2_GET_PROPERTY cmd = 0x%x", prop->props->cmd);

    if (ioctl(fd, V4L2_GET_PROPERTY, &v4l2_prop) == -1)
    {
        ALOGE("ioctl V4L2_GET_PROPERTY failed, error:%s", strerror(errno));
        free(property);
        return 1;
    }

    for (i = 0; i < prop->num; ++i)
    {
        (prop->props + i)->result = (v4l2_prop.props + i)->result;
        (prop->props + i)->u.data = (v4l2_prop.props + i)->data;
    }

    free(property);
    v4l2_prop.props = NULL;

    return 0;
}

int setAudioOutmode(int fd, int mode) {
    struct dtv_properties props;
    struct dtv_property prop;

    memset(&props, 0, sizeof(props));
    memset(&prop, 0, sizeof(prop));

    prop.cmd = V4L2_SOUND_SYS;
    prop.u.data = mode;

    props.num = 1;
    props.props = &prop;

    if (v4l2_set_prop(fd, &props)  != 0) {
         ALOGE("setAudioOutmode failed, (%s)", strerror(errno));
         return 0;
    }

    ALOGE("%s:mode:%d SUCCESS!", __FUNCTION__, mode);
    return 0;

}

int getAudioOutmode(int fd) {
    int ret = 0;
    struct dtv_properties props;
    struct dtv_property prop;

    memset(&props, 0, sizeof(props));
    memset(&prop, 0, sizeof(prop));

    prop.cmd = V4L2_SOUND_SYS;
    prop.u.data = 0;

    props.num = 1;
    props.props = &prop;

    if (v4l2_get_prop(fd, &props) != 0) {
         ALOGE("getAudioOutmode failed");
         return ret;
    }

    ret = prop.u.data;
    ALOGE("%s:mode:0x%x", __FUNCTION__, ret);
    return ret;
}



