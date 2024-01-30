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

#ifndef ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_ATSC3_DEVICE_H_
#define ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_ATSC3_DEVICE_H_

#include "FrontendDevice.h"

using namespace std;

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

class Frontend;

class FrontendAtsc3Device : public FrontendDevice {
public:
    FrontendAtsc3Device(uint32_t hwId, FrontendType type, const sp<Frontend>& context);
    virtual int getFrontendSettings(FrontendSettings *settings, void* fe_params);
    virtual int getFeDeliverySystem(FrontendType type);
    virtual FrontendModulationStatus getFeModulationStatus();

private:
    ~FrontendAtsc3Device();

    fe_bandwidth_t getFeAtsc3BandwidthType(const FrontendAtsc3Bandwidth& atsc3bandwidth);
    int getFeAtsc3ModulationType(const FrontendAtsc3Modulation& atsc3Modulation);
    int getFeAtsc3DemodOutputFormatType(const FrontendAtsc3DemodOutputFormat& atsc3DemodOutputFormat);
    int getFeAtsc3TimeInterleaveMode(const FrontendAtsc3TimeInterleaveMode& atsc3InterleaveMode);
    int getFeAtsc3CodeRate(const FrontendAtsc3CodeRate& atsc3CodeRate);
    int getFeAtsc3FecType(const FrontendAtsc3Fec& atsc3Fec);


};


}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl

#endif  // ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_ATSC3_DEVICE_H_

