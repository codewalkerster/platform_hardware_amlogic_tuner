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
#include "FrontendDvbcDevice.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

FrontendDvbcDevice::FrontendDvbcDevice(uint32_t hwId, FrontendType type, const sp<Frontend>& context)
    : FrontendDevice(hwId, type, context) {
}

FrontendDvbcDevice::~FrontendDvbcDevice() {
}

FrontendModulationStatus FrontendDvbcDevice::getFeModulationStatus() {
    FrontendModulationStatus modulationStatus;
    modulationStatus.dvbc(getFeDevice()->feSettings->dvbc().modulation);
    return modulationStatus;
}

int FrontendDvbcDevice::getFrontendSettings(FrontendSettings *settings, void * fe_params) {
    struct dvb_frontend_parameters *p_fe_params = (struct dvb_frontend_parameters*)(fe_params);

    if (settings->getDiscriminator() != FrontendSettings::hidl_discriminator::dvbc) {
        return -1;
    }

    p_fe_params->frequency = settings->dvbc().frequency;
    p_fe_params->u.qam.symbol_rate = settings->dvbc().symbolRate;
    if (settings->dvbc().modulation == FrontendDvbcModulation::UNDEFINED) {
        settings->dvbc().modulation = FrontendDvbcModulation::AUTO;
    }
    p_fe_params->u.qam.modulation =
        (fe_modulation_t)(getFeModulationType(settings->dvbc().modulation));
    if (settings->dvbc().fec == FrontendInnerFec::FEC_UNDEFINED) {
        settings->dvbc().fec = FrontendInnerFec::AUTO;
    }
    p_fe_params->u.qam.fec_inner =
        (fe_code_rate_t)(getFeInnerFecType(settings->dvbc().fec));
    if (settings->dvbc().outerFec == FrontendDvbcOuterFec::UNDEFINED) {
        settings->dvbc().outerFec = FrontendDvbcOuterFec::OUTER_FEC_NONE;
    }
    if (settings->dvbc().annex == FrontendDvbcAnnex::UNDEFINED) {
        settings->dvbc().annex = FrontendDvbcAnnex::A;
    }
    if (settings->dvbc().spectralInversion == FrontendDvbcSpectralInversion::UNDEFINED) {
        settings->dvbc().spectralInversion = FrontendDvbcSpectralInversion::NORMAL;
    }

    return 0;
}

int FrontendDvbcDevice::getFeDeliverySystem(FrontendType type) {
    enum fe_delivery_system feSystem;

    if (type != FrontendType::DVBC) {
        feSystem = SYS_UNDEFINED;
    } else {
        switch (getFeDevice()->feSettings->dvbc().annex) {
            case FrontendDvbcAnnex::B:
                feSystem = SYS_DVBC_ANNEX_B;
                break;
            case FrontendDvbcAnnex::C:
                feSystem = SYS_DVBC_ANNEX_C;
                break;
            default:
                feSystem = SYS_DVBC_ANNEX_A;
                break;
        }
    }

    return (int)(feSystem);
}

int FrontendDvbcDevice::getFeModulationType(const FrontendDvbcModulation& modulation) {
    int feModulationtype = QAM_AUTO;

    switch (modulation) {
        case FrontendDvbcModulation::MOD_16QAM:
            feModulationtype = QAM_16;
            break;
        case FrontendDvbcModulation::MOD_32QAM:
            feModulationtype = QAM_32;
            break;
        case FrontendDvbcModulation::MOD_64QAM:
            feModulationtype = QAM_64;
            break;
        case FrontendDvbcModulation::MOD_128QAM:
            feModulationtype = QAM_128;
            break;
        case FrontendDvbcModulation::MOD_256QAM:
            feModulationtype = QAM_256;
            break;
        default:
            feModulationtype = QAM_AUTO;
            break;
    }

    return feModulationtype;
}

int FrontendDvbcDevice::getFeInnerFecType(const FrontendInnerFec & fec) {
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

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
