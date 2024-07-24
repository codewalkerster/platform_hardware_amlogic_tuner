/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#define LOG_TAG "HwDemuxSCWrap"

#include <dlfcn.h>
#include <utils/Log.h>
#include "HwDemuxSCWrap.h"

typedef void* (*AmHwDemux_Create_func)(int mode, void* arg);
typedef int (*AmHwDemux_Destroy_func)(void* handle);
typedef int (*AmHwDemux_Init_func)(void* handle,int mode,void* arg);
typedef int (*AmHwDemux_ResetStatus_func)(void* handle);
typedef int (*AmHwDemux_GetStreamControlStatus_func)(void* handle,void* arg,int64_t WriteTsSize,int vPid,int aPid);
typedef int (*AmHwDemux_GetMultiStreamControlStatus_func)(void* handle, const StreamControlArgs& args);
typedef int (*AmHwDemux_Flush_func)(void* handle);

static AmHwDemux_Create_func gAmHwDemux_Create = NULL;
static AmHwDemux_Destroy_func gAmHwDemux_Destroy = NULL;
static AmHwDemux_Init_func gAmHwDemux_Init = NULL;
static AmHwDemux_ResetStatus_func gAmHwDemux_ResetStatus = NULL;
static AmHwDemux_GetStreamControlStatus_func gAmHwDemux_GetStreamControlStatus = NULL;
static AmHwDemux_GetMultiStreamControlStatus_func gAmHwDemux_GetMultiStreamControlStatus = NULL;
static AmHwDemux_Flush_func gAmHwDemux_Flush = NULL;

HwDemuxOpsSCWrap::HwDemuxOpsSCWrap() {
    DmxLibInit();
    ALOGD("ctor videotunnel_ops\n");
};

HwDemuxOpsSCWrap::~HwDemuxOpsSCWrap() {
    DmxLibRelease();
    ALOGD("videotunnel_ops leave\n");
};

bool HwDemuxOpsSCWrap::DmxLibInit()
{
    bool err = false;
    if (isInit) {
        ALOGE("videoTunnelLibInit has inited\n");
        return true;
    }

    if (libHandle == NULL) {
        libHandle = dlopen("libmediahal_hardware_demux.so", RTLD_NOW);
        if (libHandle == NULL) {
            ALOGE( "unable to dlopen libmediahal_hardware_demux.so: %s", dlerror());
            return err;
        }
    }

    typedef void* (*creat)(int mode,void* arg);
    gAmHwDemux_Create =
        (creat)dlsym(libHandle, "AmHwDemux_Create");
    if (gAmHwDemux_Create == NULL) {
        ALOGE(" dlsym AmHwDemux_Create failed, err=%s \n", dlerror());
        return err;
    }

    typedef int (*Destroy)(void* handle);
    gAmHwDemux_Destroy =
        (Destroy)dlsym(libHandle, "AmHwDemux_Destroy");
    if (gAmHwDemux_Destroy == NULL) {
        ALOGE(" dlsym AmHwDemux_Destroy failed, err=%s \n", dlerror());
        return err;
    }

    typedef int (*init)(void* handle,int mode,void* arg);
    gAmHwDemux_Init =
        (init)dlsym(libHandle, "AmHwDemux_Init");
    if (gAmHwDemux_Init == NULL) {
        ALOGE("dlsym AmHwDemux_Init failed, err=%s \n", dlerror());
        return err;
    }

    typedef int (*resetStatus)(void* handle);
    gAmHwDemux_ResetStatus =
        (resetStatus)dlsym(libHandle, "AmHwDemux_ResetStatus");
    if (gAmHwDemux_ResetStatus == NULL) {
        ALOGE("dlsym AmHwDemux_ResetStatus failed, err=%s \n", dlerror());
        return err;
    }

    typedef int (*getStreamControlStatus)(void* handle,void* arg,int64_t WriteTsSize,int vPid,int aPid);
    gAmHwDemux_GetStreamControlStatus =
        (getStreamControlStatus)dlsym(libHandle, "AmHwDemux_GetStreamControlStatus");
    if (gAmHwDemux_GetStreamControlStatus == NULL) {
        ALOGE("dlsym AmHwDemux_GetStreamControlStatus failed, err=%s \n", dlerror());
        return err;
    }

    typedef int (*getMultiStreamControlStatus)(void* handle, const StreamControlArgs& args);
    gAmHwDemux_GetMultiStreamControlStatus =
            (getMultiStreamControlStatus)dlsym(libHandle, "AmHwDemux_GetMultiStreamControlStatus");
    if (gAmHwDemux_GetMultiStreamControlStatus == NULL) {
        ALOGE("dlsym AmHwDemux_GetMultiStreamControlStatus failed, err=%s \n", dlerror());
        return err;
    }

    typedef int (*flush)(void *handle);
    gAmHwDemux_Flush =
        (flush)dlsym(libHandle, "AmHwDemux_Flush");
    if (gAmHwDemux_Flush == NULL) {
        ALOGE("dlsym AmHwDemux_Flush failed, error=%s \n", dlerror());
        return err;
    }

    ALOGI( "demxLibInit ok\n");

    isInit = true;
    err = true;
    return err;
}

bool HwDemuxOpsSCWrap::DmxLibRelease()
{
    if (libHandle) {
        dlclose(libHandle);
        libHandle = NULL;
    }

    ALOGI("dmxLibRelease\n");

    return true;
}

void* HwDemuxOpsSCWrap::AmHwDemux_Create(int mode, void* arg) {
    if (gAmHwDemux_Create) {
        return gAmHwDemux_Create(mode, arg);
    } else {
        ALOGE("[%s] gAmHwDemux_Create is NULL \n", __func__);
        return NULL;
    }
}

int HwDemuxOpsSCWrap::AmHwDemux_Destroy(void* handle) {
     if (handle != NULL && gAmHwDemux_Destroy)  {
         return gAmHwDemux_Destroy(handle);
     } else {
        ALOGE("[%s] no handle\n", __func__);
        return -1;
     }
}

int HwDemuxOpsSCWrap::AmHwDemux_Init(void* handle, int mode, void* arg) {
    if (handle != NULL && gAmHwDemux_Init)  {
        return gAmHwDemux_Init(handle, mode, arg);
    } else {
       ALOGE("[%s] no handle\n", __func__);
       return -1;
    }
}

int HwDemuxOpsSCWrap::AmHwDemux_ResetStatus(void* handle) {
    if (handle != NULL && gAmHwDemux_ResetStatus)  {
        return gAmHwDemux_ResetStatus(handle);
    } else {
       ALOGE("[%s] no handle\n", __func__);
       return -1;
    }
}

int HwDemuxOpsSCWrap::AmHwDemux_GetStreamControlStatus(void* handle, void* arg,
        int64_t WriteTsSize, int vPid, int aPid) {
    if (handle != NULL && gAmHwDemux_GetStreamControlStatus)  {
        return gAmHwDemux_GetStreamControlStatus(handle, arg, WriteTsSize, vPid, aPid);
    } else {
       ALOGE("[%s] no handle\n", __func__);
       return -1;
    }
}

int HwDemuxOpsSCWrap::AmHwDemux_GetMultiStreamControlStatus(void* handle, const StreamControlArgs& args) {
    if (handle != NULL && gAmHwDemux_GetMultiStreamControlStatus) {
        return gAmHwDemux_GetMultiStreamControlStatus(handle, args);
    } else {
        ALOGE("[%s] no handle\n", __func__);
        return -1;
    }
}
int HwDemuxOpsSCWrap::AmHwDemux_Flush(void *handle) {
    if (handle != NULL && gAmHwDemux_Flush) {
        return gAmHwDemux_Flush(handle);
    } else {
        ALOGE("[%s] no handle\n", __func__);
        return -1;
    }
}
