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
#include "FrontendAtscDevice.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

FrontendAtscDevice::FrontendAtscDevice(uint32_t hwId, FrontendType type, const sp<Frontend>& context)
    : FrontendDevice(hwId, type, context) {
}

FrontendAtscDevice::~FrontendAtscDevice() {
}

FrontendModulationStatus FrontendAtscDevice::getFeModulationStatus() {
    FrontendModulationStatus modulationStatus;
    ALOGW("FrontendDvbtDevice: should not get modulationStatus in atsc type.");
    modulationStatus.dvbc(FrontendDvbcModulation::UNDEFINED);
    return modulationStatus;
}

int FrontendAtscDevice::getFrontendSettings(FrontendSettings *settings, void * fe_params) {
    struct dvb_frontend_parameters *p_fe_params = (struct dvb_frontend_parameters*)(fe_params);

    if (settings->getDiscriminator() != FrontendSettings::hidl_discriminator::atsc) {
        return -1;
    }

    p_fe_params->frequency = settings->atsc().frequency;
    switch (settings->atsc().modulation) {
        case FrontendAtscModulation::MOD_8VSB:
            p_fe_params->u.vsb.modulation = VSB_8;
            break;
        case FrontendAtscModulation::MOD_16VSB:
            p_fe_params->u.vsb.modulation = VSB_16;
            break;
        default:
            settings->atsc().modulation = FrontendAtscModulation::MOD_8VSB;
            p_fe_params->u.vsb.modulation = VSB_8;
            break;
    }

    return 0;
}

int FrontendAtscDevice::getFeDeliverySystem(FrontendType type) {
    enum fe_delivery_system fe_system;

    if (type != FrontendType::ATSC) {
        fe_system = SYS_UNDEFINED;
    } else {
        fe_system = SYS_ATSC;
    }

    return (int)(fe_system);
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
