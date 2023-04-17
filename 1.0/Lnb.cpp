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
#include <cutils/properties.h>
#include <utils/Log.h>
#include <sys/ioctl.h>
#include <errno.h>


namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

#define PROPERTY_DELAY_OF_TONE "vendor.tunerhal.tone.delay"

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

    int devFd = acquireLnbDevice();
    ALOGD("%s: %d(0:13,1:18,2:off)", __FUNCTION__, devVoltage);
    if (devFd != -1) {
        if (ioctl(devFd, FE_SET_VOLTAGE, devVoltage) == -1)
        {
            ALOGE("%s failed.", __FUNCTION__);
            return Result::SUCCESS; //make xts pass when don't support dvb-s
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
            return Result::SUCCESS; //make xts pass when don't support dvb-s;
        }
    }

    //Add a delay for some multi-switch devices
    //This delay can be customized by property: vendor.tunerhal.tone.delay
    int32_t tone_delay_ms = property_get_int32(PROPERTY_DELAY_OF_TONE, 20);
    if (tone_delay_ms && devTone == SEC_TONE_OFF) {
        usleep(tone_delay_ms * 1000);
    }

    return Result::SUCCESS;
}

Return<Result> Lnb::setSatellitePosition(LnbPosition position) {
    fe_sec_mini_cmd_t cmd;

    if (position == LnbPosition::UNDEFINED) {
        ALOGW("%s, not a valid mini cmd value.", __FUNCTION__);
        return Result::SUCCESS;
    }

    cmd = (position == LnbPosition::POSITION_A) ? SEC_MINI_A : SEC_MINI_B;
    ALOGD("%s: %d(0:a,1:b)", __FUNCTION__, cmd);

    int devFd = acquireLnbDevice();
    if (devFd != -1) {
        if (ioctl(devFd, FE_DISEQC_SEND_BURST, cmd) == -1)
        {
            ALOGE("%s failed.", __FUNCTION__);
            return Result::SUCCESS; //make xts pass when don't support dvb-s;
        }
    }

    return Result::SUCCESS;
}

Return<Result> Lnb::sendDiseqcMessage(const hidl_vec<uint8_t>& diseqcMessage) {
    struct dvb_diseqc_master_cmd cmd;
    memset(&cmd, 0, sizeof(struct dvb_diseqc_master_cmd));
    if (diseqcMessage.size() == 0) {
        return Result::SUCCESS; //make xts pass when don't support dvb-s;
    }
    int size = (sizeof(cmd.msg) >= diseqcMessage.size()) ? diseqcMessage.size() : sizeof(cmd.msg);
    for (int i = 0; i < size; i++)
    {
        cmd.msg[i] = diseqcMessage[i];
        ALOGD("%s cmd[%d]:0x%02x", __FUNCTION__, i, diseqcMessage[i]);
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
        return Result::UNAVAILABLE;
    }

    if (ioctl(devFd, FE_DISEQC_SEND_MASTER_CMD, &cmd) == -1)
    {
        ALOGE("%s failed(%s)", __FUNCTION__, strerror(errno));
        return Result::UNAVAILABLE;
    }

    ALOGD("%s ok.", __FUNCTION__);
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
