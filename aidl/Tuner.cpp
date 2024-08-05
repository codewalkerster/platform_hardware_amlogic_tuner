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

//#define LOG_NDEBUG 0
#define LOG_TAG "tunerhal2.0-Tuner"
#include "Tuner.h"
#include <aidl/android/hardware/tv/tuner/DemuxFilterMainType.h>
#include <aidl/android/hardware/tv/tuner/Result.h>
#include <utils/Log.h>
#include <sys/stat.h>
#include <algorithm>
#include "Demux.h"
#include "Descrambler.h"
#include "Frontend.h"
#include "Lnb.h"
#include <json/json.h>
#include "FileSystemIo.h"

#include "dsc_dev.h"

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

#define NUMDEMUX 4
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

#define TUNER_CONFIG_FILE "/vendor/etc/tuner_hal/frontendinfos.json"
#define FRONTEND_DEVICE "/dev/dvb0.frontend0"
#define TS_CLONE "/sys/class/dmx/ts_clone"

//check device exist or not
static bool isDeviceExist(const char *file_name)
{
    struct stat tmp_st;
    return stat(file_name, &tmp_st) == 0;
}

Tuner::Tuner() {}

void Tuner::init() {
    if (!isDeviceExist(FRONTEND_DEVICE)) {
        mFrontendSize = 0;
        ALOGD("frontend device is not exist");
    } else {
        ALOGD("frontend device is exist");
        const char* tuner_config_file = TUNER_CONFIG_FILE;
        FILE* fp = fopen(tuner_config_file, "r");
        if (fp != NULL) {
            fseek(fp, 0L, SEEK_END);
            const auto len = ftell(fp);
            char* data = (char*)malloc(len + 1);

            rewind(fp);
            //fread(data, sizeof(char), len, fp);
            int rc = fread(data, sizeof(char), len, fp);
            if (rc == 0) {
                ALOGD("fread fd failed!");
            }
            data[len] = '\0';

            Json::Value root;
            Json::Reader reader;

            if (reader.parse(data, root)) {
                auto& arrayHwFes = root["hwfe"];
                auto& arrayFronts = root["frontends"];
                auto& dvrSetting = root["dvrsetting"];
                for (int i = 0; i < arrayHwFes.size(); i ++) {
                    if (!arrayHwFes[i]["id"].isNull()) {
                        int hwId = arrayHwFes[i]["id"].asInt();
                        int tsInput = arrayHwFes[i]["ts_input"].asInt();
                        sp<HwFeState> hwFeState = new HwFeState(hwId, tsInput);
                        mHwFes.push_back(hwFeState);
                    }
                }
                for (int i = 0; i < arrayFronts.size(); i ++) {
                    if (!arrayFronts[i]["type"].isNull()) {
                        int frontType = arrayFronts[i]["type"].asInt();
                        int id = i;
                        int hwId = arrayFronts[i]["hwid"].asInt();
                        HwFeCaps_t hwCaps;
                        hwCaps.statusCap = 0;
                        hwCaps.id = hwId;
                        if (hwId >= 0 && hwId < arrayHwFes.size()) {
                            hwCaps.minFreq = arrayHwFes[hwId]["minFreq"].asUInt();
                            hwCaps.maxFreq = arrayHwFes[hwId]["maxFreq"].asUInt();
                            hwCaps.minSymbol = arrayHwFes[hwId]["minSymbol"].asUInt();
                            hwCaps.maxSymbol = arrayHwFes[hwId]["maxSymbol"].asUInt();
                            hwCaps.acquireRange = arrayHwFes[hwId]["acquireRange"].asUInt();
                            hwCaps.statusCap = arrayHwFes[hwId]["statusCap"].asUInt();
                        }
                        vector<FrontendStatusType> statusCaps;
                        /*for (int s = 0; s < static_cast<int>(FrontendStatusType::ATSC3_PLP_INFO); s ++) {
                            if ((hwCaps.statusCap & (1 << s)) == (1 << s)) {
                                statusCaps.push_back(static_cast<FrontendStatusType>(s));
                            }
                        }*/
                        FrontendInfo info;
                        FrontendCapabilities caps = FrontendCapabilities();
                        switch (frontType)
                        {
                            case static_cast<int>(FrontendType::ANALOG):{
                                FrontendAnalogCapabilities analogCaps {
                                    .typeCap = arrayFronts[i]["analogTypeCap"].asInt(),
                                    .sifStandardCap = arrayFronts[i]["sifCap"].asInt(),
                                };
                                caps.set<FrontendCapabilities::Tag::analogCaps> (analogCaps);
                                mMaxUsableFrontends[FrontendType::ANALOG] = 1;
                                statusCaps = {
                                    FrontendStatusType::LAYER_ERROR,
                                    FrontendStatusType::MER,
                                    FrontendStatusType::UEC,
                                    FrontendStatusType::TS_DATA_RATES,
                                };
                            }
                            break;
                            case static_cast<int>(FrontendType::ATSC): {
                                FrontendAtscCapabilities atscCaps {
                                    .modulationCap = arrayFronts[i]["modulationCap"].asInt(),
                                };
                                caps.set<FrontendCapabilities::Tag::atscCaps>(atscCaps);
                                mMaxUsableFrontends[FrontendType::ATSC] = 1;
                                statusCaps = {
                                    FrontendStatusType::FREQ_OFFSET,
                                    FrontendStatusType::RF_LOCK,
                                    FrontendStatusType::MODULATIONS,
                                    FrontendStatusType::IS_LINEAR,
                                };
                            }
                            break;
                            case static_cast<int>(FrontendType::ATSC3): {
                                FrontendAtsc3Capabilities atsc3Caps{
                                    .modulationCap = arrayFronts[i]["modulationCap"].asInt(),
                                    .bandwidthCap = arrayFronts[i]["bandwidthCap"].asInt(),
                                    .timeInterleaveModeCap = arrayFronts[i]["timeInterleaveModeCap"].asInt(),
                                    .codeRateCap = arrayFronts[i]["codeRateCap"].asInt(),
                                    .fecCap = arrayFronts[i]["fecCap"].asInt(),
                                    .demodOutputFormatCap = static_cast<int8_t>(arrayFronts[i]["demodOutputFormatCap"].asInt()),
                                };
                                caps.set<FrontendCapabilities::Tag::atsc3Caps>(atsc3Caps);
                                mMaxUsableFrontends[FrontendType::ATSC3] = 1;
                                statusCaps = {
                                    FrontendStatusType::BER,
                                    FrontendStatusType::PER,
                                    FrontendStatusType::ATSC3_PLP_INFO,
                                    FrontendStatusType::MODULATIONS,
                                    FrontendStatusType::BERS,
                                    FrontendStatusType::INTERLEAVINGS,
                                    FrontendStatusType::BANDWIDTH,
                                    FrontendStatusType::ATSC3_ALL_PLP_INFO,
                                };
                            }
                            break;
                            case static_cast<int>(FrontendType::DVBC): {
                                FrontendDvbcCapabilities dvbcCaps {
                                    .modulationCap = arrayFronts[i]["modulationCap"].asInt(),
                                    .fecCap = arrayFronts[i]["fecCap"].asInt64(),
                                    .annexCap =  static_cast<int8_t>(arrayFronts[i]["annexCap"].asInt()),
                                };
                                caps.set<FrontendCapabilities::Tag::dvbcCaps>(dvbcCaps);
                                mMaxUsableFrontends[FrontendType::DVBC] = 1;
                                statusCaps = {
                                    FrontendStatusType::PRE_BER,
                                    FrontendStatusType::SIGNAL_QUALITY,
                                    FrontendStatusType::MODULATION,
                                    FrontendStatusType::SPECTRAL,
                                    FrontendStatusType::MODULATIONS,
                                    FrontendStatusType::CODERATES,
                                    FrontendStatusType::INTERLEAVINGS,
                                    FrontendStatusType::BANDWIDTH,
                                };
                            }
                            break;
                            case static_cast<int>(FrontendType::DVBS): {
                                FrontendDvbsCapabilities dvbsCaps {
                                    .modulationCap = arrayFronts[i]["modulationCap"].asInt(),
                                    .innerfecCap = arrayFronts[i]["fecCap"].asUInt(),
                                    .standard =  static_cast<int8_t>(arrayFronts[i]["stdCap"].asInt()),
                                };
                                caps.set<FrontendCapabilities::Tag::dvbsCaps>(dvbsCaps);
                                mMaxUsableFrontends[FrontendType::DVBS] = 1;
                                statusCaps = {
                                    FrontendStatusType::SIGNAL_STRENGTH,
                                    FrontendStatusType::SYMBOL_RATE,
                                    FrontendStatusType::MODULATION,
                                    FrontendStatusType::MODULATIONS,
                                    FrontendStatusType::ROLL_OFF,
                                    FrontendStatusType::IS_MISO,
                               };
                            }
                            break;
                            case static_cast<int>(FrontendType::DVBT): {
                                FrontendDvbtCapabilities dvbtCaps {
                                    .transmissionModeCap = arrayFronts[i]["transmissionCap"].asInt(),
                                    .bandwidthCap = arrayFronts[i]["bandwidthCap"].asInt(),
                                    .constellationCap = arrayFronts[i]["constellationCap"].asInt(),
                                    .coderateCap = arrayFronts[i]["coderateCap"].asInt(),
                                    .hierarchyCap = arrayFronts[i]["hierarchyCap"].asInt(),
                                    .guardIntervalCap = arrayFronts[i]["guardIntervalCap"].asInt(),
                                    .isT2Supported = arrayFronts[i]["supportT2"].asBool(),
                                    .isMisoSupported = arrayFronts[i]["constellationCap"].asBool(),
                                };
                                caps.set<FrontendCapabilities::Tag::dvbtCaps>(dvbtCaps);
                                mMaxUsableFrontends[FrontendType::DVBT] = 1;
                                statusCaps = {
                                    FrontendStatusType::EWBS,
                                    FrontendStatusType::PLP_ID,
                                    FrontendStatusType::HIERARCHY,
                                    FrontendStatusType::MODULATIONS,
                                    FrontendStatusType::BANDWIDTH,
                                    FrontendStatusType::GUARD_INTERVAL,
                                    FrontendStatusType::TRANSMISSION_MODE,
                                    FrontendStatusType::T2_SYSTEM_ID,
                                    FrontendStatusType::DVBT_CELL_IDS,
                                };
                            }
                            break;
                            case static_cast<int>(FrontendType::ISDBT): {
                                FrontendIsdbtCapabilities isdbtCaps {
                                    .modeCap = arrayFronts[i]["modeCap"].asInt(),
                                    .bandwidthCap = arrayFronts[i]["bandwidthCap"].asInt(),
                                    .modulationCap = arrayFronts[i]["modulationCap"].asInt(),
                                    .coderateCap = arrayFronts[i]["coderateCap"].asInt(),
                                    .guardIntervalCap = arrayFronts[i]["guardIntervalCap"].asInt(),
                                };
                                caps.set<FrontendCapabilities::Tag::isdbtCaps>(isdbtCaps);
                                mMaxUsableFrontends[FrontendType::ISDBT] = 1;
                                statusCaps = {
                                    FrontendStatusType::AGC,
                                    FrontendStatusType::LNA,
                                    FrontendStatusType::MODULATION,
                                    FrontendStatusType::MODULATIONS,
                                    FrontendStatusType::BANDWIDTH,
                                    FrontendStatusType::GUARD_INTERVAL,
                                    FrontendStatusType::TRANSMISSION_MODE,
                                    FrontendStatusType::ISDBT_SEGMENTS,
                                };
                            }
                            break;
                            case static_cast<int>(FrontendType::DTMB): {
                                FrontendDtmbCapabilities dtmbCaps {
                                    .transmissionModeCap = arrayFronts[i]["transmissionCap"].asInt(),
                                    .bandwidthCap = arrayFronts[i]["bandwidthCap"].asInt(),
                                    .modulationCap = arrayFronts[i]["modulationCap"].asInt(),
                                    .codeRateCap = arrayFronts[i]["coderateCap"].asInt(),
                                    .guardIntervalCap = arrayFronts[i]["guardIntervalCap"].asInt(),
                                    .interleaveModeCap = arrayFronts[i]["interleaveModeCap"].asInt(),
                                };
                                caps.set<FrontendCapabilities::Tag::dtmbCaps>(dtmbCaps);
                                mMaxUsableFrontends[FrontendType::DTMB] = 1;
                                statusCaps = {
                                    FrontendStatusType::MODULATIONS,
                                    FrontendStatusType::INTERLEAVINGS,
                                    FrontendStatusType::BANDWIDTH,
                                    FrontendStatusType::GUARD_INTERVAL,
                                    FrontendStatusType::TRANSMISSION_MODE,
                                };
                            }
                            break;
                            default:
                                break;
                        }
                        uint32_t minFreq, maxFreq, minSymbol, maxSymbol, exclusiveId;
                        if (!arrayFronts[i]["minFreq"].isNull()) {
                            minFreq = arrayFronts[i]["minFreq"].asUInt();
                        } else {
                            minFreq = hwCaps.minFreq;
                        }
                        if (!arrayFronts[i]["maxFreq"].isNull()) {
                            maxFreq = arrayFronts[i]["maxFreq"].asUInt();
                        } else {
                            maxFreq = hwCaps.maxFreq;
                        }
                        if (!arrayFronts[i]["minSymbol"].isNull()) {
                            minSymbol = arrayFronts[i]["minSymbol"].asUInt();
                        } else {
                            minSymbol = hwCaps.minSymbol;
                        }
                        if (!arrayFronts[i]["maxSymbol"].isNull()) {
                            maxSymbol = arrayFronts[i]["maxSymbol"].asUInt();
                        } else {
                            maxSymbol = hwCaps.maxSymbol;
                        }
                        if (!arrayFronts[i]["exclusiveGroupId"].isNull()) {
                            exclusiveId = arrayFronts[i]["exclusiveId"].asUInt();
                        } else {
                            exclusiveId = (uint32_t)(hwCaps.id);
                        }
                        info = {
                            .type = static_cast<FrontendType>(frontType),
                            .minFrequency = minFreq,
                            .maxFrequency = maxFreq,
                            .minSymbolRate = static_cast<int32_t>(minSymbol),
                            .maxSymbolRate = static_cast<int32_t>(maxSymbol),
                            .acquireRange = hwCaps.acquireRange,
                            .exclusiveGroupId = static_cast<int32_t>(exclusiveId),
                            .statusCaps = statusCaps,
                            .frontendCaps = caps,
                        };
                        ALOGD("Add frontend type(%d), id(%d), hwId(%d),exclusiveGroupId(%u)",
                            frontType, id, hwCaps.id, info.exclusiveGroupId);
                        FrontendInfos_t fes = {id, hwCaps.id, nullptr, info};
                        mFrontendInfos.push_back(fes);
                        mFrontendSize ++;
                    }
                }

                if (!dvrSetting["encrypt_pvr"].isNull()) {
                    mEncryptPvr = dvrSetting["encrypt_pvr"].asInt();
                    ALOGD("encrypt_pvr = %d", mEncryptPvr);
                }

            }
            mLnbs.resize(1);
            if (mHwFes.size() > 0) {
                mLnbs[0] = ndk::SharedRefBase::make<Lnb>(0, mHwFes[0], "hardware_lnb");
            } else {
                mLnbs[0] = ndk::SharedRefBase::make<Lnb>(0, nullptr, "virtual_lnb");
            }
            root.clear();
            if (data)
                free(data);
            fclose(fp);
            fp = NULL;
        } else {
            mFrontendSize = 0;
        }
    }

#if PLATFORM_SDK_VERSION > 33
    for (int i = 0; i < NUMDEMUX; i++) {
        mDemuxes[i] = ndk::SharedRefBase::make<Demux>(i, static_cast<int32_t>(DemuxFilterMainType::TS));
    }
#endif

    setTsnSource();
    ca_init();
}

/*
void Tuner::init() {
    // Static Frontends array to maintain local frontends information
    // Array index matches their FrontendId in the default impl
    mFrontendSize = 10;
    mFrontends[0] = ndk::SharedRefBase::make<Frontend>(FrontendType::ISDBS, 0, this->ref<Tuner>());
    mFrontends[1] = ndk::SharedRefBase::make<Frontend>(FrontendType::ATSC3, 1, this->ref<Tuner>());
    mFrontends[2] = ndk::SharedRefBase::make<Frontend>(FrontendType::DVBC, 2, this->ref<Tuner>());
    mFrontends[3] = ndk::SharedRefBase::make<Frontend>(FrontendType::DVBS, 3, this->ref<Tuner>());
    mFrontends[4] = ndk::SharedRefBase::make<Frontend>(FrontendType::DVBT, 4, this->ref<Tuner>());
    mFrontends[5] = ndk::SharedRefBase::make<Frontend>(FrontendType::ISDBT, 5, this->ref<Tuner>());
    mFrontends[6] = ndk::SharedRefBase::make<Frontend>(FrontendType::ANALOG, 6, this->ref<Tuner>());
    mFrontends[7] = ndk::SharedRefBase::make<Frontend>(FrontendType::ATSC, 7, this->ref<Tuner>());
    mFrontends[8] = ndk::SharedRefBase::make<Frontend>(FrontendType::ISDBS3, 8, this->ref<Tuner>());
    mFrontends[9] = ndk::SharedRefBase::make<Frontend>(FrontendType::DTMB, 9, this->ref<Tuner>());

    mMaxUsableFrontends[FrontendType::ISDBS] = 1;
    mMaxUsableFrontends[FrontendType::ATSC3] = 1;
    mMaxUsableFrontends[FrontendType::DVBC] = 1;
    mMaxUsableFrontends[FrontendType::DVBS] = 1;
    mMaxUsableFrontends[FrontendType::DVBT] = 1;
    mMaxUsableFrontends[FrontendType::ISDBT] = 1;
    mMaxUsableFrontends[FrontendType::ANALOG] = 1;
    mMaxUsableFrontends[FrontendType::ATSC] = 1;
    mMaxUsableFrontends[FrontendType::ISDBS3] = 1;
    mMaxUsableFrontends[FrontendType::DTMB] = 1;

    mLnbs.resize(2);
    mLnbs[0] = ndk::SharedRefBase::make<Lnb>(0);
    mLnbs[1] = ndk::SharedRefBase::make<Lnb>(1);
}*/

Tuner::~Tuner() {}

::ndk::ScopedAStatus Tuner::getFrontendIds(std::vector<int32_t>* _aidl_return) {
    ALOGV("%s", __FUNCTION__);
    if (mFrontendSize > 0) {
        _aidl_return->resize(mFrontendSize);
        for (int i = 0; i < mFrontendSize; i++) {
            (*_aidl_return)[i] = mFrontendInfos[i].id;//mFrontends[i]->getFrontendId();
        }
    }
    return ::ndk::ScopedAStatus::ok();
}

#if PLATFORM_SDK_VERSION > 33
::ndk::ScopedAStatus Tuner::getDemuxInfo(int32_t in_demuxId, DemuxInfo* _aidl_return) {
    if (mDemuxes.find(in_demuxId) == mDemuxes.end()) {
         return ::ndk::ScopedAStatus::fromServiceSpecificError(
                 static_cast<int32_t>(Result::INVALID_ARGUMENT));
    } else {
         mDemuxes[in_demuxId]->getDemuxInfo(_aidl_return);
          return ::ndk::ScopedAStatus::ok();
    }
}

::ndk::ScopedAStatus Tuner::getDemuxIds(std::vector<int32_t>* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    int numOfDemuxes = mDemuxes.size();
    _aidl_return->resize(numOfDemuxes);
    int i = 0;
    for (auto e = mDemuxes.begin(); e != mDemuxes.end(); e++) {
        (*_aidl_return)[i++] = e->first;
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::openDemuxById(int32_t in_demuxId,
                                    std::shared_ptr<IDemux>* _aidl_return) {
    ALOGD("%s in_demuxId = %d", __FUNCTION__, in_demuxId);

    if (mDemuxes.find(in_demuxId) == mDemuxes.end()) {
        ALOGW("[   WARN   ] Demux with id %d isn't available", in_demuxId);
        *_aidl_return = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }

    if (mDemuxes[in_demuxId]->isInUse()) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Result::UNAVAILABLE));
    } else {
        mDemuxes[in_demuxId]->setTunerService(this->ref<Tuner>());
        mDemuxes[in_demuxId]->setInUse(true);

        *_aidl_return = mDemuxes[in_demuxId];
    }
    return ::ndk::ScopedAStatus::ok();
}
#endif

::ndk::ScopedAStatus Tuner::openFrontendById(int32_t in_frontendId,
                                             std::shared_ptr<IFrontend>* _aidl_return) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

    if (in_frontendId >= mFrontendSize || in_frontendId < 0) {
        ALOGW("[   WARN   ] Frontend with id %d isn't available", in_frontendId);
        *_aidl_return = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }
    std::shared_ptr<Frontend> frontend;
    if (mFrontendInfos[in_frontendId].mFrontend == nullptr) {
        frontend = ndk::SharedRefBase::make<Frontend>(mFrontendInfos[in_frontendId].mInfo.type, mFrontendInfos[in_frontendId].id,
            this->ref<Tuner>(), mHwFes[mFrontendInfos[in_frontendId].hwId]);
        mFrontendInfos[in_frontendId].mFrontend = frontend;
    } else {
        frontend = mFrontendInfos[in_frontendId].mFrontend;
    }
    mFrontendId = in_frontendId;
    ALOGD("%s/%d in_frontendId = %d", __FUNCTION__, __LINE__, in_frontendId);
    *_aidl_return = frontend;//mFrontends[in_frontendId];
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::openDemux(std::vector<int32_t>* out_demuxId,
                                      std::shared_ptr<IDemux>* _aidl_return) {
#if PLATFORM_SDK_VERSION > 33
    ALOGD("%s", __FUNCTION__);
    bool found = false;
    int32_t demuxId = 0;
    for (auto e = mDemuxes.begin(); e != mDemuxes.end(); e++) {
        if (!e->second->isInUse()) {
            found = true;
            demuxId = e->second->getDemuxId();
        }
    }

    if (found) {
        out_demuxId->push_back(demuxId);
        return openDemuxById(demuxId, _aidl_return);
    } else {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNAVAILABLE));
    }
#else
    ALOGD("%s/%d mDemuxes size = %d", __FUNCTION__, __LINE__, mDemuxes.size());
     std::lock_guard<std::mutex> lock(mLock);
     mLastUsedId = 0;
     std::map<int32_t, std::shared_ptr<Demux>>::iterator it;
     it = mDemuxes.find(mLastUsedId);
     while (it != mDemuxes.end()) {
         mLastUsedId++;
         ALOGD("mLastUsedId = %d", mLastUsedId);
         it = mDemuxes.find(mLastUsedId);
     }

     if (mLastUsedId == NUMDEMUX)
         mLastUsedId = 1;

     //DemuxId demuxId = mLastUsedId;
     mDemuxes[mLastUsedId] = ndk::SharedRefBase::make<Demux>(mLastUsedId, this->ref<Tuner>());
     out_demuxId->push_back(mLastUsedId);
     *_aidl_return = mDemuxes[mLastUsedId];

     return ::ndk::ScopedAStatus::ok();
#endif
}

::ndk::ScopedAStatus Tuner::getDemuxCaps(DemuxCapabilities* _aidl_return) {
    ALOGV("%s", __FUNCTION__);
    _aidl_return->numDemux                = NUMDEMUX;
    _aidl_return->numRecord               = NUMRECORD;
    _aidl_return->numPlayback             = NUMPLAYBACK;
    _aidl_return->numTsFilter             = NUMTSFILTER;
    _aidl_return->numSectionFilter        = NUMSECTIONFILTER;
    _aidl_return->numAudioFilter          = NUMAUDIOFILTER;
    _aidl_return->numVideoFilter          = NUMVIDEOFILTER;
    _aidl_return->numPesFilter            = NUMPESFILTER;
    _aidl_return->numPcrFilter            = NUMPCRFILTER;
    _aidl_return->numBytesInSectionFilter = NUMBYTESINSECTIONFILTER;
    // IP filter can be an MMTP filter's data source.
    _aidl_return->linkCaps = {0x00, 0x00, 0x02, 0x00, 0x00};
    // Support time filter testing
    _aidl_return->bTimeFilter = true;

#if PLATFORM_SDK_VERSION > 33
    // set filterCaps as the bitwize OR of all the demux' caps
    std::vector<int32_t> demuxIds;
    getDemuxIds(&demuxIds);
    int32_t filterCaps = 0;

    for (int i = 0; i < demuxIds.size(); i++) {
        DemuxInfo demuxInfo;
        getDemuxInfo(demuxIds[i], &demuxInfo);
        filterCaps |= demuxInfo.filterTypes;
    }
    _aidl_return->filterCaps = filterCaps;
#endif
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::openDescrambler(std::shared_ptr<IDescrambler>* _aidl_return) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);
    int32_t dscId = 0;
    std::map<int32_t, std::shared_ptr<Descrambler>>::iterator it;

    std::lock_guard<std::mutex> lock(mLock);

    it = mDescramblers.find(dscId);
    while (it != mDescramblers.end()) {
        dscId++;
        if (dscId == NUMDSC) {
            ALOGE("%s/%d Too many descramblers have been opened!", __FUNCTION__, __LINE__);
            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_STATE));
        }
        it = mDescramblers.find(dscId);
    }

    mDescramblers[dscId] = ndk::SharedRefBase::make<Descrambler>(dscId, this->ref<Tuner>());
    *_aidl_return = mDescramblers[dscId];
    ALOGD("%s/%d dscId:%d dscNum:%zd", __FUNCTION__, __LINE__, dscId, mDescramblers.size());

    return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::getFrontendInfo(int32_t in_frontendId, FrontendInfo* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    if (in_frontendId < 0 || in_frontendId >= mFrontendSize) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }
    * _aidl_return = mFrontendInfos[in_frontendId].mInfo;
    //mFrontends[in_frontendId]->getFrontendInfo(_aidl_return);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::getLnbIds(std::vector<int32_t>* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    _aidl_return->resize(mLnbs.size());
    for (int i = 0; i < mLnbs.size(); i++) {
        (*_aidl_return)[i] = mLnbs[i]->getId();
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::openLnbById(int32_t in_lnbId, std::shared_ptr<ILnb>* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    if (in_lnbId >= mLnbs.size()) {
        *_aidl_return = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }

    *_aidl_return = mLnbs[in_lnbId];
    return ::ndk::ScopedAStatus::ok();
}

std::shared_ptr<Frontend> Tuner::getFrontendById(int32_t frontendId) {
    ALOGV("%s", __FUNCTION__);

    return mFrontendInfos[frontendId].mFrontend;;
}

::ndk::ScopedAStatus Tuner::openLnbByName(const std::string& in_lnbName,
                                          std::vector<int32_t>* out_lnbId,
                                          std::shared_ptr<ILnb>* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    //out_lnbId->push_back(1234);
    int id = mLnbs.size();
    out_lnbId->push_back(id);
    if (id <= 0 || mLnbs.empty() || mHwFes.size() == 0) {
        *_aidl_return = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    } else {
        ALOGD("%s/%d new lnb success!", __FUNCTION__, __LINE__);
        mLnbs[id] = ndk::SharedRefBase::make<Lnb>(id, mHwFes[0], in_lnbName.c_str());
        *_aidl_return = mLnbs[id];
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::setLna(bool in_bEnable) {
    ALOGV("%s", __FUNCTION__);
    if (mFrontendId != -1 && mFrontendInfos[mFrontendId].mFrontend != nullptr) {
        mFrontendInfos[mFrontendId].mFrontend->setLna(in_bEnable);
     } else {
        ALOGE("%s, frontend is not ready", __FUNCTION__);
     }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::setMaxNumberOfFrontends(FrontendType in_frontendType,
                                                    int32_t in_maxNumber) {
    ALOGV("%s", __FUNCTION__);

    // In the default implementation, every type only has one frontend.
    if (in_maxNumber < 0 || in_maxNumber > 1) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }
    mMaxUsableFrontends[in_frontendType] = in_maxNumber;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::getMaxNumberOfFrontends(FrontendType in_frontendType,
                                                    int32_t* _aidl_return) {
    ALOGV("%s", __FUNCTION__);
    *_aidl_return = mMaxUsableFrontends[in_frontendType];
    return ::ndk::ScopedAStatus::ok();
}

#if PLATFORM_SDK_VERSION > 33
::ndk::ScopedAStatus Tuner::isLnaSupported(bool* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    *_aidl_return = true;
    return ::ndk::ScopedAStatus::ok();
}
#endif

binder_status_t Tuner::dump(int fd, const char** args, uint32_t numArgs) {
    ALOGV("%s", __FUNCTION__);
    {
        dprintf(fd, "Frontends:\n");
        for (int i = 0; i < mFrontendSize; i++) {
            mFrontends[i]->dump(fd, args, numArgs);
        }
    }
    {
        dprintf(fd, "Demuxes:\n");
        map<int32_t, std::shared_ptr<Demux>>::iterator it;
        for (it = mDemuxes.begin(); it != mDemuxes.end(); it++) {
            it->second->dump(fd, args, numArgs);
        }
    }
    {
        dprintf(fd, "Lnbs:\n");
        for (int i = 0; i < mLnbs.size(); i++) {
            mLnbs[i]->dump(fd, args, numArgs);
        }
    }
    return STATUS_OK;
}

void Tuner::setFrontendAsDemuxSource(int32_t frontendId, int32_t demuxId) {
    mFrontendToDemux[frontendId] = demuxId;
    if (mFrontends[frontendId] != nullptr && mFrontends[frontendId]->isLocked()) {
        mDemuxes[demuxId]->startFrontendInputLoop();
    }
}

void Tuner::removeDemux(int32_t demuxId) {
    ALOGD("%s/%d demuxId = %d", __FUNCTION__, __LINE__, demuxId);
    map<int32_t, int32_t>::iterator it;
    for (it = mFrontendToDemux.begin(); it != mFrontendToDemux.end(); it++) {
        if (it->second == demuxId) {
            it = mFrontendToDemux.erase(it);
            break;
        }
    }
#if PLATFORM_SDK_VERSION > 33
    mDemuxes[demuxId]->setInUse(false);
#else
    mDemuxes.erase(demuxId);
#endif
}

void Tuner::removeFrontend(int32_t frontendId) {
#if PLATFORM_SDK_VERSION > 33
    map<int32_t, int32_t>::iterator it = mFrontendToDemux.find(frontendId);
    if (it != mFrontendToDemux.end()) {
        mDemuxes[it->second]->setInUse(false);
    }
    mFrontendToDemux.erase(frontendId);
#else
    map<int32_t, int32_t>::iterator it = mFrontendToDemux.find(frontendId);
    if (it != mFrontendToDemux.end()) {
        mDemuxes.erase(it->second);
    }
    mFrontendToDemux.erase(frontendId);
#endif
}

void Tuner::removeDescrambler(int32_t dscId) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);
    std::lock_guard<std::mutex> lock(mLock);

    mDescramblers.erase(dscId);
    ALOGD("%s/%d dscId:%d dscNum:%zd", __FUNCTION__, __LINE__, dscId, mDescramblers.size());
}

void Tuner::frontendStopTune(int32_t frontendId) {
    map<int32_t, int32_t>::iterator it = mFrontendToDemux.find(frontendId);
    int32_t demuxId;
    if (it != mFrontendToDemux.end()) {
        demuxId = it->second;
        mDemuxes[demuxId]->stopFrontendInput();
    }
}

void Tuner::frontendStartTune(int32_t frontendId) {
    map<int32_t, int32_t>::iterator it = mFrontendToDemux.find(frontendId);
    int32_t demuxId;
    if (it != mFrontendToDemux.end()) {
        demuxId = it->second;
        mDemuxes[demuxId]->startFrontendInputLoop();
    }
}

void Tuner::attachDescramblerToDemux(int32_t dscId, int32_t demuxId) {
  ALOGD("%s/%d dscId:%d demuxId:%d", __FUNCTION__, __LINE__, dscId, demuxId);

  if (mDescramblers.find(dscId) != mDescramblers.end()
      && mDemuxes.find(demuxId) != mDemuxes.end()) {
    mDemuxes.at(demuxId)->attachDescrambler(dscId, mDescramblers.at(dscId));
  }
}

void Tuner::detachDescramblerFromDemux(int32_t dscId, int32_t demuxId) {
  ALOGD("%s/%d dscId:%d demuxId:%d", __FUNCTION__, __LINE__, dscId, demuxId);

  if (mDescramblers.find(dscId) != mDescramblers.end()
      && mDemuxes.find(demuxId) != mDemuxes.end()) {
    mDemuxes.at(demuxId)->detachDescrambler(dscId);
  }
}

uint32_t Tuner::getTsInput(uint32_t frontendId) {
    int tsInput = mHwFes[mFrontendInfos[frontendId].hwId]->getTsInput();
    ALOGD("[frontendId] = %d, tsInput = %d, hwId = %d", frontendId, tsInput, mFrontendInfos[frontendId].hwId);
    return tsInput;
}
// use for record
uint32_t Tuner::getTsInput() {
    if (mFrontendId != -1) {
        return mHwFes[mFrontendInfos[mFrontendId].hwId]->getTsInput();
    } else {
        ALOGD("mFrontendId = %d", mFrontendId);
        return -1;
    }
}

void Tuner::setTsnSource() {
    char ts_clone_str[32] = {0};
    if (access(TS_CLONE, F_OK) == 0) {
        FileSystem_create();
        if (!FileSystem_readFile(TS_CLONE, ts_clone_str, sizeof(ts_clone_str))) {
            ALOGI("ts_clone is %s", ts_clone_str);
        } else {
            ALOGW("can't read ts_clone! %s", strerror(errno));
        }
        if (strstr(ts_clone_str, "ts clone 1")) {
            ALOGD("set tsn_source to local");
            return;
        } else {
            ALOGD("ts clone is not 1!");
            setTsnSourceNoTsClone();
        }
    } else {
        ALOGW("ts_clone node does not exist! %s", strerror(errno));
        setTsnSourceNoTsClone();
    }
}

void Tuner::setTsnSourceNoTsClone() {
    mDscMode = CA_DSC_COMMON_TYPE;
    char dmx_ver[32] = {0};
    char tsn_source[32] = {0};
    FileSystem_create();
    if (!FileSystem_readFile(TSN_DMX_VER, dmx_ver, sizeof(dmx_ver))) {
        ALOGI("dmx_ver is %s", dmx_ver);
        if (!strncmp(dmx_ver, "sc2-a", 5)
          || !strncmp(dmx_ver, "sc2-b", 5)
          || !strncmp(dmx_ver, "sc2-c", 5))
        mDscMode = CA_DSC_TSD_TYPE;
    } else {
        ALOGW("can't read dmx_ver! %s", strerror(errno));
    }
    if (!FileSystem_readFile(TSN_SOURCE, tsn_source, sizeof(tsn_source))) {
        ALOGI("tsn_source is %s", tsn_source);
    } else {
        ALOGW("can't read tsn_source! %s", strerror(errno));
        return;
    }
    if (!strncmp(dmx_ver, "sc2-d", 5)) {
        #ifdef SUPPORT_CBS
        if (!strstr(tsn_source, TSN_LOCAL)) {
            ALOGD("set tsn_source to local");
            FileSystem_writeFile(TSN_SOURCE, TSN_LOCAL);
        }
        #endif
    } else {
        if (!strstr(tsn_source, TSN_DEMOD)) {
            ALOGD("set tsn_source to demod");
            FileSystem_writeFile(TSN_SOURCE, TSN_DEMOD);
        }
    }
}

uint32_t Tuner::getDscMode() {
    return mDscMode;
}

vector<FrontendStatusType> Tuner::getstatusCaps(int32_t frontendId) {
    return mFrontendInfos[frontendId].mInfo.statusCaps;
}

std::shared_ptr<Demux> Tuner::getDemuxById(uint32_t demuxId) {
    if (mDemuxes.find(demuxId) != mDemuxes.end()) {
        return mDemuxes.at(demuxId);
    } else {
        return nullptr;
    }
}

int Tuner::allocateDemuxResource() {
    mInternalDemuxId = NUMDEMUX;
    auto it = std::find(mInterDmxIdManager.begin(), mInterDmxIdManager.end(), mInternalDemuxId);
    while (it != mInterDmxIdManager.end()) {
        mInternalDemuxId++;
        it = std::find(mInterDmxIdManager.begin(), mInterDmxIdManager.end(), mInternalDemuxId);
    }
    ALOGD("allocate mInternalDemuxId = %d", mInternalDemuxId);
    mInterDmxIdManager.push_back(mInternalDemuxId);
    return mInternalDemuxId;
}

void Tuner::removeDemuxResource(int internalDemuxId) {
    ALOGD("[%s/%d]internalDemuxId = %d", __FUNCTION__, __LINE__, internalDemuxId);
    for (auto iter = mInterDmxIdManager.begin(); iter != mInterDmxIdManager.end();) {
        if (internalDemuxId == *iter) {
            iter = mInterDmxIdManager.erase(iter);
        } else {
            ++iter;
        }
    }
}

uint32_t Tuner::getEncryptPvrSetting() {
    return mEncryptPvr;
}
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
