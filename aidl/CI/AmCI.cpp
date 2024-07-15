/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description: usbcam handler.
 */
#define LOG_TAG "AmCI"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <pthread.h>
#include <utils/Log.h>
#include <sys/prctl.h>
#include "AmCI.h"
#include "dmx.h"

#define REC_BUFF_SIZE (USB_CIMODULE_MEDIA_MAX_SIZE*20)

#define MEDIA_INPUT_ENABLE 1
#define MEDIA_OUTPUT_ENABLE 1

enum aml_usbcam_device_state device_state;

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

AmCI::AmCI(int dmxId, int ts_input, int source) {
    mDemuxId = dmxId;
    mTsInput = ts_input;
    mSource  = source;
    mpCIApi = new AmCIModuleApi();
}

AmCI::~AmCI() {
    ALOGV("%s", __FUNCTION__);
    delete mpCIApi;
}

AmCIModuleApi* AmCI::getCIModuelApi() {
    return mpCIApi;
}

void AmCI::init_mutex()
{
    if (!mutex_init)
    {
        pthread_mutex_init(&gs_tMediaWriteCondMut, NULL);
        pthread_cond_init(&gs_tMediaWriteCond, NULL);
        pthread_mutex_init(&gs_tMediaReadCondMut, NULL);
        pthread_cond_init(&gs_tMediaReadCond, NULL);
        mutex_init = true;
    }
}
int AmCI::ci_ts_read_open()
{
    ALOGV("%s", __FUNCTION__);
    int fd = -1;
    const char *read_node = "/dev/cimodule_media0";

    if (0 == access(read_node, F_OK))
    {
        fd = mpCIApi->cimodule_media_intf_open(read_node, O_RDONLY );//| O_NONBLOCK
        if (fd < 0)
            ALOGE("open %s failed", read_node);
    }
    return fd;
}

bool AmCI::set_usbcam_recording_demux(int source)
{
    char rec_dmx_path[64];
    struct dmx_pes_filter_params params;
    int ret;

    //Aml_MP_SetDemuxSource(rec_dev_id, source); it has been setting in amDvr.cpp
    //mpCIApi->set_dvb_source(rec_dev_id, mTsInput, mSource);
    setDvbSource(rec_dev_id, INPUT_DEMOD, getTsInputById(source));
    setDvbSource(inj_dev_id, INPUT_LOCAL, DMA_4);
    ALOGD("================= set usb camcard data source %d", source);

    snprintf(rec_dmx_path, sizeof(rec_dmx_path), "/dev/dvb0.demux%d", rec_dev_id);
    if (rec_dmx_fd < 0)
        rec_dmx_fd = open(rec_dmx_path, O_RDWR);
    ALOGD("================= rec_dmx_path %s", rec_dmx_path);
    ALOGD("================= rec_dmx_fd %d", rec_dmx_fd);

    fcntl(rec_dmx_fd, F_SETFL, O_NONBLOCK);
    memset(&params, 0, sizeof(params));

    params.pid = 0x2000;
    // params.pid = 0x211;
    params.input = DMX_IN_FRONTEND;
    params.output = DMX_OUT_TS_TAP;
    params.pes_type = DMX_PES_OTHER;

    ret = ioctl(rec_dmx_fd, DMX_SET_PES_FILTER, &params);
    if (ret == -1)
    {
        ALOGE("DMX_SET_PES_FILTER failed");
        return false;
    }
    ret = ioctl(rec_dmx_fd, DMX_START, 0);
    if (ret == -1)
    {
        ALOGE("DMX_START failed");
        return false;
    }
    return true;
}

int AmCI::ci_ts_write_open()
{
    ALOGV("%s", __FUNCTION__);
    int fd = -1;
    const char *write_node = "/dev/cimodule_media0";

    if (0 == access(write_node, F_OK))
    {
        fd = mpCIApi->cimodule_media_intf_open(write_node, O_WRONLY);
        if (fd < 0)
            ALOGE("open %s failed", write_node);
    }
    return fd;
}

int AmCI::ci_ts_read_close(int fd, unsigned char *data)
{
    ALOGV("%s", __FUNCTION__);
    if (fd < 0)
    {
        ALOGE("media interface close failed, fd = %d", fd);
        return -1;
    }

    if (data)
    {
        mpCIApi->cimodule_media_intf_readbuf(data);
        data = NULL;
        ALOGD("read buffer unmap ok");
    }

    ALOGD("media interface close, fd = %d", fd);
    mpCIApi->cimodule_media_intf_close(fd);

    return 0;
}

int AmCI::ci_ts_write_close(int fd, unsigned char *data)
{
    ALOGV("%s", __FUNCTION__);
    if (fd < 0)
    {
        ALOGE("media interface close failed, fd = %d", fd);
        return -1;
    }

    if (data)
    {
        mpCIApi->cimodule_media_intf_writebuf(data);
        data = NULL;
        ALOGD("media write buffer munmap successfully");
    }

    ALOGD("media interface close, fd = %d", fd);
    mpCIApi->cimodule_media_intf_close(fd);

    return 0;
}

void* AmCI::cimodule_media_read_task(void *args)
{
    AmCI *pAmCI = (AmCI*)args;

    int ret, inj_len;
    int fdMedia = -1;
    unsigned char *pbMediaReadBuf = NULL;
    struct usb_cimodule_info tUsbCiModuleInfo;
    unsigned char bMediaOutputCtrl;
    unsigned int read_len;
    int count = 0;
    //int save_fd = -1;
    prctl(PR_SET_NAME, "cimodule_media_read_task");
    struct pollfd fds[1];
    int timeout_ms = 500;
    ALOGD("entry");
    fdMedia = pAmCI->ci_ts_read_open();
    fds[0].fd = fdMedia;
    fds[0].events = POLLIN | POLLERR;
    while (pAmCI->thread_running)
    {
        pbMediaReadBuf = NULL;
        if (fdMedia < 0)
        {
            ALOGD("please insert or reinsert the usb ci module...");
            pthread_mutex_lock(&pAmCI->gs_tMediaReadCondMut);
            pthread_cond_wait(&pAmCI->gs_tMediaReadCond, &pAmCI->gs_tMediaReadCondMut);
            pthread_mutex_unlock(&pAmCI->gs_tMediaReadCondMut);
            fdMedia = pAmCI->ci_ts_read_open();
            if (fdMedia < 0)
                continue;
        }

        ALOGD("open usb media interface successfully,read handle:%d", fdMedia);

        ret = pAmCI->getCIModuelApi()->cimodule_get_usb_cimodule_info(fdMedia, &tUsbCiModuleInfo);
        if (ret < 0)
        {
            ALOGD("(handle: %d),get device info error,error code:%d", fdMedia, ret);
            pAmCI->ci_ts_read_close(fdMedia, NULL);
            fdMedia = -1;
            continue;
        }

        if (!tUsbCiModuleInfo.m_bIsCI20Detected)
        {
            // refer to CI_OVER_USB_1.0 SPEC
            bMediaOutputCtrl = (unsigned char)((tUsbCiModuleInfo.m_dwCiCompatibility >> 7) & 0x01);
            if (MEDIA_OUTPUT_ENABLE != bMediaOutputCtrl)
            {
                ALOGD("can not read media from usb ci module in this mode");
                pAmCI->ci_ts_read_close(fdMedia, NULL);
                fdMedia = -1;
                continue;
            }
        }

        pbMediaReadBuf = pAmCI->getCIModuelApi()->cimodule_media_intf_readbuf(fdMedia, USB_CIMODULE_MEDIA_MAX_SIZE);
        if (NULL == pbMediaReadBuf)
        {
            ALOGD("(handle: %d),media read buffer mmap failed", fdMedia);
            pAmCI->ci_ts_read_close(fdMedia, pbMediaReadBuf);
            fdMedia = -1;
            continue;
        }
        while (pAmCI->thread_running)
        {
            ALOGD("read pAmCI->thread_running %d", pAmCI->thread_running);
            ret = poll(fds, 1, timeout_ms);
            ALOGD("read ret %d fds[0].revents %d", ret,fds[0].revents);
            if (ret == 1)//&& (fds[0].revents & POLLIN)
                ret = pAmCI->getCIModuelApi()->cimodule_media_intf_read(fdMedia, pbMediaReadBuf, USB_CIMODULE_MEDIA_MAX_SIZE, &read_len, -1);
            ALOGD("read pAmCI->thread_running %d read_len %d", pAmCI->thread_running ,read_len);
            if (read_len > 0)
            {
                if (read_len == 10)
                {
                    continue;
                }
                else
                {
                    count++;
                    inj_len = pAmCI->inject_usbcam_source_demux(pbMediaReadBuf, read_len);
                    ALOGD("read count %d, inject %d", count, inj_len);
#ifdef DMX_USB_TEST
                    if (save_fd < 0)
                        save_fd = open("/data/w.ts", O_RDWR);
                    write(save_fd, pbMediaReadBuf, read_len);
#endif
                }
            }
            else
            {
                ALOGD("read len %d ret %d", read_len, ret);
                // goto EXIT;
            }
        }

        pAmCI->ci_ts_read_close(fdMedia, pbMediaReadBuf);
        fdMedia = -1;
    }
// EXIT:
    ALOGD("usbcam unplug, media read task exit.");
    // pAmCI->ci_ts_read_close(fdMedia, pbMediaReadBuf);
    // fdMedia = -1;

    return NULL;
}

void* AmCI::cimodule_media_write_task(void *args)
{
    AmCI *pAmCI = (AmCI*)args;
    int ret;
    int fdMedia = -1;
    unsigned char *pbMediaWriteBuf = NULL;
    struct usb_cimodule_info tUsbCiModuleInfo;
    //unsigned int dwCiCompatibility;
    unsigned char bMediaInputCtrl;
    unsigned char *buffer;
    unsigned int write_len;
    int rec_len, threshold;
    prctl(PR_SET_NAME, "cimodule_media_write_task");
    //static int fd = -1;
    int count = 0;
    unsigned char arDummyTsHdr[10] = {0x00, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    ALOGD("entry");
    buffer = (unsigned char *)malloc(REC_BUFF_SIZE);
    write_len = 0;
    fdMedia = pAmCI->ci_ts_write_open();
    // Aml_MP_SetDemuxSource(0, DVB_DEMUX_SOURCE_DMA0 + inj_dev_id);
    while (pAmCI->thread_running)
    {
        threshold = 0;
        if (fdMedia < 0)
        {
            ALOGD("please insert or reinsert the usb ci module...");
            pthread_mutex_lock(&pAmCI->gs_tMediaWriteCondMut);
            pthread_cond_wait(&pAmCI->gs_tMediaWriteCond, &pAmCI->gs_tMediaWriteCondMut);
            pthread_mutex_unlock(&pAmCI->gs_tMediaWriteCondMut);
            fdMedia = pAmCI->ci_ts_write_open();
            if (fdMedia < 0)
                continue;
        }

        ALOGD("open usb cimodlue media interface successfully,write handle:%d", fdMedia);

        ret = pAmCI->getCIModuelApi()->cimodule_get_usb_cimodule_info(fdMedia, &tUsbCiModuleInfo);
        if (ret < 0)
        {
            ALOGD("(handle: %d),get device info error,error code:%d", fdMedia, ret);
            pAmCI->ci_ts_write_close(fdMedia, NULL);
            fdMedia = -1;
            continue;
        }
        ALOGD("get cimodule info ok");

        if (!tUsbCiModuleInfo.m_bIsCI20Detected)
        {
            // refer to CI_OVER_USB_1.0 SPEC
            bMediaInputCtrl = (unsigned char)((tUsbCiModuleInfo.m_dwCiCompatibility >> 6) & 0x01);
            if (MEDIA_INPUT_ENABLE != bMediaInputCtrl)
            {
                ALOGD("can not write media to usb ci module in this mode");
                pAmCI->ci_ts_write_close(fdMedia, NULL);
                fdMedia = -1;
                continue;
            }
        }
        ALOGD("ci20 detected ok");

        pbMediaWriteBuf = pAmCI->getCIModuelApi()->cimodule_media_intf_writebuf(fdMedia, USB_CIMODULE_MEDIA_MAX_SIZE);
        if (NULL == pbMediaWriteBuf)
        {
            ALOGD("(handle: %d),media write buffer mmap failed", fdMedia);
            pAmCI->ci_ts_write_close(fdMedia, NULL);
            fdMedia = -1;
            continue;
        }
        ALOGD("ready to inject ts, buf %p", pbMediaWriteBuf);
        while (pAmCI->thread_running)
        {
#ifndef INJECT_FROM_FILE
            rec_len = pAmCI->record_from_tsin(buffer + threshold, USB_CIMODULE_MEDIA_MAX_SIZE);
#else
            if (fd <= 0)
                fd = open("/data/test.ts", O_RDONLY);
            rec_len = read(fd, buffer, USB_CIMODULE_MEDIA_MAX_SIZE);
#endif
            if (rec_len > 0)
                threshold += rec_len;

            ALOGD("record_from_tsin rec_len %d threshold %d", rec_len,threshold);
            while ((threshold >= USB_CIMODULE_MEDIA_MAX_SIZE) && pAmCI->thread_running)
            {
                ALOGD("(threshold >= USB_CIMODULE_MEDIA_MAX_SIZE) && pAmCI->thread_running");
                count++;
                // DMX_USB_DBG("write dummy first");
                memcpy(pbMediaWriteBuf, arDummyTsHdr, 10);
                ret = pAmCI->getCIModuelApi()->cimodule_media_intf_write(fdMedia, pbMediaWriteBuf, 10, &write_len, -1);
// #ifdef DEMUX_USB_MODULE_DEBUG
                ALOGD("write dummy len %d ret %d", write_len, ret);
// #endif
                memcpy(pbMediaWriteBuf, buffer, USB_CIMODULE_MEDIA_MAX_SIZE);
                if (ret == 0)
                {
                    ret = pAmCI->getCIModuelApi()->cimodule_media_intf_write(fdMedia, pbMediaWriteBuf, USB_CIMODULE_MEDIA_MAX_SIZE, &write_len
, -1);
                    if (ret != 0)
                    {
                        ALOGD("write usb cam failed!!!! %d", ret);
                        goto EXIT;
                        //  pAmCI->thread_running = false;
                        // break;
                    }
// #ifdef DEMUX_USB_MODULE_DEBUG
                    ALOGD("write ts len %d ret %d", write_len, ret);
                    ALOGD("write count %d", count);
// #endif
                    memmove(buffer, buffer + write_len, threshold - write_len);
                    threshold -= write_len;
                }
#ifdef DEMUX_USB_MODULE_DEBUG
                else
                    ALOGD("write dummy failed %d!!!!", ret);
#endif
            }
        }

         pAmCI->ci_ts_write_close(fdMedia, pbMediaWriteBuf);
        fdMedia = -1;
    }
EXIT:
    ALOGD("usbcam unplug, media write task exit.");
    pAmCI->ci_ts_write_close(fdMedia, pbMediaWriteBuf);
    fdMedia = -1;

    return NULL;
}

void AmCI::prepare_working_demuxes()
{
    ALOGD("%s mTsInput %d inj_dev_id = %d, rec_dev_id = %d", __FUNCTION__,mTsInput, inj_dev_id, rec_dev_id);
    //int ret;
    char inj_dvr_path[64];
    char rec_dvr_path[64];


    ev_fd = eventfd(0, 0);
    snprintf(inj_dvr_path, sizeof(inj_dvr_path), "/dev/dvb0.dvr%d", inj_dev_id);
    if (inj_dvr_fd < 0)
        inj_dvr_fd = open(inj_dvr_path, O_WRONLY);
    ioctl(inj_dvr_fd, DMX_SET_INPUT, INPUT_LOCAL);
    snprintf(rec_dvr_path, sizeof(rec_dvr_path), "/dev/dvb0.dvr%d", rec_dev_id);
    if (rec_dvr_fd < 0)
    rec_dvr_fd = open(rec_dvr_path, O_RDONLY);

    fcntl(rec_dvr_fd, F_SETFL, fcntl(rec_dvr_fd, F_GETFL, 0) | O_NONBLOCK, 0);
    ioctl(rec_dvr_fd, DMX_SET_BUFFER_SIZE, REC_BUFF_SIZE);

    set_usbcam_recording_demux(mTsInput/*aml_hw_cfg.tuners[aml_hw_cfg.tuner_num - 1].ts_input_idx*/);
}

void AmCI::dev_close()
{
    close(inj_dvr_fd);
    close(rec_dmx_fd);
    close(rec_dvr_fd);
}

int AmCI::record_from_tsin(void* buff, int buff_len)
{
    int ret;
    struct pollfd fds[2];

    memset(fds, 0, sizeof(fds));

    fds[0].fd = rec_dvr_fd;
    fds[1].fd = ev_fd;
    fds[0].events = fds[1].events = POLLIN | POLLERR;
    ret = poll(fds, 2, 300);
    if (ret <= 0)
    {
#ifdef DEMUX_USB_MODULE_DEBUG
        ALOGD("poll ret %d rec_dvr_fd %d rec_dmx_fd %d", ret, rec_dvr_fd, rec_dmx_fd);
#endif
        return -1;
    }

    if (!(fds[0].revents & POLLIN))
    {
#ifdef DEMUX_USB_MODULE_DEBUG
        ALOGD("fds revents %d", fds[0].revents);
#endif
        // return -1;
    }
    ret = read(rec_dvr_fd, buff, buff_len);
    if (ret < 0)
        ALOGD("record_from_tsin error: ret = %d, [%d]%s", ret, -errno, strerror(errno));
    return ret;
}

int AmCI::inject_usbcam_source_demux(void* data, int data_len)
{
    return write(inj_dvr_fd, data, data_len);
}

uint8_t AmCI::CIUsbGetDmxSource(bool live)
{
    if (live)
        return DMA_1 + inj_dev_id;
    else
        return DMA_0 + inj_dev_id;
}

bool AmCI::CIUsbModuleInserted()
{
    return module_inserted;
}

void* AmCI::CIUsbMonitorMediaWRTread(void *args)
{
    AmCI *pAmCI = (AmCI*)args;
    const char *media_node = "/dev/cimodule_media0";
    void *status = NULL;
    while (1) {
        usleep(10000);
        if (0 == access(media_node, F_OK)) {

            if (pAmCI->thread_running == false) {

                pAmCI->inj_dev_id = 4;
                pAmCI->rec_dev_id = 5;
                pAmCI->prepare_working_demuxes();
                pAmCI->thread_running = true;

                pthread_create(&(pAmCI->tMediaReadTaskId), NULL, AmCI::cimodule_media_read_task, pAmCI);
                ALOGD("cimodule_media_read_task success");
                pthread_create(&(pAmCI->tMediaWriteTaskId), NULL, AmCI::cimodule_media_write_task, pAmCI);
                ALOGD("cimodule_media_write_task success");
            }
        } else {

            if (pAmCI->thread_running == true) {

                pAmCI->thread_running = false;

                if (pthread_join(pAmCI->tMediaReadTaskId, &status) != 0)
                {
                    ALOGD("media read task join failed =======");
                }

                if (pthread_join(pAmCI->tMediaWriteTaskId, &status) != 0)
                {
                    ALOGD("media write task join failed ======");
                }

                pAmCI->setDvbSource(pAmCI->rec_dev_id, INPUT_DEMOD, FRONTEND_TS0);
                pAmCI->setDvbSource(pAmCI->inj_dev_id, INPUT_DEMOD, FRONTEND_TS0);
                ioctl(pAmCI->rec_dmx_fd, DMX_STOP, 0);
                close(pAmCI->rec_dmx_fd);
                close(pAmCI->rec_dvr_fd);
                close(pAmCI->inj_dvr_fd);
                pAmCI->rec_dmx_fd = -1;
                pAmCI->rec_dvr_fd = -1;
                pAmCI->inj_dvr_fd = -1;
            }
        }
    }

    return NULL;

}

int AmCI::CIUsbOpen()
{
    ALOGD("AmCI %s", __FUNCTION__);
    init_mutex();

    module_inserted = true;
    if (thread_init == false) {
        pthread_create(&tMediaWRTaskId, NULL, CIUsbMonitorMediaWRTread, this);
        thread_init = true;
    }
    return true;
}

int AmCI::CIUsbClose()
{
    ALOGD("CIUsbClose");

    module_inserted = false;

    return 0;
}

uint8_t AmCI::CIUsbCamTotal(void) {
    return 1;
}

int AmCI::DMXUsbGetTsDemux()
{
    return inj_dev_id;
}

bool AmCI::DmxUsbIsEnable() {
    return true;
}

int AmCI::setDvbSource(int dmxId, int input, int source) {
    mpCIApi->set_dvb_source(dmxId, input, source);
    return 0;
}

int AmCI::getUsbcamDriverStatus()
{
    // ioctl(fdMedia, AML_USBCAM_IOC_GET_MODULE_STATE, &device_state);
    //         if (device_state == DEVICE_DISCONNECT) {
    //             return DEVICE_DISCONNECT;
    //         }
    return 0;
}

