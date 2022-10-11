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
#include "FrontendIsdbtDevice.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

FrontendIsdbtDevice::FrontendIsdbtDevice(uint32_t hwId, FrontendType type, const sp<Frontend>& context)
    : FrontendDevice(hwId, type, context) {
}

FrontendIsdbtDevice::~FrontendIsdbtDevice() {
}

FrontendModulationStatus FrontendIsdbtDevice::getFeModulationStatus() {
    FrontendModulationStatus modulationStatus;
    modulationStatus.isdbt(getFeDevice()->feSettings->isdbt().modulation);
    return modulationStatus;
}

int FrontendIsdbtDevice::getFrontendSettings(FrontendSettings *settings, void * fe_params) {
    struct dvb_frontend_parameters *p_fe_params = (struct dvb_frontend_parameters*)(fe_params);

    if (settings->getDiscriminator() != FrontendSettings::hidl_discriminator::isdbt) {
        return -1;
    }

    p_fe_params->frequency = settings->isdbt().frequency;
    if (settings->isdbt().bandwidth == FrontendIsdbtBandwidth::UNDEFINED) {
        settings->isdbt().bandwidth = FrontendIsdbtBandwidth::AUTO;
    }
    p_fe_params->u.ofdm.bandwidth =
        (fe_bandwidth_t)(getFeBandwithType(settings->isdbt().bandwidth));;
    if (settings->isdbt().modulation == FrontendIsdbtModulation::UNDEFINED) {
        settings->isdbt().modulation = FrontendIsdbtModulation::AUTO;
    }
    p_fe_params->u.ofdm.constellation =
        (fe_modulation_t)(getFeModulationType(settings->isdbt().modulation));
    if (settings->isdbt().guardInterval == FrontendDvbtGuardInterval::UNDEFINED) {
        settings->isdbt().guardInterval = FrontendDvbtGuardInterval::AUTO;
    }
    p_fe_params->u.ofdm.guard_interval =
        (fe_guard_interval_t)(getFeDvbGuardIntervalType(settings->isdbt().guardInterval));
    if (settings->isdbt().coderate == FrontendDvbtCoderate::UNDEFINED) {
        settings->isdbt().coderate = FrontendDvbtCoderate::AUTO;
    }
    if (settings->isdbt().mode == FrontendIsdbtMode::UNDEFINED) {
        settings->isdbt().mode = FrontendIsdbtMode::AUTO;
    }
    p_fe_params->u.ofdm.code_rate_HP = FEC_AUTO;
    p_fe_params->u.ofdm.code_rate_LP = FEC_AUTO;
    p_fe_params->u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;

    return 0;
}

int FrontendIsdbtDevice::getFeDeliverySystem(FrontendType type) {
    enum fe_delivery_system fe_system;

    if (type != FrontendType::ISDBT) {
        fe_system = SYS_UNDEFINED;
    } else {
        fe_system = SYS_ISDBT;
    }

    return (int)(fe_system);
}

int FrontendIsdbtDevice::getFeModulationType(const FrontendIsdbtModulation& type) {
    int fe_modulation_type = QAM_AUTO;

    switch (type) {
        case FrontendIsdbtModulation::MOD_16QAM:
            fe_modulation_type = QAM_16;
            break;
        case FrontendIsdbtModulation::MOD_64QAM:
            fe_modulation_type = QAM_64;
            break;
        case FrontendIsdbtModulation::MOD_QPSK:
            fe_modulation_type = QPSK;
            break;
        default:
            fe_modulation_type = QAM_AUTO;
            break;
    }
    return fe_modulation_type;
}

int FrontendIsdbtDevice::getFeInnerFecTypeFromCodeRate(const FrontendDvbtCoderate& rate) {
    int fec_inner_type = FEC_AUTO;

    switch (rate) {
        case FrontendDvbtCoderate::CODERATE_1_2:
            fec_inner_type = FEC_1_2;
            break;
        case FrontendDvbtCoderate::CODERATE_2_3:
            fec_inner_type = FEC_2_3;
            break;
        case FrontendDvbtCoderate::CODERATE_3_4:
            fec_inner_type = FEC_3_4;
            break;
        case FrontendDvbtCoderate::CODERATE_4_5:
            fec_inner_type = FEC_4_5;
            break;
        case FrontendDvbtCoderate::CODERATE_5_6:
            fec_inner_type = FEC_5_6;
            break;
        case FrontendDvbtCoderate::CODERATE_6_7:
            fec_inner_type = FEC_6_7;
            break;
        case FrontendDvbtCoderate::CODERATE_7_8:
            fec_inner_type = FEC_7_8;
            break;
        case FrontendDvbtCoderate::CODERATE_8_9:
            fec_inner_type = FEC_8_9;
            break;
        case FrontendDvbtCoderate::CODERATE_3_5:
            fec_inner_type = FEC_3_5;
            break;
        default:
            fec_inner_type = FEC_AUTO;
            break;
    }

    return fec_inner_type;
}

int FrontendIsdbtDevice::getFeBandwithType(const FrontendIsdbtBandwidth& bandWidth) {
    int fe_bandwidth_type = BANDWIDTH_AUTO;
    switch (bandWidth) {
        case FrontendIsdbtBandwidth::BANDWIDTH_8MHZ:
            fe_bandwidth_type = BANDWIDTH_8_MHZ;
            break;
        case FrontendIsdbtBandwidth::BANDWIDTH_7MHZ:
            fe_bandwidth_type = BANDWIDTH_7_MHZ;
            break;
        case FrontendIsdbtBandwidth::BANDWIDTH_6MHZ:
            fe_bandwidth_type = BANDWIDTH_6_MHZ;
            break;
        default:
            fe_bandwidth_type = BANDWIDTH_AUTO;
            break;
    }
    return fe_bandwidth_type;
}

int FrontendIsdbtDevice::getFeDvbGuardIntervalType(const FrontendDvbtGuardInterval& type) {
    int fe_guard_interval_type = GUARD_INTERVAL_AUTO;

    switch (type) {
        case FrontendDvbtGuardInterval::INTERVAL_1_32:
            fe_guard_interval_type = GUARD_INTERVAL_1_32;
            break;
        case FrontendDvbtGuardInterval::INTERVAL_1_16:
            fe_guard_interval_type = GUARD_INTERVAL_1_16;
            break;
        case FrontendDvbtGuardInterval::INTERVAL_1_8:
            fe_guard_interval_type = GUARD_INTERVAL_1_8;
            break;
        case FrontendDvbtGuardInterval::INTERVAL_1_4:
            fe_guard_interval_type = GUARD_INTERVAL_1_4;
            break;
        case FrontendDvbtGuardInterval::INTERVAL_1_128:
            fe_guard_interval_type = GUARD_INTERVAL_1_128;
            break;
        case FrontendDvbtGuardInterval::INTERVAL_19_128:
            fe_guard_interval_type = GUARD_INTERVAL_19_128;
            break;
        case FrontendDvbtGuardInterval::INTERVAL_19_256:
            fe_guard_interval_type = GUARD_INTERVAL_19_256;
            break;
        default:
            fe_guard_interval_type = GUARD_INTERVAL_AUTO;
            break;
    }

    return fe_guard_interval_type;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
