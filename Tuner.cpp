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

#define LOG_TAG "android.hardware.tv.tuner@1.0-Tuner"

#include "Tuner.h"
#include <android/hardware/tv/tuner/1.0/IFrontendCallback.h>
#include <utils/Log.h>
#include "Demux.h"
#include "Descrambler.h"
#include "Frontend.h"
#include "Lnb.h"
#include <json/json.h>

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

using ::android::hardware::tv::tuner::V1_0::DemuxId;

Tuner::Tuner() {
    const char* tuner_config_file = "/vendor/etc/tuner_hal/frontendinfos.json";
    FILE* fp = fopen(tuner_config_file, "r");
    if (fp != NULL) {
        fseek(fp, 0L, SEEK_END);
        const auto len = ftell(fp);
        char* data = (char*)malloc(len + 1);

        rewind(fp);
        fread(data, sizeof(char), len, fp);
        data[len] = '\0';

        Json::Value root;
        Json::Reader reader;

        if (reader.parse(data, root)) {
            auto& arrayHwFes = root["hwfe"];
            auto& arrayFronts = root["frontends"];
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
                    FrontendInfo::FrontendCapabilities caps = FrontendInfo::FrontendCapabilities();
                    switch (frontType)
                    {
                        case static_cast<int>(FrontendType::ANALOG):{
                            FrontendAnalogCapabilities analogCaps{
                                .typeCap = arrayFronts[i]["analogTypeCap"].asUInt(),
                                .sifStandardCap = arrayFronts[i]["sifCap"].asUInt(),
                            };
                            caps.analogCaps(analogCaps);
                        }
                        break;
                        case static_cast<int>(FrontendType::ATSC): {
                            FrontendAtscCapabilities atscCaps{
                                .modulationCap = arrayFronts[i]["modulationCap"].asUInt(),
                            };
                            caps.atscCaps(atscCaps);
                        }
                        break;
                        case static_cast<int>(FrontendType::DVBC): {
                            FrontendDvbcCapabilities dvbcCaps{
                                .modulationCap = arrayFronts[i]["modulationCap"].asUInt(),
                                .fecCap = arrayFronts[i]["fecCap"].asUInt64(),
                                .annexCap = (uint8_t)(arrayFronts[i]["annexCap"].asUInt()),
                            };
                            caps.dvbcCaps(dvbcCaps);
                        }
                        break;
                        case static_cast<int>(FrontendType::DVBS): {
                            FrontendDvbsCapabilities dvbsCaps{
                                .modulationCap = arrayFronts[i]["modulationCap"].asInt(),
                                .innerfecCap = arrayFronts[i]["fecCap"].asUInt(),
                                .standard = (uint8_t)(arrayFronts[i]["stdCap"].asUInt()),
                            };
                            caps.dvbsCaps(dvbsCaps);
                        }
                        break;
                        case static_cast<int>(FrontendType::DVBT): {
                            FrontendDvbtCapabilities dvbtCaps{
                                .transmissionModeCap = arrayFronts[i]["transmissionCap"].asUInt(),
                                .bandwidthCap = arrayFronts[i]["bandwidthCap"].asUInt(),
                                .constellationCap = arrayFronts[i]["constellationCap"].asUInt(),
                                .coderateCap = arrayFronts[i]["coderateCap"].asUInt(),
                                .hierarchyCap = arrayFronts[i]["hierarchyCap"].asUInt(),
                                .guardIntervalCap = arrayFronts[i]["guardIntervalCap"].asUInt(),
                                .isT2Supported = arrayFronts[i]["supportT2"].asBool(),
                                .isMisoSupported = arrayFronts[i]["constellationCap"].asBool(),
                            };
                            caps.dvbtCaps(dvbtCaps);
                        }
                        break;
                        case static_cast<int>(FrontendType::ISDBT): {
                            FrontendIsdbtCapabilities isdbtCaps{
                                .modeCap = arrayFronts[i]["modeCap"].asUInt(),
                                .bandwidthCap = arrayFronts[i]["bandwidthCap"].asUInt(),
                                .modulationCap = arrayFronts[i]["modulationCap"].asUInt(),
                                .coderateCap = arrayFronts[i]["coderateCap"].asUInt(),
                                .guardIntervalCap = arrayFronts[i]["guardIntervalCap"].asUInt(),
                            };
                            caps.isdbtCaps(isdbtCaps);
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
                        .minSymbolRate = minSymbol,
                        .maxSymbolRate = maxSymbol,
                        .acquireRange = hwCaps.acquireRange,
                        .exclusiveGroupId = exclusiveId,
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
        }
        root.clear();
        if (data)
            free(data);
        fclose(fp);
    }

    mLnbs.resize(2);
    mLnbs[0] = new Lnb(0);
    mLnbs[1] = new Lnb(1);
}

Tuner::~Tuner() {}

Return<void> Tuner::getFrontendIds(getFrontendIds_cb _hidl_cb) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

    vector<FrontendId> frontendIds;
    frontendIds.resize(mFrontendSize);
    for (int i = 0; i < mFrontendSize; i++) {
        frontendIds[i] = mFrontendInfos[i].id;
    }

    _hidl_cb(Result::SUCCESS, frontendIds);
    return Void();
}

Return<void> Tuner::openFrontendById(uint32_t frontendId, openFrontendById_cb _hidl_cb) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

    if (frontendId >= mFrontendSize || frontendId < 0) {
        ALOGW("[   WARN   ] Frontend with id %d isn't available", frontendId);
        _hidl_cb(Result::UNAVAILABLE, nullptr);
        return Void();
    }

    sp<Frontend> frontend;
    if (mFrontendInfos[frontendId].mFrontend == nullptr) {
        frontend = new Frontend(mFrontendInfos[frontendId].mInfo.type, mFrontendInfos[frontendId].id,
            this, mHwFes[mFrontendInfos[frontendId].hwId]);
        mFrontendInfos[frontendId].mFrontend = frontend;
    } else {
        frontend = mFrontendInfos[frontendId].mFrontend;
    }
    _hidl_cb(Result::SUCCESS, frontend);
    return Void();
}

Return<void> Tuner::openDemux(openDemux_cb _hidl_cb) {
    ALOGV("%s/%d mDemuxes size = %d", __FUNCTION__, __LINE__, mDemuxes.size());
    mLastUsedId = 0;
    std::map<uint32_t, sp<Demux>>::iterator it = mDemuxes.find(mLastUsedId);
    while (it != mDemuxes.end()) {
        mLastUsedId++;
        ALOGD("mLastUsedId = %d", mLastUsedId);
        it = mDemuxes.find(mLastUsedId);
    }

    if (mLastUsedId == NUMDEMX)
        mLastUsedId = 0;

    DemuxId demuxId = mLastUsedId;
    sp<Demux> demux = new Demux(demuxId, this);
    mDemuxes[demuxId] = demux;

    _hidl_cb(Result::SUCCESS, demuxId, demux);
    return Void();
}

Return<void> Tuner::getDemuxCaps(getDemuxCaps_cb _hidl_cb) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

    DemuxCapabilities caps;
    caps.numDemux                = NUMDEMX;
    caps.numRecord               = NUMRECORD;
    caps.numPlayback             = NUMPLAYBACK;
    caps.numTsFilter             = NUMTSFILTER;
    caps.numSectionFilter        = NUMSECTIONFILTER;
    caps.numAudioFilter          = NUMAUDIOFILTER;
    caps.numVideoFilter          = NUMVIDEOFILTER;
    caps.numPesFilter            = NUMPESFILTER;
    caps.numPcrFilter            = NUMPCRFILTER;
    caps.numBytesInSectionFilter = NUMBYTESINSECTIONFILTER;
    // ts filter can be an ts filter's data source.
    caps.linkCaps = {0x02, 0x00, 0x00, 0x00, 0x00};
    _hidl_cb(Result::SUCCESS, caps);
    return Void();
}

Return<void> Tuner::openDescrambler(openDescrambler_cb _hidl_cb) {

    if (mLastUsedId < 0) {
        ALOGE("%s/%d Invalid mLastUsedId:%d!", __FUNCTION__, __LINE__, mLastUsedId);
    }

    uint32_t descramblerId = mLastUsedId;

    sp<Descrambler> descrambler = new Descrambler(descramblerId, this);
    mDescramblers[descramblerId] = descrambler;

    _hidl_cb(Result::SUCCESS, descrambler);
    return Void();
}

Return<void> Tuner::getFrontendInfo(FrontendId frontendId, getFrontendInfo_cb _hidl_cb) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

    FrontendInfo info;
    if (frontendId >= mFrontendSize) {
        _hidl_cb(Result::INVALID_ARGUMENT, info);
        return Void();
    }

    info = mFrontendInfos[frontendId].mInfo;
    _hidl_cb(Result::SUCCESS, info);
    return Void();
}

Return<void> Tuner::getLnbIds(getLnbIds_cb _hidl_cb) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

    vector<LnbId> lnbIds;
    lnbIds.resize(mLnbs.size());
    for (int i = 0; i < lnbIds.size(); i++) {
        lnbIds[i] = mLnbs[i]->getId();
    }

    _hidl_cb(Result::SUCCESS, lnbIds);
    return Void();
}

Return<void> Tuner::openLnbById(LnbId lnbId, openLnbById_cb _hidl_cb) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

    if (lnbId >= mLnbs.size()) {
        _hidl_cb(Result::INVALID_ARGUMENT, nullptr);
        return Void();
    }

    _hidl_cb(Result::SUCCESS, mLnbs[lnbId]);
    return Void();
}

sp<Frontend> Tuner::getFrontendById(uint32_t frontendId) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

    return mFrontendInfos[frontendId].mFrontend;
}

Return<void> Tuner::openLnbByName(const hidl_string& /*lnbName*/, openLnbByName_cb _hidl_cb) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

    sp<ILnb> lnb = new Lnb();

    _hidl_cb(Result::SUCCESS, 1234, lnb);
    return Void();
}

void Tuner::setFrontendAsDemuxSource(uint32_t frontendId, uint32_t demuxId) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);
    map<uint32_t, uint32_t>::iterator it = mFrontendToDemux.find(frontendId);
    uint32_t dmxId;
    if (it != mFrontendToDemux.end()) {
        dmxId = it->second;
        ALOGD("find demuxid = %d in mFrontendToDemux, don't need to startFrontendInputLoop", dmxId);
        return;
    }

    mFrontendToDemux[frontendId] = demuxId;
    sp<Frontend> frontend = mFrontendInfos[frontendId].mFrontend;
    if (frontend != nullptr && frontend->isLocked()) {
        mDemuxes[demuxId]->startFrontendInputLoop();
    }
}

void Tuner::frontendStopTune(uint32_t frontendId) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

    map<uint32_t, uint32_t>::iterator it = mFrontendToDemux.find(frontendId);
    uint32_t demuxId;
    if (it != mFrontendToDemux.end()) {
        demuxId = it->second;
        mDemuxes[demuxId]->stopFrontendInput();
    }
}

void Tuner::frontendStartTune(uint32_t frontendId) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

    map<uint32_t, uint32_t>::iterator it = mFrontendToDemux.find(frontendId);
    uint32_t demuxId;
    if (it != mFrontendToDemux.end()) {
        demuxId = it->second;
        mDemuxes[demuxId]->startFrontendInputLoop();
    }
}

void Tuner::attachDescramblerToDemux(uint32_t descramblerId,
                                     uint32_t demuxId) const {
  ALOGV("%s/%d", __FUNCTION__, __LINE__);

  if (mDescramblers.find(descramblerId) != mDescramblers.end()
      && mDemuxes.find(demuxId) != mDemuxes.end()) {
    mDemuxes.at(demuxId)->attachDescrambler(descramblerId,
                                            mDescramblers.at(descramblerId));
  }
}

void Tuner::detachDescramblerFromDemux(uint32_t descramblerId,
                                       uint32_t demuxId) const {
  ALOGV("%s/%d", __FUNCTION__, __LINE__);

  if (mDescramblers.find(descramblerId) != mDescramblers.end()
      && mDemuxes.find(demuxId) != mDemuxes.end()) {
    mDemuxes.at(demuxId)->detachDescrambler(descramblerId);
  }
}

void Tuner::removeDemux(uint32_t demuxId) {
   map<uint32_t, uint32_t>::iterator it;
   for (it = mFrontendToDemux.begin(); it != mFrontendToDemux.end(); it++) {
       if (it->second == demuxId) {
           it = mFrontendToDemux.erase(it);
           break;
       }
   }
   mDemuxes.erase(demuxId);
}

void Tuner::removeFrontend(uint32_t frontendId) {
   mFrontendToDemux.erase(frontendId);
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
