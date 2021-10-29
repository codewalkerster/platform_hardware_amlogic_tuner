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

#define LOG_TAG "android.hardware.tv.tuner@1.0-Frontend"

#include "Frontend.h"
#include <android/hardware/tv/tuner/1.0/IFrontendCallback.h>
#include <utils/Log.h>
#include "FrontendDvbtDevice.h"
#include "FrontendAtscDevice.h"
#include "FrontendAnalogDevice.h"
#include "FrontendDvbcDevice.h"
#include "FrontendDvbsDevice.h"
#include "FrontendIsdbtDevice.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

Frontend::Frontend(FrontendType type, FrontendId id, const sp<Tuner>& tuner, const sp<HwFeState>& hwFe) {
    mType = type;
    mId = id;
    mTunerService = tuner;
    // Init callback to nullptr
    mCallback = nullptr;
    //hardware
    if (type == FrontendType::DVBT) {
        mFeDev = new FrontendDvbtDevice(id, type, this);
    } else if (type == FrontendType::ATSC) {
        mFeDev = new FrontendAtscDevice(id, type, this);
    } else if (type == FrontendType::ANALOG) {
        mFeDev = new FrontendAnalogDevice(id, type, this);
    } else if (type == FrontendType::DVBC) {
        mFeDev = new FrontendDvbcDevice(id, type, this);
    } else if (type == FrontendType::DVBS) {
        mFeDev = new FrontendDvbsDevice(id, type, this);
    } else if (type == FrontendType::ISDBT) {
        mFeDev = new FrontendIsdbtDevice(id, type, this);
    } else {
        mFeDev = new FrontendDevice(id, type, this);
    }
    mFeDev->setHwFe(hwFe);
}

Frontend::~Frontend() {
    mFeDev->release();
}

Return<Result> Frontend::close() {
    ALOGV("%s", __FUNCTION__);
    // Reset callback
    mCallback = nullptr;
    mIsLocked = false;
    mFeDev->stop();
    mTunerService->removeFrontend(mId);

    return Result::SUCCESS;
}

Return<Result> Frontend::setCallback(const sp<IFrontendCallback>& callback) {
    ALOGV("%s", __FUNCTION__);
    if (callback == nullptr) {
        ALOGW("[   WARN   ] Set Frontend callback with nullptr");
        return Result::INVALID_ARGUMENT;
    }

    mCallback = callback;
    return Result::SUCCESS;
}

Return<Result> Frontend::tune(const FrontendSettings& settings) {
    ALOGV("%s", __FUNCTION__);
    if (mCallback == nullptr) {
        ALOGW("[   WARN   ] Frontend callback is not set when tune");
        return Result::INVALID_STATE;
    }

    int ret = mFeDev->tune(settings);
    if (mId != mExistId) {
        mTunerService->frontendStartTune(mId);
        mExistId = mId;
    }
    //mCallback->onEvent(FrontendEventType::LOCKED);
    //mIsLocked = true;

    return Result(ret);
}

Return<Result> Frontend::stopTune() {
    ALOGV("%s", __FUNCTION__);

    mTunerService->frontendStopTune(mId);
    mIsLocked = false;

    int ret = mFeDev->stopTune();

    return Result(ret);
}

Return<Result> Frontend::scan(const FrontendSettings& settings, FrontendScanType type) {
    ALOGV("%s", __FUNCTION__);

    int ret = mFeDev->scan(settings, type);

    return Result(ret);
}

Return<Result> Frontend::stopScan() {
    ALOGV("%s", __FUNCTION__);

    mIsLocked = false;
    int ret = mFeDev->stopScan();

    return Result(ret);
}

Return<void> Frontend::getStatus(const hidl_vec<FrontendStatusType>& statusTypes,
                                 getStatus_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    vector<FrontendStatus> statuses;
    for (int i = 0; i < statusTypes.size(); i++) {
        FrontendStatusType type = statusTypes[i];
        FrontendStatus status;
        // assign randomly selected values for testing.
        switch (type) {
            case FrontendStatusType::DEMOD_LOCK: {
                status.isDemodLocked(mIsLocked);
                break;
            }
            case FrontendStatusType::SNR: {
                status.snr(mFeDev->getFeSnr());
                break;
            }
            case FrontendStatusType::BER: {
                status.ber(mFeDev->getFeBer());
                break;
            }
            case FrontendStatusType::PER: {
                status.per(0);//TODO::how to gt PER
                break;
            }
            case FrontendStatusType::PRE_BER: {
                status.preBer(0);//TODO::how to get PRE_BER
                break;
            }
            case FrontendStatusType::SIGNAL_QUALITY: {
                status.signalQuality(mFeDev->getFeSnr());//TODO::use snr as dtvkit now
                break;
            }
            case FrontendStatusType::SIGNAL_STRENGTH: {
                status.signalStrength(mFeDev->getSingnalStrenth());
                break;
            }
            case FrontendStatusType::SYMBOL_RATE: {
                status.symbolRate(0);
                break;
            }
            case FrontendStatusType::FEC: {
                FrontendInnerFec fec = FrontendInnerFec::AUTO;
                status.innerFec(fec);
                break;
            }
            case FrontendStatusType::MODULATION: {
                FrontendModulationStatus modulationStatus = mFeDev->getFeModulationStatus();
                status.modulation(modulationStatus);
                break;
            }
            case FrontendStatusType::SPECTRAL: {
                status.inversion(FrontendDvbcSpectralInversion::NORMAL);
                break;
            }
            case FrontendStatusType::LNB_VOLTAGE: {
                status.lnbVoltage(LnbVoltage::VOLTAGE_5V);
                break;
            }
            case FrontendStatusType::PLP_ID: {
                status.plpId(101);  // type uint8_t
                break;
            }
            case FrontendStatusType::EWBS: {
                status.isEWBS(false);
                break;
            }
            case FrontendStatusType::AGC: {
                status.agc(7);
                break;
            }
            case FrontendStatusType::LNA: {
                status.isLnaOn(false);
                break;
            }
            case FrontendStatusType::LAYER_ERROR: {
                vector<bool> v = {false, true, true};
                status.isLayerError(v);
                break;
            }
            case FrontendStatusType::MER: {
                status.mer(8);
                break;
            }
            case FrontendStatusType::FREQ_OFFSET: {
                status.freqOffset(9);
                break;
            }
            case FrontendStatusType::HIERARCHY: {
                status.hierarchy(FrontendDvbtHierarchy::HIERARCHY_1_NATIVE);
                break;
            }
            case FrontendStatusType::RF_LOCK: {
                status.isRfLocked(mIsLocked);
                break;
            }
            case FrontendStatusType::ATSC3_PLP_INFO: {
                vector<FrontendStatusAtsc3PlpInfo> v;
                FrontendStatusAtsc3PlpInfo info1{
                        .plpId = 3,
                        .isLocked = false,
                        .uec = 313,
                };
                FrontendStatusAtsc3PlpInfo info2{
                        .plpId = 5,
                        .isLocked = true,
                        .uec = 515,
                };
                v.push_back(info1);
                v.push_back(info2);
                status.plpInfo(v);
                break;
            }
            default: {
                continue;
            }
        }
        statuses.push_back(status);
    }
    _hidl_cb(Result::SUCCESS, statuses);

    return Void();
}

Return<Result> Frontend::setLna(bool /* bEnable */) {
    ALOGV("%s", __FUNCTION__);

    return Result::SUCCESS;
}

Return<Result> Frontend::setLnb(uint32_t /* lnb */) {
    ALOGV("%s", __FUNCTION__);
    if (!supportsSatellite()) {
        return Result::INVALID_STATE;
    }
    return Result::SUCCESS;
}

FrontendType Frontend::getFrontendType() {
    return mType;
}

FrontendId Frontend::getFrontendId() {
    return mId;
}

bool Frontend::supportsSatellite() {
    return mType == FrontendType::DVBS || mType == FrontendType::ISDBS ||
           mType == FrontendType::ISDBS3;
}

bool Frontend::isLocked() {
    return mIsLocked;
}

void Frontend::sendScanCallBack(uint32_t freq, bool isLocked, bool isEnd) {
    mIsLocked = isLocked;
    FrontendScanMessage msg;
    msg.isLocked(isLocked);
    mCallback->onScanMessage(FrontendScanMessageType::LOCKED, msg);
    msg.isEnd(isEnd);
    mCallback->onScanMessage(FrontendScanMessageType::END, msg);
    msg.frequencies({freq});
    mCallback->onScanMessage(FrontendScanMessageType::FREQUENCY, msg);
}

void Frontend::sendEventCallBack(FrontendEventType locked) {
    mCallback->onEvent(locked);
    if (locked == FrontendEventType::LOCKED) {
      mIsLocked = true;
    } else {
      mIsLocked = false;
    }
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
