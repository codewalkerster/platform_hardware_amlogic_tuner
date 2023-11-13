/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * dvr record module
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#ifndef DEBUG_ON_PC
#include <dmx.h>
#endif

#include "dvr_record.h"
#include "ts_indexer.h"

#define DVR_MAX_RECORD_SESSION_CNT  (4)
#define DVR_MAX_RECORD_PID_CNT      (16)
#define DVR_MAX_RECORD_PUSI_CNT     (1000)
#define DVR_TIMEOUT                 (100)

#define DVR_RECORD_OUTPUTBUFFER_SIZE    (20*188*1024)

#define HAVE_PUSI(_m_)      ((_m_) & INDEX_PUSI)
#define HAVE_IFRAME(_m_)    ((_m_) & INDEX_IFRAME)
#define HAVE_PTS(_m_)       ((_m_) & INDEX_PTS)

typedef TS_Indexer_RingBuffer_t DVR_RecordOutput_t;

/**\brief DVR record state*/
typedef enum {
  DVR_RECORD_STATE_OPENED,      /**< DVR record state is opened*/
  DVR_RECORD_STATE_STARTED,     /**< DVR record state is started*/
  DVR_RECORD_STATE_STOPPED,     /**< DVR record state is stopped*/
  DVR_RECORD_STATE_CLOSED       /**< DVR record state is closed*/
} DVR_RecordState_t;

/**\brief DVR record stream information*/
typedef struct {
  int fd;                       /**< DVR record filter's fd*/
  uint16_t pid;                 /**< DVR record stream PID*/
  uint8_t needs_encryption;     /**< needs re-encryption*/
  uint32_t key_token;           /**< key token for re-encryption*/
} DVR_RecordStream_t;

/**\brief DVR record context*/
typedef struct {
  int fd[3];                                            /**< DVR record device file descriptor*/
  int evtfd;                                            /**< DVR record eventfd for poll's exit*/
  int dmx_dev_id[3];                                    /**< DVR record devices*/
  pthread_mutex_t lock;                                 /**< DVR record context mutex*/
  DVR_RecordState_t state;                              /**< DVR record state*/
  size_t none_sec_ringbuf_size;                         /**< DVR record none-secure ringbuffer size*/
  int  is_secure_mode;                                  /**< DVR record session run in secure mode*/
  uint32_t key_token;                                   /**< DVR record key token for re-encryption*/
  uint32_t block_size;                                  /**< DVR record block size*/
  DVR_RecordStream_t streams[DVR_MAX_RECORD_PID_CNT];   /**< DVR record stream list*/
  TS_Indexer_Pusi_t pusi[DVR_MAX_RECORD_PUSI_CNT];      /**< DVR record PUSI*/
  DVR_RecordOutput_t output;                            /**< DVR record output for tunerhal read*/
  TS_Indexer_t ts_indexer;                              /**< DVR record ts indxer handle*/
  int reserved[8];                                      /**< DVR record reserved bytes*/
} DVR_RecordContext_t;

static DVR_RecordContext_t record_ctx[DVR_MAX_RECORD_SESSION_CNT] = {
  {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .state = DVR_RECORD_STATE_CLOSED
  },
  {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .state = DVR_RECORD_STATE_CLOSED
  },
  {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .state = DVR_RECORD_STATE_CLOSED
  },
  {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .state = DVR_RECORD_STATE_CLOSED
  }
};

DVR_Result_t dvr_record_open(DVR_RecordHandle_t *p_handle, DVR_RecordOpenParams_t *params)
{
  DVR_RecordContext_t *p_ctx;
  int i;
  char dev_name[32];

  DVR_CHECK(p_handle != NULL);
  DVR_CHECK(params != NULL);

  for (i = 0; i < DVR_MAX_RECORD_SESSION_CNT; i++) {
    if (record_ctx[i].state == DVR_RECORD_STATE_CLOSED) {
      break;
    }
  }
  DVR_CHECK(i < DVR_MAX_RECORD_SESSION_CNT);

  p_ctx = &record_ctx[i];
  pthread_mutex_lock(&p_ctx->lock);
  for (int i = 0; i < DVR_MAX_RECORD_PID_CNT; i++) {
    p_ctx->streams[i].fd = -1;
    p_ctx->streams[i].pid = DVR_INVALID_PID;
    p_ctx->streams[i].needs_encryption = 0;
    p_ctx->streams[i].key_token = -1;
  }

  memcpy(p_ctx->dmx_dev_id, params->dmx_dev_id, sizeof(params->dmx_dev_id));
  p_ctx->is_secure_mode = 0;
  //p_ctx->block_size =
  params->none_sec_ringbuf_size = params->none_sec_ringbuf_size;

  // Init PUSI array
  memset(&p_ctx->pusi[0], 0, sizeof(TS_Indexer_Pusi_t) * DVR_MAX_RECORD_PUSI_CNT);

  // Init a ts indexer instance for video PTS/I-FRAME indexs
  ts_indexer_init(&p_ctx->ts_indexer);

  // Init the output
  memset(&p_ctx->output, 0, sizeof(DVR_RecordOutput_t));

  // Alloc output buffer which tuner hal will read data from
  p_ctx->output.buffer = malloc(DVR_RECORD_OUTPUTBUFFER_SIZE);
  DVR_CHECK_WITH_UNLOCK(p_ctx->output.buffer != NULL, &p_ctx->lock);

  // Config the output buffer as a ringbuffer for ts indexer
  // ts indexer parser will update r_offset
  // dvr read() will update w_offset
  p_ctx->output.len = DVR_RECORD_OUTPUTBUFFER_SIZE;
  p_ctx->output.r_offset = 0;
  p_ctx->output.w_offset = 0;

#ifndef DEBUG_ON_PC
  // Set hw demux source
  dvb_set_demux_source(params->dmx_dev_id[0], params->src);

  // Open dvr device
  // For Clear PVR, only use the dvr device dmx_dev_id[0]
  // For Secure DVR, use the dvr device dmx_dev_id[0] to record descrambled
  // stream to secure buffer A. Then inject A to dmx_dev_id[1] and dmx_dev_id[2].
  // To record re-encrypted PES from the dvr device dmx_dev_id[1] with TSE
  // To record other TS like section data from the dvr device dmx_dev_id[2]
  memset(dev_name, 0, sizeof(dev_name));
  snprintf(dev_name, sizeof(dev_name), "/dev/dvb0.dvr%d", params->dmx_dev_id[0]);
  p_ctx->fd[0] = open(dev_name, O_RDONLY);
  if (p_ctx->fd[0] == -1) {
    DVR_ERROR("%s cannot open \"%s\" (%s)", __func__, dev_name, strerror(errno));
    pthread_mutex_unlock(&p_ctx->lock);
    return DVR_FAILURE;
  }
  if (fcntl(p_ctx->fd[0], F_SETFL, fcntl(p_ctx->fd[0], F_GETFL, 0) | O_NONBLOCK, 0) < 0) {
    DVR_ERROR("%s set nonblock flag failed \"%s\"", __func__ ,strerror(errno));
    pthread_mutex_unlock(&p_ctx->lock);
    return DVR_FAILURE;
  }

  p_ctx->evtfd = eventfd(0, 0);

  // set dvbcore ring buffer size which should be 188 Bytes aligned
  if (params->none_sec_ringbuf_size > 0) {
    if (ioctl(p_ctx->fd[0], DMX_SET_BUFFER_SIZE, params->none_sec_ringbuf_size) == -1) {
      DVR_ERROR("%s set dvr ringbuf size failed \"%s\" (%s) buf_size:%d",
        __func__, dev_name, strerror(errno), params->none_sec_ringbuf_size);
    } else {
      DVR_INFO("%s set dvr ringbuf size success \"%s\" buf_size:%d",
        __func__, dev_name, params->none_sec_ringbuf_size);
    }
  }

  //TODO: load secdmx library
#endif
  memcpy(&p_ctx->reserved[0], &params->reserved[0], sizeof(params->reserved));
  p_ctx->state = DVR_RECORD_STATE_OPENED;

  *p_handle = p_ctx;
  pthread_mutex_unlock(&p_ctx->lock);

  return DVR_SUCCESS;
}

DVR_Result_t dvr_record_close(DVR_RecordHandle_t handle)
{
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
                p_ctx->state != DVR_RECORD_STATE_CLOSED,
                &p_ctx->lock);

  // close record device
  close(p_ctx->fd[0]);
  close(p_ctx->evtfd);
  if (p_ctx->is_secure_mode) {
    close(p_ctx->fd[1]);
    close(p_ctx->fd[2]);
  }

  if (p_ctx->output.buffer) {
    free(p_ctx->output.buffer);
  }

  p_ctx->is_secure_mode = 0;
  p_ctx->state = DVR_RECORD_STATE_CLOSED;
  pthread_mutex_unlock(&p_ctx->lock);

  return DVR_SUCCESS;
}

DVR_Result_t dvr_record_start(DVR_RecordHandle_t handle)
{
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
                p_ctx->state != DVR_RECORD_STATE_STARTED,
                &p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
                p_ctx->state != DVR_RECORD_STATE_CLOSED,
                &p_ctx->lock);

  // start record
  p_ctx->state = DVR_RECORD_STATE_STARTED;
  pthread_mutex_unlock(&p_ctx->lock);

  return DVR_SUCCESS;
}

DVR_Result_t dvr_record_stop(DVR_RecordHandle_t handle)
{
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
                p_ctx->state != DVR_RECORD_STATE_STOPPED,
                &p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
                p_ctx->state != DVR_RECORD_STATE_CLOSED,
                &p_ctx->lock);

  // stop record device
  p_ctx->state = DVR_RECORD_STATE_STOPPED;
  pthread_mutex_unlock(&p_ctx->lock);
  return DVR_SUCCESS;
}

int dvr_record_open_filter(DVR_RecordHandle_t handle, DVR_RecordFilterParams_t *params)
{
  int i;
  int ret;
  int fd = -1;
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  DVR_CHECK(params != NULL);
  DVR_CHECK(params->pid != DVR_INVALID_PID);
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
                p_ctx->state != DVR_RECORD_STATE_CLOSED,
                &p_ctx->lock);

  if (params->type == DVR_STREAM_VIDEO_TYPE)
  {
    // Set video pid and video format to ts indexer
    // support to parse MPEG2/AVC/HEVC's PTS and I-Frame index
    ts_indexer_set_video_pid(&p_ctx->ts_indexer, params->pid);
    ts_indexer_set_video_format(&p_ctx->ts_indexer, params->vfmt);
  } else if (params->type == DVR_STREAM_AUDIO_TYPE) {
    // Set audio pid to ts indexer, support to parse audio pts
    ts_indexer_set_audio_pid(&p_ctx->ts_indexer, params->pid);
  }

  // record pid
  for (i = 0; i < DVR_MAX_RECORD_PID_CNT; i++) {
    if (p_ctx->streams[i].pid == DVR_INVALID_PID)
      break;
  }
  DVR_CHECK_WITH_UNLOCK(
                i < DVR_MAX_RECORD_PID_CNT,
                &p_ctx->lock);

#ifndef DEBUG_ON_PC
  char dev_name[32];
  struct dmx_pes_filter_params filter_params;

  // open pid filter
  snprintf(dev_name, sizeof(dev_name), "/dev/dvb0.demux%d", p_ctx->dmx_dev_id[0]);
  fd = open(dev_name, O_RDWR);
  if (fd == -1) {
    DVR_ERROR("%s cannot open \"%s\" (%s)", __func__, dev_name, strerror(errno));
    goto exit;
  }

  // setting pes filter
  memset(&filter_params, 0, sizeof(filter_params));
  filter_params.pid = params->pid;
  filter_params.input = DMX_IN_FRONTEND;
  filter_params.output = DMX_OUT_TS_TAP;
  filter_params.pes_type = DMX_PES_OTHER;
  ret = ioctl(fd, DMX_SET_PES_FILTER, &filter_params);
  if (ret == -1) {
    DVR_ERROR("%s set pes filter failed: %s", __func__, strerror(errno));
    goto exit;
  }
#else
  fd = 0;
#endif

  p_ctx->streams[i].fd = fd;
  p_ctx->streams[i].pid = params->pid;
  pthread_mutex_unlock(&p_ctx->lock);
  DVR_INFO("open filter pid: %#x, fd[%d]: %d", params->pid, i, fd);
  return i;

exit:
  pthread_mutex_unlock(&p_ctx->lock);
  return -1;
}

DVR_Result_t dvr_record_start_filter(DVR_RecordHandle_t handle, int filter_idx)
{
  int ret;
  int fd;
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(filter_idx >= 0);
  DVR_CHECK(filter_idx < DVR_MAX_RECORD_PID_CNT);
  DVR_CHECK(p_ctx != NULL);
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
                p_ctx->state != DVR_RECORD_STATE_CLOSED,
                &p_ctx->lock);

  fd = p_ctx->streams[filter_idx].fd;
  DVR_CHECK_WITH_UNLOCK(fd >= 0, &p_ctx->lock);

#ifndef DEBUG_ON_PC
  // start a filter
  ret = ioctl(fd, DMX_START, 0);
  if (ret == -1) {
    DVR_ERROR("%s start PES filter failed:(%s), fd: %d",
            __func__, strerror(errno), fd);
    pthread_mutex_unlock(&p_ctx->lock);
    return DVR_FAILURE;
  }
#endif
  pthread_mutex_unlock(&p_ctx->lock);
  DVR_INFO("start filter fd: %d", fd);
  return DVR_SUCCESS;
}

DVR_Result_t dvr_record_stop_filter(DVR_RecordHandle_t handle, int filter_idx)
{
  int ret;
  int fd;
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(filter_idx >= 0);
  DVR_CHECK(filter_idx < DVR_MAX_RECORD_PID_CNT);
  DVR_CHECK(p_ctx != NULL);
  pthread_mutex_lock(&p_ctx->lock);

  fd = p_ctx->streams[filter_idx].fd;
  DVR_CHECK_WITH_UNLOCK(fd >= 0, &p_ctx->lock);

#ifndef DEBUG_ON_PC
  // stop a filter
  ret = ioctl(fd, DMX_STOP, 0);
  if (ret == -1) {
    DVR_ERROR("%s stop pes filter failed (%s), fd: %d",
            __func__, strerror(errno), fd);
    pthread_mutex_unlock(&p_ctx->lock);
    return DVR_FAILURE;
  }
#endif

  DVR_INFO("stop filter fd:%d", fd);
  pthread_mutex_unlock(&p_ctx->lock);
  return DVR_SUCCESS;
}

DVR_Result_t dvr_record_close_filter(DVR_RecordHandle_t handle, int filter_idx)
{
  int i;
  int fd;
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(filter_idx >= 0);
  DVR_CHECK(filter_idx < DVR_MAX_RECORD_PID_CNT);
  DVR_CHECK(p_ctx != NULL);
  pthread_mutex_lock(&p_ctx->lock);

  fd = p_ctx->streams[filter_idx].fd;
  DVR_CHECK_WITH_UNLOCK(fd >= 0, &p_ctx->lock);
  for (i = 0; i < DVR_MAX_RECORD_PID_CNT; i++) {
    if (p_ctx->streams[i].fd == fd)
        break;
  }
  DVR_CHECK_WITH_UNLOCK(
                i < DVR_MAX_RECORD_PID_CNT,
                &p_ctx->lock);
#ifndef DEBUG_ON_PC
  // close a filter
  close(fd);
#endif

  p_ctx->streams[i].fd = -1;
  p_ctx->streams[i].pid = DVR_INVALID_PID;
  p_ctx->streams[i].needs_encryption = 0;
  p_ctx->streams[i].key_token = -1;

  pthread_mutex_unlock(&p_ctx->lock);
  return DVR_SUCCESS;
}

// This function is called to indicate that it is currently in secure dvr.
// We should allocate secure memory and configure it for dvr device.
DVR_Result_t dvr_record_set_key_token(DVR_RecordHandle_t handle, int pid, uint32_t key_token)
{
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);

  pthread_mutex_lock(&p_ctx->lock);
  p_ctx->is_secure_mode = 1;

  // Currently, only single-key re-encryption is supported
  // We need to keep track of all pids that need to be encrypted
  p_ctx->key_token = key_token;
  pthread_mutex_unlock(&p_ctx->lock);

  return DVR_SUCCESS;
}

// Read away the data of the dvr device directly without
// using ringbuffer and ts indexer
int do_direct_dvr(DVR_RecordContext_t *p_ctx, uint8_t *buf, size_t len)
{
  int ret;
  size_t act_len = 0;
  struct pollfd fds[2];

  memset(fds, 0, sizeof(fds));
  fds[0].fd = p_ctx->fd[0];
  fds[1].fd = p_ctx->evtfd;

  fds[0].events = POLLIN | POLLERR;
  fds[1].events = POLLIN | POLLERR;
  ret = poll(fds, 2, 10);
  if (ret < 0) {
    DVR_ERROR("%s failed: %s. fd: %d, evtfd: %d",
            __func__, strerror(errno), p_ctx->fd[0], p_ctx->evtfd);
    return DVR_FAILURE;
  }

  if (!(fds[0].revents & POLLIN))
    return DVR_FAILURE;

  act_len = read(fds[0].fd, buf, len);
  if (act_len <= 0 || (act_len % 188) != 0) {
    DVR_ERROR("%s read failed: %s. fd: %d, len: %#x\n",
            __func__,
            strerror(errno),
            p_ctx->fd[0],
            act_len);
    return DVR_FAILURE;
  }
  DVR_INFO("%s record %#x bytes\n", __func__, act_len);

  return act_len;
}

int do_secure_dvr(DVR_RecordContext_t *p_ctx)
{
  return DVR_FAILURE;
}

int do_normal_dvr(DVR_RecordContext_t *p_ctx)
{
  ssize_t len;
  int data_fd;
  int ret;

#ifdef DEBUG_ON_PC
  data_fd = p_ctx->reserved[0];
#else
  struct pollfd fds[2];

  memset(fds, 0, sizeof(fds));
  fds[0].fd = p_ctx->fd[0];
  fds[1].fd = p_ctx->evtfd;

  fds[0].events = POLLIN | POLLERR;
  fds[1].events = POLLIN | POLLERR;
  ret = poll(fds, 2, DVR_TIMEOUT);
  if (ret < 0) {
    DVR_ERROR("%s failed: %s. fd: %d, evtfd: %d",
            __func__, strerror(errno), p_ctx->fd[0], p_ctx->evtfd);
    return DVR_FAILURE;
  }

  if (!(fds[0].revents & POLLIN))
    return DVR_FAILURE;

  data_fd = fds[0].fd;
#endif

  DVR_RecordOutput_t *ringbuf = &p_ctx->output;
  DVR_INFO("before read, r_offset: %#x, w_offset: %#x\n",
        ringbuf->r_offset, ringbuf->w_offset);

  // If the write offset is greater than or equal to the read offset,
  // then the data is recorded to from the write offset to the end of
  // the ring buffer. Otherwise the data is recorded at the position
  // from write offset to read offset.
  if (ringbuf->w_offset >= ringbuf->r_offset) {
    len = read(data_fd,
            ringbuf->buffer + ringbuf->w_offset,
            DVR_RECORD_OUTPUTBUFFER_SIZE - ringbuf->w_offset);
    ringbuf->size = ringbuf->w_offset - ringbuf->r_offset + len;;
  } else {
    len = read(data_fd,
            p_ctx->output.buffer + ringbuf->w_offset,
            ringbuf->r_offset - ringbuf->w_offset);
    ringbuf->size = DVR_RECORD_OUTPUTBUFFER_SIZE -
            ringbuf->r_offset +
            ringbuf->w_offset + len;
  }
  if (len <= 0 || (len % 188) != 0) {
    DVR_ERROR("%s read failed: %s. fd: %d, r: %#x, w: %#x, len: %#x\n",
            __func__, strerror(errno), p_ctx->fd[0],
            ringbuf->r_offset,
            ringbuf->w_offset,
            len);
    return DVR_FAILURE;
  }
  DVR_INFO("%s read %#x bytes\n", __func__, len);

  ringbuf->w_offset =
          (ringbuf->w_offset + len) %
          DVR_RECORD_OUTPUTBUFFER_SIZE;

  DVR_INFO("%s record %#x bytes. r: %#x, w: %#x, size: %#x\n",
        __func__, len,
        ringbuf->r_offset,
        ringbuf->w_offset,
        ringbuf->size);

  return DVR_SUCCESS;
}

// Read a piece of pusi data and flags according to the index array
ssize_t outputbuffer_read(DVR_RecordContext_t *p_ctx, DVR_RecordReceiveParams_t *params)
{
  int i;
  size_t pusi_len;

  // Find the first valid PUSI
  for (i = 0; i < DVR_MAX_RECORD_PUSI_CNT; i++) {
    if (p_ctx->pusi[i].state == TS_INDEXER_PUSI_DONE) {
      break;
    }
  }

  // Return no data if PUSI not found
  if (i >= DVR_MAX_RECORD_PUSI_CNT) {
    DVR_INFO("%s no PUSI\n", __func__);
    return DVR_FAILURE;
  }

  TS_Indexer_Pusi_t *pusi = &p_ctx->pusi[i];
  if (pusi->start == pusi->end
        || pusi->start >= p_ctx->output.len
        || pusi->end >= p_ctx->output.len ) {
    DVR_ERROR("%s wrong pusi. pusi_start: %#x, pusi_end: %#x, output len: %#x",
            __func__, pusi->start, pusi->end, p_ctx->output.len);
    return DVR_FAILURE;
  }

  if (pusi->flags & DVR_INDEX_IFRAME)
    DVR_ERROR("pusi[%d] read %#x ~ %#x, flags: %d", i, pusi->start, pusi->end, pusi->flags);
  if (pusi->end > pusi->start) {
    // Buffer length MUST be enough
    pusi_len = pusi->end - pusi->start + 1;
    if (pusi_len > params->len) {
      DVR_ERROR("buffer is not enough! buffer len: %#x, data len:%#x. %#x-%#x",
            params->len, pusi_len, pusi->start, pusi->end);
      return DVR_FAILURE;
    }

    // Copy from start to end
    memcpy(&params->buf[0],
            &p_ctx->output.buffer[pusi->start],
            pusi_len);
  } else {
    pusi_len = (p_ctx->output.len - pusi->start) + pusi->end + 1;
    if (pusi_len > params->len) {
      DVR_ERROR("buffer is not enough! buffer len: %#x, data len:%#x. %#x-%#x",
            params->len, pusi_len, pusi->start, pusi->end);
      return DVR_FAILURE;
    }
    // Copy from start to tail
    memcpy(&params->buf[0],
            &p_ctx->output.buffer[pusi->start],
            p_ctx->output.len - pusi->start);

    // Copy from head to end
    memcpy(&params->buf[p_ctx->output.len - pusi->start],
            &p_ctx->output.buffer[0],
            pusi->end + 1);
  }
  params->flags = pusi->flags;
  params->pts = pusi->pts;

  memset(pusi, 0, sizeof(TS_Indexer_Pusi_t));

  return pusi_len;
}

// Move the left tail PUSI to head
int pusi_move(DVR_RecordContext_t *p_ctx)
{
  int i;

  for (i = 0; i < DVR_MAX_RECORD_PUSI_CNT; i++) {
    if (p_ctx->pusi[i].state)
      break;
  }

  if (i < DVR_MAX_RECORD_PUSI_CNT && i != 0) {
    memcpy(&p_ctx->pusi[0], &p_ctx->pusi[i], sizeof(TS_Indexer_Pusi_t));
    memset(&p_ctx->pusi[i], 0, sizeof(TS_Indexer_Pusi_t));
    DVR_INFO("move the %d pusi to head, %#x ~ %#x",
        i, p_ctx->pusi[0].start, p_ctx->pusi[0].end);
  }

  return DVR_SUCCESS;
}

// For secure dvr, this function reads the TS encrypted by TSE after
// descrambling. For non-secure pvr, it directly reads the descrambled TS.
// Only TS between two consecutive PUSIs(parsed by ts indexer) is returned,
// otherwise the TS is buffered.
ssize_t dvr_record_read(DVR_RecordHandle_t handle, DVR_RecordReceiveParams_t *params)
{
  ssize_t len;
  int ret;
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  DVR_CHECK(params != NULL);
  DVR_CHECK(params->buf != NULL);
  DVR_CHECK(
        (params->len > 188 &&
        (params->len % 188) == 0) &&
        params->len <= DVR_RECORD_OUTPUTBUFFER_SIZE
  );

  DVR_CHECK(
        (params->mode == DVR_DIRECT_RECORD_MODE ||
        params->mode == DVR_PUSI_RECORD_MODE)
  );

  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
                p_ctx->state == DVR_RECORD_STATE_STARTED,
                &p_ctx->lock);

  // It's direct dvr mode, not need ts indexer and ringbuffer
  if (params->mode == DVR_DIRECT_RECORD_MODE)
  {
    len = do_direct_dvr(p_ctx, params->buf, params->len);
    goto exit;
  }

  //Need ts indexer with ringbuffer
  len = outputbuffer_read(p_ctx, params);
  if (len > 0) {
    //DVR_INFO("%s data len: %#x, flags: %#x", __func__,
    //        len, params->flags);
    pthread_mutex_unlock(&p_ctx->lock);
    return len;
  }

  pusi_move(p_ctx);

  do {
    if (p_ctx->is_secure_mode) {
      ret = do_secure_dvr(p_ctx);
    } else {
      int pusi_cnt = DVR_MAX_RECORD_PUSI_CNT;
      ret = DVR_FAILURE;

      if (do_normal_dvr(p_ctx))
        break;

      if (ts_indexer_parse(&p_ctx->ts_indexer,
                        &p_ctx->output,
                        &p_ctx->pusi[0],
                        pusi_cnt)) {
        DVR_ERROR("%s ts indexer parse should be 188 aligned. check data!",
                __func__);
        break;
      }
    }
  } while (0);

  if (ret != DVR_SUCCESS)
    len = -1;

exit:
  pthread_mutex_unlock(&p_ctx->lock);
  return len;
}

