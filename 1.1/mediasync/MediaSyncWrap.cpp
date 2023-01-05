/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#define LOG_TAG "MediaSyncWrap"
#include <utils/Log.h>
#include "MediaSyncWrap.h"

MediaSyncWrap::MediaSyncWrap() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    mMediaSync = MediaSync_create();
}

MediaSyncWrap::~MediaSyncWrap() {
    if (mMediaSync != nullptr) {
        ALOGD("%s/%d", __FUNCTION__, __LINE__);
        MediaSync_destroy(mMediaSync);
        mMediaSync = nullptr;
    }
}

int MediaSyncWrap::getAvSyncHwId(int dmxId, int pid) {
    int32_t syncInsId = -1;
    if (mMediaSync != nullptr) {
        MediaSync_allocInstance(mMediaSync, dmxId, pid, &syncInsId);
    }
    return syncInsId;
}

int64_t MediaSyncWrap::getAvSyncTime() {
    int64_t avSyncTime = -1;
    if (mMediaSync != nullptr) {
        MediaSync_getTrackMediaTime(mMediaSync, &avSyncTime);
    }
    return avSyncTime;
}

void MediaSyncWrap::bindAvSyncId(uint32_t avSyncHwId) {
    //Bind is only for set param, not for sync
    if (mMediaSync != nullptr) {
        MediaSync_bindInstance(mMediaSync, avSyncHwId, MEDIA_COMMON);
    }
}

void MediaSyncWrap::destroyMediaSync() {
    if (mMediaSync != nullptr) {
        MediaSync_destroy(mMediaSync);
        mMediaSync = nullptr;
    }
}

void MediaSyncWrap::setParameter(mediasync_parameter type, void* arg) {
    if (mMediaSync != nullptr) {
        mediasync_setParameter(mMediaSync, type, arg);
    } else {
        ALOGD("%s/%d", __FUNCTION__, __LINE__);
    }
}

