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
#define LOG_TAG "android.hardware.tv.tuner-service.droidlogic-Tuner"

#include <aidl/android/hardware/tv/tuner/Result.h>
#include <utils/Log.h>
#include <sys/stat.h>
#include "Demux.h"
#include "Descrambler.h"
#include "Frontend.h"
#include "Lnb.h"
#include "Tuner.h"
#include <json/json.h>
#include "FileSystemIo.h"

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
                auto& dmxSetting = root["dmxsetting"];
                for (int i = 0; i < arrayHwFes.size(); i ++) {
                    if (!arrayHwFes[i]["id"].isNull()) {
                        int hwId = arrayHwFes[i]["id"].asInt();
                        sp<HwFeState> hwFeState = new HwFeState(hwId);
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
                        for (int s = 0; s < static_cast<int>(FrontendStatusType::ATSC3_PLP_INFO); s ++) {
                            if ((hwCaps.statusCap & (1 << s)) == (1 << s)) {
                                statusCaps.push_back(static_cast<FrontendStatusType>(s));
                            }
                        }
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
                            }
                            break;
                            case static_cast<int>(FrontendType::ATSC): {
                                FrontendAtscCapabilities atscCaps {
                                    .modulationCap = arrayFronts[i]["modulationCap"].asInt(),
                                };
                                caps.set<FrontendCapabilities::Tag::atscCaps>(atscCaps);
                            }
                            break;
                            case static_cast<int>(FrontendType::DVBC): {
                                FrontendDvbcCapabilities dvbcCaps {
                                    .modulationCap = arrayFronts[i]["modulationCap"].asInt(),
                                    .fecCap = arrayFronts[i]["fecCap"].asInt64(),
                                    .annexCap =  static_cast<int8_t>(arrayFronts[i]["annexCap"].asInt()),
                                };
                                caps.set<FrontendCapabilities::Tag::dvbcCaps>(dvbcCaps);
                            }
                            break;
                            case static_cast<int>(FrontendType::DVBS): {
                                FrontendDvbsCapabilities dvbsCaps {
                                    .modulationCap = arrayFronts[i]["modulationCap"].asInt(),
                                    .innerfecCap = arrayFronts[i]["fecCap"].asUInt(),
                                    .standard =  static_cast<int8_t>(arrayFronts[i]["stdCap"].asInt()),
                                };
                                caps.set<FrontendCapabilities::Tag::dvbsCaps>(dvbsCaps);
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

                if (!dmxSetting["ts_input"].isNull()) {
                    mTsInput = dmxSetting["ts_input"].asInt();
                    ALOGD("ts_input = %d", mTsInput);
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

    setTsnSource();
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

::ndk::ScopedAStatus Tuner::openFrontendById(int32_t in_frontendId,
                                             std::shared_ptr<IFrontend>* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

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

    *_aidl_return = frontend;//mFrontends[in_frontendId];
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::openDemux(std::vector<int32_t>* out_demuxId,
                                      std::shared_ptr<IDemux>* _aidl_return) {
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
        mLastUsedId = 0;

    //DemuxId demuxId = mLastUsedId;
    mDemuxes[mLastUsedId] = ndk::SharedRefBase::make<Demux>(mLastUsedId, this->ref<Tuner>());
    out_demuxId->push_back(mLastUsedId);
    *_aidl_return = mDemuxes[mLastUsedId];

    return ::ndk::ScopedAStatus::ok();
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

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::openDescrambler(std::shared_ptr<IDescrambler>* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    uint32_t descramblerId = mLastUsedDescramblerId + 1;
    if (descramblerId == NUMDSC)
        descramblerId = 0;

    std::map<int32_t, std::shared_ptr<Descrambler>>::iterator it;
    it = mDescramblers.find(descramblerId);
    while (it != mDescramblers.end()) {
        descramblerId++;
        if (descramblerId == mLastUsedDescramblerId + 1) {
            if (descramblerId != 0) {
                ALOGW("%s/%d reset descrambler 0!", __FUNCTION__, __LINE__);
                descramblerId = 0;
            }
            break;
        }
        if (descramblerId == NUMDSC)
            descramblerId = 0;
        it = mDescramblers.find(descramblerId);
    }

    mDescramblers[descramblerId] = ndk::SharedRefBase::make<Descrambler>(descramblerId, this->ref<Tuner>());
    *_aidl_return = mDescramblers[descramblerId];
    mLastUsedDescramblerId = descramblerId;
    ALOGD("%s descrambler id: %d", __FUNCTION__, descramblerId);

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
    mLnbs[id] = ndk::SharedRefBase::make<Lnb>(id, mHwFes[0], in_lnbName.c_str());
    *_aidl_return = mLnbs[id];

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Tuner::setLna(bool /* in_bEnable */) {
    ALOGV("%s", __FUNCTION__);

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
    *_aidl_return = mMaxUsableFrontends[in_frontendType];
    return ::ndk::ScopedAStatus::ok();
}

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
    map<int32_t, int32_t>::iterator it;
    for (it = mFrontendToDemux.begin(); it != mFrontendToDemux.end(); it++) {
        if (it->second == demuxId) {
            it = mFrontendToDemux.erase(it);
            break;
        }
    }
    mDemuxes.erase(demuxId);
}

void Tuner::removeFrontend(int32_t frontendId) {
    map<int32_t, int32_t>::iterator it = mFrontendToDemux.find(frontendId);
    if (it != mFrontendToDemux.end()) {
        mDemuxes.erase(it->second);
    }
    mFrontendToDemux.erase(frontendId);
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

void Tuner::attachDescramblerToDemux(int32_t descramblerId, int32_t demuxId) {
  ALOGV("%s/%d", __FUNCTION__, __LINE__);

  if (mDescramblers.find(descramblerId) != mDescramblers.end()
      && mDemuxes.find(demuxId) != mDemuxes.end()) {
    mDemuxes.at(demuxId)->attachDescrambler(descramblerId, mDescramblers.at(descramblerId));
  }
}

void Tuner::detachDescramblerFromDemux(int32_t descramblerId, int32_t demuxId) {
  ALOGV("%s/%d", __FUNCTION__, __LINE__);

  if (mDescramblers.find(descramblerId) != mDescramblers.end()
      && mDemuxes.find(demuxId) != mDemuxes.end()) {
    mDemuxes.at(demuxId)->detachDescrambler(descramblerId);
    mDescramblers.erase(descramblerId);
    if (mDescramblers.size() == 0)
        mLastUsedDescramblerId = -1;
  }
}

uint32_t Tuner::getTsInput() {
    return mTsInput;
}

void Tuner::setTsnSource() {
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
        if (!strstr(tsn_source, TSN_LOCAL)) {
            ALOGD("set tsn_source to local");
            FileSystem_writeFile(TSN_SOURCE, TSN_LOCAL);
        }
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

}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
