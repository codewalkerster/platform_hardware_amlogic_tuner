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

#include <aidl/android/hardware/tv/tuner/IFrontend.h>
#include <aidl/android/hardware/tv/tuner/FrontendType.h>

#define CONFIG_AMLOGIC_DVB_COMPAT
#include "atv_frontend.h"
#include <semaphore.h>
#include <utils/Thread.h>
#include <utils/Errors.h>
#include "utils/frontend.h"
#include "utils/stbtrace.h"

using namespace std;
using ::android::sp;

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

#ifdef CONFIG_AMLOGIC_DVB_COMPAT
#ifndef SYS_ANALOG
#define SYS_ANALOG (SYS_DVBC_ANNEX_C+1)
#endif
#endif

class Frontend;
class HwFeState;

typedef enum {
    FE_SIGNAL_WAIT,
    FE_SIGNAL_LOCKED,
    FE_SIGNAL_TIMEOUT,
}e_signal_status_t;

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
    int  setLna(bool bEnable);
    bool getLna();
    LnbVoltage  getLnbVoltage();
    uint32_t  getSymbolRate();
    uint32_t  getActualTerrHierarchy();
    vector<int32_t> getMPLPIDList();
    vector<atsc3_plp_list_entry_t> getAtsc3MPLPIDList();
    uint32_t getEwbsFlag();
    int32_t getCurrentMPlpId();
    uint32_t getFeSystem();
    virtual int getFrontendSettings(FrontendSettings *settings, void* fe_params) {return -1;};
    virtual int getFeDeliverySystem(FrontendType type) {return SYS_UNDEFINED;};
    void setHwFe(const sp<HwFeState>& hwFe);
    int getFrontendId();
    FrontendType getFeType();
    stbtrace_info mStbTrace_info;
    struct timeval tune_start_time;
    struct timeval tune_end_time;
    struct timeval tune_elapsed_time;
    FrontendSettings* getFeSetting();

    typedef struct {
        uint32_t          id;
        sp<HwFeState>     mHw;
        int               devFd;
        int               deliverySys;
        FrontendType      type;
        FrontendSettings* feSettings;
        uint32_t          blindFreq;
        uint32_t          blindEndFreq;
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

    typedef enum {
        MTS_NONE = 0,
        SET_MTS_MODE,
        GET_MTS_MODE,
    }mts_event_t;

    fe_dev_t* getFeDevice();
    virtual e_signal_status_t getsignalStatus(int fd, uint32_t &locked_freq);
    int getAnalogPara(FrontendAnalogType & at, FrontendAnalogSifStandard & ast);

    const unsigned long V4L2_COLOR_STD_PAL = ((unsigned long)0x04000000);
    const unsigned long V4L2_COLOR_STD_NTSC = ((unsigned long)0x08000000);
    const unsigned long V4L2_COLOR_STD_SECAM = ((unsigned long)0x10000000);
    //const unsigned long V4L2_COLOR_STD_AUTO = ((unsigned long)0x02000000);

private:
    sp<Frontend>     mContext;
    sem_t            threadSemaphore;
    std::mutex       mThreadStatLock;
    std::mutex       mHwDevLock;
    e_event_stat_t   mThreadState;
    fe_dev_t         mDev;
    bool             unsupportSystem;
    bool             mRequestTuningStop;
    int32_t          mPlpId;
    FrontendScanType mScanType = FrontendScanType::SCAN_UNDEFINED;
    FrontendSettings userFeSettings;
    int mMtsEvent = MTS_NONE;

    virtual bool     threadLoop(void);
    virtual status_t readyToRun(void);
    virtual void     onFirstRef(void);

    uint32_t getClockMilliSeconds(void);
    int getThreadState(void);
    void updateThreadState(int state);
    void requestTuneStop(void);
    int getFeProp(struct dtv_properties *prop);

    int setFeSystem();
    int internalTune(const FrontendSettings & settings);
    int interAnalogTune(const FrontendSettings & settings);
    int blindTune(const FrontendSettings& settings);
    timeval tuneStartTime();
    int dvb_wait_event (dvb_frontend_event *evt, int timeout);
    int dvbsx_blindscan_getscanevent(dvbsx_blindscanevent *pbsevent);
    int setDvbsBlindScanParams(bool start);
    void analogMTS(int mode, int value);
    int setAudioOutmode(int mode);
    int getAudioOutmode(void);
    int mtsCallBack(int state);
    int v4l2_set_prop (int fd, const struct dtv_properties *prop);
    int v4l2_get_prop(int fd, struct dtv_properties *prop);
};


}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  //android
}  // namespace aidl

#endif  // ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_DEVICE_H_
