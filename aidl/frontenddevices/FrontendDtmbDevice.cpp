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

#define LOG_TAG "droidlogic_frontend"

#include <sys/ioctl.h>
#include <sys/poll.h>

#include "Tuner.h"
#include <utils/Log.h>
#include "FrontendDtmbDevice.h"
using ::aidl::android::hardware::tv::tuner::FrontendDtmbSettings;

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

FrontendDtmbDevice::FrontendDtmbDevice(uint32_t hwId, FrontendType type, const sp<Frontend>& context)
    : FrontendDevice(hwId, type, context) {
}

FrontendDtmbDevice::~FrontendDtmbDevice() {
}

FrontendModulationStatus FrontendDtmbDevice::getFeModulationStatus() {
    FrontendModulationStatus modulationStatus;
    ALOGW("FrontendDtmbDevice: should not get modulationStatus in dtmb type.");
    modulationStatus.set<FrontendModulationStatus::Tag::dvbc>(FrontendDvbcModulation::UNDEFINED);
    return modulationStatus;
}

int FrontendDtmbDevice::getFrontendSettings(FrontendSettings *settings, void * fe_params) {
    struct dvb_frontend_parameters *p_fe_params = (struct dvb_frontend_parameters*)(fe_params);

    if (settings->getTag() != FrontendSettings::Tag::dtmb) {
        return -1;
    }

    FrontendDtmbSettings dtmbSetting;
    //transmissionMode
    p_fe_params->frequency = settings->get<FrontendSettings::Tag::dtmb>().frequency;
    if (settings->get<FrontendSettings::Tag::dtmb>().transmissionMode == FrontendDtmbTransmissionMode::UNDEFINED) {
        dtmbSetting.transmissionMode = FrontendDtmbTransmissionMode::AUTO;
    }
    p_fe_params->u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;

    //bandwidth
    if (settings->get<FrontendSettings::Tag::dtmb>().bandwidth == FrontendDtmbBandwidth::UNDEFINED) {
        dtmbSetting.bandwidth = FrontendDtmbBandwidth::AUTO;
    }
    p_fe_params->u.ofdm.bandwidth =
        (fe_bandwidth_t)(getFeDtmbBandwithType(settings->get<FrontendSettings::Tag::dtmb>().bandwidth));

    //modulation
    if (settings->get<FrontendSettings::Tag::dtmb>().modulation == FrontendDtmbModulation::UNDEFINED) {
        dtmbSetting.modulation = FrontendDtmbModulation::AUTO;
    }
    p_fe_params->u.ofdm.constellation =
        (fe_modulation_t)(getFeModulationType(settings->get<FrontendSettings::Tag::dtmb>().modulation));

    //codeRate
    if (settings->get<FrontendSettings::Tag::dtmb>().codeRate == FrontendDtmbCodeRate::UNDEFINED) {
        p_fe_params->u.ofdm.code_rate_HP = FEC_NONE;
        p_fe_params->u.ofdm.code_rate_LP = FEC_AUTO;
    } else {
        //linux dvb not support other types
        p_fe_params->u.ofdm.code_rate_HP = FEC_AUTO;
        p_fe_params->u.ofdm.code_rate_LP = FEC_AUTO;
    }

    //guardInterval
    if (settings->get<FrontendSettings::Tag::dtmb>().guardInterval != FrontendDtmbGuardInterval::UNDEFINED) {
        dtmbSetting.guardInterval = FrontendDtmbGuardInterval::AUTO;
    }
    p_fe_params->u.ofdm.guard_interval = GUARD_INTERVAL_AUTO;

    //interleaveMode -- not support in linux dvb

    settings->set<FrontendSettings::Tag::dtmb>(dtmbSetting);

    return 0;
}

int FrontendDtmbDevice::getFeDeliverySystem(FrontendType type) {
    enum fe_delivery_system fe_system;

    if (type != FrontendType::DTMB) {
        fe_system = SYS_UNDEFINED;
    } else {
        fe_system = SYS_DTMB;
    }
    return (int)(fe_system);
}

int FrontendDtmbDevice::getFeDtmbBandwithType(const FrontendDtmbBandwidth& dtmbBandWidth) {
    int fe_bandwidth_type = BANDWIDTH_8_MHZ;
    switch (dtmbBandWidth) {
        case FrontendDtmbBandwidth::BANDWIDTH_8MHZ:
            fe_bandwidth_type = BANDWIDTH_8_MHZ;
            break;
        case FrontendDtmbBandwidth::BANDWIDTH_6MHZ:
            fe_bandwidth_type = BANDWIDTH_6_MHZ;
            break;
        default:
            fe_bandwidth_type = BANDWIDTH_AUTO;
            break;
    }
    return fe_bandwidth_type;
}

int FrontendDtmbDevice::getFeModulationType(const FrontendDtmbModulation& modulation) {
    int fe_modulation_type = QAM_AUTO;

    switch (modulation) {
        case FrontendDtmbModulation::CONSTELLATION_16QAM:
            fe_modulation_type = QAM_16;
            break;
        case FrontendDtmbModulation::CONSTELLATION_32QAM:
            fe_modulation_type = QAM_32;
            break;
        case FrontendDtmbModulation::CONSTELLATION_64QAM:
            fe_modulation_type = QAM_64;
            break;
        default:
            fe_modulation_type = QAM_AUTO;
            break;
    }
    return fe_modulation_type;
}

}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  //namespace android
}  // namespace aidl
