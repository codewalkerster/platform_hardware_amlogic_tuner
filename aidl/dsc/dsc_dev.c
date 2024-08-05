/*
 * Copyright (c) 2021 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "cas_dsc_dev"
#include "utils/Log.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "dsc_dev.h"

#define MAX_DSC_DEV 16
#define DEV_NAME "/dev/dvb0.ca"

struct _dsc_dev{
    size_t use_count;
    int fd;
};
typedef struct _dsc_dev dsc_dev;

static dsc_dev dvb_dsc_dev[MAX_DSC_DEV] = {0};
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
bool dsc_inited = false;

int ca_init(void)
{
    pthread_mutex_lock(&lock);
    if (!dsc_inited) {
        memset(&dvb_dsc_dev[0], 0, sizeof(dvb_dsc_dev));
        dsc_inited = true;
    } else {
        ALOGW("ca dev has been initialized!");
    }
    pthread_mutex_unlock(&lock);
    ALOGI("ca_init success");

    return CA_DSC_OK;
}

int ca_open(int devno)
{
    int ret, fd;
    char buf[32];

    pthread_mutex_lock(&lock);

    if (devno >= MAX_DSC_DEV) {
        ALOGE("ca_open invalid devno:%d!", devno);
        ret = CA_DSC_ERROR;
        goto ERROR_EXIT;
    }
    if (dvb_dsc_dev[devno].use_count > 0) {
        dvb_dsc_dev[devno].use_count += 1;
        ALOGD("devno: %d use_count: %zd", devno, dvb_dsc_dev[devno].use_count);
        ret = CA_DSC_OK;
        goto ERROR_EXIT;
    }

    snprintf(buf, sizeof(buf), DEV_NAME"%d", devno);
    fd = open(buf, O_RDWR);
    if (fd == -1)
    {
        ALOGE("open %s%d (%d:%s) failed!", DEV_NAME, devno, errno, strerror(errno));
        ret = CA_DSC_ERROR;
        goto ERROR_EXIT;
    }
    dvb_dsc_dev[devno].fd = fd;
    dvb_dsc_dev[devno].use_count += 1;
    ALOGD("ca_open %s%d success. fd: 0x%x", DEV_NAME, devno, dvb_dsc_dev[devno].fd);

    pthread_mutex_unlock(&lock);
    return CA_DSC_OK;
ERROR_EXIT:
    pthread_mutex_unlock(&lock);
    return ret;
}

int ca_alloc_chan(int devno, unsigned int pid, int algo, int dsc_type)
{
    int ret, fd;
    struct ca_sc2_descr_ex desc;
    memset(&desc, 0, sizeof(desc));

    pthread_mutex_lock(&lock);

    ALOGI("ca_alloc_chan devno:%d pid:0x%0x algo:%d dsc_type:%d", devno, pid, algo, dsc_type);
    desc.cmd = CA_ALLOC;
    desc.params.alloc_params.pid = pid;
    desc.params.alloc_params.algo = algo;
    desc.params.alloc_params.dsc_type = dsc_type;
    desc.params.alloc_params.ca_index = -1;
    desc.params.alloc_params.loop = 0;

    if (devno >= MAX_DSC_DEV || dvb_dsc_dev[devno].use_count == 0) {
        ALOGE("ca_alloc_chan failed! devno:%d", devno);
        goto ERROR_EXIT;
    }
    fd = dvb_dsc_dev[devno].fd;
    ret = ioctl(fd, CA_SC2_SET_DESCR_EX, &desc);
    if (ret != 0) {
        ALOGE("ca_alloc_chan ioctl failed! ret:0x%0x", ret);
        goto ERROR_EXIT;
    }

    pthread_mutex_unlock(&lock);
    ALOGI("ca_alloc_chan index:%d", desc.params.alloc_params.ca_index);
    return desc.params.alloc_params.ca_index;
ERROR_EXIT:
    pthread_mutex_unlock(&lock);
    return CA_DSC_ERROR;
}

int ca_free_chan(int devno, int index)
{
    int ret = CA_DSC_OK, fd = -1;
    struct ca_sc2_descr_ex desc;
    memset(&desc, 0, sizeof(desc));

    pthread_mutex_lock(&lock);

    ALOGI("ca_free_chan  devno:%d index:%d", devno, index);
    desc.cmd = CA_FREE;
    desc.params.free_params.ca_index = index;

    if (devno >= MAX_DSC_DEV || dvb_dsc_dev[devno].use_count == 0) {
        ALOGE("ca_free_chan failed! devno:%d", devno);
        goto ERROR_EXIT;
    }
    fd = dvb_dsc_dev[devno].fd;
    ret = ioctl(fd, CA_SC2_SET_DESCR_EX, &desc);
    if (ret != 0) {
        ALOGE("ca_free_chan ioctl failed! ret:0x%0x", ret);
        goto ERROR_EXIT;
    }

    pthread_mutex_unlock(&lock);
    ALOGI("ca_free_chan index:%d\n", index);
    return CA_DSC_OK;
ERROR_EXIT:
    pthread_mutex_unlock(&lock);
    return CA_DSC_ERROR;
}


int ca_set_key(int devno, int index, int parity, uint32_t key_index)
{
    int ret = CA_DSC_OK, fd = -1;
    struct ca_sc2_descr_ex desc;
    memset(&desc, 0, sizeof(desc));

    pthread_mutex_lock(&lock);

    ALOGI("ca_set_key devno:%d index:%d parity:%d key_index:%#x", devno, index, parity, key_index);
    desc.cmd = CA_KEY;
    desc.params.key_params.ca_index = index;
    desc.params.key_params.parity = parity;
    desc.params.key_params.key_index = key_index;

    if (devno >= MAX_DSC_DEV || dvb_dsc_dev[devno].use_count == 0) {
        ALOGE("ca_set_key failed! devno:%d\n", devno);
        goto ERROR_EXIT;
    }
    fd = dvb_dsc_dev[devno].fd;
    ret = ioctl(fd, CA_SC2_SET_DESCR_EX, &desc);
    if (ret != 0) {
        ALOGE("ca_set_key ioctl failed! ret:0x%0x", ret);
        goto ERROR_EXIT;
    }

    pthread_mutex_unlock(&lock);
    ALOGI("ca_set_key ok index:%d parity:%d key_index:%#x", index, parity, key_index);
    return CA_DSC_OK;
ERROR_EXIT:
    pthread_mutex_unlock(&lock);
    return CA_DSC_ERROR;
}

int ca_close(int devno)
{
    int fd = -1;
    pthread_mutex_lock(&lock);

    if (devno >= MAX_DSC_DEV || dvb_dsc_dev[devno].use_count == 0) {
        ALOGE("ca_close failed! devno:%d", devno);
        goto ERROR_EXIT;
    }

    dvb_dsc_dev[devno].use_count -= 1;
    ALOGI("ca_close use_count:%zd", dvb_dsc_dev[devno].use_count);
    fd = dvb_dsc_dev[devno].fd;

    if (dvb_dsc_dev[devno].use_count == 0) {
        ALOGI("ca_close fd:0x%x", fd);
        close(fd);
        dvb_dsc_dev[devno].fd = -1;
    }

    pthread_mutex_unlock(&lock);
    return CA_DSC_OK;
ERROR_EXIT:
    pthread_mutex_unlock(&lock);
    return CA_DSC_ERROR;
}

int ca_set_scb(int devno, int ca_index,int ca_scb, int ca_scb_as_is)
{
    int ret = CA_DSC_OK, fd = -1;
    struct ca_sc2_descr_ex desc;
    memset(&desc, 0, sizeof(desc));

    pthread_mutex_lock(&lock);

    if (devno >= MAX_DSC_DEV || dvb_dsc_dev[devno].use_count == 0) {
        ALOGE("ca_set_scb failed! devno:%d", devno);
        goto ERROR_EXIT;
    }
    desc.cmd = CA_SET_SCB;
    desc.params.scb_params.ca_index = ca_index;
    desc.params.scb_params.ca_scb = ca_scb;
    desc.params.scb_params.ca_scb_as_is = ca_scb_as_is;

    fd = dvb_dsc_dev[devno].fd;
    ret = ioctl(fd, CA_SC2_SET_DESCR_EX, &desc);
    if (ret != 0) {
        ALOGE("ca_set_scb ioctl failed! ret:0x%0x", ret);
        return CA_DSC_ERROR;
    }
    ALOGI("ca_set_scb ca_index:%d ca_scb:%d ca_scb_as_is:%d ret:0x%x\n", \
        desc.params.scb_params.ca_index, desc.params.scb_params.ca_scb, \
        desc.params.scb_params.ca_scb_as_is, ret);

    pthread_mutex_unlock(&lock);
    return CA_DSC_OK;
ERROR_EXIT:
    pthread_mutex_unlock(&lock);
    return CA_DSC_ERROR;
}

