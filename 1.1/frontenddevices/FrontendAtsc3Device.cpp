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
#include "FrontendAtsc3Device.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

FrontendAtsc3Device::FrontendAtsc3Device(uint32_t hwId, FrontendType type, const sp<Frontend>& context)
    : FrontendDevice(hwId, type, context) {
}

FrontendAtsc3Device::~FrontendAtsc3Device() {
}

FrontendModulationStatus FrontendAtsc3Device::getFeModulationStatus() {
    FrontendModulationStatus modulationStatus;
    ALOGW("FrontendAtsc3Device: should not get modulationStatus in atsc3 type.");
    modulationStatus.dvbc(FrontendDvbcModulation::UNDEFINED);
    return modulationStatus;
}

int FrontendAtsc3Device::getFrontendSettings(FrontendSettings *settings, void * fe_params) {
    struct dvb_frontend_parameters *p_fe_params = (struct dvb_frontend_parameters*)(fe_params);

    if (settings->getDiscriminator() != FrontendSettings::hidl_discriminator::atsc3) {
        return -1;
    }

    p_fe_params->frequency = settings->atsc3().frequency;

    if (settings->atsc3().bandwidth == FrontendAtsc3Bandwidth::UNDEFINED) {
        settings->atsc3().bandwidth = FrontendAtsc3Bandwidth::AUTO;
    }
    p_fe_params->u.ofdm.bandwidth = getFeAtsc3BandwidthType(settings->atsc3().bandwidth);

    if (settings->atsc3().demodOutputFormat == FrontendAtsc3DemodOutputFormat::UNDEFINED) {
        settings->atsc3().demodOutputFormat = FrontendAtsc3DemodOutputFormat::ATSC3_LINKLAYER_PACKET;
    }
    /*p_fe_params->u.ofdm.demodOutputFormat =
        (fe_demodOutputFormat_t)(getFeAtsc3DemodOutputFormatType(settings->atsc3().demodOutputFormat));
    */

    int len = settings->atsc3().plpSettings.size();
    for (int i = 0; i < len; i++) {
        if (settings->atsc3().plpSettings[i].modulation == FrontendAtsc3Modulation::UNDEFINED) {
            settings->atsc3().plpSettings[i].modulation = FrontendAtsc3Modulation::AUTO;
        }

        if (settings->atsc3().plpSettings[i].interleaveMode == FrontendAtsc3TimeInterleaveMode::UNDEFINED) {
            settings->atsc3().plpSettings[i].interleaveMode = FrontendAtsc3TimeInterleaveMode::AUTO;
        }

        if (settings->atsc3().plpSettings[i].codeRate == FrontendAtsc3CodeRate::UNDEFINED) {
            settings->atsc3().plpSettings[i].codeRate = FrontendAtsc3CodeRate::AUTO;
        }

        if (settings->atsc3().plpSettings[i].fec == FrontendAtsc3Fec::UNDEFINED) {
            settings->atsc3().plpSettings[i].fec = FrontendAtsc3Fec::AUTO;
        }
    }
    return 0;
}

int FrontendAtsc3Device::getFeDeliverySystem(FrontendType type) {
    enum fe_delivery_system fe_system;

    if (type != FrontendType::ATSC3) {
        fe_system = SYS_UNDEFINED;
    } else {
        fe_system = SYS_ATSCMH;
    }
    return (int)(fe_system);
}

fe_bandwidth_t FrontendAtsc3Device::getFeAtsc3BandwidthType(const FrontendAtsc3Bandwidth& atsc3bandwidth) {
    fe_bandwidth_t fe_bandwidth_type = BANDWIDTH_AUTO;
    switch (atsc3bandwidth) {
    case FrontendAtsc3Bandwidth::BANDWIDTH_6MHZ:
        fe_bandwidth_type = BANDWIDTH_6_MHZ;
        break;
    case FrontendAtsc3Bandwidth::BANDWIDTH_7MHZ:
        fe_bandwidth_type = BANDWIDTH_7_MHZ;
        break;
    case FrontendAtsc3Bandwidth::BANDWIDTH_8MHZ:
        fe_bandwidth_type = BANDWIDTH_8_MHZ;
        break;
    default:
        fe_bandwidth_type = BANDWIDTH_AUTO;
        break;
    }
    return fe_bandwidth_type;
}

int FrontendAtsc3Device::getFeAtsc3ModulationType(const FrontendAtsc3Modulation& atsc3Modulation) {
    int feModulationtype = QPSK;
    switch (atsc3Modulation) {
        case FrontendAtsc3Modulation::MOD_QPSK:
            feModulationtype = QPSK;
            break;
        case FrontendAtsc3Modulation::MOD_16QAM:
            feModulationtype = QAM_16;
            break;
        case FrontendAtsc3Modulation::MOD_64QAM:
            feModulationtype = QAM_64;
            break;
        case FrontendAtsc3Modulation::MOD_256QAM:
            feModulationtype = QAM_256;
            break;
        /*case FrontendAtsc3Modulation::MOD_1024QAM:
            feModulationtype = QAM_1024;
            break;
        case FrontendAtsc3Modulation::MOD_4096QAM:
            feModulationtype = QAM_4096;
            break;*/
        default:
            feModulationtype = QPSK;
            break;
    }
    return feModulationtype;
}


int FrontendAtsc3Device::getFeAtsc3DemodOutputFormatType(const FrontendAtsc3DemodOutputFormat& atsc3DemodOutputFormat) {
    int feAtsc3DemodOutputFormatType = 1 << 0;
    switch (atsc3DemodOutputFormat) {
        case FrontendAtsc3DemodOutputFormat::ATSC3_LINKLAYER_PACKET:
            feAtsc3DemodOutputFormatType = 1 << 0;
            break;
        case FrontendAtsc3DemodOutputFormat::BASEBAND_PACKET:
            feAtsc3DemodOutputFormatType = 1 << 1;
            break;
        default:
            feAtsc3DemodOutputFormatType = 1 << 0;
            break;
    }
    return feAtsc3DemodOutputFormatType;
}

int FrontendAtsc3Device::getFeAtsc3TimeInterleaveMode(const FrontendAtsc3TimeInterleaveMode& atsc3InterleaveMode) {
    int feAtsc3InterleaveMode = 1 << 0;
    switch (atsc3InterleaveMode) {
        case FrontendAtsc3TimeInterleaveMode::CTI:
            feAtsc3InterleaveMode = 1 << 1;
            break;
        case FrontendAtsc3TimeInterleaveMode::HTI:
            feAtsc3InterleaveMode = 1 << 2;
            break;
        default:
            feAtsc3InterleaveMode = 1 << 0;
            break;
    }
    return feAtsc3InterleaveMode;
}

int FrontendAtsc3Device::getFeAtsc3CodeRate(const FrontendAtsc3CodeRate& atsc3CodeRate) {
    int feAtsc3CodeRate = 1 << 0;
    switch (atsc3CodeRate) {
        case FrontendAtsc3CodeRate::CODERATE_2_15:
            feAtsc3CodeRate = 1 << 1;
            break;
        case FrontendAtsc3CodeRate::CODERATE_3_15 :
            feAtsc3CodeRate = 1 << 2;
            break;
        case FrontendAtsc3CodeRate::CODERATE_4_15 :
            feAtsc3CodeRate = 1 << 3;
            break;
        case FrontendAtsc3CodeRate::CODERATE_5_15 :
            feAtsc3CodeRate = 1 << 4;
            break;
        case FrontendAtsc3CodeRate::CODERATE_6_15 :
            feAtsc3CodeRate = 1 << 5;
            break;
        case FrontendAtsc3CodeRate::CODERATE_7_15 :
            feAtsc3CodeRate = 1 << 6;
            break;
        case FrontendAtsc3CodeRate::CODERATE_8_15 :
            feAtsc3CodeRate = 1 << 7;
            break;
        case FrontendAtsc3CodeRate::CODERATE_9_15 :
            feAtsc3CodeRate = 1 << 8;
            break;
        case FrontendAtsc3CodeRate::CODERATE_10_15 :
            feAtsc3CodeRate = 1 << 9;
            break;
        case FrontendAtsc3CodeRate::CODERATE_11_15 :
            feAtsc3CodeRate = 1 << 10;
            break;
        case FrontendAtsc3CodeRate::CODERATE_12_15 :
            feAtsc3CodeRate = 1 << 11;
            break;
        case FrontendAtsc3CodeRate::CODERATE_13_15 :
            feAtsc3CodeRate = 1 << 12;
            break;
        default:
            feAtsc3CodeRate = 1 << 0;
            break;
    }
    return feAtsc3CodeRate;
}

int FrontendAtsc3Device::getFeAtsc3FecType(const FrontendAtsc3Fec& atsc3Fec) {
    int feAtsc3Fec = 1 << 0;
    switch (atsc3Fec) {
        case FrontendAtsc3Fec::BCH_LDPC_16K:
            feAtsc3Fec = 1 << 1;
            break;
        case FrontendAtsc3Fec::BCH_LDPC_64K:
            feAtsc3Fec = 1 << 2;
            break;
        case FrontendAtsc3Fec::CRC_LDPC_16K:
            feAtsc3Fec = 1 << 3;
            break;
        case FrontendAtsc3Fec::CRC_LDPC_64K:
            feAtsc3Fec = 1 << 4;
            break;
        case FrontendAtsc3Fec::LDPC_16K:
            feAtsc3Fec = 1 << 5;
            break;
        case FrontendAtsc3Fec::LDPC_64K:
            feAtsc3Fec = 1 << 6;
            break;
        default:
            feAtsc3Fec = 1 << 0;
            break;
    }
    return feAtsc3Fec;
}


}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android

