/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */



//#define printf_LEVEL 5

//#include "am_mem.h"
//#include <am_misc.h>
#define LOG_NDEBUG 0
#define LOG_TAG "AmLinuxDvb"

#include <utils/Log.h>
#include <cutils/properties.h>

#include <stdio.h>

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
/*add for config define for linux dvb *.h*/
//#include <am_config.h>
#include <AmLinuxDvb.h>
#include <AmDmx.h>

#include <dmx.h>
#include <TSPHandler.h>

AmLinuxDvb::AmLinuxDvb() {
    ALOGI("%s/%d", __FUNCTION__, __LINE__);
    mDvrFd = -1;
    pollFailCount = 0;
    mFilterMemInfoFd = -1;
}

AmLinuxDvb::~AmLinuxDvb() {
    if (mFilterMemInfoFd != -1) {
        close(mFilterMemInfoFd);
    }

    ALOGI("%s/%d", __FUNCTION__, __LINE__);
}

AM_ErrorCode_t AmLinuxDvb::dvb_open(AM_DMX_Device *dev) {
    DVBDmx_t *dmx;
    int i;
    ALOGI("%s/%d", __FUNCTION__, __LINE__);

    dmx = (DVBDmx_t*)malloc(sizeof(DVBDmx_t));
    if (!dmx) {
        ALOGE("not enough memory");
        return AM_DMX_ERR_NO_MEM;
    }
    snprintf(dmx->dev_name, sizeof(dmx->dev_name), "/dev/dvb0.demux%d", dev->dev_no);
    for (i = 0; i < DMX_FILTER_COUNT; i++) {
        dmx->fd[i] = -1;
       }
    dev->drv_data = dmx;
    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_close(AM_DMX_Device *dev) {
    ALOGI("%s/%d", __FUNCTION__, __LINE__);

    DVBDmx_t *dmx = (DVBDmx_t*)dev->drv_data;
    if (dmx != NULL) {
        free(dmx);
    }
    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_alloc_filter(AM_DMX_Device *dev, AM_DMX_Filter *filter) {
    DVBDmx_t *dmx = (DVBDmx_t*)dev->drv_data;
    int fd = -1;
    ALOGI("%s/%d", __FUNCTION__, __LINE__);

    fd = open(dmx->dev_name, O_RDWR);
    if (fd == -1) {
        ALOGE("cannot open \"%s\" (%s)", dmx->dev_name, strerror(errno));
        return AM_DMX_ERR_CANNOT_OPEN_DEV;
    }

    dmx->fd[filter->id] = fd;
    filter->drv_data = (void*)(long)fd;
    ALOGI("open (%s) ok! fd:%d \n",dmx->dev_name,fd);
    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_free_filter(AM_DMX_Device *dev, AM_DMX_Filter *filter) {
    ALOGI("%s/%d", __FUNCTION__, __LINE__);

    DVBDmx_t *dmx = (DVBDmx_t*)dev->drv_data;
    int fd = (long)filter->drv_data;
    close(fd);
    dmx->fd[filter->id] = -1;
    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_get_stc(AM_DMX_Device *dev, AM_DMX_Filter *filter) {
    ALOGI("%s/%d", __FUNCTION__, __LINE__);

    UNUSED(dev);
    int fd = (long)filter->drv_data;
    int ret;
    struct dmx_stc stc;
    int i = 0;
    for (i = 0; i < 3; i++) {
        memset(&stc, 0, sizeof(struct dmx_stc));
        stc.num = i;
        ret = ioctl(fd, DMX_GET_STC, &stc);
        if (ret == 0) {
            ALOGI("get stc num %d: base:0x%0x, stc:0x%llx\n", stc.num, stc.base, stc.stc);
        } else {
            ALOGE("get stc %d, fail\n", i);
        }
    }
    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_set_sec_filter(AM_DMX_Device *dev, AM_DMX_Filter *filter, const struct dmx_sct_filter_params *params) {
    ALOGI("%s/%d", __FUNCTION__, __LINE__);

    UNUSED(dev);
    struct dmx_sct_filter_params p;
    int fd = (long)filter->drv_data;
    int ret;
    p = *params;
    /*
    if (p.filter.mask[0] == 0) {
    p.filter.filter[0] = 0x00;
    p.filter.mask[0]   = 0x80;
    }
    */
    ret = ioctl(fd, DMX_SET_FILTER, &p);
    if (ret ==-1) {
        ALOGE("set section filter failed (%s)", strerror(errno));
        return AM_DMX_ERR_SYS;
    }

    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_set_pes_filter(AM_DMX_Device *dev, AM_DMX_Filter *filter, const struct dmx_pes_filter_params *params) {
    ALOGI("%s/%d", __FUNCTION__, __LINE__);

    UNUSED(dev);
    int fd = (long)filter->drv_data;
    int ret;
    fcntl(fd,F_SETFL,O_NONBLOCK);

    ret = ioctl(fd, DMX_SET_PES_FILTER, params);
    if (ret == -1) {
        ALOGE("set section filter failed (%s)", strerror(errno));
        return AM_DMX_ERR_SYS;
    }
    ALOGI("%s success\n", __FUNCTION__);
    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_enable_filter(AM_DMX_Device *dev, AM_DMX_Filter *filter, bool enable) {
    ALOGI("%s/%d", __FUNCTION__, __LINE__);

    UNUSED(dev);
    int fd = (long)filter->drv_data;
    int ret;
    if (enable) {
        ret = ioctl(fd, DMX_START, 0);
    } else {
        ret = ioctl(fd, DMX_STOP, 0);
    }

    if (ret < 0) {
        ALOGE("start filter failed (%s)", strerror(errno));
        return AM_DMX_ERR_SYS;
    }

    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_get_mem_info(AM_DMX_Filter *filter, dmx_mem_info* mDmxMenInfo) {
    int fd = (long)filter->drv_data;
    int ret;
    ALOGV("%s/%d", __FUNCTION__, __LINE__);
    memset(mDmxMenInfo, 0, sizeof(struct dmx_mem_info));
    ret = ioctl(fd, DMX_GET_MEM_INFO, mDmxMenInfo);
    if (ret == -1) {
        ALOGE("get mem info failed (%s)", strerror(errno));
        return AM_DMX_ERR_SYS;
    }

    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_get_filter_mem_info(AM_DMX_Device *dev, dmx_filter_mem_info* mDmxFilterMemInfo) {

    //int fd = 0;//(long)filter->drv_data;
    int ret;
    if (mFilterMemInfoFd == -1) {
        char dev_name[32];
        snprintf(dev_name, 32, "/dev/dvb0.demux%d", dev->dev_no);
        mFilterMemInfoFd = open(dev_name, O_RDWR);
        if (mFilterMemInfoFd == -1) {
            ALOGE("cannot open \"%s\" (%s)", dev_name, strerror(errno));
            return AM_DMX_ERR_CANNOT_OPEN_DEV;
        }
    }

    memset(mDmxFilterMemInfo, 0, sizeof(struct dmx_filter_mem_info));
    ret = ioctl(mFilterMemInfoFd, DMX_GET_FILTER_MEM_INFO, mDmxFilterMemInfo);
    if (ret == -1) {
        ALOGE("get filter mem info failed (%s)", strerror(errno));
        return AM_DMX_ERR_SYS;
    }
    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_set_buf_size(AM_DMX_Device *dev, AM_DMX_Filter *filter, int size) {
    ALOGI("%s/%d", __FUNCTION__, __LINE__);
    UNUSED(dev);
    int fd = (long)filter->drv_data;
    int ret;
    ret = ioctl(fd, DMX_SET_BUFFER_SIZE, size);
    if (ret == -1) {
        ALOGE("set buffer size failed (%s)", strerror(errno));
        return AM_DMX_ERR_SYS;
    }

    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_poll(AM_DMX_Device *dev, AM_DMX_FilterMask_t *mask, int timeout) {
    DVBDmx_t *dmx = (DVBDmx_t*)dev->drv_data;
    struct pollfd fds[DMX_FILTER_COUNT];
    int fids[DMX_FILTER_COUNT];
    int i, cnt = 0, ret;

    for (i = 0; i < DMX_FILTER_COUNT; i++) {
        if (dmx->fd[i] != -1) {
            fds[cnt].events = POLLIN|POLLERR;
            fds[cnt].fd     = dmx->fd[i];
            fids[cnt] = i;
            cnt++;
        }
    }

    if (!cnt) {
        return AM_DMX_ERR_TIMEOUT;
    }

    ret = poll(fds, cnt, timeout);
    if (ret <= 0) {
        pollFailCount++;
        if (pollFailCount % 20 == 0) {
            ALOGE("dvb_poll error cnt:%d pollFailCount:%d (%s)",
                cnt, pollFailCount, strerror(errno));
        }
        return AM_DMX_ERR_TIMEOUT;
    }

    for (i = 0; i < cnt; i++) {
        if (fds[i].revents&(POLLIN|POLLERR)) {
            pollFailCount = 0;
            //ALOGI("dvb_poll  i:%d cnt:%d fd:%d\n",i,cnt,fds[i].fd);
            AM_DMX_FILTER_MASK_SET(mask, fids[i]);
        }
    }

    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvb_read(AM_DMX_Device *dev, AM_DMX_Filter *filter, uint8_t *buf, int *size,bool pollflag) {
    UNUSED(dev);

    int fd = (long)filter->drv_data;
    int len = *size;
    int ret;
    //struct pollfd pfd;

    if (fd == -1) {
        ALOGI("dvb_read fd:%d\n", fd);
        return AM_DMX_ERR_NOT_ALLOCATED;
    }
    if (pollflag) {
        struct pollfd pfd;
        pfd.events = POLLIN|POLLERR;
        pfd.fd     = fd;

        ret = poll(&pfd, 1, 0);
        if (ret <= 0) {
            ALOGW("dvb_read dmx poll no data ret=%d!", ret);
            return AM_DMX_ERR_NO_DATA;
        }
    }
    ret = read(fd, buf, len);
    if (ret < 0) {
        if (errno != EAGAIN)
            ALOGE("read demux failed (%s) %d", strerror(errno), errno);
        if (errno == ETIMEDOUT) {
            return AM_DMX_ERR_TIMEOUT;
        }
        return AM_DMX_ERR_SYS;
    }

    *size = ret;
    return AM_SUCCESS;
}

int AmLinuxDvb::getDmaByDemuxId(int demuxId) {
    switch (demuxId) {
        case 0:
            return DMA_0;
        case 1:
            return DMA_1;
        case 2:
            return DMA_2;
        case 3:
            return DMA_3;
        case 4:
            return DMA_4;
        case 5:
            return DMA_5;
        case 6:
            return DMA_6;
        case 7:
            return DMA_7;
        default:
            assert(0);
    }
    return -1;
}

AM_ErrorCode_t AmLinuxDvb::dvb_set_source(AM_DMX_Device *dev, dmx_input_source_t inputSource) {
    DVBDmx_t *dmx = (DVBDmx_t*)dev->drv_data;
    int fd = -1;
    int ret = 0;
    ALOGI("%s/%d", __FUNCTION__, __LINE__);

    fd = open(dmx->dev_name, O_RDWR);
    if (fd == -1) {
        ALOGE("cannot open \"%s\" (%s)", dmx->dev_name, strerror(errno));
        return AM_DMX_ERR_CANNOT_OPEN_DEV;
    }

    if (inputSource == INPUT_LOCAL) {
        ALOGI("set ---> INPUT_LOCAL \n");
        ret = ioctl(fd, DMX_SET_INPUT, INPUT_LOCAL);
        ALOGI("DMX_SET_INPUT ret:%d\n", ret);
        ret = ioctl(fd, DMX_SET_HW_SOURCE, getDmaByDemuxId(dev->dev_no));
        ALOGI("DMX_SET_HW_SOURCE ret:%d\n", ret);
    } else if (inputSource == INPUT_DEMOD) {
        ALOGI("set ---> INPUT_DEMOD \n" );
        ret = ioctl(fd, DMX_SET_INPUT, INPUT_DEMOD);
        ALOGI("DMX_SET_INPUT ret:%d\n", ret);
        ret = ioctl(fd, DMX_SET_HW_SOURCE, FRONTEND_TS0);
        ALOGI("DMX_SET_HW_SOURCE ret:%d\n", ret);
    }
    return AM_SUCCESS;
}

AM_ErrorCode_t AmLinuxDvb::dvr_open(AM_DMX_Device *dev, dmx_input_source_t inputSource) {
    //int ret = 0;
    char name[32];
    ALOGI("%s/%d", __FUNCTION__, __LINE__);

    snprintf(name, sizeof(name), "/dev/dvb0.dvr%d", dev->dev_no);
    mDvrFd = open(name, O_WRONLY);
    if (mDvrFd == -1) {
        ALOGE("cannot open \"%s\" (%s)", name, strerror(errno));
        return -1;
    }
    ALOGI("open %s ok mDvrFd = %d\n", name, mDvrFd);
    dvb_set_source(dev, inputSource);
    return AM_SUCCESS;
}

int AmLinuxDvb::dvr_data_write(uint8_t *buf, int size,uint64_t timeout) {
    int ret;
    int left = size;
    uint8_t *p = buf;
    int64_t nowUs = TSPLooper::GetNowUs();
    //timeout *= 10;
    if (mDvrFd < 0) {
        ALOGE("mDvrFd:%d", mDvrFd);
        return -1;
    }
    while (left > 0) {

        ret = write(mDvrFd, p, left);

        if (ret == -1) {
            if (errno != EINTR) {
                ALOGE("Write DVR data failed: %s", strerror(errno));
                break;
            }
            ret = 0;
        }

        left -= ret;
        p += ret;
        if (TSPLooper::GetNowUs() - nowUs > timeout) {
            ALOGE("dvr_data_write timeout(%lld) \n",timeout);
            break;
        }
    }

    return (size - left);
}

AM_ErrorCode_t AmLinuxDvb::dvr_close(void) {
    ALOGI("%s/%d", __FUNCTION__, __LINE__);

    if (mDvrFd > 0) {
        close(mDvrFd);
    }
    return AM_SUCCESS;
}

#if 0
AM_ErrorCode_t AmLinuxDvb::dvb_set_source(AM_DMX_Device *dev, AM_DMX_Source_t src) {
    char buf[32];
    char *cmd;

    snprintf(buf, sizeof(buf), "/sys/class/stb/demux%d_source", dev->dev_no);

    switch (src)
    {
        case AM_DMX_SRC_TS0:
            cmd = "ts0";
            break;
        case AM_DMX_SRC_TS1:
            cmd = "ts1";
            break;
#if defined(CHIP_8226M) || defined(CHIP_8626X)
        case AM_DMX_SRC_TS2:
            cmd = "ts2";
            break;
#endif
        case AM_DMX_SRC_TS3:
            cmd = "ts3";
            break;
        case AM_DMX_SRC_HIU:
            cmd = "hiu";
            break;
        case AM_DMX_SRC_HIU1:
            cmd = "hiu1";
            break;
        default:
            ALOGE("do not support demux source %d", src);
        return AM_DMX_ERR_NOT_SUPPORTED;
    }
    return 0;
    return AM_FileEcho(buf, cmd);
}
#endif
