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
#include "FrontendDvbtDevice.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

FrontendDvbtDevice::FrontendDvbtDevice(uint32_t hwId, FrontendType type, const sp<Frontend>& context)
    : FrontendDevice(hwId, type, context) {
}

FrontendDvbtDevice::~FrontendDvbtDevice() {
}

FrontendModulationStatus FrontendDvbtDevice::getFeModulationStatus() {
    FrontendModulationStatus modulationStatus;
    ALOGW("FrontendDvbtDevice: should not get modulationStatus in dvbt type.");
    modulationStatus.dvbc(FrontendDvbcModulation::UNDEFINED);
    return modulationStatus;
}

int FrontendDvbtDevice::getFrontendSettings(FrontendSettings *settings, void * fe_params) {
    struct dvb_frontend_parameters *p_fe_params = (struct dvb_frontend_parameters*)(fe_params);

    if (settings->getDiscriminator() != FrontendSettings::hidl_discriminator::dvbt) {
        return -1;
    }

    p_fe_params->frequency = settings->dvbt().frequency;
    if (settings->dvbt().bandwidth == FrontendDvbtBandwidth::UNDEFINED) {
        settings->dvbt().bandwidth = FrontendDvbtBandwidth::AUTO;
    }
    p_fe_params->u.ofdm.bandwidth =
        (fe_bandwidth_t)(getFeDvbBandwidthType(settings->dvbt().bandwidth));
    if (settings->dvbt().hpCoderate == FrontendDvbtCoderate::UNDEFINED) {
        settings->dvbt().hpCoderate = FrontendDvbtCoderate::AUTO;
    }
    p_fe_params->u.ofdm.code_rate_HP =
        (fe_code_rate_t)(getFeInnerFecTypeFromCodeRate(settings->dvbt().hpCoderate));
    if (settings->dvbt().lpCoderate == FrontendDvbtCoderate::UNDEFINED) {
        settings->dvbt().lpCoderate = FrontendDvbtCoderate::AUTO;
    }
    p_fe_params->u.ofdm.code_rate_LP =
        (fe_code_rate_t)(getFeInnerFecTypeFromCodeRate(settings->dvbt().lpCoderate));;
    if (settings->dvbt().constellation == FrontendDvbtConstellation::UNDEFINED) {
        settings->dvbt().constellation = FrontendDvbtConstellation::AUTO;
    }
    p_fe_params->u.ofdm.constellation =
        (fe_modulation_t)(getFeModulationType(settings->dvbt().constellation));
    if (settings->dvbt().guardInterval == FrontendDvbtGuardInterval::UNDEFINED) {
        settings->dvbt().guardInterval = FrontendDvbtGuardInterval::AUTO;
    }
    p_fe_params->u.ofdm.guard_interval =
        (fe_guard_interval_t)(getFeDvbGuardIntervalType(settings->dvbt().guardInterval));
    if (settings->dvbt().hierarchy == FrontendDvbtHierarchy::UNDEFINED) {
        settings->dvbt().hierarchy = FrontendDvbtHierarchy::AUTO;
    }
    p_fe_params->u.ofdm.hierarchy_information =
        (fe_hierarchy_t)getFeDvbHierarchy(settings->dvbt().hierarchy);
    if (settings->dvbt().transmissionMode == FrontendDvbtTransmissionMode::UNDEFINED) {
        settings->dvbt().transmissionMode = FrontendDvbtTransmissionMode::AUTO;
    }
    p_fe_params->u.ofdm.transmission_mode =
        (fe_transmit_mode_t)getFeDvbTransmissionMode(settings->dvbt().transmissionMode);

    return 0;
}

int FrontendDvbtDevice::getFeDeliverySystem(FrontendType type) {
    enum fe_delivery_system fe_system;

    if (type != FrontendType::DVBT) {
        fe_system = SYS_UNDEFINED;
    } else {
        if (NULL != getFeDevice()->feSettings) {
            if (getFeDevice()->feSettings->getDiscriminator() == FrontendSettings::hidl_discriminator::dvbt) {
                if (getFeDevice()->feSettings->dvbt().standard == FrontendDvbtStandard::T2)
                    fe_system = SYS_DVBT2;
                else
                    fe_system = SYS_DVBT;
            } else {
                fe_system = SYS_DVBT;
                ALOGW("%s: set default fe_system .\n", __FUNCTION__);
            }
        } else {
            fe_system = SYS_DVBT;
            ALOGW("%s: not init feSettings set default fe_system\n", __FUNCTION__);
        }
    }
    return (int)(fe_system);
}

int FrontendDvbtDevice::getFeModulationType(const FrontendDvbtConstellation& type) {
    int fe_modulation_type = QAM_AUTO;

    switch (type) {
        case FrontendDvbtConstellation::CONSTELLATION_16QAM:
            fe_modulation_type = QAM_16;
            break;
        case FrontendDvbtConstellation::CONSTELLATION_64QAM:
            fe_modulation_type = QAM_64;
            break;
        case FrontendDvbtConstellation::CONSTELLATION_256QAM:
            fe_modulation_type = QAM_256;
            break;
        case FrontendDvbtConstellation::CONSTELLATION_QPSK:
            fe_modulation_type = QPSK;
            break;
        default:
            fe_modulation_type = QAM_AUTO;
            break;
    }
    return fe_modulation_type;
}

int FrontendDvbtDevice::getFeInnerFecTypeFromCodeRate(const FrontendDvbtCoderate& rate) {
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

int FrontendDvbtDevice::getFeDvbBandwidthType(const FrontendDvbtBandwidth& dvbBandWidth) {
    int fe_bandwidth_type = BANDWIDTH_8_MHZ;
    switch (dvbBandWidth) {
        case FrontendDvbtBandwidth::BANDWIDTH_8MHZ:
            fe_bandwidth_type = BANDWIDTH_8_MHZ;
            break;
        case FrontendDvbtBandwidth::BANDWIDTH_7MHZ:
            fe_bandwidth_type = BANDWIDTH_7_MHZ;
            break;
        case FrontendDvbtBandwidth::BANDWIDTH_6MHZ:
            fe_bandwidth_type = BANDWIDTH_6_MHZ;
            break;
        case FrontendDvbtBandwidth::BANDWIDTH_5MHZ:
            fe_bandwidth_type = BANDWIDTH_5_MHZ;
            break;
        case FrontendDvbtBandwidth::BANDWIDTH_1_7MHZ:
            fe_bandwidth_type = BANDWIDTH_1_712_MHZ;
            break;
        case FrontendDvbtBandwidth::BANDWIDTH_10MHZ:
            fe_bandwidth_type = BANDWIDTH_10_MHZ;
            break;
        default:
            fe_bandwidth_type = BANDWIDTH_AUTO;
            break;
    }
    return fe_bandwidth_type;
}

int FrontendDvbtDevice::getFeDvbGuardIntervalType(const FrontendDvbtGuardInterval& type) {
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

int FrontendDvbtDevice::getFeDvbHierarchy(const FrontendDvbtHierarchy& val) {
    int hierarchy = HIERARCHY_AUTO;

    switch (val) {
        case FrontendDvbtHierarchy::HIERARCHY_NON_NATIVE:
            hierarchy = HIERARCHY_NONE;
            break;
        case FrontendDvbtHierarchy::HIERARCHY_1_NATIVE:
            hierarchy = HIERARCHY_1;
            break;
        case FrontendDvbtHierarchy::HIERARCHY_2_NATIVE:
            hierarchy = HIERARCHY_2;
            break;
        case FrontendDvbtHierarchy::HIERARCHY_4_NATIVE:
            hierarchy = HIERARCHY_4;
            break;
        default: //TODO: how to map INDEPTH
            hierarchy = HIERARCHY_AUTO;
            break;
    }
    return hierarchy;
}

int FrontendDvbtDevice::getFeDvbTransmissionMode(const FrontendDvbtTransmissionMode& mode) {
    int transmit_mode = TRANSMISSION_MODE_AUTO;

    switch (mode) {
        case FrontendDvbtTransmissionMode::MODE_2K:
            transmit_mode = TRANSMISSION_MODE_2K;
            break;
        case FrontendDvbtTransmissionMode::MODE_8K:
            transmit_mode = TRANSMISSION_MODE_8K;
            break;
        case FrontendDvbtTransmissionMode::MODE_4K:
            transmit_mode = TRANSMISSION_MODE_4K;
            break;
        case FrontendDvbtTransmissionMode::MODE_1K:
            transmit_mode = TRANSMISSION_MODE_1K;
            break;
        case FrontendDvbtTransmissionMode::MODE_16K:
            transmit_mode = TRANSMISSION_MODE_16K;
            break;
        case FrontendDvbtTransmissionMode::MODE_32K:
            transmit_mode = TRANSMISSION_MODE_32K;
            break;
        default:
            transmit_mode = TRANSMISSION_MODE_AUTO;
            break;
    }

    return transmit_mode;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
