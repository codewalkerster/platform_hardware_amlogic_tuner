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

#define LOG_TAG "tunerhal2.0-Frontend"

#include <sys/ioctl.h>
#include <sys/poll.h>
#include <math.h>
#include "Tuner.h"
#include <utils/Log.h>
#include "Demux.h"
#include "Descrambler.h"
#include "Frontend.h"
#include "Lnb.h"
#include "FrontendDevice.h"
#include "HwFeState.h"

#define FE_POLL_TIMEOUT_MS 50
#define FE_STATE_DTV_TIMEOUT_MS 3000
#define FE_STATE_ATV_TIMEOUT_MS 10
#define FE_SIGNAL_CHECK_INTERVAL_MS 10
#define MAX_PLP_NUMBER 256
#define FEND_WAIT_TIMEOUT           (500)
#define M_BS_START_FREQ             (950)               /*The start RF frequency, 950MHz*/
#define M_BS_STOP_FREQ              (2150)              /*The stop RF frequency, 2150MHz*/
#define M_BS_MAX_SYMB               (45)
#define M_BS_MIN_SYMB               (2)
#define PLP_SIZE (sizeof(atsc3_plp_list_entry_t) * (MAX_PLP_NUMBER) + sizeof(atsc3_l1basic_t) + sizeof(atsc3_l1detail_raw_t))

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

static uint32_t adjustFrequencyOffSet(uint32_t fre) {
    //this handle frequency, only for android vts. so ugly.
    uint32_t frequency = fre;
    if (frequency%1000000 == 0) {
        return frequency;
    } else {
        double f = (double)frequency/1000000;
        frequency = ceil(f) * 1000000;
        ALOGD("adjust frequency = %d", frequency);
    }
    return frequency;
}

FrontendDevice::FrontendDevice(uint32_t thId, FrontendType type, const sp<Frontend>& context) {
    mContext = context;
    mDev.id  = thId;
    mDev.mHw = nullptr;
    mDev.type = type;
    mDev.devFd = -1;
    mDev.feSettings = NULL;
    mDev.blindFreq = 0;
    mDev.tuneFreq = 0;
    mDev.islocked = false;
    mRequestTuningStop = false;
    mThreadState = STATE_INITIAL_IDLE;
    mPlpId = 0;
    if (type == FrontendType::ISDBS
        || type == FrontendType::ISDBS3) {
        unsupportSystem = true;
    } else {
        unsupportSystem = false;
    }
    sem_init(&threadSemaphore, 0, 1);
}

FrontendModulationStatus FrontendDevice::getFeModulationStatus() {
    FrontendModulationStatus modulationStatus;
    ALOGW("FrontendDevice: should not get modulationStatus in unsupported type.");
    modulationStatus.set<FrontendModulationStatus::Tag::dvbc>(FrontendDvbcModulation::UNDEFINED);
    return modulationStatus;
}

FrontendDevice::~FrontendDevice() {
    release();
    sem_destroy(&threadSemaphore);
}

FrontendDevice::fe_dev_t* FrontendDevice::getFeDevice() {
    return &mDev;
}

void FrontendDevice::setHwFe(const sp<HwFeState>& hwFe) {
    mDev.mHw = hwFe;
}

int FrontendDevice::getFrontendId() {
    return mDev.id;
}

FrontendType FrontendDevice::getFeType() {
    return mDev.type;
}

void FrontendDevice::release() {
    ALOGI("%s (id:%d).", __FUNCTION__, mDev.id);
    requestTuneStop();
    updateThreadState(FrontendDevice::STATE_FINISH);
    sem_post(&threadSemaphore);
    clearTuner();
    if (mDev.mHw != nullptr)
    {
        std::lock_guard<std::mutex> lock(mHwDevLock);
        mDev.mHw->release(mDev.devFd, this);
        mDev.devFd = -1;
    }
    requestExitAndWait();
}

void FrontendDevice::stop() {
    requestTuneStop();
    mScanType = FrontendScanType::SCAN_UNDEFINED;
    mDev.tuneFreq  = 0;
    clearTuner();
    if (mDev.mHw != nullptr)
    {
        std::lock_guard<std::mutex> lock(mHwDevLock);
        clearTuner();
        mDev.mHw->release(mDev.devFd, this);
        mDev.devFd = -1;
    }

    ALOGI("%s finish (id:%d).", __FUNCTION__, mDev.id);
}

void FrontendDevice::stopByHw() {
    ALOGW("[id:%d] stop for hw reclaimed.", mDev.id);
    switch (mThreadState)
    {
        case FrontendDevice::STATE_TUNE_START:
            //mContext->sendEventCallBack(FrontendEventType::NO_SIGNAL);
            mThreadState = FrontendDevice::STATE_STOP;
            break;
        case FrontendDevice::STATE_SCAN_START:
            //mContext->sendScanCallBack(mDev.tuneFreq, false, true);
            mThreadState = FrontendDevice::STATE_STOP;
            break;
        case FrontendDevice::STATE_TUNE_IDLE:
            //mContext->sendEventCallBack(FrontendEventType::LOST_LOCK);
            mThreadState = FrontendDevice::STATE_STOP;
            break;
        default:
            break;
    }
    {
        std::lock_guard<std::mutex> lock(mHwDevLock);
        mDev.devFd = -1;
    }
}

bool FrontendDevice::checkOpen(bool autoOpen) {
    ALOGD("%s-(id:%d)", __FUNCTION__, mDev.id);
    bool ret=  true;

    if (unsupportSystem) return false;

    std::lock_guard<std::mutex> lock(mHwDevLock);
    if (mDev.devFd == -1 && mDev.mHw != nullptr) {
        if (autoOpen) {
            if ((mDev.devFd = mDev.mHw->acquire(this)) <0) {
                ALOGE("Cannot acquire tuner for %p", this);
                ret = false;
            }
        } else {
            ret = false;
        }
    }
    return ret;
}

int FrontendDevice::tune(const FrontendSettings & settings) {
    requestTuneStop();
    updateThreadState(FrontendDevice::STATE_TUNE_START);

    userFeSettings = settings;
    mDev.feSettings = &userFeSettings;
    if (mDev.type == FrontendType::ANALOG)
        return interAnalogTune(settings);
    else
        return internalTune(settings);
}

static int bandwidth_hz (enum fe_bandwidth bw) {
    int hz;

    switch (bw) {
    case BANDWIDTH_8_MHZ:
    default:
        hz = 8000000;
        break;
    case BANDWIDTH_7_MHZ:
        hz = 7000000;
        break;
    case BANDWIDTH_6_MHZ:
        hz = 6000000;
        break;
    case BANDWIDTH_5_MHZ:
        hz = 5000000;
        break;
    case BANDWIDTH_10_MHZ:
        hz = 10000000;
        break;
    case BANDWIDTH_1_712_MHZ:
        hz = 1712000;
        break;
    }

    return hz;
}

int FrontendDevice::getAnalogPara(FrontendAnalogType & at, FrontendAnalogSifStandard & ast) {
    struct v4l2_analog_parameters v4l2_para;

    if (!checkOpen(true)) {
        ALOGE("Open fe failed.");
        return UNAVAILABLE;
    }

    if (ioctl(mDev.devFd, V4L2_GET_FRONTEND, &v4l2_para) == -1)
    {
        ALOGE("ioctl V4L2_GET_FRONTEND failed, error:%s", strerror(errno));
        return UNAVAILABLE;
    }

    if ((v4l2_para.std & V4L2_COLOR_STD_PAL) == V4L2_COLOR_STD_PAL) {
        if ((v4l2_para.audmode & V4L2_STD_PAL_M) == V4L2_STD_PAL_M)
          at = FrontendAnalogType::PAL_M;
        else
          at = FrontendAnalogType::PAL;
    } else if ((v4l2_para.std & V4L2_COLOR_STD_NTSC) == V4L2_COLOR_STD_NTSC) {
        at = FrontendAnalogType::NTSC;
    } else if ((v4l2_para.std & V4L2_COLOR_STD_SECAM) == V4L2_COLOR_STD_SECAM) {
        at = FrontendAnalogType::SECAM;
    } else {
        at = FrontendAnalogType::AUTO;
    }

    if (((v4l2_para.audmode & V4L2_STD_PAL_DK) == V4L2_STD_PAL_DK) ||
        ((v4l2_para.audmode & V4L2_STD_SECAM_DK) == V4L2_STD_SECAM_DK)) {
        ast = FrontendAnalogSifStandard::DK;
    } else if ((v4l2_para.audmode & V4L2_STD_PAL_I) == V4L2_STD_PAL_I) {
        ast = FrontendAnalogSifStandard::I;
    } else if (((v4l2_para.audmode & V4L2_STD_PAL_BG) == V4L2_STD_PAL_BG) ||
               ((v4l2_para.audmode & V4L2_STD_SECAM_B) == V4L2_STD_SECAM_B) ||
               ((v4l2_para.audmode & V4L2_STD_SECAM_G) == V4L2_STD_SECAM_G )) {
        ast = FrontendAnalogSifStandard::BG;
    } else if (((v4l2_para.audmode & V4L2_STD_PAL_M) == V4L2_STD_PAL_M) ||
               ((v4l2_para.audmode & V4L2_STD_NTSC_M) == V4L2_STD_NTSC_M)) {
        ast = FrontendAnalogSifStandard::M;
    } else if ((v4l2_para.audmode & V4L2_STD_SECAM_L) == V4L2_STD_SECAM_L) {
        ast = FrontendAnalogSifStandard::L;
    } else {
        ast = FrontendAnalogSifStandard::DK;
    }

    return 0;
}

int FrontendDevice::interAnalogTune(const FrontendSettings & settings) {
    struct v4l2_analog_parameters v4l2_para;

    ALOGD("%s, id(%d)", __FUNCTION__, mDev.id);
    FrontendSettings tuneSettings = settings;
    mDev.feSettings = &tuneSettings;
    if (getFrontendSettings(&tuneSettings, &v4l2_para) <0) {
        ALOGE("[id:%d] Wrong delivery system in FrontendSettings, or not support it.", mDev.id);
        sem_post(&threadSemaphore);
        return INVALID_ARGUMENT;
    }

    mDev.tuneFreq = v4l2_para.frequency;
    if (!checkOpen(true)) {
        ALOGE("Open fe failed.");
        sem_post(&threadSemaphore);
        return UNAVAILABLE;
    }
    ALOGD("%s, frequency = %d, audmode:%d, soundsys:0x%x, std:0x%llx, flag:%d, afc_range:%d", __FUNCTION__,
        mDev.tuneFreq,
        v4l2_para.audmode,
        v4l2_para.soundsys,
        v4l2_para.std,
        v4l2_para.flag,
        v4l2_para.afc_range);
    if (ioctl(mDev.devFd, V4L2_SET_FRONTEND, &v4l2_para) == -1) {
         ALOGE("tune failed, (%s)", strerror(errno));
         sem_post(&threadSemaphore);
         return UNAVAILABLE;
    }
    sem_post(&threadSemaphore);
    return 0;
}

int FrontendDevice::internalTune(const FrontendSettings & settings) {
    ALOGD("%s, id(%d)", __FUNCTION__, mDev.id);
    dvb_frontend_parameters fe_params;

    FrontendSettings tuneSettings = settings;
    if (getFrontendSettings(&tuneSettings, &fe_params) <0) {
        ALOGE("[id:%d] Wrong delivery system in FrontendSettings, or not support it.", mDev.id);
        sem_post(&threadSemaphore);
        return INVALID_ARGUMENT;
    }

    if (mDev.type == FrontendType::DVBS) {
        mDev.tuneFreq = adjustFrequencyOffSet(tuneSettings.get<FrontendSettings::Tag::dvbs>().frequency);
    } else if (mDev.type == FrontendType::DVBT && mScanType == FrontendScanType::SCAN_BLIND) {
        mScanType = FrontendScanType::SCAN_AUTO;
        mDev.tuneFreq = adjustFrequencyOffSet(tuneSettings.get<FrontendSettings::Tag::dvbt>().frequency);
    } else {
        mDev.tuneFreq = fe_params.frequency;
    }

    if (!checkOpen(true)) {
        ALOGE("Open fe failed.");
        sem_post(&threadSemaphore);
        return UNAVAILABLE;
    }

    /*
    if (ioctl(mDev.devFd, FE_SET_FRONTEND, &fe_params) < 0) {
        ALOGE("%s error(%d):%s", __FUNCTION__, errno, strerror(errno));
        return UNAVAILABLE;
    }*/
    struct dtv_properties props;
    struct dtv_property cmds[16];
    struct dtv_property *cmd = cmds;
    int ncmd = 0;

    cmd->cmd = DTV_DELIVERY_SYSTEM;
    cmd->u.data = getFeDeliverySystem(mDev.type);
    cmd ++;
    ncmd ++;

    cmd->cmd = DTV_FREQUENCY;
    cmd->u.data = fe_params.frequency;
    cmd ++;
    ncmd ++;

    switch (mDev.type) {
    case FrontendType::ATSC:
        cmd->cmd = DTV_MODULATION;
        cmd->u.data = fe_params.u.vsb.modulation;
        cmd ++;
        ncmd ++;
        break;
    case FrontendType::ATSC3:
    {
        // set plp for atsc3
        uint8_t plp_ids[MAX_PLP_NUMBER];
        int plp_num = settings.get<FrontendSettings::Tag::atsc3>().plpSettings.size();
        //int plp_num = settings.atsc3().plpSettings.size();
        cmd->cmd = DTV_BANDWIDTH_HZ;
        cmd->u.data = bandwidth_hz(fe_params.u.ofdm.bandwidth);
        cmd ++;
        ncmd ++;

        cmd->cmd = DTV_DVBT2_PLP_ID;
        cmd->u.data = plp_num;
        cmd ++;
        ncmd ++;

        cmd->cmd = DTV_STREAM_ID;
        for (int i = 0; i < plp_num; i++) {
            plp_ids[i] = settings.get<FrontendSettings::Tag::atsc3>().plpSettings[i].plpId;
            //plp_ids[i] = settings.atsc3().plpSettings[i].plpId;
            cmd->u.data |= plp_ids[i] << i * 8;
        }
        cmd ++;
        ncmd ++;
    }
        break;
    case FrontendType::DVBC:
        cmd->cmd = DTV_MODULATION;
        cmd->u.data = fe_params.u.qam.modulation;
        cmd ++;
        ncmd ++;

        cmd->cmd = DTV_SYMBOL_RATE;
        cmd->u.data = fe_params.u.qam.symbol_rate;
        cmd ++;
        ncmd ++;
        break;
    case FrontendType::DVBS:
        cmd->cmd = DTV_SYMBOL_RATE;
        cmd->u.data = fe_params.u.qpsk.symbol_rate;
        cmd ++;
        ncmd ++;

        cmd->cmd = DTV_INNER_FEC;
        cmd->u.data = fe_params.u.qpsk.fec_inner;
        cmd ++;
        ncmd ++;
        break;
    case FrontendType::DVBT:
    case FrontendType::DTMB:
        if (fe_params.u.ofdm.bandwidth != BANDWIDTH_AUTO) {
            cmd->cmd = DTV_BANDWIDTH_HZ;
            cmd->u.data = bandwidth_hz(fe_params.u.ofdm.bandwidth);
            cmd ++;
            ncmd ++;
        }

        cmd->cmd = DTV_TRANSMISSION_MODE;
        cmd->u.data = fe_params.u.ofdm.transmission_mode;
        cmd ++;
        ncmd ++;

        cmd->cmd = DTV_GUARD_INTERVAL;
        cmd->u.data = fe_params.u.ofdm.guard_interval;
        cmd ++;
        ncmd ++;

        if (mDev.type == FrontendType::DVBT) {
            if ((settings.get<FrontendSettings::Tag::dvbt>().standard == FrontendDvbtStandard::T2)) {
                cmd->cmd = DTV_DVBT2_PLP_ID_LEGACY;
                cmd->u.data = settings.get<FrontendSettings::Tag::dvbt>().plpId;
                ALOGD("DTV DVBT2 plpId = %d", settings.get<FrontendSettings::Tag::dvbt>().plpId);
                mPlpId = settings.get<FrontendSettings::Tag::dvbt>().plpId;

                cmd ++;
                ncmd ++;
            }
        }
        break;
    case FrontendType::ISDBT:
        if (fe_params.u.ofdm.bandwidth != BANDWIDTH_AUTO) {
            cmd->cmd = DTV_BANDWIDTH_HZ;
            cmd->u.data = bandwidth_hz(fe_params.u.ofdm.bandwidth);
            cmd ++;
            ncmd ++;
        }
        break;
    default:
        break;
    }

    cmd->cmd = DTV_TUNE;
    cmd ++;
    ncmd ++;

    props.num = ncmd;
    props.props = cmds;
    ALOGD("%s, frequency = %d", __FUNCTION__, mDev.tuneFreq);

    gettimeofday(&tune_start_time, NULL);
    if (ioctl(mDev.devFd, FE_SET_PROPERTY, &props) == -1) {
         ALOGE("tune failed, (%s)", strerror(errno));
         sem_post(&threadSemaphore);
         return UNAVAILABLE;
    }

    sem_post(&threadSemaphore);
    return 0;
}

uint16_t FrontendDevice::getFeSnr() {
    uint16_t snr = 0;;

    if (!checkOpen(true)) {
        return snr;
    }

    if (ioctl(mDev.devFd, FE_READ_SNR, &snr) < 0) {
        ALOGE("%s error(%d):%s", __FUNCTION__, errno, strerror(errno));
    }

    ALOGD("%s:%u", __FUNCTION__, snr);
    return snr;
}

uint32_t FrontendDevice::getFeBer() {
    uint32_t ber = 0;;

    if (!checkOpen(true)) {
        return ber;
    }

    if (ioctl(mDev.devFd, FE_READ_BER, &ber) < 0) {
        ALOGE("%s error(%d):%s", __FUNCTION__, errno, strerror(errno));
    }

    ALOGD("%s:%u", __FUNCTION__, ber);
    return ber;
}

uint16_t FrontendDevice::getSignalStrength() {
    uint16_t strength = 0;;

    if (!checkOpen(true)) {
        return strength;
    }

    if (ioctl(mDev.devFd, FE_READ_SIGNAL_STRENGTH, &strength) < 0) {
        ALOGE("%s error(%d):%s", __FUNCTION__, errno, strerror(errno));
    }

    ALOGD("%s:%u", __FUNCTION__, strength);
    return strength;
}

int FrontendDevice::getFeProp(struct dtv_properties *prop) {
    if (!checkOpen(true)) return UNAVAILABLE;

    if (ioctl(mDev.devFd, FE_GET_PROPERTY, prop) == -1) {
        ALOGE("%s error(%d):%s", __FUNCTION__, errno, strerror(errno));
        return UNAVAILABLE;
    }

    return SUCCESS;
}

int FrontendDevice::setFeSystem() {
    ALOGI("%s, id(%d)", __FUNCTION__, mDev.id);
    if (mDev.devFd != -1) {
        int sys = getFeDeliverySystem(mDev.type);
        struct dtv_property p =
            {.cmd = DTV_DELIVERY_SYSTEM,
             .u.data = (enum fe_delivery_system)sys};
        struct dtv_properties props = {.num = 1, .props = &p};
        if (ioctl(mDev.devFd, FE_SET_PROPERTY, &props) == -1) {
            ALOGE("Set fe system failed with id(%d): %s", mDev.id, strerror(errno));
            return -1;
        }
        mDev.deliverySys = sys;
    }

    return 0;
}

int FrontendDevice::blindTune(const FrontendSettings & settings) {
    ALOGD("%s, id(%d)", __FUNCTION__, mDev.id);
    struct dvb_frontend_info fe_info;


    if (mDev.blindFreq == 0) {
        if (ioctl(mDev.devFd, FE_GET_INFO, &fe_info) < 0 ) {
            return UNKNOWN_ERROR;
        }
        mDev.blindFreq = fe_info.frequency_min;
    }
    FrontendSettings tuneSettings = settings;
    requestTuneStop();
    updateThreadState(FrontendDevice::STATE_SCAN_START);

    dvb_frontend_parameters fe_params;

    //int frequency = adjustFrequencyOffSet(tuneSettings.get<FrontendSettings::Tag::dvbs>().frequency);
    mDev.feSettings = &tuneSettings;
    if (getFrontendSettings(&tuneSettings, &fe_params) <0) {
        ALOGE("[id:%d] Wrong delivery system in FrontendSettings, or not support it.", mDev.id);
        sem_post(&threadSemaphore);
        return INVALID_ARGUMENT;
    }

    if (mDev.type == FrontendType::DVBS) {
        mDev.tuneFreq = adjustFrequencyOffSet(tuneSettings.get<FrontendSettings::Tag::dvbs>().frequency);
        mDev.blindEndFreq = adjustFrequencyOffSet(tuneSettings.get<FrontendSettings::Tag::dvbs>().endFrequency);
    } else {
        mDev.tuneFreq = fe_params.frequency;
        mDev.blindEndFreq = 0;
    }

    ALOGI("Blind scan %uHz to %uHz", mDev.tuneFreq, mDev.blindEndFreq);
    if (!checkOpen(true)) {
        ALOGE("Open fe failed.");
        sem_post(&threadSemaphore);
        return UNAVAILABLE;
    }
    /*
    if (ioctl(mDev.devFd, FE_SET_FRONTEND, &fe_params) < 0) {
    ALOGE("%s error(%d):%s", __FUNCTION__, errno, strerror(errno));
    return UNAVAILABLE;
    }*/
    setDvbsBlindScanParams(true);

    sem_post(&threadSemaphore);
    return 0;

}

int FrontendDevice::scan(const FrontendSettings & settings, FrontendScanType type) {
    int ret = 0;

    if (!checkOpen(true)) return UNAVAILABLE;
    if (type == FrontendScanType::SCAN_BLIND && settings.getTag() == FrontendSettings::Tag::dvbs) {
        mScanType = FrontendScanType::SCAN_BLIND;
        ret = blindTune(settings);
    } else {
        mScanType = type;
        mDev.blindFreq = 0;
        requestTuneStop();
        updateThreadState(FrontendDevice::STATE_SCAN_START);
        userFeSettings = settings;
        mDev.feSettings = &userFeSettings;
        if (mDev.type == FrontendType::ANALOG)
          ret = interAnalogTune(settings);
        else
          ret = internalTune(settings);
    }
    return ret;
}

void FrontendDevice::clearTuner() {
#if 0
    if (!checkOpen(true)) return;

    struct dtv_property p = {.cmd = DTV_CLEAR};
    struct dtv_properties props = {.num = 1, .props = &p};

    ioctl(mDev.devFd, FE_SET_PROPERTY, &props);
#endif
}

int FrontendDevice::stopTune() {
    stop();
    return 0;
}

int FrontendDevice::stopScan() {
    stop();
    //mContext->sendScanCallBack(mDev.tuneFreq, false, true);
    return 0;
}

int FrontendDevice::setLna(bool bEnable) {
    if (mDev.type != FrontendType::DVBT && mDev.type != FrontendType::ISDBT) {
        return SUCCESS;
    }

    if (!checkOpen(true)) return UNAVAILABLE;

    struct dtv_properties props;
    struct dtv_property cmds[16];
    struct dtv_property *cmd = cmds;
    int ncmd = 0;

    cmd->cmd = DTV_DELIVERY_SYSTEM;
    cmd->u.data = getFeDeliverySystem(mDev.type);
    cmd ++;
    ncmd ++;

    cmd->cmd = DTV_LNA;
    cmd->u.data = int(bEnable);
    cmd ++;
    ncmd ++;

    props.num = ncmd;
    props.props = cmds;

    if (ioctl(mDev.devFd, FE_SET_PROPERTY, &props) == -1) {
         ALOGE("setLna failed, (%s)", strerror(errno));
         return UNAVAILABLE;
    }

    return SUCCESS;

}

bool FrontendDevice::getLna() {
    struct dtv_property p = {.cmd = DTV_LNA, .u.data = 0};
    struct dtv_properties props = {.num = 1, .props = &p};

    if (getFeProp(&props) != SUCCESS) {
        return false;
    }

    ALOGD("getLna: %d", p.u.data);//driver will return -1
    return (p.u.data == 1);
}

LnbVoltage FrontendDevice::getLnbVoltage() {
    LnbVoltage lnbVoltage;
    fe_sec_voltage_t devVoltage;

    struct dtv_property p = {.cmd = DTV_VOLTAGE, .u.data = 0};
    struct dtv_properties props = {.num = 1, .props = &p};

    if (mDev.type != FrontendType::DVBS) {
        return LnbVoltage::NONE;
    }

    if (getFeProp(&props) != SUCCESS) {
        return LnbVoltage::NONE;
    }

    devVoltage = (fe_sec_voltage_t)(p.u.data);
    switch (devVoltage) {
        case SEC_VOLTAGE_13:
            lnbVoltage = LnbVoltage::VOLTAGE_13V;
            break;
        case SEC_VOLTAGE_18:
            lnbVoltage = LnbVoltage::VOLTAGE_18V;
            break;
        case SEC_VOLTAGE_OFF:
            lnbVoltage = LnbVoltage::NONE;
            break;
    }

    ALOGD("getLnbVoltage: %u(0:13,1:18,2:off)", devVoltage);
    return lnbVoltage;
}

uint32_t FrontendDevice::getSymbolRate() {
    uint32_t ret = 0;
    struct dtv_property cmd;
    struct dtv_properties props;

    if (mDev.type != FrontendType::DVBC && mDev.type != FrontendType::DVBS) {
        return 0;
    }

    memset(&cmd, 0, sizeof(struct dtv_property));
    cmd.cmd = DTV_DELIVERY_SYSTEM;
    props.num = 1;
    props.props = &cmd;

    if (getFeProp(&props) == SUCCESS) {
        ret = cmd.reserved[1];
    }

    ALOGD("%s:[0] %u, [1] %u", __FUNCTION__, cmd.reserved[0], ret);
    return ret;
}

uint32_t FrontendDevice::getActualTerrHierarchy() {
    uint32_t retval = 0;
    struct dtv_property cmd;
    struct dtv_properties props;
    uint8_t plp_ids[MAX_PLP_NUMBER];
    memset(&cmd, 0, sizeof(struct dtv_property));
    cmd.cmd = DTV_DVBT2_PLP_ID_LEGACY;
    cmd.u.buffer.reserved1[1] = (~0U);
    cmd.u.buffer.reserved2 = plp_ids;

    props.num = 1;
    props.props = &cmd;
    if (getFeProp(&props) != SUCCESS) {
        return 0;
    }
    retval = cmd.u.buffer.reserved1[0];
    if (retval != 0) {
        /* Return the value of the max PLP id */
        retval--;
    }
    ALOGD("getActualTerrHierarchy: %d", retval);
    return retval;
}

vector<int32_t> FrontendDevice::getMPLPIDList() {
    int retval = 0;
    struct dtv_property cmd;
    struct dtv_properties props;
    uint8_t plp_ids[MAX_PLP_NUMBER];
    vector<int32_t> plpIds;
    memset(&cmd, 0, sizeof(struct dtv_property));
    cmd.cmd = DTV_DVBT2_PLP_ID_LEGACY;
    cmd.u.buffer.reserved1[1] = MAX_PLP_NUMBER;
    cmd.u.buffer.reserved2 = plp_ids;

    props.num = 1;
    props.props = &cmd;
    if (getFeProp(&props) != SUCCESS) {
        return plpIds;
    }
    retval = cmd.u.buffer.reserved1[0];
    if (retval != 0) {
        ALOGD("getMPLPIDList plpId num = %d", retval);
        plpIds.resize(retval);
        for (int i = 0; i < retval; i++) {
            plpIds[i] = plp_ids[i];
            ALOGD("getMPLPIDList pipid[%d] = %d", i, plpIds[i]);
        }
    }
    return plpIds;
}

vector<atsc3_plp_list_entry_t> FrontendDevice::getAtsc3MPLPIDList() {
    ALOGV("%s/%d getAtsc3MPLPIDList", __FUNCTION__, __LINE__);
    unsigned char buffer[PLP_SIZE];
    int plp_list_num =0;
    int L1Basic_len = sizeof(atsc3_l1basic_t);
    int L1Detail_len = sizeof(atsc3_l1detail_raw_t);
    vector<atsc3_plp_list_entry_t> plp_list_entry_t;
    uint8_t l1basic[L1Basic_len];
    uint8_t l1detail_raw_t[L1Detail_len];
    struct dtv_property cmd;
    struct dtv_properties props;

    cmd.cmd = DTV_STREAM_ID;
    cmd.u.buffer.reserved1[0] = 0;
    cmd.u.buffer.reserved2 = buffer;
    props.num = 1;
    props.props = &cmd;

    if (getFeProp(&props) != SUCCESS) {
        return plp_list_entry_t;
    }

    plp_list_num = cmd.u.buffer.reserved1[0];
    ALOGV("%s plp_list_num = %d", __FUNCTION__, plp_list_num);
    if (plp_list_num != 0) {
        plp_list_entry_t.resize(plp_list_num);
        memcpy(plp_list_entry_t.data(), buffer, plp_list_num * sizeof(atsc3_plp_list_entry_t));
        memcpy(&l1basic, buffer + plp_list_num * sizeof(atsc3_plp_list_entry_t), sizeof(l1basic));
        memcpy(&l1detail_raw_t, buffer + plp_list_num * sizeof(atsc3_plp_list_entry_t) + sizeof(l1basic), sizeof(l1detail_raw_t));
    }

    for (int i = 0; i < plp_list_num; i++) {
        ALOGV("%s getAtsc3MPLPIDList pipid[%d] = %d", __FUNCTION__, i, plp_list_entry_t[i].id);
        ALOGV("%s getAtsc3MPLPIDList plp.lls_flg[%d] is =%d", __FUNCTION__, i, plp_list_entry_t[i].lls_flg);
    }
    return plp_list_entry_t;
}

uint32_t FrontendDevice::getEwbsFlag() {
    uint32_t retval = 0;
    uint32_t sys_id = 0;
    struct dtv_property cmd;
    struct dtv_properties props;
    memset(&cmd, 0, sizeof(struct dtv_property));
    cmd.cmd = DTV_ISDBT_PARTIAL_RECEPTION;

    props.num = 1;
    props.props = &cmd;
    if (getFeProp(&props) != SUCCESS) {
        return 0;
    }
    sys_id = cmd.u.buffer.reserved1[0];
    retval = cmd.u.buffer.reserved1[1];
    ALOGV("%s/%d get isdbt partial reception (sys_id:%u ewbs:%u)", __FUNCTION__, __LINE__, sys_id, retval);
    return retval;
}

int32_t FrontendDevice::getCurrentMPlpId() {
    return mPlpId;
}

uint32_t FrontendDevice::getFeSystem() {
    uint32_t ret = 0xffff;
    struct dtv_property cmd;
    struct dtv_properties props;

    memset(&cmd, 0, sizeof(struct dtv_property));
    cmd.cmd = DTV_DELIVERY_SYSTEM;
    props.num = 1;
    props.props = &cmd;

    if (getFeProp(&props) == SUCCESS) {
        ret = cmd.reserved[0];
    }

    ALOGD("%s:[0] %u, [1] %u", __FUNCTION__, ret, cmd.reserved[1]);
    return ret;
}

status_t FrontendDevice::readyToRun() {
    ALOGI("%s with frontendType(%d), id(%d)", __FUNCTION__, mDev.type, mDev.id);
    return 0;
}

void FrontendDevice::onFirstRef(void) {
    mThreadState = STATE_INITIAL_IDLE;
    run("DroidFeTask");
}

e_signal_status_t FrontendDevice::getsignalStatus(int fd, uint32_t &locked_freq) {
    struct dvb_frontend_event fe_event;
    e_signal_status_t sig_status = FE_SIGNAL_WAIT;

    if (ioctl(fd, FE_GET_EVENT, &fe_event) >= 0) {
      if ((fe_event.status & FE_HAS_LOCK) != 0) {
         sig_status = FE_SIGNAL_LOCKED;
         locked_freq = mDev.tuneFreq;
      } else if ((fe_event.status & FE_TIMEDOUT) != 0) {
         sig_status = FE_SIGNAL_TIMEOUT;
      } else {
         sig_status = FE_SIGNAL_WAIT;
      }
    }
    return sig_status;
}

bool FrontendDevice::threadLoop() {
    int state = getThreadState();
    bool stop;
    uint32_t start_time, fe_timeout;
    uint32_t locked_freq = mDev.tuneFreq;
    struct pollfd pfd;
    e_signal_status_t sig_st = FE_SIGNAL_WAIT;
    memset(&mStbTrace_info, 0, sizeof(stbtrace_info));

    if (state == FrontendDevice::STATE_TUNE_START
       || state == FrontendDevice::STATE_SCAN_START
       || state == FrontendDevice::STATE_TUNE_IDLE) {
        stop = false;
        int newState = getThreadState();
        if (mRequestTuningStop || newState == FrontendDevice::STATE_STOP) {
            stop = true;
        }
        if (mScanType == FrontendScanType::SCAN_BLIND) {
            bool inBlindScan = true;
            while (!stop)
            {
                struct dvbsx_blindscanevent cur_bsevent;

                if (mRequestTuningStop) {
                    stop = true;
                }

                memset(&cur_bsevent, 0, sizeof(dvbsx_blindscanevent));
                dvbsx_blindscan_getscanevent(&cur_bsevent);

                if (cur_bsevent.status == BLINDSCAN_UPDATEPROCESS)
                {
                    locked_freq = cur_bsevent.u.m_uiprogress;
                    locked_freq |= 0xC0000000;

                    if (cur_bsevent.u.m_uiprogress >= 100)
                    {
                        mContext->sendScanCallBack(locked_freq, false, true);
                        setDvbsBlindScanParams(false);
                        inBlindScan = false;
                        stop = true;
                    }
                    else
                    {
                        mContext->sendScanCallBack(locked_freq, false, false);
                    }
                }
                else if(cur_bsevent.status == BLINDSCAN_UPDATERESULTFREQ)
                {
                    locked_freq = cur_bsevent.u.parameters.frequency;
                    mContext->sendScanCallBack(locked_freq, true, false,
                        cur_bsevent.u.parameters.u.qpsk.symbol_rate);
                    //updateThreadState(FrontendDevice::STATE_STOP);
                }
            }
            if (inBlindScan) {
                setDvbsBlindScanParams(false);
                inBlindScan = false;
            }
            updateThreadState(FrontendDevice::STATE_STOP);
        } else {
            if (state == FrontendDevice::STATE_TUNE_START
                || state == FrontendDevice::STATE_SCAN_START) {
                mDev.islocked = false;
            }
            {
                std::lock_guard<std::mutex> lock(mHwDevLock);
                pfd.fd = mDev.devFd;
            }

            if (mDev.type == FrontendType::ANALOG)
                fe_timeout = FE_STATE_DTV_TIMEOUT_MS;
            else
                fe_timeout = FE_STATE_ATV_TIMEOUT_MS;

            pfd.events = POLLIN;
            pfd.revents = 0;
            for (start_time = getClockMilliSeconds();
                 !stop && ((getClockMilliSeconds() - start_time) < fe_timeout);) {
                if (poll(&pfd, 1, FE_POLL_TIMEOUT_MS) == 1) {
                    sig_st = getsignalStatus(mDev.devFd, locked_freq);
                    if (sig_st == FE_SIGNAL_LOCKED ||
                        sig_st == FE_SIGNAL_TIMEOUT) {
                        break;
                    }
                } else {
                    if (state == FrontendDevice::STATE_TUNE_IDLE || mRequestTuningStop) {
                        //scan and tune need check several seconds for signal
                        //will not stable. and in ilde state, we just poll once
                        stop = true;
                    }
                }
                newState = getThreadState();
                if (mRequestTuningStop || newState == FrontendDevice::STATE_STOP) {
                    stop = true;
                }
            }
            newState = getThreadState();
            if (mRequestTuningStop || newState == FrontendDevice::STATE_STOP) {
                stop = true;
            }
            if (sig_st != FE_SIGNAL_WAIT != 0 && !stop) {
                bool locked = (sig_st == FE_SIGNAL_LOCKED);
                ALOGI("%s-(id:%d): getsignalStatus: %d, locked=%d, dev_locked=%d", __FUNCTION__, mDev.id, sig_st, locked, mDev.islocked);
                if (state == STATE_SCAN_START) {
                    ALOGD("%s-(id:%d): send scan event.", __FUNCTION__, mDev.id);
                    mDev.islocked = locked;
                    mContext->sendScanCallBack(locked_freq, locked, false);
                    updateThreadState(FrontendDevice::STATE_STOP);
                } else if (state == STATE_TUNE_START) {
                    ALOGI("%s-(id:%d): send tune event.", __FUNCTION__, mDev.id);
                    mDev.islocked = locked;
                    gettimeofday(&tune_end_time, NULL);
                    timeval mTune_start_time = tuneStartTime();
                    timersub(&tune_end_time, &mTune_start_time, &tune_elapsed_time);
                    //ALOGD("%s locked elapsed time: mTune_elapsed = %ld ms", __FUNCTION__, tune_elapsed_time.tv_sec * 1000 + tune_elapsed_time.tv_usec / 1000);
                    snprintf(mStbTrace_info.module_name, sizeof(mStbTrace_info.module_name), "droidlogic_frontend");
                    tune_time_trace_log(&mStbTrace_info, "locked elapsed time",  tune_elapsed_time);
                    updateThreadState(FrontendDevice::STATE_TUNE_IDLE);
                    if (locked) {
                      mContext->sendEventCallBack(FrontendEventType::LOCKED);
                    } else {
                      mContext->sendEventCallBack(FrontendEventType::NO_SIGNAL);
                    }
                } else {
                    if (locked != mDev.islocked) {
                        ALOGI("%s-(id:%d): send evt changed.", __FUNCTION__, mDev.id);
                        mDev.islocked = locked;
                        if (locked) {
                            mContext->sendEventCallBack(FrontendEventType::LOCKED);
                        } else {
                            mContext->sendEventCallBack(FrontendEventType::LOST_LOCK);
                        }
                    }
                }
            }
            if (mRequestTuningStop) {
                updateThreadState(FrontendDevice::STATE_STOP);
            }
            if (getThreadState() == FrontendDevice::STATE_TUNE_IDLE) {
                usleep(1000*FE_SIGNAL_CHECK_INTERVAL_MS);
            }
        }
    } else if (state == FrontendDevice::STATE_STOP
       || state == FrontendDevice::STATE_INITIAL_IDLE) {
        ALOGD("%s-(id-%d): fe thread wait in stop state.", __FUNCTION__, mDev.id);
        sem_wait(&threadSemaphore);
    } else if (state == FrontendDevice::STATE_FINISH) {
        usleep(1000*20);//wait to exit thread
    }
    return true;
}

uint32_t FrontendDevice::getClockMilliSeconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec/1000000;
}

int FrontendDevice::getThreadState(void) {
    std::lock_guard<std::mutex> lock(mThreadStatLock);
    return (int)mThreadState;
}

void FrontendDevice::updateThreadState(int state) {
    std::lock_guard<std::mutex> lock(mThreadStatLock);
    mThreadState = (e_event_stat_t)state;
}

void FrontendDevice::requestTuneStop(void) {
    bool ready = false;
    mRequestTuningStop = true;
    while (ready == false) {
        int state = getThreadState();
        ALOGV("%s-(id:%d): state=%d", __FUNCTION__, mDev.id, state);
        if (state == FrontendDevice::STATE_STOP
            || state == FrontendDevice::STATE_INITIAL_IDLE) {
            ready = true;
        }
    }
    mRequestTuningStop = false;
}

FrontendSettings* FrontendDevice::getFeSetting() {
    return mDev.feSettings;
}

timeval FrontendDevice::tuneStartTime() {
    return tune_start_time;
}

//once kernel space add event, poll event and get event
int FrontendDevice::dvb_wait_event (struct dvb_frontend_event *evt, int timeout)
{
    int ret;
    struct pollfd pfd;
    struct dvb_frontend_event event;
    int fd = mDev.devFd;

    pfd.fd = fd;
    pfd.events = POLLIN;

    ret = poll(&pfd, 1, timeout);

    if (ret != 1)
    {
        return 1;
    }

    if (ioctl(fd, FE_GET_EVENT, &event) == -1)
    {
        ALOGE("ioctl FE_GET_EVENT failed, error:%s", strerror(errno));
        return 1;
    }

    evt->status = event.status;
    evt->parameters.frequency = event.parameters.frequency;
    evt->parameters.inversion = event.parameters.inversion;
    evt->parameters.u.qpsk = event.parameters.u.qpsk;
    evt->parameters.u.qam = event.parameters.u.qam;
    evt->parameters.u.ofdm = event.parameters.u.ofdm;
    evt->parameters.u.vsb = event.parameters.u.vsb;

    return 0;
}

//get event and process events.
//this function should be called in a loop while the blind scan thread is running.
int FrontendDevice::dvbsx_blindscan_getscanevent(struct dvbsx_blindscanevent *pbsevent)
{
    int ret = 0;
    //int i,isize;
    //char* pev;
    struct dvb_frontend_event event;
    //int frontend_fd = mDev.devFd;

    ret = dvb_wait_event(&event, 200);
    //isize = sizeof(dvb_frontend_event);
    //ALOGD("[%s]: %d  ret = %d    size = %d\n", __FUNCTION__, __LINE__, ret, isize);
    //pev = (char*)&event;
#if 0
    for (i=0;i<sizeof(event);i++)
    {
        ALOGD("%s,0x%x,",__FUNCTION__,pev[i]);
    }
#endif
    if (0 == ret)
    {
        //ALOGD("[%s]: %d  event.status = 0x%x, frequency = %d\n", __FUNCTION__, __LINE__, event.status, event.parameters.frequency);
        if (event.status&BLINDSCAN_UPDATESTARTFREQ)
        {
            pbsevent->status = BLINDSCAN_UPDATESTARTFREQ;
            pbsevent->u.m_uistartfreq_khz = event.parameters.frequency;
            ALOGD("[%s]: %d  BLINDSCAN_UPDATESTARTFREQ event.status = 0x%x, frequency = %d\n", __FUNCTION__, __LINE__, event.status, event.parameters.frequency);
        }
        else if (event.status&BLINDSCAN_UPDATEPROCESS)
        {
            pbsevent->status = BLINDSCAN_UPDATEPROCESS;
            pbsevent->u.m_uiprogress = event.parameters.frequency;
            ALOGD("[%s]: %d  BLINDSCAN_UPDATEPROCESS event.status = 0x%x, frequency = %d\n", __FUNCTION__, __LINE__, event.status, event.parameters.frequency);
        }
        else if (event.status&BLINDSCAN_UPDATERESULTFREQ)
        {
            pbsevent->status = BLINDSCAN_UPDATERESULTFREQ;
            memcpy(&(pbsevent->u.parameters),
                   &(event.parameters), sizeof(struct dvb_frontend_parameters));
            ALOGD("[%s]: %d  BLINDSCAN_UPDATERESULTFREQ event.status = 0x%x, frequency = %d\n", __FUNCTION__, __LINE__, event.status, event.parameters.frequency);
        }
        else
        {
            ALOGE("[%s]: %d  event.status = 0x%x, frequency = %d\n", __FUNCTION__, __LINE__, event.status, event.parameters.frequency);
            //pbsevent->status = BLINDSCAN_UPDATERESULT_OTHERS;
        }
    }

    return ret;
}

int FrontendDevice::setDvbsBlindScanParams(bool start) {
    if (!start) {
        struct dtv_properties props;
        struct dtv_property property;
        memset(&property, 0, sizeof(struct dtv_property));

        props.num = 1;
        props.props = &property;
        property.cmd = DTV_CANCEL_BLIND_SCAN;
        property.u.data = 0;

        if (ioctl(mDev.devFd, FE_SET_PROPERTY, &props) == -1) {
            ALOGE("Cancel blind failed, (%s)", strerror(errno));
            return UNAVAILABLE;
        }
    } else {
        uint32_t startKHz = mDev.tuneFreq / 1000;
        uint32_t endKHz = mDev.blindEndFreq / 1000;
        struct dtv_properties props;
        struct dtv_property cmds[9];
        struct dtv_property *cmd = cmds;
        int ncmd = 0;
        memset(cmds, 0, sizeof(struct dtv_property) * 9);

        if (startKHz < 950000 || startKHz > 2150000)
            startKHz = 950000;
        if (endKHz < startKHz || endKHz > 2150000) {
            endKHz = 2150000;
        }

        cmd->cmd = DTV_DELIVERY_SYSTEM;
        cmd->u.data = getFeDeliverySystem(mDev.type);
        cmd ++;
        ncmd ++;

        /*set min fre*/
        cmd->cmd = DTV_BLIND_SCAN_MIN_FRE;
        cmd->u.data = startKHz;
        cmd ++;
        ncmd ++;

        /*set max fre*/
        cmd->cmd = DTV_BLIND_SCAN_MAX_FRE;
        cmd->u.data = endKHz;//settingsExt1_1.endFrequency
        cmd ++;
        ncmd ++;

        /*set min rate*/
        cmd->cmd = DTV_BLIND_SCAN_MIN_SRATE;
        cmd->u.data = M_BS_MIN_SYMB * 1000 * 1000;
        cmd ++;
        ncmd ++;

        /*set max rate*/
        cmd->cmd = DTV_BLIND_SCAN_MAX_SRATE;
        cmd->u.data = M_BS_MAX_SYMB * 1000 * 1000;
        cmd ++;
        ncmd ++;

        /*set fre range*/
        cmd->cmd = DTV_BLIND_SCAN_FRE_RANGE;
        cmd->u.data = 100;
        cmd ++;
        ncmd ++;

        /*set fre step*/
        cmd->cmd = DTV_BLIND_SCAN_FRE_STEP;
        cmd->u.data = 1000;
        cmd ++;
        ncmd ++;

        /*set time out*/
        cmd->cmd = DTV_BLIND_SCAN_TIMEOUT;
        cmd->u.data = FEND_WAIT_TIMEOUT;
        cmd ++;
        ncmd ++;

        /*set start blind scan*/
        cmd->cmd = DTV_START_BLIND_SCAN;
        cmd->u.data = 0;
        cmd ++;
        ncmd ++;

        props.num = ncmd;
        props.props = cmds;

        if (ioctl(mDev.devFd, FE_SET_PROPERTY, &props) == -1) {
            ALOGE("tune failed, (%s)", strerror(errno));
            sem_post(&threadSemaphore);
            return UNAVAILABLE;
        }
    }

    return SUCCESS;
}
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  //android
}  // namespace aidl
