/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef MEDIASYNCWRAP_H
#define MEDIASYNCWRAP_H
#include <utils/RefBase.h>

using namespace android;
extern "C"  {
#include "MediaSyncInterface.h"
extern mediasync_result MediaSync_allocInstance(void* handle, int32_t DemuxId,
                                                               int32_t PcrPid,
                                                               int32_t *SyncInsId);
extern mediasync_result MediaSync_getTrackMediaTime(void* handle, int64_t *outMediaUs);
extern mediasync_result MediaSync_bindInstance(void* handle, uint32_t SyncInsId,
                                                             sync_stream_type streamtype);
extern mediasync_result mediasync_setParameter(void* handle, mediasync_parameter type, void* arg);
extern void MediaSync_destroy(void* handle);
}

class MediaSyncWrap : public RefBase {
public:
    MediaSyncWrap();
    ~MediaSyncWrap();
    int getAvSyncHwId(int dmxId, int pid);
    int64_t getAvSyncTime();
    void bindAvSyncId(uint32_t avSyncHwId);
    void destroyMediaSync();
    void setParameter(mediasync_parameter type, void* arg);

private:
    void* mMediaSync;

};

#endif

