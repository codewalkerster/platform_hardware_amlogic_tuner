/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * dvr record header file
 */

#ifndef _TUNER_HAL_DVR_REC_H
#define _TUNER_HAL_DVR_REC_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "dvr_types.h"
#include "dvb_utils.h"

// Max PUSI data len
#define DVR_BLOCK_SIZE              (12*188*1024)

/**\brief DVR record handle*/
typedef void* DVR_RecordHandle_t;

/**\brief DVR TSIndexer flags*/
enum {
  DVR_INDEX_PUSI    = 0x01, /**< PUSI type*/
  DVR_INDEX_IFRAME  = 0x02, /**< IFrame type*/
  DVR_INDEX_PTS     = 0x04  /**< PTS type*/
};

/**\brief DVR record mode */
typedef enum {
  DVR_DIRECT_RECORD_MODE,     /**< direct dvr mode*/
  DVR_PUSI_RECORD_MODE,       /**< PUSI dvr mode*/
  DVR_INVALID_RECORD_MODE     /**< invalid dvr mode*/
} DVR_RecordMode_t;

/**\brief DVR stream type*/
typedef enum {
  DVR_STREAM_SECTION_TYPE,     /**< section type*/
  DVR_STREAM_VIDEO_TYPE,       /**< video stream type*/
  DVR_STREAM_AUDIO_TYPE,       /**< audio stream type*/
  DVR_STREAM_OTHER_PES_TYPE,   /**< other pes stream type*/
  DVR_STREAM_INVALID_TYPE      /**< invalid stream type*/
} DVR_StreamType_t;

/**\brief DVR video stream format*/
typedef enum {
  DVR_VIDEO_FORMAT_MPEG2, /**< MPEG2*/
  DVR_VIDEO_FORMAT_H264,  /**< H264*/
  DVR_VIDEO_FORMAT_HEVC,  /**< HEVC*/
  DVR_VIDEO_FORMAT_INVALID/**< Invalid*/
} DVR_VideoFormat_t;

/**\brief DVR Record filter parameters*/
typedef struct {
  int                 pid;      /**< filter pid*/
  DVR_StreamType_t    type;     /**< filter type*/
  DVR_VideoFormat_t   vfmt;     /**< video format of video filter for ts indexer*/
} DVR_RecordFilterParams_t;

/**\brief DVR record open parameters*/
typedef struct {
  DVB_DemuxSource_t     src;                    /**< Demux input source */
  int                   dmx_dev_id[3];          /**< Demux device id */
  int                   sec_buf_size;           /**< secure dvr buffer size */
  int                   non_sec_ringbuf_size;   /**< none secure ring buffer size */
  int                   reserved[8];            /**< reserved */
} DVR_RecordOpenParams_t;

/**\brief DVR record receive parameters*/
typedef struct {
  uint8_t *buf;             /**< receive buffer address*/
  size_t len;               /**< receive buffer size*/
  DVR_RecordMode_t mode;    /**< DVR mode*/
  int flags;                /**< DVR TSIndexer flags*/
  uint64_t pts;             /**< PUSI's PTS*/
} DVR_RecordReceiveParams_t;

/**
 * @brief Open a DVR recording session
 *
 * @param p_handle Return the session of the newly created dvr session
 * @param params Open parameters
 * @return DVR_Result_t error code
 */
DVR_Result_t dvr_record_open(DVR_RecordHandle_t *p_handle, DVR_RecordOpenParams_t *params);

/**
 * @brief Close a DVR recording session
 *
 * @param handle The handle of DVR recording session
 * @return DVR_Result_t error code
 */
DVR_Result_t dvr_record_close(DVR_RecordHandle_t handle);

/**
 * @brief Start a DVR recording session
 *
 * @param handle The handle of DVR recording session
 * @return DVR_Result_t error code
 */
DVR_Result_t dvr_record_start(DVR_RecordHandle_t handle);

/**
 * @brief Stop a DVR recording session
 *
 * @param handle The handle of DVR recording session
 * @return DVR_Result_t error code
 */
DVR_Result_t dvr_record_stop(DVR_RecordHandle_t handle);

/**
 * @brief Open a pid filter for DVR recording
 *
 * @param handle The handle of DVR recording session
 * @param params The parameters of DVR recording filter
 * @return int the allocated filter index
 */
int dvr_record_open_filter(DVR_RecordHandle_t handle, DVR_RecordFilterParams_t *params);

/**
 * @brief Start the specific filter of the DVR recording
 *
 * @param handle The handle of DVR recording session
 * @param filter_idx The filter's index
 * @return DVR_Result_t error code
 */

DVR_Result_t dvr_record_start_filter(DVR_RecordHandle_t handle, int filter_idx);

/**
 * @brief Stop the specific filter of the DVR recording
 *
 * @param handle The handle of DVR recording session
 * @param filter_idx The filter's index
 * @return DVR_Result_t error code
 */

DVR_Result_t dvr_record_stop_filter(DVR_RecordHandle_t handle, int filter_idx);

/**
 * @brief Close one filter of the DVR recording
 *
 * @param handle The handle of DVR recording session
 * @param filter_idx The filter's index
 * @return DVR_Result_t error code
 */

DVR_Result_t dvr_record_close_filter(DVR_RecordHandle_t handle, int filter_idx);

/**
 * @brief Set key token for the Re-encrypted DVR recording
 *
 * @param handle The handle of DVR recording session
 * @param pid The stream's PID
 * @param key_token The key token for Re-encryption
 * @return DVR_Result_t error code
 */
DVR_Result_t dvr_record_set_key_token(DVR_RecordHandle_t handle, int pid, uint32_t key_token);

/**
 * @brief Read dvr data from the DVR recording session
 *
 * @param handle The DVR recording session
 * @param params The DVR recording receive parameters
 * @return ssize_t -1 if failed, else the actual read length
 */
ssize_t dvr_record_read(DVR_RecordHandle_t handle, DVR_RecordReceiveParams_t *params);

#ifdef __cplusplus
}
#endif

#endif  /* _TUNER_HAL_DVR_REC_H */
