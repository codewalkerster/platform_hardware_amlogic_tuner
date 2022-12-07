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

int FrontendAnalogDevice::open_tvafe()
{
    int ret = -1;
    struct tvin_parm_s vdinParam;

    if (fd_vdin == -1) {
        fd_vdin = open("/dev/vdin0", O_RDWR);
        if (fd_vdin == -1)
        {
            ALOGE("!!! Open vdin module, error (%s).\n", strerror(errno));
            return ret;
        }

        vdinParam.port = TVIN_PORT_CVBS3;
        vdinParam.index = 0;

        ret = ioctl(fd_vdin, TVIN_IOC_STOP_DEC);
        if (ret < 0)
        {
            ALOGE("!!! ioctl TVIN_IOC_STOP_DEC, error (%s).\n", strerror(errno));
        }

        ret = ioctl(fd_vdin, TVIN_IOC_OPEN, &vdinParam);
        if (ret < 0)
        {
            ALOGE("!!! ioctl TVIN_IOC_OPEN, error (%s).\n", strerror(errno));
            return ret;
        }
    } else {
            vdinParam.port = TVIN_PORT_CVBS3;
            vdinParam.index = 0;
            ret = ioctl(fd_vdin, TVIN_IOC_OPEN, &vdinParam);
            if (ret < 0)
            {
                ALOGE("!!! ioctl TVIN_IOC_OPEN, error (%s).\n", strerror(errno));
                return ret;
            }
    }

    if (fd_tvafe == -1) {
        fd_tvafe = open("/dev/tvafe0", O_RDWR);
        if (fd_tvafe == -1)
        {
            ALOGE(" [%s] Open tvafe module, error (%s).\n", __FUNCTION__, strerror(errno));
        }
    }

    ALOGI(" [%s]  vdin fd:[%d] , tvafe fd:[%d].\n", __FUNCTION__, fd_vdin, fd_tvafe);
    return ret;
}

int FrontendAnalogDevice::close_tvafe()
{
    int ret = -1;

    if (fd_vdin != -1)
    {
        ret = ioctl(fd_vdin, TVIN_IOC_STOP_DEC);
        if (ret == -1)
        {
            ALOGE("!!! ioctl TVIN_IOC_STOP_DEC, error (%s).\n", strerror(errno));
        }

        ret = ioctl(fd_vdin, TVIN_IOC_CLOSE);
        if (ret == -1)
        {
            ALOGE("!!! ioctl TVIN_IOC_CLOSE, error (%s).\n", strerror(errno));
        }
        close(fd_vdin);
        fd_vdin = -1;
    }

    if (fd_tvafe != -1)
    {
        close(fd_tvafe);
        fd_tvafe = -1;
    }

    return ret;
}

int FrontendAnalogDevice::set_tvafe(unsigned long std)
{
    //set CVBS
    int ret = -1;
    enum tvin_sig_fmt_e fmt =(enum tvin_sig_fmt_e)typeEnumToCvbsFmt (std);
    ret = ioctl(fd_tvafe, TVIN_IOC_S_AFE_CVBS_STD, &fmt);
    if (ret < 0) {
        ALOGD( "set_tvafe, error: return(%d), error(%s)!\n", ret, strerror ( errno ) );
    }

    return ret;
}

int FrontendAnalogDevice::typeEnumToCvbsFmt (unsigned long feType)
{
    ALOGI("[%s] type:%lu", __FUNCTION__, feType);
    enum tvin_sig_fmt_e cvbs_fmt = TVIN_SIG_FMT_NULL;
    switch (feType) {
        case (unsigned long)FrontendAnalogType::PAL:
            cvbs_fmt = TVIN_SIG_FMT_CVBS_PAL_I;
            break;
        case (unsigned long)FrontendAnalogType::PAL_M:
            cvbs_fmt = TVIN_SIG_FMT_CVBS_PAL_M;
            break;
        case (unsigned long)FrontendAnalogType::PAL_N:
            cvbs_fmt = TVIN_SIG_FMT_CVBS_PAL_CN;
            break;
        case (unsigned long)FrontendAnalogType::PAL_60:
            cvbs_fmt = TVIN_SIG_FMT_CVBS_PAL_60;
            break;
        case (unsigned long)FrontendAnalogType::NTSC:
            cvbs_fmt = TVIN_SIG_FMT_CVBS_NTSC_M;
            break;
        case (unsigned long)FrontendAnalogType::NTSC_443:
            cvbs_fmt = TVIN_SIG_FMT_CVBS_NTSC_443;
            break;
        case (unsigned long)FrontendAnalogType::SECAM:
            cvbs_fmt = TVIN_SIG_FMT_CVBS_SECAM;
            break;
    }
    ALOGI("[%s] fmt:0x%x", __FUNCTION__, cvbs_fmt);
    return cvbs_fmt;
}

int FrontendAnalogDevice::getFrontendSettings(FrontendSettings *settings, void* fe_params) {
    struct v4l2_analog_parameters *p_fe_params = (struct v4l2_analog_parameters*)(fe_params);
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

    set_tvafe((unsigned long)settings->get<FrontendSettings::Tag::analog>().type);

    p_fe_params->audmode = tmpAudStd;
    p_fe_params->soundsys = 0xff;
    p_fe_params->std = tmpTVidStd | tmpAudStd;
    if (settings->get<FrontendSettings::Tag::analog>().type == FrontendAnalogType::AUTO) {
        ALOGD("search , afc set true");
        p_fe_params->flag  |= ANALOG_FLAG_ENABLE_AFC;
    } else {
        ALOGD("play , afc set fasle");
        p_fe_params->flag  &= ~ANALOG_FLAG_ENABLE_AFC;
    }
    p_fe_params->afc_range = 1000000;
    return 0;
}

int FrontendAnalogDevice::getFeDeliverySystem(FrontendType type) {
    enum fe_delivery_system fe_system;

    if (type != FrontendType::ANALOG) {
        fe_system = SYS_UNDEFINED;
    } else {
        fe_system = (enum fe_delivery_system)SYS_ANALOG;
    }

    return (int)(fe_system);
}

e_signal_status_t FrontendAnalogDevice::getsignalStatus(int fd, uint32_t &locked_freq) {
    struct v4l2_frontend_event v4l2_evt;
    e_signal_status_t sig_status = FE_SIGNAL_WAIT;

    if (ioctl(fd, V4L2_GET_EVENT, &v4l2_evt) >= 0) {
      if ((v4l2_evt.status & V4L2_HAS_LOCK) != 0) {
          sig_status = FE_SIGNAL_LOCKED;
          locked_freq = v4l2_evt.parameters.frequency;
      } else if ((v4l2_evt.status & V4L2_TIMEDOUT) != 0) {
          sig_status = FE_SIGNAL_TIMEOUT;
      } else {
          sig_status = FE_SIGNAL_WAIT;
      }
    }
    return sig_status;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
