/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * dvr playback header file
 */

#ifndef _TUNER_HAL_DVR_PLAYBACK_H
#define _TUNER_HAL_DVR_PLAYBACK_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "dvr_types.h"
#include "dvb_utils.h"

/**\brief DVR playback handle*/
typedef void* DVR_PlaybackHandle_t;

/**\brief DVR playback open parameters*/
typedef struct {
    int     dmx_dev_id;     /**< Demux device id */
    int     reserved[2];    /**< Reserved */
} DVR_PlaybackOpenParams_t;

/**
 * @brief Open a playback session
 *
 * @param p_handle Return the session of the newly created dvr session
 * @param params Open parameters
 * @return DVR_Result error code
 */
DVR_Result_t dvr_playback_open(DVR_PlaybackHandle_t *p_handle, DVR_PlaybackOpenParams_t *params);

/**
 * @brief Close a playback session
 *
 * @param handle The DVR playback session
 * @return DVR_Result error code
 */
DVR_Result_t dvr_playback_close(DVR_PlaybackHandle_t handle);

/**
 * @brief Start a playback session
 *
 * @param handle The DVR playback session
 * @return DVR_Result error code
 */
DVR_Result_t dvr_playback_start(DVR_PlaybackHandle_t handle);

/**
 * @brief Stop a playback session
 *
 * @param handle The DVR playback session
 * @return DVR_Result_t error code
 */
DVR_Result_t dvr_playback_stop(DVR_PlaybackHandle_t handle);

/**
 * @brief Set key token for the pid decryption
 *
 * @param handle The DVR playback session
 * @param pid The stream's PID
 * @param key_token The key token for decryption
 * @return DVR_Result_t error code
 */
DVR_Result_t dvr_playback_set_key_token(DVR_PlaybackHandle_t handle, int pid, uint32_t key_token);

/**
 * @brief Read dvr data from the DVR playback session
 *
 * @param handle The DVR playback session
 * @param data The data buffer
 * @param len The buffer length
 * @return size_t -1 if failed, else the actual written length
 */
size_t dvr_playback_write(DVR_PlaybackHandle_t handle, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif  /* _TUNER_HAL_DVR_PLAYBACK_H */
