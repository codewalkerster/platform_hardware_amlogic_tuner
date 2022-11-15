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
#include "FrontendDvbsDevice.h"

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {


FrontendDvbsDevice::FrontendDvbsDevice(uint32_t hwId, FrontendType type, const sp<Frontend>& context)
    : FrontendDevice(hwId, type, context) {
}

FrontendDvbsDevice::~FrontendDvbsDevice() {
}

FrontendModulationStatus FrontendDvbsDevice::getFeModulationStatus() {
    FrontendModulationStatus modulationStatus;
    modulationStatus.set<FrontendModulationStatus::Tag::dvbs>(getFeDevice()->feSettings->get<FrontendSettings::Tag::dvbs>().modulation);
    return modulationStatus;
}

int FrontendDvbsDevice::getFrontendSettings(FrontendSettings *settings, void * fe_params) {
    struct dvb_frontend_parameters *p_fe_params = (struct dvb_frontend_parameters*)(fe_params);

    if (settings->getTag() != FrontendSettings::Tag::dvbs) {
        return -1;
    }

    FrontendDvbsSettings dvbsSetting;
    p_fe_params->frequency = (settings->get<FrontendSettings::Tag::dvbs>().frequency) / 1000;
    p_fe_params->u.qpsk.symbol_rate = settings->get<FrontendSettings::Tag::dvbs>().symbolRate;
    if (settings->get<FrontendSettings::Tag::dvbs>().modulation == FrontendDvbsModulation::UNDEFINED) {
        dvbsSetting.modulation = FrontendDvbsModulation::MOD_QPSK;
        settings->set<FrontendSettings::Tag::dvbs>(dvbsSetting);
    }
    if (settings->get<FrontendSettings::Tag::dvbs>().coderate.fec == FrontendInnerFec::FEC_UNDEFINED) {
        dvbsSetting.coderate.fec = FrontendInnerFec::AUTO;
        settings->set<FrontendSettings::Tag::dvbs>(dvbsSetting);
    }
    p_fe_params->u.qpsk.fec_inner =
        (fe_code_rate_t)(getFeInnerFecType(settings->get<FrontendSettings::Tag::dvbs>().coderate.fec));
    //TODO: which usage of other parameters

    return 0;
}

int FrontendDvbsDevice::getFeDeliverySystem(FrontendType type) {
    enum fe_delivery_system feSystem;

    if (type != FrontendType::DVBS) {
        feSystem = SYS_UNDEFINED;
    } else {
        if (getFeDevice()->feSettings->get<FrontendSettings::Tag::dvbs>().standard == FrontendDvbsStandard::S2
            || getFeDevice()->feSettings->get<FrontendSettings::Tag::dvbs>().standard == FrontendDvbsStandard::S2X) {
            feSystem = SYS_DVBS2;
        } else {
            feSystem = SYS_DVBS;
        }
    }

    return (int)(feSystem);
}

int FrontendDvbsDevice::getFeModulationType(const FrontendDvbsModulation& modulation) {
    int feModulationtype = QPSK;

    switch (modulation) {
        case FrontendDvbsModulation::MOD_QPSK:
            feModulationtype = QPSK;
            break;
        case FrontendDvbsModulation::MOD_8PSK:
            feModulationtype = PSK_8;
            break;
        case FrontendDvbsModulation::MOD_16APSK:
            feModulationtype = APSK_16;
            break;
        case FrontendDvbsModulation::MOD_32APSK:
            feModulationtype = APSK_32;
            break;
        default:
            feModulationtype = QPSK;
            break;
    }

    return feModulationtype;
}

int FrontendDvbsDevice::getFeInnerFecType(const FrontendInnerFec & fec) {
    int feCodeRate = FEC_AUTO;

    switch (fec) {
        case FrontendInnerFec::FEC_1_2:
            feCodeRate = FEC_1_2;
            break;
        case FrontendInnerFec::FEC_2_3:
            feCodeRate = FEC_2_3;
            break;
        case FrontendInnerFec::FEC_3_4:
            feCodeRate = FEC_3_4;
            break;
        case FrontendInnerFec::FEC_4_5:
            feCodeRate = FEC_4_5;
            break;
        case FrontendInnerFec::FEC_5_6:
            feCodeRate = FEC_5_6;
            break;
        case FrontendInnerFec::FEC_6_7:
            feCodeRate = FEC_6_7;
            break;
        case FrontendInnerFec::FEC_7_8:
            feCodeRate = FEC_7_8;
            break;
        case FrontendInnerFec::FEC_8_9:
            feCodeRate = FEC_8_9;
            break;
        case FrontendInnerFec::FEC_3_5:
            feCodeRate = FEC_3_5;
            break;
        case FrontendInnerFec::FEC_9_10:
            feCodeRate = FEC_9_10;
            break;
        case FrontendInnerFec::FEC_2_5:
            feCodeRate = FEC_2_5;
            break;
        default:
            feCodeRate = FEC_AUTO;
            break;
    }

    return feCodeRate;
}

}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}//  namespace aidl
