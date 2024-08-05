/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * dvr playback module
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <poll.h>

#ifndef DEBUG_ON_PC
#include <dmx.h>
#endif

#include "libdsm.h"
#include "dsc_dev.h"
#include "dvr_types.h"
#include "dvr_playback.h"

//#define DVR_DUMP_INJECT
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
  int ca_chan;                      /**< DVR Playback ca channels*/
  int ca_ready;                     /**< DVR Playback descrabmbler status*/
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
  int fd2;                                                          /**< DVR Playback record fd*/
  int recfd;                                                        /**< DVR Playback record filter fd*/
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

  if (stream->ca_ready) {
    return DVR_SUCCESS;
  }

  DVR_CHECK(
        DSM_GetProperty(p_ctx->dsm_sess,
        DSM_PROP_DEC_SLOT_READY,
        &ready) == 0);
  if (ready != DSM_PROP_SLOT_IS_READY) {
    DVR_INFO("%s slot not ready", __func__);
    return DVR_SUCCESS;
  }

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
    int ca_chan = stream->ca_chan;
    int dsc_type = CA_DSC_COMMON_TYPE;
    uint32_t parity;
    struct dsm_keyslot *slot = &keyslot_list.keyslots[i];
    if (slot->is_enc)
      continue;

    // Allocate ca dsc channel
    if (ca_chan == -1) {
      ca_chan = ca_alloc_chan(
                      p_ctx->dmx_dev_id,
                      stream->pid,
                      slot->algo,
                      dsc_type);
      DVR_CHECK(ca_chan >= 0);
      stream->ca_chan = ca_chan;
    }

    DVR_INFO("%s alloc ca channel(%d, %d) ok.",
        __func__, stream->pid, ca_chan);

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

    stream->ca_ready = 1;
  }

  return 0;
}

static int ca_release(
    DVR_PlaybackContext_t *p_ctx,
    DVR_PlaybackEncryptStream_t *stream)
{
  // Free the ca channel
  ca_free_chan(p_ctx->dmx_dev_id, stream->ca_chan);
  stream->ca_chan = -1;
  stream->ca_ready = 0;
  stream->pid = DVR_INVALID_PID;
  stream->key_token = -1;

  return 0;
}

#ifdef DVR_DUMP_INJECT
// Return the filter fd
static int create_filter(int dmx_dev_id, int pid)
{
  int ret;
  int fd = -1;
  char dev_name[32];
  struct dmx_pes_filter_params filter_params;

  // open pid filter
  snprintf(dev_name, sizeof(dev_name), "/dev/dvb0.demux%d", dmx_dev_id);
  fd = open(dev_name, O_RDWR);
  if (fd == -1) {
    DVR_ERROR("%s cannot open \"%s\" (%s)", __func__, dev_name, strerror(errno));
    return fd;
  }

  // setting pes filter
  memset(&filter_params, 0, sizeof(filter_params));
  filter_params.pid = pid;
  filter_params.input = DMX_IN_FRONTEND;
  filter_params.output = DMX_OUT_TS_TAP;
  filter_params.pes_type = DMX_PES_OTHER;
  ret = ioctl(fd, DMX_SET_PES_FILTER, &filter_params);
  if (ret == -1) {
    DVR_ERROR("%s set pes filter failed: %s", __func__, strerror(errno));
  }

  DVR_INFO("create record filter success, pid: %#x, dmx: %d", pid, dmx_dev_id);
  return fd;
}
#endif

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
    p_ctx->streams[i].ca_chan = -1;
    p_ctx->streams[i].ca_ready = 0;
  }
  p_ctx->dsm_sess = -1;
  p_ctx->fd = -1;
  p_ctx->fd2 = -1;
  p_ctx->recfd = -1;

  dvb_set_demux_source(
        params->dmx_dev_id,
        DVB_DEMUX_SOURCE_DMA0 + params->dmx_dev_id
  );

  //DVR_CHECK(ca_init() == 0);
  ca_open(params->dmx_dev_id);

  // Open dmx_dev_id[2] for inject and recording the re-encrypted ts
  if (p_ctx->fd == -1) {
    // DVR write fd
    p_ctx->fd = dvb_dvr_device_open(params->dmx_dev_id, 0);

    #ifdef DVR_DUMP_INJECT
    // DVR read fd
    p_ctx->fd2 = dvb_dvr_device_open(params->dmx_dev_id, 1);
    DVR_CHECK(p_ctx->fd2 >= 0);
    dvb_dvr_set_ringbuffer(p_ctx->fd2, 5 * 188 * 1024);
    #endif
  }

  #ifdef DVR_DUMP_INJECT
  if (p_ctx->recfd == -1) {
    // Create the 0x2000 pid filter to record the whole ts on the demux
    p_ctx->recfd = create_filter(params->dmx_dev_id, 0x2000);
    DVR_CHECK(p_ctx->recfd != -1);
    // Start a filter
    int ret = ioctl(p_ctx->recfd, DMX_START, 0);
    if (ret == -1) {
      DVR_ERROR("DMX_START failed, %s", strerror(errno));
    }
    DVR_CHECK(ret != -1);
  }
  p_ctx->dump_fd = params->reserved[0];
  #endif
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
  close(p_ctx->fd2);
  close(p_ctx->recfd);
#endif

  // We should release decryption ca channels if the pid stream is secure
  for (int i = 0; i < DVR_MAX_PLAYBACK_ENCRYPT_CNT; i++) {
    if (p_ctx->streams[i].key_token != -1) {
      ca_release(p_ctx, &p_ctx->streams[i]);
    }
  }

  if (p_ctx->dsm_sess != -1) {
    DSM_CloseSession(p_ctx->dsm_sess);
    p_ctx->dsm_sess = -1;
  }

  ca_close(p_ctx->dmx_dev_id);
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
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
        p_ctx->state != DVR_PLAYBACK_STATE_CLOSED,
        &p_ctx->lock);

  if (key_token == -1) {
    if (p_ctx->dsm_sess != -1) {
      DSM_CloseSession(p_ctx->dsm_sess);
      p_ctx->dsm_sess = -1;
      DVR_INFO("%s close dsm session", __func__);
    }
  } else if (p_ctx->dsm_sess == -1) {
    p_ctx->dsm_sess = DSM_OpenSession(0);
    DVR_CHECK_WITH_UNLOCK(p_ctx->dsm_sess != -1, &p_ctx->lock);
    DVR_CHECK_WITH_UNLOCK(
            DSM_BindToken(p_ctx->dsm_sess, key_token) == 0,
            &p_ctx->lock);
  }

  for (i = 0; i < DVR_MAX_PLAYBACK_ENCRYPT_CNT; i++) {
    if (p_ctx->streams[i].pid == pid &&
        p_ctx->streams[i].key_token != -1) {
      // Release ca resource if set invalid key token
      if (key_token == -1) {
        ca_release(p_ctx, &p_ctx->streams[i]);
      }
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
  p_ctx->streams[i].key_token = key_token;
  // Prepare ca
  DVR_CHECK_WITH_UNLOCK(
      ca_prepare(p_ctx, &p_ctx->streams[i]) == 0,
      &p_ctx->lock);

exit:
  pthread_mutex_unlock(&p_ctx->lock);
  return DVR_SUCCESS;
}

#ifdef DVR_DUMP_INJECT
static int dvr_poll(int dvr_fd, pthread_mutex_t lock)
{
  int ret;
  struct pollfd poll_fd;

  memset(&poll_fd, 0, sizeof(poll_fd));
  poll_fd.fd = dvr_fd;
  poll_fd.events = POLLIN | POLLERR;

  pthread_mutex_unlock(&lock);
  ret = poll(&poll_fd, 1, 100);
  if (ret < 0) {
    DVR_ERROR("%s failed: %s. fd: %d", __func__, strerror(errno), dvr_fd);
    pthread_mutex_lock(&lock);
    return -1;
  }

  if (!(poll_fd.revents & POLLIN)) {
    pthread_mutex_lock(&lock);
    return -1;
  }

  pthread_mutex_lock(&lock);
  return 0;
}

// Record pid stream to normal buffer
// Return recorded data length
static ssize_t normal_dvr(int dvr_fd, uint8_t *buf, size_t len, pthread_mutex_t lock)
{
  ssize_t rec_len = 0;

  if (dvr_poll(dvr_fd, lock) || dvr_fd < 0) {
    DVR_INFO("%s no poll in, dvr_fd: %d\n", __func__, dvr_fd);
    return rec_len;
  }

  rec_len = read(dvr_fd, buf, len);

  return rec_len;
}
#endif

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

  // Cuz the kte maybe not ready when dvr_playback_set_key_token called, we need
  // check the ca ready status every time before inject ts. And try to prepare
  // ca resource if ca channel is not ready
  for (int i = 0; i < DVR_MAX_PLAYBACK_ENCRYPT_CNT; i++) {
    if (p_ctx->streams[i].key_token != -1 && !p_ctx->streams[i].ca_ready) {
      ca_prepare(p_ctx, &p_ctx->streams[i]);
    }
  }

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
    DVR_INFO("%s %#zx bytes written", __func__, ret);
  }
#endif
#ifdef DVR_DUMP_INJECT
  if (p_ctx->dump_fd >= 0) {
    uint8_t *buf = (uint8_t *)malloc(len);
    ssize_t act_rec_len = 0;
    int time = 5;
    do {
      act_rec_len += normal_dvr(p_ctx->fd2,
                            buf + act_rec_len,
                            len - act_rec_len,
                            p_ctx->lock);
      if (act_rec_len >= len) {
        break;
      }
      usleep(2000*1000);
    } while (time-- > 0);
    if (write(p_ctx->dump_fd, buf, act_rec_len) != act_rec_len) {
      DVR_ERROR("%s dump write failed\n", __func__);
    }
    if (act_rec_len != len) {
      DVR_ERROR("lost %#x bytes", len - act_rec_len);
    }
  }
#endif

  pthread_mutex_unlock(&p_ctx->lock);
  return ret;
}
