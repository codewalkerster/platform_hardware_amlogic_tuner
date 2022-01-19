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

#define LOG_TAG "droidlogic_lnb"

#include "Lnb.h"
#include "FrontendDevice.h"
#include <utils/Log.h>
#include <sys/ioctl.h>

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

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

Return<Result> Lnb::setCallback(const sp<ILnbCallback>& callback) {
    ALOGV("%s", __FUNCTION__);
    //hardware diseqc version < 2.0, not support diseqc event
    return Result::SUCCESS;
}

Return<Result> Lnb::setVoltage(LnbVoltage voltage) {
    fe_sec_voltage_t devVoltage;

    switch (voltage) {
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

    ALOGD("%s: %d(0:13,1:18,2:off)", __FUNCTION__, devVoltage);
    int devFd = acquireLnbDevice();
    if (devFd != -1) {
        if (ioctl(devFd, FE_SET_VOLTAGE, devVoltage) == -1)
        {
            ALOGE("%s failed.", __FUNCTION__);
            return Result::UNAVAILABLE;
        }
    }
    return Result::SUCCESS;
}

Return<Result> Lnb::setTone(LnbTone tone) {
    fe_sec_tone_mode_t devTone;

    devTone = (tone == LnbTone::CONTINUOUS) ? SEC_TONE_ON : SEC_TONE_OFF;
    ALOGD("%s: %d(0:on,1:off)", __FUNCTION__, devTone);

    int devFd = acquireLnbDevice();
    if (devFd != -1) {
        if (ioctl(devFd, FE_SET_TONE, devTone) == -1)
        {
            ALOGE("%s failed.", __FUNCTION__);
            return Result::UNAVAILABLE;
        }
    }

    return Result::SUCCESS;
}

Return<Result> Lnb::setSatellitePosition(LnbPosition position) {
    fe_sec_mini_cmd_t cmd;

    if (position == LnbPosition::UNDEFINED) {
        ALOGW("%s, not a valid mini cmd value.", __FUNCTION__);
        return Result::UNAVAILABLE;
    }

    cmd = (position == LnbPosition::POSITION_A) ? SEC_MINI_A : SEC_MINI_B;
    ALOGD("%s: %d(0:a,1:b)", __FUNCTION__, cmd);

    int devFd = acquireLnbDevice();
    if (devFd != -1) {
        if (ioctl(devFd, FE_DISEQC_SEND_BURST, cmd) == -1)
        {
            ALOGE("%s failed.", __FUNCTION__);
            return Result::UNAVAILABLE;
        }
    }

    return Result::SUCCESS;
}

Return<Result> Lnb::sendDiseqcMessage(const hidl_vec<uint8_t>& diseqcMessage) {
    struct dvb_diseqc_master_cmd cmd;
    memset(&cmd, 0, sizeof(struct dvb_diseqc_master_cmd));

    for (int i = 0; i < diseqcMessage.size(); i++)
    {
        cmd.msg[i] = diseqcMessage[i];
        ALOGD("%s cmd[%d]:%u", __FUNCTION__, i, diseqcMessage[i]);
    }

    cmd.msg_len = diseqcMessage.size();

    int devFd = acquireLnbDevice();
    if (ioctl(devFd, FE_DISEQC_SEND_MASTER_CMD, &cmd) == -1)
    {
        ALOGE("%s failed.", __FUNCTION__);
        return Result::UNAVAILABLE;
    }

    return Result::SUCCESS;
}

Return<Result> Lnb::close() {
    ALOGD("%s", __FUNCTION__);

    if (mHw != nullptr) {
        mHw->releaseFromLnb();
    }

    return Result::SUCCESS;
}

int Lnb::getId() {
    return mId;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
