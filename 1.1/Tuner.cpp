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

#define LOG_TAG "android.hardware.tv.tuner@1.1-Tuner"

#include "Tuner.h"
#include <utils/Log.h>
#include <json/json.h>
#include "Demux.h"
#include "Descrambler.h"
#include "Frontend.h"
#include "Lnb.h"
#include "FileSystemIo.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

#define TUNER_CONFIG_FILE "/vendor/etc/tuner_hal/frontendinfos.json"
#define FRONTEND_DEVICE "/dev/dvb0.frontend0"

//check device exist or not
static bool isDeviceExist(const char *file_name)
{
    struct stat tmp_st;
    return stat(file_name, &tmp_st) == 0;
}

/*
Tuner::Tuner() {
    // Static Frontends array to maintain local frontends information
    // Array index matches their FrontendId in the default impl
    mFrontendSize = 10;
    mFrontends[0] = new Frontend(FrontendType::ISDBS, 0, this);
    mFrontends[1] = new Frontend(FrontendType::ATSC3, 1, this);
    mFrontends[2] = new Frontend(FrontendType::DVBC, 2, this);
    mFrontends[3] = new Frontend(FrontendType::DVBS, 3, this);
    mFrontends[4] = new Frontend(FrontendType::DVBT, 4, this);
    mFrontends[5] = new Frontend(FrontendType::ISDBT, 5, this);
    mFrontends[6] = new Frontend(FrontendType::ANALOG, 6, this);
    mFrontends[7] = new Frontend(FrontendType::ATSC, 7, this);
    mFrontends[8] = new Frontend(FrontendType::ISDBS3, 8, this);
    mFrontends[9] =
            new Frontend(static_cast<V1_0::FrontendType>(V1_1::FrontendType::DTMB), 9, this);

    FrontendInfo::FrontendCapabilities caps;
    vector<FrontendStatusType> statusCaps;

    caps = FrontendInfo::FrontendCapabilities();
    caps.isdbsCaps(FrontendIsdbsCapabilities());
    mFrontendCaps[0] = caps;
    statusCaps = {
            FrontendStatusType::DEMOD_LOCK,
            FrontendStatusType::SNR,
            FrontendStatusType::FEC,
            FrontendStatusType::MODULATION,
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::MODULATIONS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::ROLL_OFF),
    };
    mFrontendStatusCaps[0] = statusCaps;

    caps = FrontendInfo::FrontendCapabilities();
    caps.atsc3Caps(FrontendAtsc3Capabilities());
    mFrontendCaps[1] = caps;
    statusCaps = {
            FrontendStatusType::BER,
            FrontendStatusType::PER,
            FrontendStatusType::ATSC3_PLP_INFO,
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::MODULATIONS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::BERS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::INTERLEAVINGS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::BANDWIDTH),
    };
    mFrontendStatusCaps[1] = statusCaps;

    caps = FrontendInfo::FrontendCapabilities();
    caps.dvbcCaps(FrontendDvbcCapabilities());
    mFrontendCaps[2] = caps;
    statusCaps = {
            FrontendStatusType::PRE_BER,
            FrontendStatusType::SIGNAL_QUALITY,
            FrontendStatusType::MODULATION,
            FrontendStatusType::SPECTRAL,
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::MODULATIONS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::CODERATES),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::INTERLEAVINGS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::BANDWIDTH),
    };
    mFrontendStatusCaps[2] = statusCaps;

    caps = FrontendInfo::FrontendCapabilities();
    caps.dvbsCaps(FrontendDvbsCapabilities());
    mFrontendCaps[3] = caps;
    statusCaps = {
            FrontendStatusType::SIGNAL_STRENGTH,
            FrontendStatusType::SYMBOL_RATE,
            FrontendStatusType::MODULATION,
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::MODULATIONS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::ROLL_OFF),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::IS_MISO),
    };
    mFrontendStatusCaps[3] = statusCaps;

    caps = FrontendInfo::FrontendCapabilities();
    caps.dvbtCaps(FrontendDvbtCapabilities());
    mFrontendCaps[4] = caps;
    statusCaps = {
            FrontendStatusType::EWBS,
            FrontendStatusType::PLP_ID,
            FrontendStatusType::HIERARCHY,
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::MODULATIONS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::BANDWIDTH),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::GUARD_INTERVAL),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::TRANSMISSION_MODE),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::T2_SYSTEM_ID),
    };
    mFrontendStatusCaps[4] = statusCaps;

    caps = FrontendInfo::FrontendCapabilities();
    FrontendIsdbtCapabilities isdbtCaps{
            .modeCap = FrontendIsdbtMode::MODE_1 | FrontendIsdbtMode::MODE_2,
            .bandwidthCap = (unsigned int)FrontendIsdbtBandwidth::BANDWIDTH_6MHZ,
            .modulationCap = (unsigned int)FrontendIsdbtModulation::MOD_16QAM,
            // ISDBT shares coderate and guard interval with DVBT
            .coderateCap = FrontendDvbtCoderate::CODERATE_4_5 | FrontendDvbtCoderate::CODERATE_6_7,
            .guardIntervalCap = (unsigned int)FrontendDvbtGuardInterval::INTERVAL_1_128,
    };
    caps.isdbtCaps(isdbtCaps);
    mFrontendCaps[5] = caps;
    statusCaps = {
            FrontendStatusType::AGC,
            FrontendStatusType::LNA,
            FrontendStatusType::MODULATION,
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::MODULATIONS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::BANDWIDTH),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::GUARD_INTERVAL),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::TRANSMISSION_MODE),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::ISDBT_SEGMENTS),
    };
    mFrontendStatusCaps[5] = statusCaps;

    caps = FrontendInfo::FrontendCapabilities();
    caps.analogCaps(FrontendAnalogCapabilities());
    mFrontendCaps[6] = caps;
    statusCaps = {
            FrontendStatusType::LAYER_ERROR,
            FrontendStatusType::MER,
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::UEC),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::TS_DATA_RATES),
    };
    mFrontendStatusCaps[6] = statusCaps;

    caps = FrontendInfo::FrontendCapabilities();
    caps.atscCaps(FrontendAtscCapabilities());
    mFrontendCaps[7] = caps;
    statusCaps = {
            FrontendStatusType::FREQ_OFFSET,
            FrontendStatusType::RF_LOCK,
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::MODULATIONS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::IS_LINEAR),
    };
    mFrontendStatusCaps[7] = statusCaps;

    caps = FrontendInfo::FrontendCapabilities();
    caps.isdbs3Caps(FrontendIsdbs3Capabilities());
    mFrontendCaps[8] = caps;
    statusCaps = {
            FrontendStatusType::DEMOD_LOCK,
            FrontendStatusType::MODULATION,
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::MODULATIONS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::ROLL_OFF),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::IS_SHORT_FRAMES),
    };
    mFrontendStatusCaps[8] = statusCaps;

    statusCaps = {
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::MODULATIONS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::INTERLEAVINGS),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::BANDWIDTH),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::GUARD_INTERVAL),
            static_cast<FrontendStatusType>(V1_1::FrontendStatusTypeExt1_1::TRANSMISSION_MODE),
    };
    mFrontendStatusCaps[9] = statusCaps;

    mLnbs.resize(2);
    mLnbs[0] = new Lnb(0);
    mLnbs[1] = new Lnb(1);
}
*/
Tuner::Tuner() {
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

                if (!dmxSetting["ts_input"].isNull()) {
                    mTsInput = dmxSetting["ts_input"].asInt();
                    ALOGD("ts_input = %d", mTsInput);
                }
            }
            mLnbs.resize(1);
            if (mHwFes.size() > 0) {
                mLnbs[0] = new Lnb(0, mHwFes[0], "hardware_lnb");
            } else {
                mLnbs[0] = new Lnb(0, nullptr, "virtual_lnb");
            }

            root.clear();
            /*
             * This is the logic, no need to modify, ignore coverity weak cryptor report.
             */
            /* coverity[event_tag:SUPPRESS] */
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

Tuner::~Tuner() {}

Return<void> Tuner::getFrontendIds(getFrontendIds_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    vector<FrontendId> frontendIds;
    if (mFrontendSize > 0) {
        frontendIds.resize(mFrontendSize);
        for (int i = 0; i < mFrontendSize; i++) {
            frontendIds[i] = mFrontendInfos[i].id;
        }
    }
    _hidl_cb(Result::SUCCESS, frontendIds);
    return Void();
}

Return<void> Tuner::openFrontendById(uint32_t frontendId, openFrontendById_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    std::lock_guard<std::mutex> lock(mLock);
    if (frontendId >= mFrontendSize || (int)frontendId < 0) {
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
    ALOGD("%s/%d mDemuxes size = %d", __FUNCTION__, __LINE__, mDemuxes.size());
    std::lock_guard<std::mutex> lock(mLock);
    mLastUsedId = 0;
    std::map<uint32_t, sp<Demux>>::iterator it = mDemuxes.find(mLastUsedId);
    while (it != mDemuxes.end()) {
        mLastUsedId++;
        ALOGD("mLastUsedId = %d", mLastUsedId);
        it = mDemuxes.find(mLastUsedId);
    }

    if (mLastUsedId == NUMDEMX)
        mLastUsedId = 1; //match with cbs, dmxid 0 for dtvfs, dmxid 1 for dtvinput

    DemuxId demuxId = mLastUsedId;
    sp<Demux> demux = new Demux(demuxId, this);
    mDemuxes[demuxId] = demux;

    _hidl_cb(Result::SUCCESS, demuxId, demux);
    return Void();
}

Return<void> Tuner::getDemuxCaps(getDemuxCaps_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

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
    caps.bTimeFilter             = true;
    _hidl_cb(Result::SUCCESS, caps);
    return Void();
}

Return<void> Tuner::openDescrambler(openDescrambler_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);
    std::lock_guard<std::mutex> lock(mLock);

    uint32_t nextDescramblerId = mLastUsedDescramblerId + 1;
    uint32_t descramblerId = nextDescramblerId;
    if (descramblerId == NUMDSC)
        descramblerId = 0;

    std::map<uint32_t, sp<Descrambler>>::iterator it;
    it = mDescramblers.find(descramblerId);
    while (it != mDescramblers.end()) {
        descramblerId++;
        if (descramblerId == nextDescramblerId) {
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


    sp<Descrambler> descrambler = new Descrambler(descramblerId, this);
    mDescramblers[descramblerId] = descrambler;
    mLastUsedDescramblerId = descramblerId;
    ALOGD("%s descrambler id: %d", __FUNCTION__, descramblerId);

    _hidl_cb(Result::SUCCESS, descrambler);
    return Void();
}

Return<void> Tuner::getFrontendInfo(FrontendId frontendId, getFrontendInfo_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

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
    ALOGV("%s", __FUNCTION__);

    vector<V1_0::LnbId> lnbIds;
    lnbIds.resize(mLnbs.size());
    for (int i = 0; i < lnbIds.size(); i++) {
        lnbIds[i] = mLnbs[i]->getId();
    }

    _hidl_cb(Result::SUCCESS, lnbIds);
    return Void();
}

Return<void> Tuner::openLnbById(V1_0::LnbId lnbId, openLnbById_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    if (lnbId >= mLnbs.size()) {
        _hidl_cb(Result::INVALID_ARGUMENT, nullptr);
        return Void();
    }

    _hidl_cb(Result::SUCCESS, mLnbs[lnbId]);
    return Void();
}

sp<Frontend> Tuner::getFrontendById(uint32_t frontendId) {
    ALOGV("%s", __FUNCTION__);

    return mFrontendInfos[frontendId].mFrontend;
}

Return<void> Tuner::openLnbByName(const hidl_string& lnbName, openLnbByName_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);
    if (mHwFes.size() == 0) {
        ALOGE("%s: no hardware, cannot create lnb for client.", __FUNCTION__);
        _hidl_cb(Result::UNAVAILABLE, -1, nullptr);
    }

    int id = mLnbs.size();
    sp<Lnb> lnb = new Lnb(id, mHwFes[0], lnbName.c_str());
    mLnbs.push_back(lnb);

    _hidl_cb(Result::SUCCESS, id, lnb);

    return Void();
}

Return<void> Tuner::getFrontendDtmbCapabilities(uint32_t frontendId,
                                                getFrontendDtmbCapabilities_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    if (mFrontends[frontendId] != nullptr &&
        (mFrontends[frontendId]->getFrontendType() ==
         static_cast<V1_0::FrontendType>(V1_1::FrontendType::DTMB))) {
        _hidl_cb(Result::SUCCESS, mDtmbCaps);
    } else {
        _hidl_cb(Result::UNAVAILABLE, mDtmbCaps);
    }
    return Void();
}

void Tuner::setFrontendAsDemuxSource(uint32_t frontendId, uint32_t demuxId) {
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

void Tuner::removeDescrambler(uint32_t descramblerId) {
    std::lock_guard<std::mutex> lock(mLock);

    mDescramblers.erase(descramblerId);
    if (mDescramblers.size() == 0)
      mLastUsedDescramblerId = -1;
    ALOGD("%s/%d descrambler id: %d", __FUNCTION__, __LINE__, descramblerId);
}

void Tuner::frontendStopTune(uint32_t frontendId) {
    map<uint32_t, uint32_t>::iterator it = mFrontendToDemux.find(frontendId);
    uint32_t demuxId;
    if (it != mFrontendToDemux.end()) {
        demuxId = it->second;
        mDemuxes[demuxId]->stopFrontendInput();
    }
}

void Tuner::frontendStartTune(uint32_t frontendId) {
    map<uint32_t, uint32_t>::iterator it = mFrontendToDemux.find(frontendId);
    uint32_t demuxId;
    if (it != mFrontendToDemux.end()) {
        demuxId = it->second;
        mDemuxes[demuxId]->startFrontendInputLoop();
    }
}

void Tuner::attachDescramblerToDemux(uint32_t descramblerId, uint32_t demuxId) const {
  ALOGV("%s/%d", __FUNCTION__, __LINE__);

  if (mDescramblers.find(descramblerId) != mDescramblers.end()
      && mDemuxes.find(demuxId) != mDemuxes.end()) {
    mDemuxes.at(demuxId)->attachDescrambler(descramblerId, mDescramblers.at(descramblerId));
  }
}

void Tuner::detachDescramblerFromDemux(uint32_t descramblerId, uint32_t demuxId) const {
  ALOGV("%s/%d", __FUNCTION__, __LINE__);

  if (mDescramblers.find(descramblerId) != mDescramblers.end()
      && mDemuxes.find(demuxId) != mDemuxes.end()) {
    mDemuxes.at(demuxId)->detachDescrambler(descramblerId);
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

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
