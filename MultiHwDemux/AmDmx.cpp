/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */



#define LOG_NDEBUG 0
#define LOG_TAG "AM_DMX_Device"
#include <utils/Log.h>
#include <cutils/properties.h>

#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <am_mem.h>
#include <AmLinuxDvb.h>
#include <AmDmx.h>
#include <dmx.h>

AM_DMX_Device::AM_DMX_Device(int demuxId) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    drv = new AmLinuxDvb;
    dev_no = demuxId;
    open_count = 0;
    for (int fid = 0; fid < DMX_FILTER_COUNT; fid++) {
        filters[fid].used = false;
    }
}

AM_DMX_Device::~AM_DMX_Device() {
    if (drv) {
        drv->dvr_close();
        drv = NULL;
    }

    ALOGD("%s/%d", __FUNCTION__, __LINE__);
}

AM_ErrorCode_t AM_DMX_Device::dmx_dvr_open(dmx_input_source_t inputSource) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    AM_ErrorCode_t ret = drv->dvr_open(this,inputSource);
    return ret;
}

AM_ErrorCode_t AM_DMX_Device::dmx_get_used_filter(int filter_id, AM_DMX_Filter **pf) {
    AM_DMX_Filter *filter;

    if ((filter_id < 0) || (filter_id >= DMX_FILTER_COUNT)) {
        ALOGE("invalid filter id, must in %d~%d", 0, DMX_FILTER_COUNT-1);
        return AM_DMX_ERR_INVALID_ID;
    }

    filter = &filters[filter_id];

    if (!filter->used) {
        ALOGE("filter %d has not been allocated", filter_id);
        return AM_DMX_ERR_NOT_ALLOCATED;
    }

    *pf = filter;
    return AM_SUCCESS;
}

#if 1

static AM_ErrorCode_t read_dmx_non_sec_es_data(void *arg, int id, AM_DMX_Filter *filter, uint8_t *sec_buf, int BUF_SIZE) {
    AM_ErrorCode_t ret;
    AM_DMX_Device *dev = (AM_DMX_Device*)arg;
    AM_DMX_DataCb cb;
    void *data;
    uint8_t *sec;
    int sec_len;

    sec_len = BUF_SIZE;

    dmx_non_sec_es_header* esHeader;
    int headersize = sizeof(dmx_non_sec_es_header);
    while (1) {
#ifndef DMX_WAIT_CB
        pthread_mutex_lock(&dev->lock);
#endif

        sec_len = headersize;

        if (!filter->enable || !filter->used) {
            ret = AM_FAILURE;
        } else {
            cb   = filter->cb;
            data = filter->user_data;
            ret  = dev->drv->dvb_read(dev, filter, sec_buf, &sec_len,false);
            if (ret != AM_SUCCESS) {
              //  ALOGI("dmx_non_sec_es_data break 1 \n");
#ifndef DMX_WAIT_CB
pthread_mutex_unlock(&dev->lock);
#endif
                break;
            } else {
                //ALOGE("TEST: try to read head size:%d, got data len:%u", headersize, ((dmx_non_sec_es_header*)sec_buf)->len);
            }
            esHeader = (dmx_non_sec_es_header*)sec_buf;
            uint32_t reaminSize = esHeader->len;
            int buffSize = 10*1024*1024;
            int readsize = (reaminSize > buffSize) ? buffSize : reaminSize;
            while (reaminSize > 0) {
                //ALOGE("TEST: try to read data size:%d, remain size:%d", readsize, reaminSize);
                ret  = dev->drv->dvb_read(dev, filter, sec_buf, &readsize,false);
                if (ret != AM_SUCCESS) {
                    ALOGI("dmx_non_sec_es_data failed:%d \n", ret);
#ifndef DMX_WAIT_CB
    pthread_mutex_unlock(&dev->lock);
#endif
                    continue;
                } else {
                    reaminSize = reaminSize - readsize;
#ifndef DMX_WAIT_CB
                    pthread_mutex_unlock(&dev->lock);
#endif
                    if (ret == AM_DMX_ERR_TIMEOUT) {
                        sec = NULL;
                        sec_len = 0;
                    } else if (ret != AM_SUCCESS) {
                        return ret;
                    } else {
                        sec = sec_buf;
                    }

                    //if (cb) {
                    //    //ALOGE("TEST: send callback size:%d", readsize);
                    //    cb(dev->mDemuxWrapper, id, sec, readsize, data);
                    //}
                    readsize = (reaminSize > buffSize) ? buffSize : reaminSize;
                }
            }
       }
    }
    // ALOGI("dmx_non_sec_es_data out \n");
    return ret;
}


static AM_ErrorCode_t read_dmx_sec_es_data(void *arg, int id, AM_DMX_Filter *filter, uint8_t *sec_buf, int BUF_SIZE) {
    AM_ErrorCode_t ret;
    AM_DMX_Device *dev = (AM_DMX_Device*)arg;
    AM_DMX_DataCb cb;
    void *data;
    uint8_t *sec;
    int sec_len;

    sec_len = BUF_SIZE;

#ifndef DMX_WAIT_CB
    pthread_mutex_lock(&dev->lock);
#endif

    if (!filter->enable || !filter->used) {
        ret = AM_FAILURE;
    } else {
        cb   = filter->cb;
        data = filter->user_data;
        ret  = dev->drv->dvb_read(dev, filter, sec_buf, &sec_len);
    }

#ifndef DMX_WAIT_CB
    pthread_mutex_unlock(&dev->lock);
#endif

    if (ret == AM_DMX_ERR_TIMEOUT) {
        sec = NULL;
        sec_len = 0;
    } else if (ret != AM_SUCCESS) {
        return ret;
    } else {
        sec = sec_buf;
    }

    //if (cb) {
    //    cb(dev->mDemuxWrapper, id, sec, sec_len, data);
    //}
    return ret;
}
#endif

void* AM_DMX_Device::dmx_data_thread(void *arg) {
    ALOGI("--------->dmx_data_thread <--------------\n");
    AM_DMX_Device *dev = (AM_DMX_Device*)arg;
    AM_DMX_FilterMask_t mask = 0;
    AM_ErrorCode_t ret;

    while (dev && dev->enable_thread) {
        AM_DMX_FILTER_MASK_CLEAR(&mask);

        if (dev->drv == NULL) {
            ALOGW("dev->drv is NULL!\n");
            break;
        }
        ret = dev->drv->dvb_poll(dev, &mask, DMX_POLL_TIMEOUT);
        if (ret == AM_SUCCESS) {
            if (AM_DMX_FILTER_MASK_ISEMPTY(&mask)) {
                continue;
            }

#if defined(DMX_WAIT_CB) || defined(DMX_SYNC)
            pthread_mutex_lock(&dev->lock);
            dev->flags |= DMX_FL_RUN_CB;
            pthread_mutex_unlock(&dev->lock);
#endif

            for (int id = 0; id < DMX_FILTER_COUNT; id++) {
                AM_DMX_Filter *filter = &(dev->filters[id]);
                //AM_DMX_DataCb cb;
                //void *data;

                if (!AM_DMX_FILTER_MASK_ISSET(&mask, id) || !filter->enable || !filter->used) {
                    continue;
                }
                if (filter->cb)//postData->AM_DMX_Read->
                    filter->cb(filter->user_data,
                                           id,
                                           filter->flags & DMX_ES_OUTPUT,
                                           filter->flags & DMX_OUTPUT_RAW_MODE);
            }
#if defined(DMX_WAIT_CB) || defined(DMX_SYNC)
            pthread_mutex_lock(&dev->lock);
            dev->flags &= ~DMX_FL_RUN_CB;
            pthread_mutex_unlock(&dev->lock);
            pthread_cond_broadcast(&dev->cond);
#endif
        } else {
            if (dev->drv == NULL) {
                ALOGW("dev->drv is NULL!\n");
                break;
            }
            usleep(10 * 1000);
        }
    }

    return NULL;
}

AM_ErrorCode_t AM_DMX_Device::dmx_wait_cb(void) {
#ifdef DMX_WAIT_CB
    if (thread != pthread_self()) {
        while (flags & DMX_FL_RUN_CB)
            pthread_cond_wait(&cond, &lock);
    }
#else
    //  UNUSED(dev);
#endif
    return AM_SUCCESS;
}


AM_ErrorCode_t AM_DMX_Device::dmx_stop_filter(AM_DMX_Filter *filter) {
    AM_ErrorCode_t ret = AM_SUCCESS;

    if (!filter->used || !filter->enable) {
        return ret;
    }
    ret = drv->dvb_enable_filter(this, filter, AM_FALSE);
    if (ret >= 0) {
        filter->enable = false;
    }

    return ret;
}


int AM_DMX_Device::dmx_free_filter(AM_DMX_Filter *filter) {
    AM_ErrorCode_t ret = AM_SUCCESS;

    if (!filter->used) {
        return ret;
    }

    ret = dmx_stop_filter(filter);

    if (ret == AM_SUCCESS) {
        ret = drv->dvb_free_filter(this, filter);
    }

    if (ret == AM_SUCCESS) {
        filter->used = false;
    }

    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_Open(void) {

    AM_ErrorCode_t ret = AM_SUCCESS;
    ALOGD("%s/%d demuxId = %d", __FUNCTION__, __LINE__, dev_no);

    if (open_count > 0) {
        ALOGI("demux device %d has already been openned", dev_no);
        open_count++;
        ret = AM_SUCCESS;
        goto final;
    }

    ret = drv->dvb_open(this);
    if (ret == AM_SUCCESS) {
        pthread_mutex_init(&lock, NULL);
        pthread_cond_init(&cond, NULL);
        enable_thread = true;
        flags = 0;

        if (pthread_create(&thread, NULL, dmx_data_thread, this)) {
            pthread_mutex_destroy(&lock);
            pthread_cond_destroy(&cond);
            ret = AM_DMX_ERR_CANNOT_CREATE_THREAD;
        }
    }

    if (ret == AM_SUCCESS) {
        open_count = 1;
    }
    final:
    //  pthread_mutex_unlock(&am_gAdpLock);

    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_Close(void) {
    AM_ErrorCode_t ret = AM_SUCCESS;
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    if (open_count == 1) {
        enable_thread = false;
        pthread_join(thread, NULL);

        for (int i = 0; i < DMX_FILTER_COUNT; i++) {
            dmx_free_filter(&filters[i]);
        }
        if (drv) {
            drv->dvb_close(this);
            drv = NULL;
        }
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&cond);
    }
    open_count--;

    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_Read(int fhandle, uint8_t* buff, int *size) {
    AM_DMX_Filter *filter;
    AM_ErrorCode_t ret = AM_SUCCESS;
    //ALOGV("%s/%d", __FUNCTION__, __LINE__);

    ret = dmx_get_used_filter(fhandle, &filter);
    if (ret != AM_SUCCESS) {
        return AM_FAILURE;
    }
    if (!filter->enable || !filter->used) {
        ret = AM_FAILURE;
    } else {
        ret  = drv->dvb_read(this, filter, buff, size, false);
    }

    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_AllocateFilter(int *fhandle) {

    AM_ErrorCode_t ret = AM_SUCCESS;
    int fid;
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    assert(fhandle);

    //AM_TRY(dmx_get_openned_dev(dev_no, &dev));

    pthread_mutex_lock(&lock);

    for (fid = 0; fid < DMX_FILTER_COUNT; fid++) {
        if (!filters[fid].used) {
            break;
        }
    }

    if (fid >= DMX_FILTER_COUNT) {
        ALOGI("no free section filter");
        ret = AM_DMX_ERR_NO_FREE_FILTER;
    }

    if (ret == AM_SUCCESS) {
        dmx_wait_cb();
        filters[fid].id   = fid;
        ret = drv->dvb_alloc_filter(this, &filters[fid]);
    }

    if (ret == AM_SUCCESS) {
        filters[fid].used = true;
        *fhandle = fid;
    }

    pthread_mutex_unlock(&lock);

    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_SetSecFilter(int fhandle, const struct dmx_sct_filter_params *params) {
    //AM_DMX_Device_t *dev;
    AM_DMX_Filter *filter;
    AM_ErrorCode_t ret = AM_SUCCESS;
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    assert(params);

    pthread_mutex_lock(&lock);

    ret = dmx_get_used_filter(fhandle, &filter);

    if (ret == AM_SUCCESS) {
        dmx_wait_cb();
        ret = dmx_stop_filter(filter);
    }

    if (ret == AM_SUCCESS) {
        filter->flags = params->flags;
        ret = drv->dvb_set_sec_filter(this, filter, params);
        ALOGI("set sec filter %d PID: %d filter: %02x:%02x %02x:%02x %02x:%02x %02x:%02x %02x:%02x %02x:%02x %02x:%02x %02x:%02x",
        fhandle, params->pid,
        params->filter.filter[0], params->filter.mask[0],
        params->filter.filter[1], params->filter.mask[1],
        params->filter.filter[2], params->filter.mask[2],
        params->filter.filter[3], params->filter.mask[3],
        params->filter.filter[4], params->filter.mask[4],
        params->filter.filter[5], params->filter.mask[5],
        params->filter.filter[6], params->filter.mask[6],
        params->filter.filter[7], params->filter.mask[7]);
    }

    pthread_mutex_unlock(&lock);

    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_SetPesFilter(int fhandle, const struct dmx_pes_filter_params *params) {
    AM_DMX_Filter *filter;
    AM_ErrorCode_t ret = AM_SUCCESS;
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    assert(params);

    pthread_mutex_lock(&lock);

    ret = dmx_get_used_filter(fhandle, &filter);

    if (ret == AM_SUCCESS) {
        dmx_wait_cb();
        ret = dmx_stop_filter(filter);
    }

    if (ret == AM_SUCCESS) {
        filter->flags = params->flags;
        ret = drv->dvb_set_pes_filter(this,filter, params);
        ALOGV("set pes filter %d PID %d", fhandle, params->pid);
    }

    pthread_mutex_unlock(&lock);

    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_GetSTC(int fhandle) {

    AM_DMX_Filter *filter;
    AM_ErrorCode_t ret = AM_SUCCESS;
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    pthread_mutex_lock(&lock);

    ret = dmx_get_used_filter(fhandle, &filter);

    if (ret == AM_SUCCESS) {
        ret = drv->dvb_get_stc(this, filter);
    }

    pthread_mutex_unlock(&lock);
    ALOGV("%s line:%d\n", __FUNCTION__, __LINE__);

    return ret;
}


AM_ErrorCode_t AM_DMX_Device::AM_DMX_FreeFilter(int fhandle) {
    ALOGD("%s/%d dev_no = %d", __FUNCTION__, __LINE__, dev_no);

    AM_DMX_Filter *filter;
    AM_ErrorCode_t ret = AM_SUCCESS;
    pthread_mutex_lock(&lock);
    ret = dmx_get_used_filter(fhandle, &filter);

    if (ret == AM_SUCCESS) {
        dmx_wait_cb();
        ret = dmx_free_filter(filter);
    }

    pthread_mutex_unlock(&lock);

    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_StartFilter(int fhandle) {

    AM_DMX_Filter *filter = NULL;
    AM_ErrorCode_t ret = AM_SUCCESS;
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    pthread_mutex_lock(&lock);

    ret = dmx_get_used_filter(fhandle, &filter);

    if (!filter->enable) {
        if (ret == AM_SUCCESS) {
            ret = drv->dvb_enable_filter(this, filter, true);
        }

        if (ret == AM_SUCCESS) {
            filter->enable = true;
        }
    }

    pthread_mutex_unlock(&lock);

    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_StopFilter(int fhandle) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    AM_DMX_Filter *filter = NULL;
    AM_ErrorCode_t ret = AM_SUCCESS;

    pthread_mutex_lock(&lock);

    ret = dmx_get_used_filter(fhandle, &filter);

    if (ret == AM_SUCCESS) {
        if (filter->enable) {
            dmx_wait_cb();
            ret = dmx_stop_filter(filter);
        }
    }

    pthread_mutex_unlock(&lock);
    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_SetBufferSize(int fhandle, int size) {

    AM_DMX_Filter *filter;
    AM_ErrorCode_t ret = AM_SUCCESS;
    ALOGD("%s/%d fhandle:%d", __FUNCTION__, __LINE__, fhandle);

    pthread_mutex_lock(&lock);

    if (ret == AM_SUCCESS) {
        ret = dmx_get_used_filter(fhandle, &filter);
    }
    if (ret == AM_SUCCESS) {
        ret = drv->dvb_set_buf_size(this, filter, size);
    }
    pthread_mutex_unlock(&lock);

return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_GetCallback(int fhandle, AM_DMX_DataCb *cb, void **data) {
    AM_DMX_Filter *filter;
    AM_ErrorCode_t ret = AM_SUCCESS;
    ALOGD("%s/%d fhandle:%d", __FUNCTION__, __LINE__, fhandle);

    pthread_mutex_lock(&lock);

    ret = dmx_get_used_filter(fhandle, &filter);

    if (ret == AM_SUCCESS) {
        if (cb) {
            *cb = filter->cb;
        }
        if (data) {
            *data = filter->user_data;
        }
    }
    pthread_mutex_unlock(&lock);
    return ret;
}


AM_ErrorCode_t AM_DMX_Device::AM_DMX_SetCallback(int fhandle, AM_DMX_DataCb cb, void *data) {

    AM_DMX_Filter *filter;
    AM_ErrorCode_t ret = AM_SUCCESS;
    ALOGD("%s/%d fhandle:%d", __FUNCTION__, __LINE__, fhandle);

    pthread_mutex_lock(&lock);
    ret = dmx_get_used_filter(fhandle, &filter);

    if (ret == AM_SUCCESS) {
        dmx_wait_cb();
        filter->cb = cb;
        filter->user_data = data;
    }

    pthread_mutex_unlock(&lock);

    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_GetMenInfo(int fhandle, dmx_mem_info* mDmxMenInfo) {
    AM_DMX_Filter *filter;
    AM_ErrorCode_t ret = AM_SUCCESS;

    pthread_mutex_lock(&lock);

    ret = dmx_get_used_filter(fhandle, &filter);

    if (ret == AM_SUCCESS) {
        ret = drv->dvb_get_mem_info(filter,mDmxMenInfo);
    }
    pthread_mutex_unlock(&lock);
    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_GetFilterMenInfo(dmx_filter_mem_info* mDmxFilterMemInfo) {
    AM_ErrorCode_t ret = AM_SUCCESS;
    pthread_mutex_lock(&lock);
    ret = drv->dvb_get_filter_mem_info(this,mDmxFilterMemInfo);
    pthread_mutex_unlock(&lock);
    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_Sync()
{
    AM_ErrorCode_t ret = AM_SUCCESS;
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    pthread_mutex_lock(&lock);
    if (thread != pthread_self()) {
        while (flags&DMX_FL_RUN_CB) {
            pthread_cond_wait(&cond, &lock);
        }
    }
    pthread_mutex_unlock(&lock);

    return ret;
}

AM_ErrorCode_t AM_DMX_Device::AM_DMX_WriteTs(uint8_t* data,int32_t size,uint64_t timeout) {
    if (drv->dvr_data_write(data,size,timeout) != size) {
        ALOGE("%s/%d dvr_data_write error!", __FUNCTION__, __LINE__);
        return AM_FAILURE;
    }
    return AM_SUCCESS;
}

 #if 0
AM_ErrorCode_t AM_DMX_Device::AM_DMX_SetSource(AM_DMX_Source_t src)
{
//AM_DMX_Device_t *dev;
AM_ErrorCode_t ret = AM_SUCCESS;

//  AM_TRY(dmx_get_openned_dev(dev_no, &dev));

pthread_mutex_lock(&lock);
//if(!dev->drv->set_source)
//{
//  printf("do not support set_source");
//  ret = AM_DMX_ERR_NOT_SUPPORTED;
//  }

if (ret == AM_SUCCESS) {
ret = drv->dvb_set_source(this, src);
}

pthread_mutex_unlock(&lock);

if (ret == AM_SUCCESS)
{
//      pthread_mutex_lock(&am_gAdpLock);
src = src;
//      pthread_mutex_unlock(&am_gAdpLock);
}

return ret;
}
#endif

#if 0
AM_ErrorCode_t AM_DMX_Device::AM_DMX_GetScrambleStatus(AM_Bool_t dev_status[2])
{
#if 0
char buf[32];
char class_file[64];
int vflag, aflag;
int i;

dev_status[0] = dev_status[1] = AM_FALSE;
snprintf(class_file,sizeof(class_file), "/sys/class/dmx/demux%d_scramble", dev_no);
for (i=0; i<5; i++)
{
if (AM_FileRead(class_file, buf, sizeof(buf)) == AM_SUCCESS)
{
sscanf(buf,"%d %d", &vflag, &aflag);
if (!dev_status[0])
dev_status[0] = vflag ? AM_TRUE : AM_FALSE;
if (!dev_status[1])
dev_status[1] = aflag ? AM_TRUE : AM_FALSE;
//AM_DEBUG(1, "AM_DMX_GetScrambleStatus video scamble %d, audio scamble %d\n", vflag, aflag);
if (dev_status[0] && dev_status[1])
{
return AM_SUCCESS;
}
usleep(10*1000);
}
else
{
printf("AM_DMX_GetScrambleStatus read scamble status failed\n");
return AM_FAILURE;
}
}
#endif
return AM_SUCCESS;
}

#endif

