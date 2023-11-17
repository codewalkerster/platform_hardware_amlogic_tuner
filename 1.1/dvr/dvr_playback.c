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

#include "libdsm.h"
#include "dsc_dev.h"
#include "dvr_types.h"
#include "dvr_playback.h"

#define DVR_MAX_PLAYBACK_SESSION_CNT    (4)
#define DVR_MAX_PLAYBACK_ENCRYPT_CNT     (8)

/**\brief DVR plaback state*/
typedef enum {
  DVR_PLAYBACK_STATE_OPENED,    /**< DVR Playback state is opened*/
  DVR_PLAYBACK_STATE_STARTED,   /**< DVR Playback state is started*/
  DVR_PLAYBACK_STATE_STOPPED,   /**< DVR Playback state is stopped*/
  DVR_PLAYBACK_STATE_CLOSED     /**< DVR Playback state is closed*/
} DVR_PlaybackState_t;

/**\brief DVR playback stream CA info*/
typedef struct {
  uint16_t pid;                     /**< DVR Playback stream pid*/
  uint32_t key_token;               /**< DVR Playback dsm key token*/
  int ca_chans[DVR_MAX_CA_CHAN_CNT];/**< DVR Playback ca channels*/
} DVR_PlaybackEncryptStream_t;

/**\brief DVR plaback context*/
typedef struct {
  int fd;                                                           /**< DVR Playback device filter descriptor*/
  int dmx_dev_id;                                                   /**< DVR Playback device*/
  pthread_mutex_t lock;                                             /**< DVR Playback context mutex*/
  DVR_PlaybackState_t state;                                        /**< DVR Playback state*/
  size_t dsm_sess;                                                  /**< DVR Playback decryption session*/
  DVR_PlaybackEncryptStream_t streams[DVR_MAX_PLAYBACK_ENCRYPT_CNT]; /**< DVR Playback encryped pid count*/
  int dump_fd;                                                      /**< DVR Playback dump fd*/
} DVR_PlaybackContext_t;

static DVR_PlaybackContext_t playback_ctx[DVR_MAX_PLAYBACK_SESSION_CNT] = {
  {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .state = DVR_PLAYBACK_STATE_CLOSED
  },
  {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .state = DVR_PLAYBACK_STATE_CLOSED
  },
  {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .state = DVR_PLAYBACK_STATE_CLOSED
  },
  {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .state = DVR_PLAYBACK_STATE_CLOSED
  }
};

static int ca_prepare(
    DVR_PlaybackContext_t *p_ctx,
    DVR_PlaybackEncryptStream_t *stream)
{
  uint32_t ready = DSM_PROP_SLOT_NOT_READY;
  struct dsm_keyslot_list keyslot_list;

  if (stream->key_token != -1) {
    return DVR_SUCCESS;
  }

  DVR_CHECK(
        DSM_GetProperty(p_ctx->dsm_sess,
        DSM_PROP_DEC_SLOT_READY,
        &ready) == 0);
  DVR_CHECK(ready == DSM_PROP_SLOT_IS_READY);

  memset(&keyslot_list, 0, sizeof(struct dsm_keyslot_list));
  // Get key slot list from DSM
  DVR_CHECK(
        DSM_GetKeySlots(p_ctx->dsm_sess,
        &keyslot_list) == 0);
  DVR_CHECK(keyslot_list.count > 0);

  DVR_INFO("%s decryption slot is ready, total count: %d",
        __func__, keyslot_list.count);

  // Loop all the key slots and get the algo/is_iv/parity etc.
  for (int i = 0; i < keyslot_list.count; i++) {
    int j;
    int ca_chan = -1;
    int dsc_type = CA_DSC_COMMON_TYPE;
    uint32_t parity;
    struct dsm_keyslot *slot = &keyslot_list.keyslots[i];
    if (slot->is_enc)
      continue;

    // Allocate ca dsc channel
    ca_chan = ca_alloc_chan(
                    p_ctx->dmx_dev_id,
                    stream->pid,
                    slot->algo,
                    dsc_type);
    DVR_CHECK(ca_chan >= 0);

    DVR_INFO("%s alloc ca channel(%d, %d) ok.",
        __func__, stream->pid, ca_chan);
    for (j = 0; j < DVR_MAX_CA_CHAN_CNT; j++) {
      if (stream->ca_chans[j] == -1) {
        stream->ca_chans[j] = ca_chan;
        break;
      }
    }
    DVR_CHECK(j < DVR_MAX_CA_CHAN_CNT);

    if (slot->parity == DSM_PARITY_EVEN) {
      parity = slot->is_iv ? CA_KEY_EVEN_IV_TYPE : CA_KEY_EVEN_TYPE;
    } else if (slot->parity == DSM_PARITY_ODD) {
      parity = slot->is_iv ? CA_KEY_ODD_IV_TYPE : CA_KEY_ODD_TYPE;
    } else {
      parity = slot->is_iv ? CA_KEY_ODD_IV_TYPE : CA_KEY_ODD_TYPE;
    }

    // Set KTE to ca dsc channel
    DVR_CHECK(ca_set_key(p_ctx->dmx_dev_id, ca_chan, parity, slot->id) ==0);
    DVR_INFO("%s set ca key(%d, %d, %d, %d)",
          __func__,
          p_ctx->dmx_dev_id,
          ca_chan,
          parity,
          slot->id);
  }

  return 0;
}

static int ca_release(
    DVR_PlaybackContext_t *p_ctx,
    DVR_PlaybackEncryptStream_t *stream)
{
  int i;

  // Free the ca channel
  for (i = 0; i < DVR_MAX_CA_CHAN_CNT; i++) {
    if (stream->ca_chans[i] >= 0) {
        ca_free_chan(p_ctx->dmx_dev_id, stream->ca_chans[i]);
        stream->ca_chans[i] = -1;
    }
  }

  return 0;
}

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
  DVR_PlaybackContext_t *p_ctx;
  int i;
  DVR_CHECK(p_handle != NULL);
  DVR_CHECK(params != NULL);

  for (i = 0; i < DVR_MAX_PLAYBACK_SESSION_CNT; i++) {
    if (playback_ctx[i].state == DVR_PLAYBACK_STATE_CLOSED) {
      break;
    }
  }
  DVR_CHECK(i < DVR_MAX_PLAYBACK_SESSION_CNT);

  p_ctx = &playback_ctx[i];
  pthread_mutex_lock(&p_ctx->lock);
  for (i = 0; i < DVR_MAX_PLAYBACK_ENCRYPT_CNT; i++) {
    p_ctx->streams[i].pid = DVR_INVALID_PID;
    p_ctx->streams[i].key_token = -1;
    for (int j = 0; j < DVR_MAX_CA_CHAN_CNT; j++) {
      p_ctx->streams[i].ca_chans[j] = -1;
    }
  }
  p_ctx->dsm_sess = -1;
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
  p_ctx->fd = fd;
#endif
  p_ctx->dump_fd = params->reserved[0];
  p_ctx->dmx_dev_id = params->dmx_dev_id;

  *p_handle = p_ctx;
  p_ctx->state = DVR_PLAYBACK_STATE_OPENED;

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
  p_ctx->fd = -1;
#endif

  if (p_ctx->dsm_sess != -1) {
    DSM_CloseSession(p_ctx->dsm_sess);
    p_ctx->dsm_sess = -1;
  }
  // We should release decryption ca channels if the pid stream is secure
  for (int i = 0; i < DVR_MAX_PLAYBACK_ENCRYPT_CNT; i++) {
    if (p_ctx->streams[i].key_token != -1) {
      ca_release(p_ctx, &p_ctx->streams[i]);
      p_ctx->streams[i].key_token = -1;
    }
  }

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
  int i;
  DVR_PlaybackContext_t *p_ctx = (DVR_PlaybackContext_t *)handle;
  DVR_CHECK(p_ctx != NULL);
  DVR_CHECK(pid != DVR_INVALID_PID);
  DVR_CHECK(key_token != -1);
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
        p_ctx->state != DVR_PLAYBACK_STATE_CLOSED,
        &p_ctx->lock);

  if (p_ctx->dsm_sess == -1) {
    p_ctx->dsm_sess = DSM_OpenSession(0);
    DVR_CHECK_WITH_UNLOCK(p_ctx->dsm_sess != -1, &p_ctx->lock);
    DVR_CHECK_WITH_UNLOCK(
            DSM_BindToken(p_ctx->dsm_sess, key_token) == 0,
            &p_ctx->lock);
    DVR_CHECK_WITH_UNLOCK(ca_open(p_ctx->dmx_dev_id) == 0,
            &p_ctx->lock);
  }

  for (i = 0; i < DVR_MAX_PLAYBACK_ENCRYPT_CNT; i++) {
    // Return directly if the pid had been set key token already
    if (p_ctx->streams[i].pid == pid &&
        p_ctx->streams[i].key_token != -1) {
      goto exit;
    }
  }

  // This is a new pid, and we need to find a slot to store it
  for (i = 0; i < DVR_MAX_PLAYBACK_ENCRYPT_CNT; i++) {
    if (p_ctx->streams[i].pid == DVR_INVALID_PID) {
      break;
    }
  }
  DVR_CHECK_WITH_UNLOCK(i < DVR_MAX_PLAYBACK_ENCRYPT_CNT, &p_ctx->lock);
  p_ctx->streams[i].pid = pid;
  // Prepare ca
  DVR_CHECK_WITH_UNLOCK(
      ca_prepare(p_ctx, &p_ctx->streams[i]) == 0,
      &p_ctx->lock);
  p_ctx->streams[i].key_token = key_token;

exit:
  pthread_mutex_unlock(&p_ctx->lock);
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
