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

#ifndef ANDROID_HARDWARE_TV_TUNER_V1_1_FRONTEND_DTMB_DEVICE_H_
#define ANDROID_HARDWARE_TV_TUNER_V1_1_FRONTEND_DTMB_DEVICE_H_

#include "FrontendDevice.h"

using namespace std;

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {


class Frontend;

class FrontendDtmbDevice : public FrontendDevice {
public:
    FrontendDtmbDevice(uint32_t hwId, FrontendType type, const sp<Frontend>& context);
    virtual int getFrontendSettings(FrontendSettings *settings, void* fe_params);
    virtual int getFeDeliverySystem(FrontendType type);
    virtual FrontendModulationStatus getFeModulationStatus();
    virtual int getFrontendSettingsExt(V1_1::FrontendSettingsExt1_1 *settingsExt, void* fe_params);

private:
    ~FrontendDtmbDevice();
    int getFeDtmbBandwidthType(const V1_1::FrontendDtmbBandwidth& dtmbBandWidth);
    int getFeModulationType(const V1_1::FrontendDtmbModulation& modulation);
};

}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
}
#endif  // ANDROID_HARDWARE_TV_TUNER_AIDL_FRONTEND_DTMB_DEVICE_H_
