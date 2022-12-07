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

#ifndef ANDROID_HARDWARE_TV_TUNER_V1_0_HWFESATE_H_
#define ANDROID_HARDWARE_TV_TUNER_V1_0_HWFESATE_H_

#include <utils/RefBase.h>

using namespace std;

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

class FrontendDevice;

class HwFeState : public RefBase {
  public:
   HwFeState(int hwId);
   int acquire(sp<FrontendDevice> device);
   int acquireForLnb();
   void release(int fd, sp<FrontendDevice> device);
   void releaseFromLnb();

  private:
  int atv_open(void);
  int atv_close(int feId);
  int dtv_open(void);
  int dtv_close(int feId);
   virtual ~HwFeState();
   int hwId;
   int fd;
   bool lnbUsing;
   sp<FrontendDevice> owner;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_TV_TUNER_V1_0_HWFESATE_H_
