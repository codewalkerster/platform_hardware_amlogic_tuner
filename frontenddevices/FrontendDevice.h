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

#ifndef ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_DEVICE_H_
#define ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_DEVICE_H_

#include <android/hardware/tv/tuner/1.0/ITuner.h>

#define CONFIG_AMLOGIC_DVB_COMPAT
#include "linux/dvb/frontend.h"
#include <semaphore.h>
#include <utils/Thread.h>

using namespace std;

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

class Frontend;
class HwFeState;

class FrontendDevice : public Thread {
public:
    FrontendDevice(uint32_t thId, FrontendType type, const sp<Frontend>& context);
    virtual ~FrontendDevice();
    virtual void release();
    bool checkOpen(bool autoOpen);
    virtual void stop();
    virtual void stopByHw();
    virtual void clearTuner();
    virtual int  tune(const FrontendSettings& settings);
    virtual int  scan(const FrontendSettings& settings, FrontendScanType type);
    uint16_t getFeSnr();
    uint32_t getFeBer();
    uint16_t getSignalStrength();
    virtual FrontendModulationStatus getFeModulationStatus();
    virtual int  stopTune();
    virtual int  stopScan();
    virtual int  setLna(bool bEnable);

    virtual int getFrontendSettings(FrontendSettings *settings, void* fe_params) {return -1;};
    virtual int getFeDeliverySystem(FrontendType type) {return SYS_UNDEFINED;};
    void setHwFe(const sp<HwFeState>& hwFe);
    int getFrontendId();

    typedef struct {
        uint32_t          id;
        sp<HwFeState>     mHw;
        int               devFd;
        int               deliverySys;
        FrontendType      type;
        FrontendSettings* feSettings;
        uint32_t          blindFreq;
        uint32_t          tuneFreq;
        bool              islocked;
    }fe_dev_t;

    typedef enum {
        STATE_INITIAL_IDLE,
        STATE_TUNE_START,
        STATE_SCAN_START,
        STATE_TUNE_IDLE,
        STATE_STOP,
        STATE_FINISH,
    }e_event_stat_t;

    typedef enum {
        SUCCESS = 0,
        UNAVAILABLE,
        NOT_INITIALIZED,
        INVALID_STATE,
        INVALID_ARGUMENT,
        OUT_OF_MEMORY,
        UNKNOWN_ERROR,
    }e_return_ret_t;

    fe_dev_t* getFeDevice();

private:
    sp<Frontend>     mContext;
    sem_t            threadSemaphore;
    std::mutex       mThreadStatLock;
    std::mutex       mHwDevLock;
    e_event_stat_t   mThreadState;
    fe_dev_t         mDev;
    bool             unsupportSystem;
    bool             mRequestTunningStop;

    virtual bool     threadLoop(void);
    virtual status_t readyToRun(void);
    virtual void     onFirstRef(void);

    uint32_t getClockMilliSeconds(void);
    int getThreadState(void);
    void updateThreadState(int state);
    void requestTuneStop(void);

    int setFeSystem();
    int internalTune(const FrontendSettings & settings);
    int blindTune(const FrontendSettings& settings);
};


}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_DEVICE_H_
