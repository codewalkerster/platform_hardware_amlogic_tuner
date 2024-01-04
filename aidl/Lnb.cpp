/*
 * Copyright 2021 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "droidlogic_lnb"

#include <aidl/android/hardware/tv/tuner/Result.h>
#include "Lnb.h"
#include <utils/Log.h>
#include "FrontendDevice.h"
#include <cutils/properties.h>
#include <sys/ioctl.h>
#include <errno.h>

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

#define PROPERTY_DELAY_OF_TONE "vendor.tunerhal.tone.delay"

Lnb::Lnb() {}

Lnb::Lnb(int id, const sp<HwFeState>& hwFe, const char* name) {
    mId = id;
    mHw = hwFe;
    this->name = name;
}

Lnb::~Lnb() {}

int Lnb::acquireLnbDevice() {
    if (mHw == nullptr)
        return -1;
    return mHw->acquireForLnb();
}

bool Lnb::prepareFeSystem(int fd) {
    if (fd != -1) {
        struct dtv_property p =
            {.cmd = DTV_DELIVERY_SYSTEM,
             .u.data = SYS_DVBS
            };
        struct dtv_properties props = {.num = 1, .props = &p};
        if (ioctl(fd, FE_SET_PROPERTY, &props) != -1) {
            return true;
        } else {
            ALOGD("%s fe set dvbs failed for %s ", __FUNCTION__, strerror(errno));
        }
    }
    return false;
}

::ndk::ScopedAStatus Lnb::setCallback(const std::shared_ptr<ILnbCallback>& in_callback) {
    ALOGV("%s", __FUNCTION__);

    mCallback = in_callback;

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Lnb::setVoltage(LnbVoltage in_voltage) {
    ALOGV("%s", __FUNCTION__);
    fe_sec_voltage_t devVoltage;
    switch (in_voltage) {
        case LnbVoltage::VOLTAGE_5V:
        case LnbVoltage::VOLTAGE_11V:
        case LnbVoltage::VOLTAGE_12V:
        case LnbVoltage::VOLTAGE_13V:
        case LnbVoltage::VOLTAGE_14V:
            devVoltage = SEC_VOLTAGE_13;
            break;
        case LnbVoltage::VOLTAGE_15V:
        case LnbVoltage::VOLTAGE_18V:
        case LnbVoltage::VOLTAGE_19V:
            devVoltage = SEC_VOLTAGE_18;
            break;
        case LnbVoltage::NONE:
            devVoltage = SEC_VOLTAGE_OFF;
            break;
    }

    int devFd = acquireLnbDevice();
    ALOGD("%s: %d(0:13,1:18,2:off)", __FUNCTION__, devVoltage);
    if (devFd != -1) {
        if (ioctl(devFd, FE_SET_VOLTAGE, devVoltage) == -1)
        {
            ALOGE("%s failed.", __FUNCTION__);
            return ::ndk::ScopedAStatus::fromServiceSpecificError(
            static_cast<int32_t>(Result::UNAVAILABLE));
        }
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Lnb::setTone(LnbTone in_tone) {
    ALOGV("%s", __FUNCTION__);
    fe_sec_tone_mode_t devTone;

    devTone = (in_tone == LnbTone::CONTINUOUS) ? SEC_TONE_ON : SEC_TONE_OFF;
    ALOGD("%s: %d(0:on,1:off)", __FUNCTION__, devTone);

    int devFd = acquireLnbDevice();
    if (devFd != -1) {
        if (ioctl(devFd, FE_SET_TONE, devTone) == -1)
        {
            ALOGE("%s failed.", __FUNCTION__);
            return ::ndk::ScopedAStatus::fromServiceSpecificError(
            static_cast<int32_t>(Result::UNAVAILABLE));
        }
    }

    //Add a delay for some multi-switch devices
    //This delay can be customized by property: vendor.tunerhal.tone.delay
    int32_t tone_delay_ms = property_get_int32(PROPERTY_DELAY_OF_TONE, 20);
    if (tone_delay_ms && devTone == SEC_TONE_OFF) {
        usleep(tone_delay_ms * 1000);
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Lnb::setSatellitePosition(LnbPosition in_position) {
    ALOGV("%s", __FUNCTION__);
    fe_sec_mini_cmd_t cmd;

    if (in_position == LnbPosition::UNDEFINED) {
        ALOGW("%s, not a valid mini cmd value.", __FUNCTION__);
        return ::ndk::ScopedAStatus::ok();
    }

    cmd = (in_position == LnbPosition::POSITION_A) ? SEC_MINI_A : SEC_MINI_B;
    ALOGD("%s: %d(0:a,1:b)", __FUNCTION__, cmd);

    int devFd = acquireLnbDevice();
    if (devFd != -1) {
        if (ioctl(devFd, FE_DISEQC_SEND_BURST, cmd) == -1)
        {
            ALOGE("%s failed.", __FUNCTION__);
            return ::ndk::ScopedAStatus::fromServiceSpecificError(
            static_cast<int32_t>(Result::UNAVAILABLE));
        }
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Lnb::sendDiseqcMessage(const std::vector<uint8_t>& in_diseqcMessage) {
    ALOGV("%s", __FUNCTION__);
    struct dvb_diseqc_master_cmd cmd;
    memset(&cmd, 0, sizeof(struct dvb_diseqc_master_cmd));
    if (in_diseqcMessage.size() == 0) {
        return ::ndk::ScopedAStatus::ok();
    }
    int size = (sizeof(cmd.msg) >= in_diseqcMessage.size()) ? in_diseqcMessage.size() : sizeof(cmd.msg);
    for (int i = 0; i < size; i++)
    {
        cmd.msg[i] = in_diseqcMessage[i];
        ALOGD("%s cmd[%d]:0x%02x", __FUNCTION__, i, in_diseqcMessage[i]);
    }

    if (cmd.msg[0] == 0x70 && size > 4) {
        //en50607- ODU_CHANNEL_CHANGE : 70 d1 d2 d3
        cmd.msg_len = 4;
    } else if (cmd.msg[0] == 0x7A || cmd.msg[0] == 0x7B || cmd.msg[0] == 0x7C) {
        //en50607-
        //ODU_UB_avail : 0x7A
        //ODU_UB_PIN : 0x7B
        //ODU_UB_inuse : 0x7C
        cmd.msg_len = 1;
    } else if ((cmd.msg[0] == 0x7D || cmd.msg[0] == 0x7E) && size > 2) {
        //en50607- ODU_UB_freq : 0x7d d1
        //en50607- ODU_UB_switches : 0x7e d1
        cmd.msg_len = 2;
    } else {
        cmd.msg_len = size;
    }

    int devFd = acquireLnbDevice();
    ALOGD("%s check delivery system.", __FUNCTION__);
    if (devFd == -1 || !prepareFeSystem(devFd)) {
        if (devFd == -1)
            ALOGE("%s failed for no device.", __FUNCTION__);
        else
            ALOGE("%s failed for set dvbs failed.", __FUNCTION__);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
            static_cast<int32_t>(Result::UNAVAILABLE));
    }

    if (ioctl(devFd, FE_DISEQC_SEND_MASTER_CMD, &cmd) == -1)
    {
        ALOGE("%s failed(%s)", __FUNCTION__, strerror(errno));
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
            static_cast<int32_t>(Result::UNAVAILABLE));
    }

    if (mCallback != nullptr) {
        // The correct implementation should be to return the response from the
        // device via onDiseqcMessage(). The below implementation is only to enable
        // testing for LnbCallbacks.
        ALOGV("[aidl] %s - this is for test purpose only, and must be replaced!", __FUNCTION__);
        mCallback->onDiseqcMessage(in_diseqcMessage);
    }

    ALOGD("%s ok.", __FUNCTION__);


    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Lnb::close() {
    ALOGV("%s", __FUNCTION__);
    if (mHw != nullptr) {
        mHw->releaseFromLnb();
    }

    return ::ndk::ScopedAStatus::ok();
}

int Lnb::getId() {
    return mId;
}

}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
