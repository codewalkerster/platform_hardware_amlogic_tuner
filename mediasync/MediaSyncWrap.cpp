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
extern "C"  {
#include "MediaSyncInterface.h"
extern mediasync_result MediaSync_allocInstance(void* handle, int32_t DemuxId,
                                                               int32_t PcrPid,
                                                               int32_t *SyncInsId);
extern mediasync_result MediaSync_getTrackMediaTime(void* handle, int64_t *outMediaUs);
extern mediasync_result MediaSync_bindInstance(void* handle, uint32_t SyncInsId,
                                                             sync_stream_type streamtype);

}

MediaSyncWrap::MediaSyncWrap() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    mMediaSync = MediaSync_create();
}

MediaSyncWrap::~MediaSyncWrap() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    if (mMediaSync != nullptr) {
        MediaSync_destroy(mMediaSync);
    }
}

int MediaSyncWrap::getAvSyncHwId(int dmxId, int pid) {
    int32_t syncInsId = -1;
    MediaSync_allocInstance(mMediaSync, dmxId, pid, &syncInsId);
    return syncInsId;
}

int64_t MediaSyncWrap::getAvSyncTime() {
    int64_t avSyncTime = -1;
    MediaSync_getTrackMediaTime(mMediaSync, &avSyncTime);
    return avSyncTime;
}

void MediaSyncWrap::bindAvSyncId(uint32_t avSyncHwId) {
    //Bind is only for set param, not for sync
    MediaSync_bindInstance(mMediaSync, avSyncHwId, MEDIA_DMXPCR);
}

