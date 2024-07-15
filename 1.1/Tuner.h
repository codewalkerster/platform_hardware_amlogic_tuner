/*
 * Copyright 2020 The Android Open Source Project
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

#ifndef ANDROID_HARDWARE_TV_TUNER_V1_1_TUNER_H_
#define ANDROID_HARDWARE_TV_TUNER_V1_1_TUNER_H_

#include <android/hardware/tv/tuner/1.1/ITuner.h>
#include <map>
#include "Demux.h"
#include "Frontend.h"
#include "Lnb.h"
#include "HwFeState.h"

using namespace std;

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

using ::android::hardware::tv::tuner::V1_1::ITuner;

class Frontend;
class Demux;
class Lnb;
class Descrambler;
class HwFeState;

#define NUMDEMX 6
#define NUMDSC 16
#define NUMRECORD 4
#define NUMPLAYBACK 4
#define NUMTSFILTER 32
#define NUMSECTIONFILTER 32
#define NUMAUDIOFILTER 32
#define NUMVIDEOFILTER 32
#define NUMPESFILTER 32
#define NUMPCRFILTER 32
#define NUMBYTESINSECTIONFILTER 16

class Tuner : public ITuner {
  public:
    Tuner();

    virtual Return<void> getFrontendIds(getFrontendIds_cb _hidl_cb) override;

    virtual Return<void> openFrontendById(uint32_t frontendId,
                                          openFrontendById_cb _hidl_cb) override;

    virtual Return<void> openDemux(openDemux_cb _hidl_cb) override;

    virtual Return<void> getDemuxCaps(getDemuxCaps_cb _hidl_cb) override;

    virtual Return<void> openDescrambler(openDescrambler_cb _hidl_cb) override;

    virtual Return<void> getFrontendInfo(uint32_t frontendId, getFrontendInfo_cb _hidl_cb) override;

    virtual Return<void> getLnbIds(getLnbIds_cb _hidl_cb) override;

    virtual Return<void> openLnbById(uint32_t lnbId, openLnbById_cb _hidl_cb) override;

    virtual Return<void> openLnbByName(const hidl_string& lnbName,
                                       openLnbByName_cb _hidl_cb) override;

    virtual Return<void> getFrontendDtmbCapabilities(
            uint32_t frontendId, getFrontendDtmbCapabilities_cb _hidl_cb) override;

    sp<Frontend> getFrontendById(uint32_t frontendId);

    void setFrontendAsDemuxSource(uint32_t frontendId, uint32_t demuxId);

    void frontendStartTune(uint32_t frontendId);
    void frontendStopTune(uint32_t frontendId);
    void removeDemux(uint32_t demuxId);
    void removeFrontend(uint32_t frontendId);
    void removeDescrambler(uint32_t descramblerId);
    void attachDescramblerToDemux(uint32_t descramblerId, uint32_t demuxId) const;
    void detachDescramblerFromDemux(uint32_t descramblerId, uint32_t demuxId) const;
    sp<Demux> getDemuxById(uint32_t demuxId);

    uint32_t getTsInput();
    void setTsnSource();
    void setTsnSourceNoTsClone();
    uint32_t getDscMode();

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
        sp<Frontend> mFrontend;
        FrontendInfo mInfo;
    } FrontendInfos_t;

  private:
    virtual ~Tuner();
    vector<FrontendInfos_t> mFrontendInfos;
    // Static mFrontends array to maintain local frontends information
    map<uint32_t, sp<Frontend>> mFrontends;
    map<uint32_t, FrontendInfo::FrontendCapabilities> mFrontendCaps;
    map<uint32_t, vector<FrontendStatusType>> mFrontendStatusCaps;
    V1_1::FrontendDtmbCapabilities mDtmbCaps;
    map<uint32_t, uint32_t> mFrontendToDemux;
    map<uint32_t, sp<Demux>> mDemuxes;
    map<uint32_t, sp<Descrambler>> mDescramblers;
    // To maintain how many Frontends we have
    int mFrontendSize = 0;
    // The last used demux id. Initial value is -1.
    // First used id will be 0.
    uint32_t mLastUsedId = -1;
    uint32_t mLastUsedDescramblerId = -1;
    vector<sp<Lnb>> mLnbs;
    vector<sp<HwFeState>> mHwFes;
    uint32_t mTsInput = -1;
    uint32_t mDscMode = -1;
    std::mutex mLock;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_TV_TUNER_V1_1_TUNER_H_
