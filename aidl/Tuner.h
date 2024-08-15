/*
 * Copyright 2021 The Android Open Source Project
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

#pragma once

#include <aidl/android/hardware/tv/tuner/BnTuner.h>
#include <aidl/android/hardware/tv/tuner/FrontendCapabilities.h>

#include <map>
#include "Demux.h"
#include "Frontend.h"
#include "Lnb.h"
#include "HwFeState.h"
#include "AmCI.h"

using namespace std;

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

class Frontend;
class Demux;
class Descrambler;
class Lnb;
class HwFeState;

class Tuner : public BnTuner {
  public:
    Tuner();
    virtual ~Tuner();

    ::ndk::ScopedAStatus getFrontendIds(std::vector<int32_t>* _aidl_return) override;
    ::ndk::ScopedAStatus openFrontendById(int32_t in_frontendId,
                                          std::shared_ptr<IFrontend>* _aidl_return) override;
    ::ndk::ScopedAStatus openDemux(std::vector<int32_t>* out_demuxId,
                                   std::shared_ptr<IDemux>* _aidl_return) override;
#if PLATFORM_SDK_VERSION > 33
    ::ndk::ScopedAStatus openDemuxById(int32_t in_demuxId,
                                     std::shared_ptr<IDemux>* _aidl_return) override;
    ::ndk::ScopedAStatus getDemuxInfo(int32_t in_demuxId, DemuxInfo* _aidl_return) override;
    ::ndk::ScopedAStatus getDemuxIds(std::vector<int32_t>* _aidl_return) override;
#endif
    ::ndk::ScopedAStatus getDemuxCaps(DemuxCapabilities* _aidl_return) override;
    ::ndk::ScopedAStatus openDescrambler(std::shared_ptr<IDescrambler>* _aidl_return) override;
    ::ndk::ScopedAStatus getFrontendInfo(int32_t in_frontendId,
                                         FrontendInfo* _aidl_return) override;
    ::ndk::ScopedAStatus getLnbIds(std::vector<int32_t>* _aidl_return) override;
    ::ndk::ScopedAStatus openLnbById(int32_t in_lnbId,
                                     std::shared_ptr<ILnb>* _aidl_return) override;
    ::ndk::ScopedAStatus openLnbByName(const std::string& in_lnbName,
                                       std::vector<int32_t>* out_lnbId,
                                       std::shared_ptr<ILnb>* _aidl_return) override;
    ::ndk::ScopedAStatus setLna(bool in_bEnable) override;
    ::ndk::ScopedAStatus setMaxNumberOfFrontends(FrontendType in_frontendType,
                                                 int32_t in_maxNumber) override;
    ::ndk::ScopedAStatus getMaxNumberOfFrontends(FrontendType in_frontendType,
                                                 int32_t* _aidl_return) override;
#if PLATFORM_SDK_VERSION > 33
    ::ndk::ScopedAStatus isLnaSupported(bool* _aidl_return) override;
#endif
    binder_status_t dump(int fd, const char** args, uint32_t numArgs) override;

    std::shared_ptr<Frontend> getFrontendById(int32_t frontendId);
    void setFrontendAsDemuxSource(int32_t frontendId, int32_t demuxId);
    void frontendStartTune(int32_t frontendId);
    void frontendStopTune(int32_t frontendId);
    void removeDemux(int32_t demuxId);
    void removeFrontend(int32_t frontendId);
    void removeDescrambler(int32_t dscId);
    void init();
    void attachDescramblerToDemux(int32_t dscId, int32_t demuxId);
    void detachDescramblerFromDemux(int32_t dscId, int32_t demuxId);
    std::shared_ptr<Demux> getDemuxById(uint32_t demuxId);

    uint32_t getTsInput(uint32_t frontendId);
    uint32_t getTsInput();
    void setTsnSource();
    void setTsnSourceNoTsClone();
    uint32_t getDscMode();
    vector<FrontendStatusType> getstatusCaps(int32_t frontendId);
    int allocateDemuxResource();
    void removeDemuxResource(int internalDemuxId);
    uint32_t getEncryptPvrSetting();
    void addCiCam(int32_t CamId, sp<AmCI> amCI);
    void removeCiCam(int32_t CamId);
    sp<AmCI> findCiCambyCamId(int32_t CamId);

    typedef struct {
        int id;
        uint32_t minFreq;
        uint32_t maxFreq;
        uint32_t minSymbol;
        uint32_t maxSymbol;
        uint32_t acquireRange;
        uint32_t statusCap;
    } HwFeCaps_t;

    typedef struct {
        int id;
        int hwId;
        std::shared_ptr<Frontend> mFrontend;
        FrontendInfo mInfo;
    } FrontendInfos_t;

  private:
    vector<FrontendInfos_t> mFrontendInfos;
    // Static mFrontends array to maintain local frontends information
    //map<int32_t, std::shared_ptr<Frontend>> mFrontends;
    map<int32_t, int32_t> mFrontendToDemux;
    map<int32_t, std::shared_ptr<Demux>> mDemuxes;  // use demuxId as the key in
                                                    // this sample implementation
    std::map<int32_t, std::shared_ptr<Descrambler>> mDescramblers;
    // To maintain how many Frontends we have
    int mFrontendSize;
#if PLATFORM_SDK_VERSION < 34
    // The last used demux id. Initial value is -1.
    // First used id will be 0.
    int32_t mLastUsedId = -1;
#endif
    vector<std::shared_ptr<Lnb>> mLnbs;
    map<FrontendType, int32_t> mMaxUsableFrontends;
    vector<sp<HwFeState>> mHwFes;
    uint32_t mDscMode = -1;
    uint32_t mFrontendId = -1;
    std::mutex mLock;
    uint32_t mEncryptPvr = -1;

    //Demux resource internal manager
    uint32_t mInternalDemuxId = -1;
    vector<int> mInterDmxIdManager;

    //CI variable
    map<int32_t, sp<AmCI>> mAmCI;
 };

}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
