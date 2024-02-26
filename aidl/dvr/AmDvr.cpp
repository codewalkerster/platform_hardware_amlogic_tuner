/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AMDVR"

#include "AmDvr.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <utils/Log.h>
#include <poll.h>
#include <unistd.h>
#include <sys/prctl.h>

/****************************************************************************
* Static functions
***************************************************************************/

static AM_ErrorCode_t dvr_open(AM_DVR_Device_t *dev, dmx_input_source_t inputSource)
{
    char dev_name[32];
    int fd;
    //int ret = -1;

    snprintf(dev_name, sizeof(dev_name), "/dev/dvb0.dvr%d", dev->dev_no);
    ALOGI("dvr_open dev_name:%s\n", dev_name);
    fd = open(dev_name, O_RDONLY);
    if (fd == -1)
    {
        ALOGD("cannot open \"%s\" (%s)", dev_name, strerror(errno));
        return AM_DVR_ERR_CANNOT_OPEN_DEV;
    }
    int ret = ioctl(fd, DMX_SET_BUFFER_SIZE, 60 * 188 * 1024);
    if (ret == -1) {
        ALOGE("set buffer size failed (%s)", strerror(errno));
        close(fd);
        return -1;

    }
    //fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK, 0);
    int ret_0 = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK, 0);
    if (ret_0<0) {
        ALOGE("fcntl failed %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    dev->drv_data = (void*)(long)fd;
    return AM_SUCCESS;
}

static AM_ErrorCode_t setDvbSource(AM_DVR_Device_t *dev, dmx_input_source_t inputSource, int ts_input) {
    char dev_name[32];
    int fd;
    int ret = -1;

    snprintf(dev_name, sizeof(dev_name), "/dev/dvb0.demux%d", dev->dev_no);
    fd = open(dev_name, O_RDWR);
    if (fd == -1)
    {
        ALOGD("cannot open \"%s\" (%s)", dev_name, strerror(errno));
        return AM_DVR_ERR_CANNOT_OPEN_DEV;
    }

    if (inputSource == INPUT_DEMOD) {
        ALOGI("set ---> INPUT_DEMOD \n" );
        ret = ioctl(fd, DMX_SET_INPUT, INPUT_DEMOD);
        ALOGI("DMX_SET_INPUT ret:%d\n", ret);
        ret = ioctl(fd, DMX_SET_HW_SOURCE, ts_input);
        ALOGI("DMX_SET_HW_SOURCE ret:%d\n", ret);
    } else if (inputSource == INPUT_LOCAL) {
        ALOGI("set ---> INPUT_LOCAL \n" );
        ret = ioctl(fd, DMX_SET_INPUT, INPUT_LOCAL);
        ALOGI("DMX_SET_INPUT ret:%d\n", ret);
        ret = ioctl(fd, DMX_SET_HW_SOURCE, ts_input);
        ALOGI("DMX_SET_HW_SOURCE ret:%d\n", ret);
    }
    if (ret < 0) {
        ALOGE("dvr_open ioctl failed %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    dev->dmx_fd = fd;
    return AM_SUCCESS;

}

static AM_ErrorCode_t dvr_close(AM_DVR_Device_t *dev)
{
    int fd = (long)dev->drv_data;
    if (fd > 0) {
        close(fd);
        fd = -1;
    }

    int dmx_fd = dev->dmx_fd;
    if (dmx_fd > 0) {
        close(dmx_fd);
        dmx_fd = -1;
    }
    return AM_SUCCESS;
}

static AM_ErrorCode_t dvr_poll(AM_DVR_Device_t *dev, int timeout)
{
    int fd = (long)dev->drv_data;
    struct pollfd fds;
    int ret;

    fds.events = POLLIN|POLLERR;
    fds.fd     = fd;

    ret = poll(&fds, 1, timeout);
    if (ret <= 0)
    {
        return AM_DVR_ERR_TIMEOUT;
    }

    return AM_SUCCESS;
}

static AM_ErrorCode_t dvr_read(AM_DVR_Device_t *dev, uint8_t *buf, int *size)
{
    int fd = (long)dev->drv_data;
    int len = *size;
    int ret;

    if (fd == -1)
        return AM_DVR_ERR_NOT_ALLOCATED;

    ret = read(fd, buf, len);
    if (ret <= 0)
    {
        if (errno == ETIMEDOUT)
            return AM_DVR_ERR_TIMEOUT;
        //ALOGD("read dvr failed (%s) %d", strerror(errno), errno);
        return AM_DVR_ERR_SYS;
    }

    *size = ret;
    return AM_SUCCESS;
}

static int getTsInputById(uint32_t tsInputId) {
    switch (tsInputId) {
        case 32:
            return FRONTEND_TS0;
        case 33:
            return FRONTEND_TS1;
        case 34:
            return FRONTEND_TS2;
        case 35:
            return FRONTEND_TS3;
        case 36:
            return FRONTEND_TS4;
        case 37:
            return FRONTEND_TS5;
        case 38:
            return FRONTEND_TS6;
        case 39:
            return FRONTEND_TS7;
        default:
            assert(0);
    }
    return -1;
}

AmDvr::AmDvr(uint32_t demuxId) {
    ALOGD("%s/%d demuxId = %d", __FUNCTION__, __LINE__, demuxId);

    mDmxId = demuxId;
    mDvrDevice = new AM_DVR_Device_t;
    mDvrDevice->dev_no = demuxId;
    mDvrDevice->dmx_no = demuxId;
    mDvrDevice->dmx_fd = -1;
    mData = new AM_DVR_Data();
    mData->cb = NULL;
    mData->user_data = NULL;
    opencnt = 0;
    enable_thread = false;
    thread = 0;
}

AmDvr::~AmDvr() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    if (mDvrDevice != NULL) {
        delete mDvrDevice;
        mDvrDevice = NULL;
    }
    if (mData != NULL) {
        delete mData;
        mData = NULL;
    }
}

AM_ErrorCode_t AmDvr::AM_DVR_Open(dmx_input_source inputSource, uint32_t ts_input, bool bsetInput)
{
    ALOGD("%s/%d dev_no = %d", __FUNCTION__, __LINE__, mDvrDevice->dev_no);
    if (opencnt > 0) {
        ALOGI("dvr device %d has already been opened", mDvrDevice->dev_no);
        //opencnt++;
        return AM_SUCCESS;
    }

    AM_ErrorCode_t ret = dvr_open(mDvrDevice, inputSource);
    if (bsetInput) {
        ret = setDvbSource(mDvrDevice, inputSource, getTsInputById(ts_input));
    }
    if (ret == AM_SUCCESS) {
        pthread_mutex_init(&lock, NULL);
        pthread_cond_init(&cond, NULL);
        enable_thread = true;
        if (pthread_create(&thread, NULL, dvr_data_thread, this)) {
            pthread_mutex_destroy(&lock);
            pthread_cond_destroy(&cond);
            ret = AM_DVR_ERR_CANNOT_CREATE_THREAD;
        }
    }

    if (ret == AM_SUCCESS) {
        opencnt ++;
    }

    return ret;
}

AM_ErrorCode_t AmDvr::AM_DVR_Close()
{
    AM_ErrorCode_t ret = AM_SUCCESS;
    ALOGD("%s/%d opencnt = %d", __FUNCTION__, __LINE__, opencnt);

    if (opencnt > 0) {
        enable_thread = false;
        pthread_join(thread, NULL);
        if (mDvrDevice != NULL) {
            ret = dvr_close(mDvrDevice);
        }
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&cond);
    }

    opencnt--;
    return ret;
}

AM_ErrorCode_t AmDvr::AM_DVR_Read(uint8_t *buf, int *size)
{
    AM_ErrorCode_t ret = dvr_read(mDvrDevice, buf, size);
    return ret;
}


AM_ErrorCode_t AmDvr::AM_DVR_SetCallback(AM_DVR_DataCb cb, void* data) {
    mData->cb = cb;
    mData->user_data = data;
    return AM_SUCCESS;
}

void* AmDvr::dvr_data_thread(void *arg) {
    AmDvr *dev = (AmDvr*)arg;
    AM_ErrorCode_t ret;
    //int cnt;
    //uint8_t buf[256*1024];
    prctl(PR_SET_NAME, "dvr_data_thread");

    while (dev != NULL && dev->mDvrDevice != NULL && dev->enable_thread) {
        ret = dvr_poll(dev->mDvrDevice, 1000);
        if (ret == AM_SUCCESS ) {
            if (dev->mData != NULL && dev->mData->cb != NULL) {
                 dev->mData->cb(dev->mData->user_data);
            }
        /*
            ret = dev->drv->dvr_read(buf, &cnt);
            if (cnt > sizeof(buf))
            {
                ALOGE("return size:0x%0x bigger than 0x%0x input buf\n",cnt, sizeof(buf));
                break;
            }
            ALOGE("read from DVR0 return %d bytes\n", cnt);
        }*/

        } else {
             usleep(10 * 1000);
        }
    }

    return NULL;
}



