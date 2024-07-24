/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef HWDEMUXSCWRAP_H
#define HWDEMUXSCWRAP_H
#include <utils/RefBase.h>

using namespace android;
extern "C"  {
#include "AmHwDemuxInterface.h"
}
class HwDemuxOpsSCWrap : public RefBase {
public:
    HwDemuxOpsSCWrap();
    ~HwDemuxOpsSCWrap();
    void* AmHwDemux_Create(int mode, void* arg);
    int AmHwDemux_Destroy(void* handle);
    int AmHwDemux_Init(void* handle, int mode, void* arg);
    int AmHwDemux_ResetStatus(void* handle);
    int AmHwDemux_GetStreamControlStatus(void* handle, void* arg, int64_t WriteTsSize, int vPid, int aPid);
    int AmHwDemux_GetMultiStreamControlStatus(void* handle, const StreamControlArgs& args);
    int AmHwDemux_Flush(void* handle);

private:
    bool  isInit{false};
    void* libHandle{NULL};
    //void* handle;
    bool DmxLibInit();
    bool DmxLibRelease();
};

#endif

