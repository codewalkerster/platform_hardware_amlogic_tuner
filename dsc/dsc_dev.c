/*
 * Copyright (c) 2021 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "tuner_dsc_dev"
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
    uint8_t used;
    int fd;
};
typedef struct _dsc_dev dsc_dev;
static dsc_dev dvb_dsc_dev[MAX_DSC_DEV];
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
bool dsc_inited = false;

int ca_init(void)
{
    pthread_mutex_lock(&lock);

    if (!dsc_inited) {
        ALOGI("ca dsc inited");
        dsc_inited = true;
    }

    pthread_mutex_unlock(&lock);

    return CA_DSC_OK;
}

int ca_open(int devno)
{
    int fd;
    char buf[32];

    pthread_mutex_lock(&lock);

    if (devno >= MAX_DSC_DEV) {
        ALOGE("ca_open failed! devno:%d\n", devno);
        goto ERROR_EXIT;
    }
    if (dvb_dsc_dev[devno].used) {
        ALOGW("dvb_dsc_dev[%d] has been used!", devno);
        goto ERROR_EXIT;
    }

    snprintf(buf, sizeof(buf), DEV_NAME"%d", devno);
    fd = open(buf, O_RDWR);
    if (fd == -1)
    {
        ALOGE("Cannot open %s%d (%d:%s)", DEV_NAME, devno, errno, strerror(errno));
        goto ERROR_EXIT;
    }
    dvb_dsc_dev[devno].fd = fd;
    dvb_dsc_dev[devno].used = 1;

    pthread_mutex_unlock(&lock);

    return CA_DSC_OK;

ERROR_EXIT:
    pthread_mutex_unlock(&lock);
    return CA_DSC_ERROR;
}

int ca_alloc_chan(int devno, unsigned int pid, int algo, int dsc_type)
{
    int ret, fd;
    struct ca_sc2_descr_ex desc;

    pthread_mutex_lock(&lock);

    ALOGI("ca_alloc_chan dev:%d, pid:0x%0x, algo:%d, dsc_type:%d\n", devno, pid, algo, dsc_type);
    desc.cmd = CA_ALLOC;
    desc.params.alloc_params.pid = pid;
    desc.params.alloc_params.algo = algo;
    desc.params.alloc_params.dsc_type = dsc_type;
    desc.params.alloc_params.ca_index = -1;

    if (devno >= MAX_DSC_DEV || !dvb_dsc_dev[devno].used) {
        ALOGE("ca_alloc_chan failed! devno:%d used:%d\n", devno, dvb_dsc_dev[devno].used);
        goto ERROR_EXIT;
    }
    fd = dvb_dsc_dev[devno].fd;
    ret = ioctl(fd, CA_SC2_SET_DESCR_EX, &desc);
    if (ret != 0) {
        ALOGE(" ca_alloc_chan ioctl fail, ret:0x%0x\n", ret);
        goto ERROR_EXIT;
    }

    pthread_mutex_unlock(&lock);
    ALOGI("ca_alloc_chan, index:%d\n", desc.params.alloc_params.ca_index);

    return desc.params.alloc_params.ca_index;
ERROR_EXIT:
    pthread_mutex_unlock(&lock);
    return CA_DSC_ERROR;
}

int ca_free_chan(int devno, int index)
{
    int ret, fd;
    struct ca_sc2_descr_ex desc;

    pthread_mutex_lock(&lock);

    ALOGI("ca_free_chan:%d, index:%d\n", devno, index);
    desc.cmd = CA_FREE;
    desc.params.free_params.ca_index = index;

    if (devno >= MAX_DSC_DEV || !dvb_dsc_dev[devno].used) {
        ALOGE("ca_free_chan failed! devno:%d used:%d\n", devno, dvb_dsc_dev[devno].used);
        goto ERROR_EXIT;
    }
    fd = dvb_dsc_dev[devno].fd;
    ret = ioctl(fd, CA_SC2_SET_DESCR_EX, &desc);
    if (ret != 0) {
        ALOGE(" ca_free_chan ioctl fail\n");
        goto ERROR_EXIT;
    }

    pthread_mutex_unlock(&lock);
    ALOGI("ca_free_chan, index:%d\n", index);

    return CA_DSC_OK;
ERROR_EXIT:
    pthread_mutex_unlock(&lock);
    return CA_DSC_ERROR;
}


int ca_set_key(int devno, int index, int parity, uint32_t key_index)
{
    int ret, fd;
    struct ca_sc2_descr_ex desc;

    pthread_mutex_lock(&lock);

    ALOGI("ca_set_key dev:%d, index:%d, parity:%d, key_index:%#x\n",
            devno, index, parity, key_index);
    desc.cmd = CA_KEY;
    desc.params.key_params.ca_index = index;
    desc.params.key_params.parity = parity;
    desc.params.key_params.key_index = key_index;

    if (devno >= MAX_DSC_DEV || !dvb_dsc_dev[devno].used) {
        ALOGE("ca_set_key failed! devno:%d used:%d\n", devno, dvb_dsc_dev[devno].used);
        goto ERROR_EXIT;
    }
    fd = dvb_dsc_dev[devno].fd;
    ret = ioctl(fd, CA_SC2_SET_DESCR_EX, &desc);
    if (ret != 0) {
        ALOGE("ca_set_key ioctl fail, ret:0x%0x\n", ret);
        goto ERROR_EXIT;
    }

    pthread_mutex_unlock(&lock);
    ALOGI("ca_set_key ok, index:%d, parity:%d, key_index:%#x\n",index, parity, key_index);

    return CA_DSC_OK;
ERROR_EXIT:
    pthread_mutex_unlock(&lock);
    return CA_DSC_ERROR;
}

int ca_close(int devno)
{
    int fd;

    pthread_mutex_lock(&lock);

    ALOGI("ca_close dev:%d\n", devno);
    if (devno >= MAX_DSC_DEV || !dvb_dsc_dev[devno].used) {
        ALOGE("ca_close failed! devno:%d used:%d\n", devno, dvb_dsc_dev[devno].used);
        goto ERROR_EXIT;
    }
    fd = dvb_dsc_dev[devno].fd;
    close(fd);
    dvb_dsc_dev[devno].fd = 0;
    dvb_dsc_dev[devno].used = 0;

    pthread_mutex_unlock(&lock);

    return CA_DSC_OK;
ERROR_EXIT:
    pthread_mutex_unlock(&lock);
    return CA_DSC_ERROR;
}
