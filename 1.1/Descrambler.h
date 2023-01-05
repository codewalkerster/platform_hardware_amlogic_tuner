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

#ifndef ANDROID_HARDWARE_TV_TUNER_V1_1_DESCRAMBLER_H_
#define ANDROID_HARDWARE_TV_TUNER_V1_1_DESCRAMBLER_H_

#include <android/hardware/tv/tuner/1.0/IDescrambler.h>
#include <android/hardware/tv/tuner/1.1/ITuner.h>
#include <inttypes.h>
#include "Tuner.h"
extern "C" {
#include "libdsm.h"
#include "dsc_dev.h"
}

using namespace std;

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

#define TUNER_DSC_ERR(number, format, ...) ALOGE("[No-%d][%s:%d] " format, number, __FUNCTION__, __LINE__, ## __VA_ARGS__)
#define TUNER_DSC_WRAN(number, format, ...) ALOGW("[No-%d][%s:%d] " format, number, __FUNCTION__, __LINE__, ## __VA_ARGS__)
#define TUNER_DSC_INFO(number, format, ...) ALOGI("[No-%d][%s:%d] " format, number, __FUNCTION__, __LINE__, ## __VA_ARGS__)
#define TUNER_DSC_DBG(number, format, ...) ALOGD("[No-%d][%s:%d] " format, number, __FUNCTION__, __LINE__, ## __VA_ARGS__)
#define TUNER_DSC_VERB(number, format, ...) ALOGV("[No-%d][%s:%d] " format, number, __FUNCTION__, __LINE__, ## __VA_ARGS__)
#define TUNER_DSC_TRACE(number) ALOGI("[No-%d][%s:%d] ", number, __FUNCTION__, __LINE__);

#define TUNER_DSC_CHECK_RES(number, expr) do { \
        res = (expr); \
        if (res) { \
            ALOGE("[No-%d][%s:%d] error return 0x%x!\n", \
                number, __FUNCTION__, __LINE__, res); \
            return res; \
        } \
    } while(0);

#define TUNERHAL_DSC_TYPE_PROP "vendor.media.tunerhal.dsc_type"
#define MAX_SCRAMBLED_CACHE_SIZE 30 * 1024 * 1024

class Tuner;

class Descrambler : public IDescrambler {
  public:
    Descrambler(uint32_t descramblerId, sp<Tuner> tuner);

    virtual Return<Result> setDemuxSource(uint32_t demuxId) override;

    virtual Return<Result> setKeyToken(const hidl_vec<uint8_t>& keyToken) override;

    virtual Return<Result> addPid(const DemuxPid& pid,
                                  const sp<IFilter>& optionalSourceFilter) override;

    virtual Return<Result> removePid(const DemuxPid& pid,
                                     const sp<IFilter>& optionalSourceFilter) override;

    virtual Return<Result> close() override;
    bool isPidSupported(uint16_t pid);
    bool isDescramblerReady();
    bool allocDscChannels();
    bool clearDscChannels();
    bool bindDscChannelToKeyTable(uint32_t dsc_dev_id, uint32_t dsc_handle);
    bool allocNskDscChannels();
    bool clearNskDscChannels();
    bool getTsnSourceStatus(bool *enableLocalMode);

  private:
    virtual ~Descrambler();
    uint32_t mDescramblerId;
    sp<Tuner> mTunerService;
    // Transport stream pid only.
    std::set<uint16_t> added_pid;
    uint32_t mSourceDemuxId;
    bool mDemuxSet = false;
    std::mutex mDescrambleLock;
    bool mIsReady = false;
    uint32_t mCasSessionToken;
    bool mEnableLocalMode;

    int mDsmFd = -1;
    uint32_t mDscType = -1;
    uint32_t mDscAlgo = CA_ALGO_UNKNOWN;
    bool mIsEnc = false;
    struct dsm_keyslot_list mKeyslotList;
    std::map<uint16_t, uint32_t> es_pid_to_dsc_channel;
    uint32_t mIsNskDsc = 0;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_TV_TUNER_V1_DESCRAMBLER_H_
