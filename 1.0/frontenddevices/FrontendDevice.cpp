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
#include "Tuner.h"
#include <android/hardware/tv/tuner/1.0/IFrontendCallback.h>
#include <utils/Log.h>
#include "Demux.h"
#include "Descrambler.h"
#include "Frontend.h"
#include "Lnb.h"
#include "HwFeState.h"

#define FE_POLL_TIMEOUT_MS 50
#define FE_STATE_TIMEOUT_MS 3000
#define FE_SIGNAL_CHECK_INTERVAL_MS 200
#define MAX_PLP_NUMBER 256

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

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
    mRequestTunningStop = false;
    mThreadState = STATE_INITIAL_IDLE;
    mPlpId = 0;
    if (type == FrontendType::ATSC3
        || type == FrontendType::ISDBS
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
    modulationStatus.dvbc(FrontendDvbcModulation::UNDEFINED);
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

int FrontendDevice::internalTune(const FrontendSettings & settings) {
    ALOGD("%s, id(%d)", __FUNCTION__, mDev.id);
    dvb_frontend_parameters fe_params;

    FrontendSettings tuneSettings = settings;
    mDev.feSettings = &tuneSettings;
    if (getFrontendSettings(&tuneSettings, &fe_params) <0) {
        ALOGE("[id:%d] Wrong delivery system in FrontendSetgings, or not support it.", mDev.id);
        sem_post(&threadSemaphore);
        return INVALID_ARGUMENT;
    }

    mDev.tuneFreq = adjustFrequencyOffSet(fe_params.frequency);
    if (!checkOpen(true)) {
        ALOGE("Open fe failed.");
        sem_post(&threadSemaphore);
        return UNAVAILABLE;
    }
    ALOGD("%s, frequency = %d", __FUNCTION__, mDev.tuneFreq);

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

        if ((settings.dvbt().standard == FrontendDvbtStandard::T2)) {
            cmd->cmd = DTV_DVBT2_PLP_ID_LEGACY;
            cmd->u.data = settings.dvbt().plpId;
            ALOGD("DTV DVBT2 plpId = %d", settings.dvbt().plpId);
            mPlpId = settings.dvbt().plpId;
            cmd ++;
            ncmd ++;
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
    struct dvb_frontend_info fe_info;

    if (!checkOpen(true)) return UNAVAILABLE;

    if (mDev.blindFreq == 0) {
        if (ioctl(mDev.devFd, FE_GET_INFO, &fe_info) < 0 ) {
            return UNKNOWN_ERROR;
        }
        mDev.blindFreq = fe_info.frequency_min;
    }
    requestTuneStop();
    updateThreadState(FrontendDevice::STATE_SCAN_START);
    return internalTune(settings);
}

int FrontendDevice::scan(const FrontendSettings & settings, FrontendScanType type) {
    int ret = 0;

    if (!checkOpen(true)) return UNAVAILABLE;

    if (type == FrontendScanType::SCAN_AUTO) {
        mDev.blindFreq = 0;
        requestTuneStop();
        updateThreadState(FrontendDevice::STATE_SCAN_START);
        ret = internalTune(settings);
    } else if (type == FrontendScanType::SCAN_BLIND) {
        ret = blindTune(settings);
    } else {
        mDev.blindFreq = 0;
        ret = INVALID_ARGUMENT;
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

uint32_t FrontendDevice::getLnbVoltage() {
    uint32_t lnbVoltage;
    fe_sec_voltage_t devVoltage;

    struct dtv_property p = {.cmd = DTV_VOLTAGE, .u.data = 0};
    struct dtv_properties props = {.num = 1, .props = &p};

    if (mDev.type != FrontendType::DVBS) {
        return static_cast<uint32_t>(LnbVoltage::NONE);
    }

    if (getFeProp(&props) != SUCCESS) {
        return static_cast<uint32_t>(LnbVoltage::NONE);
    }

    devVoltage = (fe_sec_voltage_t)(p.u.data);
    switch (devVoltage) {
        case SEC_VOLTAGE_13:
            lnbVoltage = static_cast<uint32_t>(LnbVoltage::VOLTAGE_13V);
            break;
        case SEC_VOLTAGE_18:
            lnbVoltage = static_cast<uint32_t>(LnbVoltage::VOLTAGE_18V);
            break;
        case SEC_VOLTAGE_OFF:
            lnbVoltage = static_cast<uint32_t>(LnbVoltage::NONE);
            break;
    }

    ALOGD("getLnbVoltage: %u(0:13,1:18,2:off)", devVoltage);
    return lnbVoltage;
}

uint32_t FrontendDevice::getSymbolRate() {
    struct dtv_property p = {.cmd = DTV_SYMBOL_RATE, .u.data = 0};
    struct dtv_properties props = {.num = 1, .props = &p};

    if (mDev.type != FrontendType::DVBC && mDev.type != FrontendType::DVBS) {
        return 0;
    }

    if (getFeProp(&props) != SUCCESS) {
        return 0;
    }

    ALOGD("getSymbolRate: %u", p.u.data);
    return (p.u.data);
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

vector<uint8_t> FrontendDevice::getMPLPIDList() {
    int retval = 0;
    struct dtv_property cmd;
    struct dtv_properties props;
    uint8_t plp_ids[MAX_PLP_NUMBER];
    vector<uint8_t> plpIds;
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
        memcpy(plpIds.data(), plp_ids, retval * sizeof(uint8_t));
    }

    for (int i = 0; i < retval; i++) {
        ALOGD("getMPLPIDList pipid[%d] = %d", i, plpIds[i]);
    }
    return plpIds;
}

uint8_t FrontendDevice::getCurrentMPlpId() {
    return mPlpId;
}

status_t FrontendDevice::readyToRun() {
    ALOGI("%s with frontendType(%d), id(%d)", __FUNCTION__, mDev.type, mDev.id);
    return NO_ERROR;
}

void FrontendDevice::onFirstRef(void) {
    mThreadState = STATE_INITIAL_IDLE;
    run("DroidFeTask");
}

bool FrontendDevice::threadLoop() {
    int state = getThreadState();
    bool stop;
    uint32_t start_time;
    struct pollfd pfd;
    struct dvb_frontend_event fe_event;

    if (state == FrontendDevice::STATE_TUNE_START
       || state == FrontendDevice::STATE_SCAN_START
       || state == FrontendDevice::STATE_TUNE_IDLE) {
        stop = false;
        int newState = getThreadState();
        if (mRequestTunningStop || newState == FrontendDevice::STATE_STOP) {
            stop = true;
        }
        if (state == FrontendDevice::STATE_TUNE_START
            || state == FrontendDevice::STATE_SCAN_START) {
            mDev.islocked = false;
        }
        {
            std::lock_guard<std::mutex> lock(mHwDevLock);
            pfd.fd = mDev.devFd;
        }
        pfd.events = POLLIN;
        pfd.revents = 0;
        for (start_time = getClockMilliSeconds();
             !stop && ((getClockMilliSeconds() - start_time) < FE_STATE_TIMEOUT_MS);) {
            if (poll(&pfd, 1, FE_POLL_TIMEOUT_MS) == 1) {
                if (ioctl(mDev.devFd, FE_GET_EVENT, &fe_event) >= 0) {
                    if ((fe_event.status & FE_HAS_LOCK) !=0
                        || (fe_event.status & FE_TIMEDOUT) != 0) {
                        break;
                    }
                }
            } else {
                if (state == FrontendDevice::STATE_TUNE_IDLE) {
                    //scan and tune need check several seconds for signal
                    //will not stable. and in ilde state, we just poll once
                    stop = true;
                }
            }
            newState = getThreadState();
            if (mRequestTunningStop || newState == FrontendDevice::STATE_STOP) {
                stop = true;
            }
        }
        newState = getThreadState();
        if (mRequestTunningStop || newState == FrontendDevice::STATE_STOP) {
            stop = true;
        }
        if (fe_event.status != 0 && !stop) {
            bool locked = ((fe_event.status & FE_HAS_LOCK) !=0);
            ALOGI("%s-(id:%d): get fe event: 0x%02x, locked=%d, dev_locked=%d", __FUNCTION__, mDev.id, fe_event.status, locked, mDev.islocked);
            if (state == STATE_SCAN_START) {
                ALOGD("%s-(id:%d): send scan event.", __FUNCTION__, mDev.id);
                mDev.islocked = locked;
                mContext->sendScanCallBack(mDev.tuneFreq, locked, false);
                updateThreadState(FrontendDevice::STATE_STOP);
            } else if (state == STATE_TUNE_START) {
                ALOGI("%s-(id:%d): send tune event.", __FUNCTION__, mDev.id);
                mDev.islocked = locked;
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
        if (mRequestTunningStop) {
            updateThreadState(FrontendDevice::STATE_STOP);
        }
        if (getThreadState() == FrontendDevice::STATE_TUNE_IDLE) {
            usleep(1000*FE_SIGNAL_CHECK_INTERVAL_MS);
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
    struct timeval t;
    t.tv_sec = t.tv_usec = 0;
    gettimeofday(&t, NULL);
    return t.tv_sec*1000 + t.tv_usec/1000;
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

    mRequestTunningStop = true;
    while (ready == false) {
        int state = getThreadState();
        ALOGV("%s-(id:%d): state=%d", __FUNCTION__, mDev.id, state);
        if (state == FrontendDevice::STATE_STOP
            || state == FrontendDevice::STATE_INITIAL_IDLE) {
            ready = true;
        }
    }
    mRequestTunningStop = false;
}

FrontendSettings* FrontendDevice::getFeSetting() {
    return mDev.feSettings;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
