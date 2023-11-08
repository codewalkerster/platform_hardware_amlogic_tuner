/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * dvr playback module
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>

#ifndef DEBUG_ON_PC
#include <dmx.h>
#endif

#include "dvr_types.h"
#include "dvr_playback.h"

/**\brief DVR plaback state*/
typedef enum {
  DVR_PLAYBACK_STATE_OPENED,    /**< DVR Playback state is opened*/
  DVR_PLAYBACK_STATE_STARTED,   /**< DVR Playback state is started*/
  DVR_PLAYBACK_STATE_STOPPED,   /**< DVR Playback state is stopped*/
  DVR_PLAYBACK_STATE_CLOSED     /**< DVR Playback state is closed*/
} DVR_PlaybackState_t;

/**\brief DVR plaback context*/
typedef struct {
  int fd;                       /**< DVR Playback device filter descriptor*/
  int dmx_dev_id;               /**< DVR Playback device*/
  pthread_mutex_t lock;         /**< DVR Playback context mutex*/
  DVR_PlaybackState_t state;    /**< DVR Playback state*/
  int is_encrypted;             /**< DVR Playback needs decryption*/
  uint32_t key_token;           /**< DVR Playback decryption keytoken*/
  int dump_fd;                  /**< DVR Playback dump fd*/
} DVR_PlaybackContext_t;

static DVR_PlaybackContext_t playback_ctx =
{
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .state = DVR_PLAYBACK_STATE_CLOSED
};

// Open a playback session with a demux device id
// open the demux device and dvr device with the same id
// For Clear
//     set demux input to INPUT_LOCAL
//     set demux source to DVB_DEMUX_SOURCE_DMA%d with dmx_dev_id
// For Encrypted
//     set demux input to INPUT_LOCAL_SEC
//     set demux source to DVB_DEMUX_SECSOURCE_DMA%d with dmx_dev_id
DVR_Result_t dvr_playback_open(
    DVR_PlaybackHandle_t *p_handle,
    DVR_PlaybackOpenParams_t *params)
{
  DVR_CHECK(p_handle != NULL);
  DVR_CHECK(params != NULL);

  DVR_PlaybackContext_t *p_ctx = &playback_ctx;
  DVR_CHECK(p_ctx->state == DVR_PLAYBACK_STATE_CLOSED);

  pthread_mutex_lock(&p_ctx->lock);
#ifndef DEBUG_ON_PC
  int fd;
  char node[32] = {0};

  dvb_set_demux_source(
        params->dmx_dev_id,
        DVB_DEMUX_SOURCE_DMA0 + params->dmx_dev_id
  );
  memset(node, 0, sizeof(node));
  snprintf(node, sizeof(node), "/dev/dvb0.dvr%d", params->dmx_dev_id);
  fd = open(node, O_WRONLY);
  DVR_CHECK_WITH_UNLOCK(fd >= 0, &p_ctx->lock);
  playback_ctx.fd = fd;
#endif
  playback_ctx.dump_fd = params->reserved[0];

  *p_handle = &playback_ctx;
  playback_ctx.state = DVR_PLAYBACK_STATE_OPENED;

  pthread_mutex_unlock(&p_ctx->lock);

  return DVR_SUCCESS;
}

DVR_Result_t dvr_playback_close(DVR_PlaybackHandle_t handle)
{
  DVR_PlaybackContext_t *p_ctx = (DVR_PlaybackContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
                p_ctx->state != DVR_PLAYBACK_STATE_CLOSED,
                &p_ctx->lock);

#ifndef DEBUG_ON_PC
  // Close inject device
  close(p_ctx->fd);
#endif

  p_ctx->is_encrypted = 0;
  p_ctx->state = DVR_PLAYBACK_STATE_CLOSED;
  pthread_mutex_unlock(&p_ctx->lock);

  return DVR_SUCCESS;
}

int dvr_playback_start(DVR_PlaybackHandle_t handle)
{
  DVR_PlaybackContext_t *p_ctx = (DVR_PlaybackContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
        p_ctx->state != DVR_PLAYBACK_STATE_STARTED,
        &p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
        p_ctx->state != DVR_PLAYBACK_STATE_CLOSED,
        &p_ctx->lock);

  p_ctx->state = DVR_PLAYBACK_STATE_STARTED;
  pthread_mutex_unlock(&p_ctx->lock);

  return DVR_SUCCESS;
}

DVR_Result_t dvr_playback_stop(DVR_PlaybackHandle_t handle)
{
  DVR_PlaybackContext_t *p_ctx = (DVR_PlaybackContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
        p_ctx->state != DVR_PLAYBACK_STATE_STOPPED,
        &p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
        p_ctx->state != DVR_PLAYBACK_STATE_CLOSED,
        &p_ctx->lock);

  // stop inject device
  p_ctx->state = DVR_PLAYBACK_STATE_STOPPED;
  pthread_mutex_unlock(&p_ctx->lock);

  return DVR_SUCCESS;
}

DVR_Result_t dvr_playback_set_key_token(
    DVR_PlaybackHandle_t handle,
    int pid,
    uint32_t key_token)
{
  return DVR_SUCCESS;
}

size_t dvr_playback_write(
    DVR_PlaybackHandle_t handle,
    uint8_t *data,
    size_t len)
{
  DVR_PlaybackContext_t *p_ctx = (DVR_PlaybackContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  DVR_CHECK(data != NULL);
  DVR_CHECK(len > 0);
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
        p_ctx->state == DVR_PLAYBACK_STATE_STARTED,
        &p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
        len >= 188,
        &p_ctx->lock);

  ssize_t ret = 0;
  len -= (len % 188);
#ifndef DEBUG_ON_PC
  ret = write(p_ctx->fd, data, len);
  if (ret == -1) {
    if (errno != EINTR) {
      DVR_ERROR("%s write dvr failed, %s", __func__, strerror(errno));
      pthread_mutex_unlock(&p_ctx->lock);
      return DVR_FAILURE;
    }
    ret = 0;
  } else {
    DVR_INFO("%s %#x bytes written", __func__, ret);
  }
#endif
  if (p_ctx->dump_fd >= 0) {
    if (write(p_ctx->dump_fd, data, len) != len) {
      DVR_ERROR("%s dump write failed\n", __func__);
    }
  }

  pthread_mutex_unlock(&p_ctx->lock);
  return ret;
}
