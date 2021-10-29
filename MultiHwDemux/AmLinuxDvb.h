/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */



#ifndef AMLINUX_DVB_H
#define AMLINUX_DVB_H

#include <AmDmx.h>

#define DVB_DVR     "/dev/dvb0.dvr0"
#define DVB_DEMUX    "/dev/dvb0.demux0"
#define UNUSED(x) (void)x

//#define DVB_DVR     "/dev/dvb/adapter0/dvr0"
//#define DVB_DEMUX    "/dev/dvb/adapter0/demux0"

typedef struct {
    char dev_name[32];
    int fd[DMX_FILTER_COUNT];
} DVBDmx_t;

class AmLinuxDvb : public RefBase {

public:
    AmLinuxDvb();
    ~AmLinuxDvb();
    AM_ErrorCode_t dvb_open(AM_DMX_Device *dev);
    AM_ErrorCode_t dvb_close(AM_DMX_Device *dev);
    AM_ErrorCode_t dvb_alloc_filter(AM_DMX_Device *dev, AM_DMX_Filter *filter);
    AM_ErrorCode_t dvb_free_filter(AM_DMX_Device *dev, AM_DMX_Filter *filter);
    AM_ErrorCode_t dvb_get_stc(AM_DMX_Device *dev, AM_DMX_Filter *filter);
    AM_ErrorCode_t dvb_set_sec_filter(AM_DMX_Device *dev, AM_DMX_Filter *filter, const struct dmx_sct_filter_params *params);
    AM_ErrorCode_t dvb_set_pes_filter(AM_DMX_Device *dev, AM_DMX_Filter *filter, const struct dmx_pes_filter_params *params);
    AM_ErrorCode_t dvb_enable_filter(AM_DMX_Device *dev, AM_DMX_Filter *filter, bool enable);
    AM_ErrorCode_t dvb_get_mem_info(AM_DMX_Filter *filter, dmx_mem_info* mDmxMenInfo);
    AM_ErrorCode_t dvb_get_filter_mem_info(AM_DMX_Device *dev, dmx_filter_mem_info* mDmxFilterMemInfo);
    AM_ErrorCode_t dvb_set_buf_size(AM_DMX_Device *dev, AM_DMX_Filter *filter, int size);
    AM_ErrorCode_t dvb_poll(AM_DMX_Device *dev, AM_DMX_FilterMask_t *mask, int timeout);
    AM_ErrorCode_t dvb_read(AM_DMX_Device *dev, AM_DMX_Filter *filter, uint8_t *buf, int *size,bool pollflag = true);
    AM_ErrorCode_t dvb_set_source(AM_DMX_Device *dev, dmx_input_source_t inputSource);
    AM_ErrorCode_t dvr_open(AM_DMX_Device *dev,dmx_input_source_t inputSource);
    int dvr_data_write(uint8_t *buf, int size,uint64_t timeout);
    AM_ErrorCode_t dvr_close(void);
private:
    int getDmaByDemuxId(int demuxId);
    int mDvrFd;
    int pollFailCount;
    int mFilterMemInfoFd;
};

#endif
