/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#ifndef AMDVR_H
#define AMDVR_H
#include <utils/RefBase.h>
#include <am_types.h>
#include <sys/types.h>
#include "dmx.h"

#define AM_DVR_MAX_PID_COUNT  (32)

/****************************************************************************
 * Type definitions
 ***************************************************************************/

/**\brief DVR module's error code*/
enum AM_DVR_ErrorCode
{
    AM_DVR_ERROR_BASE=AM_ERROR_BASE(AM_MOD_DVR),
    AM_DVR_ERR_INVALID_ARG,            /**< Invalid argument*/
    AM_DVR_ERR_INVALID_DEV_NO,        /**< Invalid decide number*/
    AM_DVR_ERR_BUSY,                        /**< The device has already been openned*/
    AM_DVR_ERR_NOT_ALLOCATED,           /**< The device has not been allocated*/
    AM_DVR_ERR_CANNOT_CREATE_THREAD,    /**< Cannot create a new thread*/
    AM_DVR_ERR_CANNOT_OPEN_DEV,         /**< Cannot open the device*/
    AM_DVR_ERR_NOT_SUPPORTED,           /**< Not supported*/
    AM_DVR_ERR_NO_MEM,                  /**< Not enough memory*/
    AM_DVR_ERR_TIMEOUT,                 /**< Timeout*/
    AM_DVR_ERR_SYS,                     /**< System error*/
    AM_DVR_ERR_NO_DATA,                 /**< No data received*/
    AM_DVR_ERR_CANNOT_OPEN_OUTFILE,        /**< Cannot open the output file*/
    AM_DVR_ERR_TOO_MANY_STREAMS,        /**< PID number is too big*/
    AM_DVR_ERR_STREAM_ALREADY_ADD,        /**< The elementary stream has already been added*/
    AM_DVR_ERR_END
};

typedef struct AM_DVR_Device AM_DVR_Device_t;

typedef struct
{
    int            fid;    /**< DMX Filter ID*/
    uint16_t    pid;    /**< Stream PID*/
}AM_DVR_Stream_t;

/**\brief Recording parameters*/
typedef struct
{
    int        pid_count; /**< PID number*/
    int        pids[AM_DVR_MAX_PID_COUNT]; /**< PID array*/
} AM_DVR_StartRecPara_t;


/**\brief DVR设备*/
struct AM_DVR_Device
{
    int                 dev_no;  /**< 设备号*/
    int                    dmx_no;     /**< DMX设备号*/
    void               *drv_data;/**< 驱动私有数据*/
    int                 open_cnt;   /**< 设备打开计数*/
    int                    stream_cnt;    /**< 流个数*/
    AM_DVR_Stream_t        streams[AM_DVR_MAX_PID_COUNT]; /**< Streams*/
    AM_Bool_t               record;/**< 是否正在录像标志*/
    pthread_mutex_t         lock;    /**< 设备保护互斥体*/
    AM_DVR_StartRecPara_t    start_para;    /**< 启动参数*/
    //AM_DVR_Source_t        src;            /**< 源数据*/
    int                   dmx_fd;
};

typedef void (*AM_DVR_DataCb) (void* device);

struct AM_DVR_Data {
    AM_DVR_DataCb cb;
    void* user_data;
};


using namespace android;
class AmDvr : public RefBase {
public:
    AmDvr(uint32_t demuxId);
    ~AmDvr();

    AM_ErrorCode_t AM_DVR_Open(dmx_input_source_t inputSource);
    AM_ErrorCode_t AM_DVR_Close();
    AM_ErrorCode_t AM_DVR_Read(uint8_t *buf, int *size);
    AM_ErrorCode_t AM_DVR_SetCallback(AM_DVR_DataCb cb, void* data);
    static void* dvr_data_thread(void *arg);

private:
    uint32_t mDmxId;
    AM_DVR_Device_t *mDvrDevice = NULL;
    AM_DVR_Data     *mData      = NULL;
    bool          enable_thread;
    pthread_mutex_t     lock;
    pthread_cond_t      cond;
    pthread_t           thread;
    int                 opencnt;
};

#endif

