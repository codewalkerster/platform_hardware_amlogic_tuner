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

#include <aidl/android/hardware/tv/tuner/BnDescrambler.h>
#include <aidl/android/hardware/tv/tuner/ITuner.h>
#include <inttypes.h>
#include "Tuner.h"
extern "C" {
#include "libdsm.h"
#include "dsc_dev.h"
}

using namespace std;

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

#define TUNER_DSC_ERR(number, format, ...) \
    ALOGE("[No-%d][%s:%d] " format, number, __FUNCTION__, __LINE__, ## __VA_ARGS__)
#define TUNER_DSC_WRAN(number, format, ...) \
    ALOGW("[No-%d][%s:%d] " format, number, __FUNCTION__, __LINE__, ## __VA_ARGS__)
#define TUNER_DSC_INFO(number, format, ...) \
    ALOGI("[No-%d][%s:%d] " format, number, __FUNCTION__, __LINE__, ## __VA_ARGS__)
#define TUNER_DSC_DBG(number, format, ...) \
    ALOGD("[No-%d][%s:%d] " format, number, __FUNCTION__, __LINE__, ## __VA_ARGS__)
#define TUNER_DSC_VERB(number, format, ...) \
    ALOGV("[No-%d][%s:%d] " format, number, __FUNCTION__, __LINE__, ## __VA_ARGS__)
#define TUNER_DSC_TRACE(number) \
    ALOGI("[No-%d][%s:%d] ", number, __FUNCTION__, __LINE__);
#define TUNER_DSC_CHECK_RES(number, expr) do { \
    res = (expr); \
    if (res) { \
        ALOGE("[No-%d][%s:%d] error return 0x%x!\n", \
            number, __FUNCTION__, __LINE__, res); \
        return res; \
    } \
} while(0);

#define TUNERHAL_DSC_TYPE_PROP "vendor.media.tunerhal.dsc_type"
#define MAX_SCRAMBLED_CACHE_SIZE (30 * 1024 * 1024) //30MB

class Tuner;
class Descrambler : public BnDescrambler {
  public:
    Descrambler(int32_t in_dscId,     std::shared_ptr<Tuner> in_tuner);

    ::ndk::ScopedAStatus setDemuxSource(int32_t in_demuxId) override;
    ::ndk::ScopedAStatus setKeyToken(const std::vector<uint8_t>& in_keyToken) override;
    ::ndk::ScopedAStatus addPid(const DemuxPid& in_pid,
                                const std::shared_ptr<IFilter>& in_optionalSourceFilter) override;
    ::ndk::ScopedAStatus removePid(
            const DemuxPid& in_pid,
            const std::shared_ptr<IFilter>& in_optionalSourceFilter) override;
    ::ndk::ScopedAStatus close() override;

  bool isPidSupported(uint16_t in_pid);
  bool isDescramblerReady();
  bool allocDscChannels();
  bool clearDscChannels();
  bool bindDscChannelToKeyTable(uint32_t in_dscDevId, uint32_t in_dscHandle);
  bool allocNskDscChannels();
  bool clearNskDscChannels();
  bool getTsnSourceStatus(bool *out_isLocalMode);

  private:
    virtual ~Descrambler();
    int32_t mSourceDemuxId;
    bool mDemuxSet = false;

    int32_t mDescramblerId;
    std::shared_ptr<Tuner> mTuner;
    std::set<uint16_t> mAddedPid;
    std::mutex mDescrambleLock;
    bool mIsReady = false;
    uint32_t mKeyToken;
    bool mIsLocalMode = false;
    int32_t mDsmFd = -1;
    int32_t mDscType = -1;
    int32_t mDscAlgo = CA_ALGO_UNKNOWN;
    bool mIsEnc = false;
    struct dsm_keyslot_list mKeyslotList;
    std::map<uint16_t, uint32_t> mPidToDscChannel;
    uint32_t mIsNskDsc = 0;
};

}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
