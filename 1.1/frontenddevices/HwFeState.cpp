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
#include <math.h>
#include <utils/Log.h>
//#include <fcntl.h>
#include "HwFeState.h"
#include "FrontendDevice.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {


HwFeState::HwFeState(int dev_no, int tsInput) {
    this->hwId    = dev_no;
    this->tsInput = tsInput;
    fd = -1;
    owner = nullptr;
    lnbUsing = false;
}

HwFeState::~HwFeState() {
    if (fd != -1) {
      close(fd);
      fd = -1;
      owner = nullptr;
    }
}

int HwFeState::atv_open(void) {
    char fe_name[32];
    int ret;
    int fe_mode = 1;

    snprintf(fe_name, sizeof(fe_name), "/dev/v4l2_frontend");
    if ((ret = open(fe_name, O_RDWR | O_NONBLOCK)) != -1) { // open atv fe
      ALOGE("[HWFE(%d)]:open hw atv tuner with fd(%d)", this->hwId, ret);
      if (ioctl(ret, V4L2_SET_MODE, &fe_mode) == -1) { //enter atv
        ALOGE("ioctl V4L2_SET_MODE failed, error:%s", strerror(errno));
      }
    } else {
      ALOGW("[HWFE(%d)]:open %s failed: %s", this->hwId, fe_name, strerror(errno));
    }
    return ret;
}

int HwFeState::atv_close(int feId) {
    int ret;
    int fe_mode = 0;

    // close atv fe
    if (ioctl(fd, V4L2_SET_MODE, &fe_mode) == -1) { //leave atv
      ALOGE("ioctl V4L2_SET_MODE failed, error:%s", strerror(errno));
    }
    ret = close(fd);
    ALOGI("release atv fe fd(%d) with feId(%d), ret(%d)",
          fd, feId, ret);
    return ret;
}

int HwFeState::dtv_open(void) {
    char fe_name[32];
    int ret;

    snprintf(fe_name, sizeof(fe_name),
             "/dev/dvb0.frontend%d", hwId);
    if ((ret = open(fe_name, O_RDWR | O_NONBLOCK)) != -1) { // open dtv fe
      ALOGD("[HWFE(%d)]:open hw dtv tuner with fd(%d)", this->hwId, ret);
    } else {
      ALOGE("[HWFE(%d)]:open %s failed: %s", this->hwId, fe_name, strerror(errno));
    }
    return ret;
}

int HwFeState::dtv_close(int feId) {
    int ret;

    struct dtv_property p =
    { .cmd = DTV_DELIVERY_SYSTEM,
      .u.data = SYS_ANALOG };
    struct dtv_properties props = { .num = 1, .props = &p };
    if (ioctl(fd, FE_SET_PROPERTY, &props) == -1) {
      ALOGE("[HWFE(%d)]:Set fe system failed: %s", this->hwId, strerror(errno));
    }
    ret = close(fd); // close dtv fe
    ALOGI("release hw fe fd(%d) with feId(%d), ret(%d)",
          fd, feId, ret);
    return ret;
}

int HwFeState::acquire(sp<FrontendDevice> device) {
    if (device == nullptr)
        return -1;

    ALOGI("[HWFE(%d)]:acquire hw frontend from fontendId(%d) lnbUsing = %d fd = %d", this->hwId, device->getFrontendId(), lnbUsing, fd);
    if (fd != -1) {
        if (owner == nullptr &&
            lnbUsing &&
            device->getFeType() != FrontendType::ANALOG) {
            owner = device;
            return fd;
        }
        if (owner->getFrontendId() == device->getFrontendId()) {
            return fd;
        } else {
            if (owner != nullptr) {
                owner->stopByHw();
                if (device->getFeType() == FrontendType::ANALOG) {
                    //to atv from dtv
                    dtv_close(owner->getFrontendId());
                    fd = atv_open();
                } else if (owner->getFeType() == FrontendType::ANALOG) {
                    //to dtv from atv
                    atv_close(owner->getFrontendId());
                    fd = dtv_open();
                }
            }
            owner = device;
        }
    } else {
        if (device->getFeType() == FrontendType::ANALOG)
           fd = atv_open();
        else
           fd = dtv_open();
        if (fd != -1) {
            owner = device;
        }
    }
    return fd;
}

int HwFeState::acquireForLnb() {
    char fe_name[32];

    if (fd != -1) {
        lnbUsing = true;
        return fd;
    } else {
        snprintf(fe_name, sizeof(fe_name),
                 "/dev/dvb0.frontend%d", hwId);
        if ((fd = open(fe_name, O_RDWR | O_NONBLOCK)) != -1) {
            ALOGD("[HWFE(%d)]:open hw tuner with fd(%d)", this->hwId, fd);
            lnbUsing = true;
        } else {
            ALOGW("[HWFE(%d)]:open %s failed: %s", this->hwId, fe_name, strerror(errno));
        }
    }
    return fd;
}

void HwFeState::release(int fd, sp<FrontendDevice> device) {
    if (device == nullptr || fd != this->fd || fd == -1) {
        //should not happen
        return;
    }

    if (this->fd != -1 && owner->getFrontendId() == device->getFrontendId()) {
        if (owner->getFeType() == FrontendType::ANALOG)
          atv_close(owner->getFrontendId());
        else
          dtv_close(owner->getFrontendId());
        this->fd = -1;
        owner = nullptr;
    }
}

void HwFeState::releaseFromLnb() {
    if (fd != -1 && lnbUsing && owner == nullptr) {
        close(fd);
        fd = -1;
        lnbUsing = false;
    }
}

int HwFeState::getTsInput() {
    return this->tsInput;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
