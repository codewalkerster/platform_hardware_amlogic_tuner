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
#include "FrontendAnalogDevice.h"
#include "linux/videodev2.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

FrontendAnalogDevice::FrontendAnalogDevice(uint32_t hwId, FrontendType type, const sp<Frontend>& context)
    : FrontendDevice(hwId, type, context) {
}

FrontendAnalogDevice::~FrontendAnalogDevice() {
}

FrontendModulationStatus FrontendAnalogDevice::getFeModulationStatus() {
    FrontendModulationStatus modulationStatus;
    ALOGW("FrontendDvbtDevice: should not get modulationStatus in analog type.");
    modulationStatus.dvbc(FrontendDvbcModulation::UNDEFINED);
    return modulationStatus;
}

int FrontendAnalogDevice::getFrontendSettings(FrontendSettings *settings, void* fe_params) {
    struct dvb_frontend_parameters *p_fe_params = (struct dvb_frontend_parameters*)(fe_params);
    unsigned long tmpTVidStd = 0;
    unsigned long tmpAudStd = 0;

    if (settings->getDiscriminator() != FrontendSettings::hidl_discriminator::analog) {
        return -1;
    }

    p_fe_params->frequency = settings->analog().frequency;
    if (settings->analog().type >= FrontendAnalogType::UNDEFINED
       && settings->analog().type <= FrontendAnalogType::PAL_60) {
        tmpTVidStd |= V4L2_COLOR_STD_PAL;
    } else if (settings->analog().type == FrontendAnalogType::NTSC
              || settings->analog().type == FrontendAnalogType::NTSC_443) {
        tmpTVidStd |= V4L2_COLOR_STD_NTSC;
    } else if (settings->analog().type == FrontendAnalogType::SECAM) {
        tmpTVidStd |= V4L2_COLOR_STD_SECAM;
    }
    if (settings->analog().sifStandard >= FrontendAnalogSifStandard::BG
    && settings->analog().sifStandard <= FrontendAnalogSifStandard::BG_NICAM) {
        tmpAudStd |= V4L2_STD_PAL_BG;
    } else if (settings->analog().sifStandard == FrontendAnalogSifStandard::I
          || settings->analog().sifStandard == FrontendAnalogSifStandard::I_NICAM) {
        tmpAudStd |= V4L2_STD_PAL_I;
    } else if (settings->analog().sifStandard >= FrontendAnalogSifStandard::DK
          && settings->analog().sifStandard <= FrontendAnalogSifStandard::DK_NICAM) {
        tmpAudStd |= V4L2_STD_PAL_DK;
    } else if (settings->analog().sifStandard == FrontendAnalogSifStandard::L
          ||settings->analog().sifStandard == FrontendAnalogSifStandard::L_NICAM
          ||settings->analog().sifStandard == FrontendAnalogSifStandard::L_PRIME) {
        tmpAudStd |= V4L2_STD_SECAM_L;
    } else if (settings->analog().sifStandard >= FrontendAnalogSifStandard::M
          && settings->analog().sifStandard <= FrontendAnalogSifStandard::M_EIAJ) {
        tmpAudStd |= V4L2_STD_NTSC_M;
    } else {
        tmpAudStd |= V4L2_STD_PAL_BG;
    }

    if (settings->analog().type == FrontendAnalogType::UNDEFINED) {
        settings->analog().type = FrontendAnalogType::AUTO;
    }
    if (settings->analog().sifStandard == FrontendAnalogSifStandard::UNDEFINED) {
        settings->analog().sifStandard = FrontendAnalogSifStandard::AUTO;
    }

    //p_fe_params->u.analog.std = tmpTVidStd;
    //p_fe_params->u.analog.audmode = tmpAudStd;
    //p_fe_params->u.analog.afc_range = 0;
    //p_fe_params->u.analog.soundsys = 0xFF;
    return 0;
}

int FrontendAnalogDevice::getFeDeliverySystem(FrontendType type) {
    #if 0
    enum fe_delivery_system fe_system;

    if (type != FrontendType::ANALOG) {
        fe_system = SYS_UNDEFINED;
    } else {
        fe_system = SYS_UNDEFINED;
    }
    #endif

    return SYS_UNDEFINED;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
