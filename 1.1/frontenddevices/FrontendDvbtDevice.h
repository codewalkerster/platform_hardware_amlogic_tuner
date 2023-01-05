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

#ifndef ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_DVBT_DEVICE_H_
#define ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_DVBT_DEVICE_H_

#include "FrontendDevice.h"

using namespace std;

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

class Frontend;

class FrontendDvbtDevice : public FrontendDevice {
public:
    FrontendDvbtDevice(uint32_t hwId, FrontendType type, const sp<Frontend>& context);
    virtual int getFrontendSettings(FrontendSettings *settings, void* fe_params);
    virtual int getFeDeliverySystem(FrontendType type);
    virtual FrontendModulationStatus getFeModulationStatus();

private:
    ~FrontendDvbtDevice();
    int getFeModulationType(const FrontendDvbtConstellation& type);
    int getFeInnerFecTypeFromCodeRate(const FrontendDvbtCoderate& rate);
    int getFeDvbBandwithType(const FrontendDvbtBandwidth& dvbBandWidth);
    int getFeDvbGuardIntervalType(const FrontendDvbtGuardInterval& type);
    int getFeDvbHierarchy(const FrontendDvbtHierarchy& val);
    int getFeDvbTransmissionMode(const FrontendDvbtTransmissionMode& mode);
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_DVBT_DEVICE_H_
