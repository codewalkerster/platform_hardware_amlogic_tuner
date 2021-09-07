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
class MediaSyncWrap : public RefBase {
public:
    MediaSyncWrap();
    ~MediaSyncWrap();
    int getAvSyncHwId(int dmxId, int pid);
    int64_t getAvSyncTime();
    void bindAvSyncId(uint32_t avSyncHwId);

private:
    void* mMediaSync;

};

#endif

