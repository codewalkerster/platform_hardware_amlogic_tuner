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
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#ifndef DEBUG_ON_PC
#include "dmx.h"
#include "libdsm.h"
#include "dsc_dev.h"
#endif

#include "secdmx_client.h"
#include "dvr_record.h"
#include "ts_indexer.h"

#define DVR_MAX_RECORD_SESSION_CNT  (4)
#define DVR_MAX_RECORD_PID_CNT      (16)
#define DVR_MAX_RECORD_PUSI_CNT     (100)
#define DVR_TIMEOUT                 (100)

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

/**\brief DVR CA flags*/
enum {
  DVR_CA_USAGE_DES = 1 << 0,    /**< DVR CA descrambling usage flag*/
  DVR_CA_USAGE_ENC = 1 << 1,    /**< DVR CA encryption usage flag*/
  DVR_CA_ENC_KEY = 1 << 10,     /**< DVR CA encryption key flag*/
  DVR_CA_ENC_IV = 1 << 11       /**< DVR CA encryption iv flag*/
};

/**\brief DVR record CA info*/
typedef struct {
  int dev_id;                           /**< DVR record dmx/dsc dev id*/
  int chans[DVR_MAX_CA_CHAN_CNT];       /**< DVR record ca channels. 0: des, 1: enc*/
  int even_key_kte;                     /**< DVR record encryption even kte*/
  int even_iv_kte;                      /**< DVR record encryption even kte*/
  int odd_key_kte;                      /**< DVR record encryption odd kte*/
  int odd_iv_kte;                       /**< DVR record encryption odd kte*/
  int parity;                           /**< DVR record current used parity*/
  int flags;                            /**< DVR record ca usage flags*/
} DVR_CAInfo_t;

/**\brief DVR record stream information*/
typedef struct {
  int fd;                               /**< DVR record filter's fd*/
  uint16_t pid;                         /**< DVR record stream PID*/
  int started;                          /**< DVR record filter state*/
  DVR_StreamType_t type;                /**< DVR record stream type*/
  DVR_VideoFormat_t vfmt;               /**< DVR record video format*/
  uint32_t key_token;                   /**< DVR record key token for re-encryption*/
  DVR_CAInfo_t ca;                      /**< DVR record CA info*/
} DVR_RecordStream_t;

/**\brief DVR record context*/
typedef struct {
  int fd[4];                                            /**< DVR record device file descriptor*/
  int recfd;                                            /**< DVR record fd for record whole ts*/
  int dmx_dev_id[3];                                    /**< DVR record devices*/
  DVB_DemuxSource_t src;                                /**< DVR record dvr source*/
  pthread_mutex_t lock;                                 /**< DVR record context mutex*/
  DVR_RecordState_t state;                              /**< DVR record state*/

  int is_secure_mode;                                   /**< DVR record session run in secure mode*/
  size_t dsm_sess;                                      /**< DVR record descrambling session*/
  size_t sects_sess;                                    /**< DVR record secure ts indexer session*/
  int ca_flags;                                         /**< DVR record usage ready flags*/

  uint32_t block_size;                                  /**< DVR record block size, not used now*/
  DVR_RecordStream_t streams[DVR_MAX_RECORD_PID_CNT];   /**< DVR record stream list*/
  TS_Indexer_Pusi_t pusi[DVR_MAX_RECORD_PUSI_CNT];      /**< DVR record PUSI*/
  DVR_RecordOutput_t rb0;                               /**< DVR record ringbuffer on dmx_dev_id[0]*/
  DVR_RecordOutput_t rb1;                               /**< DVR record ringbuffer on dmx_dev_id[1] only used in secure mode*/
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

static void* sects_handle = NULL;
int (*SECTS_Init_Func)(void);
int (*SECTS_OpenSession_Func)(size_t *session);
int (*SECTS_CloseSession_Func)(size_t session);
void *(*SECTS_AllocSecureBuffer_Func)(size_t session, size_t size);
int (*SECTS_FreeSecureBuffer_Func)(size_t session, void *p);
int (*SECTS_SetVideoParams_Func)(size_t session, int pid, int format);
int (*SECTS_SetAudioParams_Func)(size_t session, int pid);
int (*SECTS_IndexerParse_Func)(
        size_t session,
        SECTS_IndexerRingBuffer_t *ringbuf,
        SECTS_IndexerPusi_t *pusi,
        size_t max_pusi_cnt);
size_t (*SECTS_Map2InjBuff_Func)(size_t session, size_t sec_buf, size_t len);
int (*SECTS_Deinit_Func)(void);

static int load_sects_library(void)
{
  if (sects_handle != NULL) {
    return 0;
  }

  sects_handle = dlopen("libdmx_client.so", RTLD_NOW);
  if (sects_handle == NULL) {
    DVR_ERROR("%s dlopen failed, %s", __func__, strerror(errno));
    return -1;
  }

  SECTS_Init_Func = dlsym(sects_handle, "SECTS_Init");
  SECTS_OpenSession_Func = dlsym(sects_handle, "SECTS_OpenSession");
  SECTS_CloseSession_Func = dlsym(sects_handle, "SECTS_CloseSession");
  SECTS_AllocSecureBuffer_Func = dlsym(sects_handle, "SECTS_AllocSecureBuffer");
  SECTS_FreeSecureBuffer_Func = dlsym(sects_handle, "SECTS_FreeSecureBuffer");
  SECTS_SetVideoParams_Func = dlsym(sects_handle, "SECTS_SetVideoParams");
  SECTS_SetAudioParams_Func = dlsym(sects_handle, "SECTS_SetAudioParams");
  SECTS_IndexerParse_Func = dlsym(sects_handle, "SECTS_IndexerParse");
  SECTS_Map2InjBuff_Func = dlsym(sects_handle, "SECTS_Map2InjBuff");
  SECTS_Deinit_Func = dlsym(sects_handle, "SECTS_Deinit");

  DVR_INFO("%s succeeded\n", __func__);
  return 0;
}

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

// We use dmx_dev_id[1] as secure demux device
// Load sects TA if it's not loaded and Open a sects session
// Allocate secure buffer from sects TA
// Open/set secure demux source and set secure dvr buffer for it
// Open a DSM session
static int secure_resource_prepare(DVR_RecordContext_t *p_ctx)
{
  void *buf = NULL;

  if (p_ctx->is_secure_mode) {
    DVR_INFO("%s secure mode already, do nothing", __func__);
    return 0;
  }

  // Load sects client library for secure dvr and ts indexer
  // Init and open a sects session for the dvr session
  if (sects_handle == NULL) {
    DVR_CHECK(load_sects_library() == 0);
    DVR_CHECK(SECTS_Init_Func != NULL && SECTS_Init_Func() == 0);
  }

  if (p_ctx->sects_sess == -1) {
    SECTS_OpenSession_Func(&p_ctx->sects_sess);
    DVR_CHECK(p_ctx->sects_sess != -1);
  }

  // Allocate secure buffer from sects TA
  if (SECTS_AllocSecureBuffer_Func != NULL && p_ctx->rb1.buffer == NULL) {
    p_ctx->rb1.buffer = (uint8_t *)SECTS_AllocSecureBuffer_Func(
                            p_ctx->sects_sess,
                            p_ctx->rb1.len);
    if (p_ctx->rb1.buffer == NULL) {
      DVR_ERROR("%s failed, alloc secure buffer failed", __func__);
      return -1;
    }

    DVR_INFO("%s sects alloc secure buffer success, addr: %#x, size: %#x",
        __func__, (size_t)buf, p_ctx->rb1.len);
  } else {
    DVR_ERROR("%s SECTS_AllocSecureBuffer_Func is null", __func__);
  }
  DVR_CHECK(p_ctx->rb1.buffer != NULL);

  // For clear demux dmx_dev_id[0] recording in secure mode
  if (p_ctx->rb0.buffer == NULL) {
    p_ctx->rb0.buffer = (uint8_t *)malloc(DVR_BLOCK_SIZE);
  }

  // Set secure demux source which is same with dmx_dev_id[0]
  dvb_set_demux_source(p_ctx->dmx_dev_id[1], p_ctx->src);

  // Open secure dvr device
  if (p_ctx->fd[1] == -1) {
    p_ctx->fd[1] = dvb_dvr_device_open(p_ctx->dmx_dev_id[1], 1);
    DVR_CHECK(p_ctx->fd[1] >= 0);
  }

  // Set secure buffer to secure demux for recording
  dvb_set_secure_buffer(p_ctx->dmx_dev_id[1],
                        p_ctx->rb1.buffer,
                        p_ctx->rb1.len);

  // Set secure inject demux source with TSE, hardcode to SECSOURCE_DMA7
  dvb_set_demux_source(
        p_ctx->dmx_dev_id[2],
        DVB_DEMUX_SECSOURCE_DMA0 + p_ctx->dmx_dev_id[2]);

  // Open dmx_dev_id[2] for inject and recording the re-encrypted ts
  if (p_ctx->fd[2] == -1) {
    // DVR write fd
    p_ctx->fd[2] = dvb_dvr_device_open(p_ctx->dmx_dev_id[2], 0);
    // DVR read fd
    p_ctx->fd[3] = dvb_dvr_device_open(p_ctx->dmx_dev_id[2], 1);
    DVR_CHECK(p_ctx->fd[2] >= 0);
    dvb_dvr_set_ringbuffer(p_ctx->fd[3], 5 * 188 * 1024);
  }

  if (p_ctx->recfd == -1) {
    // Create the 0x2000 pid filter to record the whole ts on the demux
    p_ctx->recfd = create_filter(p_ctx->dmx_dev_id[2], 0x2000);
    DVR_CHECK(p_ctx->recfd != -1);
    // Start a filter
    int ret = ioctl(p_ctx->recfd, DMX_START, 0);
    if (ret == -1) {
      DVR_ERROR("DMX_START failed, %s", strerror(errno));
    }
    DVR_CHECK(ret != -1);
  }

  if (p_ctx->dsm_sess == -1) {
    p_ctx->dsm_sess = DSM_OpenSession(0);
    DVR_CHECK(p_ctx->dsm_sess != -1);
  }

  p_ctx->is_secure_mode = 1;
  DVR_INFO("%s success", __func__);
  return 0;
}

// Free secure buffer and close sects session
static int secure_resource_release(DVR_RecordContext_t *p_ctx)
{
  if (!p_ctx->is_secure_mode)
    return 0;

  DVR_CHECK(p_ctx->rb1.buffer != NULL
        && p_ctx->sects_sess != -1);

  DVR_CHECK(
        SECTS_FreeSecureBuffer_Func != NULL &&
        SECTS_FreeSecureBuffer_Func(p_ctx->sects_sess,
        p_ctx->rb1.buffer) == 0);

  p_ctx->rb1.buffer = NULL;

  if (p_ctx->fd[1] >= 0) {
    close(p_ctx->fd[1]);
    p_ctx->fd[1] = -1;
  }

  if (p_ctx->fd[2] >= 0) {
    close(p_ctx->fd[2]);
    p_ctx->fd[2] = -1;
  }

  if (p_ctx->recfd >= 0) {
    close(p_ctx->recfd);
    p_ctx->recfd = -1;
  }

  DVR_CHECK(
        SECTS_CloseSession_Func != NULL &&
        SECTS_CloseSession_Func(p_ctx->sects_sess) == 0);

  p_ctx->sects_sess = -1;

  if (p_ctx->dsm_sess >= 0) {
    DSM_CloseSession(p_ctx->dsm_sess);
    p_ctx->dsm_sess = -1;
  }

  p_ctx->dsm_sess = -1;
  p_ctx->is_secure_mode = 0;
  DVR_INFO("%s success", __func__);

  return 0;
}

// Allocate dsc channel and set key for the pid stream descrambling or
// re-encryption
static int ca_prepare(
    DVR_RecordContext_t *p_ctx,
    DVR_RecordStream_t *stream,
    int usage)
{
  int i;
  int dmx_dev_id;
  int dsc_type = CA_DSC_COMMON_TYPE;
  struct dsm_keyslot_list keyslot_list;
  uint32_t ready = DSM_PROP_SLOT_NOT_READY;

  // Considering that the upper layer may have different call timing，we need
  // to call multiple times in different function. But as long as it has been
  // successfully called, then directly return
  if (stream->ca.flags & usage) {
    DVR_INFO("%s channels are ready",
        (usage == DVR_CA_USAGE_DES) ? "descrambling" : "re-encryption");
    return 0;
  }

  if (usage == DVR_CA_USAGE_DES) {
    dmx_dev_id = p_ctx->dmx_dev_id[1];
    dsc_type = CA_DSC_COMMON_TYPE;

    // Check if descrambling slot is ready
    DVR_CHECK(
        DSM_GetProperty(p_ctx->dsm_sess, DSM_PROP_DEC_SLOT_READY,
        &ready) == 0);
    if (ready != DSM_PROP_SLOT_IS_READY) {
      DVR_INFO("%s dmx%d, pid: %#x slot not ready",
            __func__, dmx_dev_id, stream->pid);
      return 0;
    }
  } else if (usage == DVR_CA_USAGE_ENC) {
    // Check if re-encryption slot is ready
    DVR_CHECK(
        DSM_GetProperty(p_ctx->dsm_sess, DSM_PROP_ENC_SLOT_READY,
        &ready) == 0);
    DVR_CHECK(ready == DSM_PROP_SLOT_IS_READY);
    dmx_dev_id = p_ctx->dmx_dev_id[2];
    dsc_type = CA_DSC_TSE_TYPE;
  } else
    return -1;

  memset(&keyslot_list, 0, sizeof(struct dsm_keyslot_list));
  // Get key lost list from DSM
  DVR_CHECK(DSM_GetKeySlots(p_ctx->dsm_sess, &keyslot_list) == 0);
  DVR_CHECK(keyslot_list.count > 0);

  if (p_ctx->ca_flags == 0) {
    DVR_CHECK(ca_init() == 0);
  }

  if (!(p_ctx->ca_flags & usage)) {
    DVR_CHECK(ca_open(dmx_dev_id) == 0);
  }

  DVR_INFO("%s %s slot is ready, total count: %d",
        __func__,
        (usage == DVR_CA_USAGE_DES) ? "descrambling" : "re-encryption",
        keyslot_list.count);

  // Loop all the key slot and get the algo/is_enc/is_iv/parity etc.
  for (i = 0; i < keyslot_list.count; i++) {
    int ca_chan = -1;
    uint32_t parity;
    struct dsm_keyslot *slot = &keyslot_list.keyslots[i];
    DVR_INFO("slot id: %#x, parity: %d, algo: %d, is_iv: %d, is_enc: %d, ca_flags: %#x",
        slot->id, slot->parity, slot->algo, slot->is_iv, slot->is_enc, stream->ca.flags);

    if (slot->is_enc && usage != DVR_CA_USAGE_ENC) {
      continue;
    }
    if (!slot->is_enc && usage != DVR_CA_USAGE_DES) {
      continue;
    }

    if (dsc_type == CA_DSC_TSE_TYPE) {
      parity = slot->is_iv ? CA_KEY_00_IV_TYPE : CA_KEY_00_TYPE;
      if (slot->is_iv) {
        if (slot->parity == DSM_PARITY_EVEN) {
          stream->ca.even_iv_kte = slot->id;
        } else {
          stream->ca.odd_iv_kte = slot->id;
        }
      } else {
        if (slot->parity == DSM_PARITY_EVEN) {
          stream->ca.even_key_kte = slot->id;
        } else {
          stream->ca.odd_key_kte = slot->id;
        }
      }
      ca_chan = stream->ca.chans[1];

      if ((stream->ca.flags & DVR_CA_ENC_IV) && slot->is_iv) {
        continue;
      }
      else if ((stream->ca.flags & DVR_CA_ENC_KEY) && !slot->is_iv) {
        continue;
      }
    } else {
      if (slot->parity == DSM_PARITY_EVEN) {
        parity = slot->is_iv ? CA_KEY_EVEN_IV_TYPE : CA_KEY_EVEN_TYPE;
      } else if (slot->parity == DSM_PARITY_ODD) {
        parity = slot->is_iv ? CA_KEY_ODD_IV_TYPE : CA_KEY_ODD_TYPE;
      } else {
        parity = slot->is_iv ? CA_KEY_00_IV_TYPE : CA_KEY_00_TYPE;
      }

      ca_chan = stream->ca.chans[0];
    }

    // Allocate ca dsc channel
    if (ca_chan == -1) {
      ca_chan = ca_alloc_chan(
                      dmx_dev_id,
                      stream->pid,
                      slot->algo,
                      dsc_type);
    }
    DVR_CHECK(ca_chan >= 0);

    if (dsc_type == CA_DSC_TSE_TYPE) {
      int scb = 3; // ODD
      DVR_CHECK(ca_set_scb(dmx_dev_id, ca_chan, scb, 0) == 0);
      stream->ca.parity = scb;
    }

    DVR_INFO("%s alloc ca channel(%#x, %d) ok.",
        __func__, stream->pid, ca_chan);
    // Record the ca channel
    if (usage == DVR_CA_USAGE_ENC) {
      stream->ca.chans[1] = ca_chan;
      if (slot->is_iv) {
        stream->ca.flags |= DVR_CA_ENC_IV;
      } else {
        stream->ca.flags |= DVR_CA_ENC_KEY;
      }
    } else {
      stream->ca.chans[0] = ca_chan;
    }

    // Set KTE to ca dsc channel
    DVR_CHECK(ca_set_key(dmx_dev_id, ca_chan, parity, slot->id) ==0);
    DVR_INFO("%s set ca key(%d, %d, %d, %d), is_iv: %d",
          __func__,
          dmx_dev_id,
          ca_chan,
          parity,
          slot->id,
          slot->is_iv);

    // This is a flag used to indicate whether the current pid stream's
    // descrambling key and re-encryption key are ready.
    stream->ca.flags |= usage;
    stream->ca.dev_id = dmx_dev_id;
  }

  // This is a global flag that indicates whether the descrambling and
  // re-encryption key are ready. If one of the pids in the usage is ready,
  // then we consider all the pids in the usage ready.
  p_ctx->ca_flags |= usage;
  return 0;
}

static int ca_release(DVR_RecordContext_t *p_ctx, DVR_RecordStream_t *stream)
{
  int i;

  // Free the ca channel
  for (i = 0; i < DVR_MAX_CA_CHAN_CNT; i++) {
    if (stream->ca.chans[i] >= 0) {
        ca_free_chan(stream->ca.dev_id, stream->ca.chans[i]);
        stream->ca.chans[i] = -1;
    }
  }

  p_ctx->ca_flags = 0;

  return 0;
}

// Check ca ready status and try to prepare ca resource if needs
int ca_ready_check(DVR_RecordContext_t *p_ctx)
{
  for (int i = 0; i < DVR_MAX_RECORD_PID_CNT; i++) {
    DVR_RecordStream_t *stream = &p_ctx->streams[i];
    if (stream->key_token == -1)
      continue;

    if ((stream->ca.flags & DVR_CA_USAGE_DES) &&
          (stream->ca.flags & DVR_CA_USAGE_ENC)) {
      continue;
    }
    if (!(stream->ca.flags & DVR_CA_USAGE_DES)) {
      // Try to prepare ca resource if descrambler not ready
      ca_prepare(p_ctx, stream, DVR_CA_USAGE_DES);
    }
    if (!(stream->ca.flags & DVR_CA_USAGE_ENC)) {
      // Try to prepare ca resource if encryption channel is not ready
      ca_prepare(p_ctx, stream, DVR_CA_USAGE_ENC);
    }
  }

  if ((p_ctx->ca_flags & DVR_CA_USAGE_DES) &&
        (p_ctx->ca_flags & DVR_CA_USAGE_ENC)) {
    return DVR_SUCCESS;
  }

  return DVR_FAILURE;
}

DVR_Result_t dvr_record_open(DVR_RecordHandle_t *p_handle, DVR_RecordOpenParams_t *params)
{
  int i;
  DVR_RecordContext_t *p_ctx;

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
  p_ctx->recfd = -1;
  p_ctx->fd[0] = -1;
  p_ctx->fd[1] = -1;
  p_ctx->fd[2] = -1;
  for (int i = 0; i < DVR_MAX_RECORD_PID_CNT; i++) {
    int j;
    p_ctx->streams[i].fd = -1;
    p_ctx->streams[i].pid = DVR_INVALID_PID;
    p_ctx->streams[i].started = 0;
    p_ctx->streams[i].type = DVR_STREAM_INVALID_TYPE;
    p_ctx->streams[i].vfmt = DVR_VIDEO_FORMAT_INVALID;
    p_ctx->streams[i].key_token = -1;
    for (j = 0; j < DVR_MAX_CA_CHAN_CNT; j++) {
      p_ctx->streams[i].ca.chans[j] = -1;
    }
    p_ctx->streams[j].ca.flags = 0;
    p_ctx->streams[j].ca.even_key_kte = -1;
    p_ctx->streams[j].ca.even_iv_kte = -1;
    p_ctx->streams[j].ca.odd_key_kte = -1;
    p_ctx->streams[j].ca.odd_iv_kte = -1;
    p_ctx->streams[j].ca.parity = -1;
    p_ctx->streams[j].ca.dev_id = -1;
  }

  memcpy(p_ctx->dmx_dev_id, params->dmx_dev_id, sizeof(params->dmx_dev_id));
  p_ctx->sects_sess = -1;
  p_ctx->dsm_sess = -1;
  p_ctx->is_secure_mode = 0;
  p_ctx->ca_flags = 0;

  memset(&p_ctx->rb0, 0, sizeof(DVR_RecordOutput_t));
  memset(&p_ctx->rb1, 0, sizeof(DVR_RecordOutput_t));

  if (params->non_sec_ringbuf_size > 0) {
    DVR_CHECK_WITH_UNLOCK(
        params->non_sec_ringbuf_size >= DVR_BLOCK_SIZE &&
        params->non_sec_ringbuf_size % 188 == 0,
        &p_ctx->lock);
  }
  p_ctx->rb0.len = params->non_sec_ringbuf_size;
  p_ctx->rb0.r_offset = 0;
  p_ctx->rb0.w_offset = 0;
  p_ctx->rb0.size = 0;

  if (params->sec_buf_size > 0) {
    DVR_CHECK_WITH_UNLOCK(
        params->sec_buf_size >= DVR_BLOCK_SIZE &&
        params->sec_buf_size % 188 == 0,
        &p_ctx->lock);
    p_ctx->rb1.len = params->sec_buf_size;
    p_ctx->rb1.r_offset = 0;
    p_ctx->rb1.w_offset = 0;
    p_ctx->rb1.size = 0;
  }

  // Alloc output buffer which tuner hal will read data from
  p_ctx->rb0.buffer = malloc(p_ctx->rb0.len);
  DVR_CHECK_WITH_UNLOCK(p_ctx->rb0.buffer != NULL, &p_ctx->lock);

  // Init PUSI array
  memset(&p_ctx->pusi[0], 0, sizeof(TS_Indexer_Pusi_t) * DVR_MAX_RECORD_PUSI_CNT);

  // Init a ts indexer instance for video PTS/I-FRAME indexs
  ts_indexer_init(&p_ctx->ts_indexer);

#ifndef DEBUG_ON_PC
  // Set hw demux source
  dvb_set_demux_source(params->dmx_dev_id[0], params->src);

  // Open normal dvr device and config ringbuffer length
  // For Clear PVR, only use the dvr device dmx_dev_id[0]
  // For Secure DVR, use the dvr device dmx_dev_id[0] to record descrambled
  // stream to secure buffer A. Then inject A to dmx_dev_id[1] and dmx_dev_id[2].
  // To record re-encrypted PES from the dvr device dmx_dev_id[1] with TSE
  // To record other TS like section data from the dvr device dmx_dev_id[2]
  // set dvbcore ring buffer size which should be 188 Bytes aligned
  p_ctx->fd[0] = dvb_dvr_device_open(params->dmx_dev_id[0], 1);
  DVR_CHECK_WITH_UNLOCK(p_ctx->fd[0] >= 0, &p_ctx->lock);
  dvb_dvr_set_ringbuffer(p_ctx->fd[0], params->non_sec_ringbuf_size);

#endif
  memcpy(&p_ctx->reserved[0], &params->reserved[0], sizeof(params->reserved));
  p_ctx->src = params->src;
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
  p_ctx->fd[0] = -1;
  if (p_ctx->is_secure_mode) {
    close(p_ctx->fd[1]);
    close(p_ctx->fd[2]);

    p_ctx->fd[1] = -1;
    p_ctx->fd[2] = -1;
  }

  if (p_ctx->rb0.buffer) {
    free(p_ctx->rb0.buffer);
    p_ctx->rb0.buffer = NULL;
  }

  // We should release both descrambling and re-encryption ca channels if
  // the pid stream is secure
  for (int i = 0; i < DVR_MAX_RECORD_PID_CNT; i++) {
    if (p_ctx->streams[i].key_token != -1) {
      ca_release(p_ctx, &p_ctx->streams[i]);
      p_ctx->streams[i].key_token = -1;
    }
  }
  secure_resource_release(p_ctx);

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
  int fd;
  int filter_idx = -1;
  int is_secure = 0;
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  DVR_CHECK(params != NULL);
  DVR_CHECK(params->pid != DVR_INVALID_PID);
  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
                p_ctx->state != DVR_RECORD_STATE_CLOSED,
                &p_ctx->lock);

  // The pid should use secure if it has key token
  for (i = 0; i < DVR_MAX_RECORD_PID_CNT; i++) {
    if (p_ctx->streams[i].pid == params->pid) {
      if (p_ctx->streams[i].key_token != -1) {
        is_secure = 1;
      }
      break;
    }
  }

  if (is_secure) {
    // Open sects session only if when video or audio pid is secure
    if ((params->type == DVR_STREAM_VIDEO_TYPE ||
            params->type == DVR_STREAM_AUDIO_TYPE) &&
            p_ctx->sects_sess == -1) {
      SECTS_OpenSession_Func(&p_ctx->sects_sess);
      DVR_CHECK_WITH_UNLOCK(p_ctx->sects_sess != -1, &p_ctx->lock);
    }

    // Set a/v pid and video format to ts indexer
    // support to parse MPEG2/AVC/HEVC's PTS and I-Frame index
    if (params->type == DVR_STREAM_VIDEO_TYPE) {
      SECTS_SetVideoParams_Func(p_ctx->sects_sess, params->pid, params->vfmt);
    } else if (params->type == DVR_STREAM_AUDIO_TYPE) {
      SECTS_SetAudioParams_Func(p_ctx->sects_sess, params->pid);
    }
  } else if (params->type == DVR_STREAM_VIDEO_TYPE) {
    // Should not open sects session before if video stream is clear
    DVR_CHECK_WITH_UNLOCK(p_ctx->sects_sess == -1, &p_ctx->lock);
    ts_indexer_set_video_pid(&p_ctx->ts_indexer, params->pid);
    ts_indexer_set_video_format(&p_ctx->ts_indexer, params->vfmt);
  } else if (params->type == DVR_STREAM_AUDIO_TYPE) {
    ts_indexer_set_audio_pid(&p_ctx->ts_indexer, params->pid);
  }

  // If the pid slot already exists, use the slot directly.
  // If the pid slot does not exist, use the first DVR_INVALID_PID slot
  for (i = 0; i < DVR_MAX_RECORD_PID_CNT; i++) {
    if (p_ctx->streams[i].pid == params->pid) {
      filter_idx = i;
      break;
    }
    if (p_ctx->streams[i].pid == DVR_INVALID_PID && filter_idx == -1) {
      filter_idx = i;
    }
  }
  DVR_CHECK_WITH_UNLOCK(
                filter_idx >= 0,
                &p_ctx->lock);

#ifndef DEBUG_ON_PC
  // Record clear pid stream on clear demux dmx_dev_id[0]
  // Record scrambled pid stream on secure demux dmx_dev_id[1]
  if (is_secure) {
    fd = create_filter(p_ctx->dmx_dev_id[1], params->pid);
  } else {
    fd = create_filter(p_ctx->dmx_dev_id[0], params->pid);
  }
  DVR_CHECK_WITH_UNLOCK(fd >= 0, &p_ctx->lock);
#endif

  p_ctx->streams[filter_idx].fd = fd;
  p_ctx->streams[filter_idx].pid = params->pid;
  p_ctx->streams[filter_idx].type = params->type;
  p_ctx->streams[filter_idx].vfmt = params->vfmt;
  pthread_mutex_unlock(&p_ctx->lock);
  DVR_INFO("open filter pid: %#x, fd[%d]: %d, is_secure: %d",
        params->pid, filter_idx, fd, is_secure);
  return filter_idx;
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
  p_ctx->streams[filter_idx].started = 1;
  pthread_mutex_unlock(&p_ctx->lock);
  DVR_INFO("start filter fd: %d, pid: %#x", fd, p_ctx->streams[filter_idx].pid);
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

  DVR_INFO("stop filter fd:%d, pid: %#x", fd, p_ctx->streams[filter_idx].pid);
  p_ctx->streams[filter_idx].started = 0;
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
  p_ctx->streams[i].fd = -1;
#endif

  p_ctx->streams[i].pid = DVR_INVALID_PID;
  p_ctx->streams[i].started = 0;

  pthread_mutex_unlock(&p_ctx->lock);
  return DVR_SUCCESS;
}

// This function is called to indicate that it is currently in secure dvr.
// We should allocate secure memory and configure it for dvr device.
DVR_Result_t dvr_record_set_key_token(DVR_RecordHandle_t handle, int pid, uint32_t key_token)
{
  int i;
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  DVR_CHECK(pid != DVR_INVALID_PID);

  pthread_mutex_lock(&p_ctx->lock);

  // Now we only support same key token for all ES stream and
  // single-key re-encryption which means encryption key will not change.
  // We need to keep track of all pids that need to be encrypted
  for (i = 0; i < DVR_MAX_RECORD_PID_CNT; i++) {
    if (p_ctx->streams[i].pid == pid) {
      DVR_RecordStream_t *stream = &p_ctx->streams[i];
      // The key token changes from invalid to valid, and possibly from valid
      // to invalid. We need to deal with these two situations accordingly
      if (stream->key_token == -1) {
        // Clear to Clear. Do nothing
        if (key_token == -1)
          goto exit;

        // Clear to Scramble.
        // Record clear pid stream on clear demux dmx_dev_id[0]
        // Record scrambled pid stream on secure demux dmx_dev_id[1]
        // Inject descrambled pid stream on dmx_dev_id[2] for re-encryption
        // with TSE
        DVR_INFO("%s Clear-Scramble, pid: %#x", __func__, pid);
        if (!p_ctx->is_secure_mode) {
          if (secure_resource_prepare(p_ctx) != 0)
            goto exit;

          // DSM Bind key token
          DVR_CHECK_WITH_UNLOCK(
                DSM_BindToken(p_ctx->dsm_sess, key_token) == 0,
                &p_ctx->lock);
        }

        // Stop/Free old clear pid filter and re-create pid filter on secure demux
        if (stream->fd >= 0) {
          ioctl(stream->fd, DMX_STOP, 0);
          close(stream->fd);
        }

        stream->fd = create_filter(p_ctx->dmx_dev_id[1], pid);
        if (stream->fd == -1) {
          DVR_ERROR("%s create filter failed on dmx%d, pid: %#x",
                __func__,
                p_ctx->dmx_dev_id[1],
                pid);
        }
        if (stream->started) {
          int ret = ioctl(stream->fd, DMX_START, 0);
          if (ret == -1) {
            DVR_ERROR("%s start PES filter failed:(%s), fd: %d",
                __func__, strerror(errno), stream->fd);
          } else {
            DVR_INFO("%s start filter pid: %#x, fd: %d",
                __func__, stream->pid, stream->fd);
          }
        }
#if 0
        // Switch to secure ts indexer
        if ((stream->type == DVR_STREAM_VIDEO_TYPE ||
            stream->type == DVR_STREAM_AUDIO_TYPE) &&
            p_ctx->sects_sess == -1) {
          SECTS_OpenSession_Func(&p_ctx->sects_sess);
          DVR_CHECK_WITH_UNLOCK(p_ctx->sects_sess != -1, &p_ctx->lock);
        }
#endif
        if (stream->type == DVR_STREAM_VIDEO_TYPE &&
            stream->vfmt != DVR_VIDEO_FORMAT_INVALID) {
          SECTS_SetVideoParams_Func(p_ctx->sects_sess, pid, stream->vfmt);
        } else if (stream->type == DVR_STREAM_AUDIO_TYPE) {
          SECTS_SetAudioParams_Func(p_ctx->sects_sess, pid);
        }

        // Prepare ca
        DVR_CHECK_WITH_UNLOCK(
            ca_prepare(p_ctx, stream, DVR_CA_USAGE_DES) == 0,
            &p_ctx->lock);
        DVR_CHECK_WITH_UNLOCK(
            ca_prepare(p_ctx, stream, DVR_CA_USAGE_ENC) == 0,
            &p_ctx->lock);
      } else if (key_token == -1) {
        // Scramble to Clear, release secure resource when dvr_record_close call
        // Move pid filter from secure demux to clear demux
        DVR_INFO("%s Scramble-Clear, pid: %#x", __func__, pid);
      } else if (key_token != stream->key_token) {
        // There should be no such case.
        DVR_INFO("%s Descrambler key token chage", __func__);
      } else {
        // Set the same valid key token again. Do nothing
        DVR_INFO("%s set the same key token %#x again", __func__, key_token);
      }

      stream->key_token = key_token;
      goto exit;
    }
  }

  // Still need prepare secure resource if there's no pid matched but key token
  // is valid. In this case, high layer maybe call this API before open filter.
  // For the one whole DVR session, we actually only need to prepare secure
  // source once. But don't worry about multiple calls, it has internal
  // protection
  if (i >= DVR_MAX_RECORD_PID_CNT &&
            key_token != -1 &&
            !p_ctx->is_secure_mode) {
      DVR_CHECK_WITH_UNLOCK(
            secure_resource_prepare(p_ctx) == 0,
            &p_ctx->lock);
  }

  // This is a new pid, and we need to find a slot to store it
  for (i = 0; i < DVR_MAX_RECORD_PID_CNT; i++) {
    if (p_ctx->streams[i].pid == DVR_INVALID_PID) {
      break;
    }
  }
  DVR_CHECK_WITH_UNLOCK(i < DVR_MAX_RECORD_PID_CNT, &p_ctx->lock);
  p_ctx->streams[i].pid = pid;
  p_ctx->streams[i].key_token = key_token;

  if (p_ctx->ca_flags == 0) {
    // DSM Bind key token
    DVR_CHECK_WITH_UNLOCK(
            DSM_BindToken(p_ctx->dsm_sess, key_token) == 0,
            &p_ctx->lock);
  }

  // Prepare ca
  DVR_CHECK_WITH_UNLOCK(
            ca_prepare(p_ctx, &p_ctx->streams[i], DVR_CA_USAGE_DES) == 0,
            &p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
            ca_prepare(p_ctx, &p_ctx->streams[i], DVR_CA_USAGE_ENC) == 0,
            &p_ctx->lock);

exit:
  pthread_mutex_unlock(&p_ctx->lock);
  DVR_INFO("%s done", __func__);
  return DVR_SUCCESS;
}

// Record scrambled pid stream to rb1 on secure demux dmx_dev_id[1].
static int secure_dvr_with_rb(int dvr_fd, DVR_RecordOutput_t *ringbuf)
{
  int ret;
  struct dvr_mem_info info;

  memset(&info, 0, sizeof(info));
  ret = ioctl(dvr_fd, DMX_GET_DVR_MEM, &info);
  if (ret != 0)
    return DVR_FAILURE;

  DVR_INFO("%s wp: %#x", __func__, info.wp_offset);
  if (ringbuf->w_offset == info.wp_offset) {
    DVR_INFO("%s no new data", __func__);
    return DVR_FAILURE;
  }

  // If the write offset is greater than or equal to the read offset,
  // then the data is recorded to from the write offset to the end of
  // the ring buffer. Otherwise the data is recorded at the position
  // from write offset to read offset.
  if (info.wp_offset >= ringbuf->r_offset) {
    ringbuf->size = info.wp_offset - ringbuf->r_offset;
  } else {
    ringbuf->size = ringbuf->len - ringbuf->r_offset + info.wp_offset;
  }

  ringbuf->w_offset = info.wp_offset;

  return DVR_SUCCESS;
}

static int dvr_poll(int dvr_fd, pthread_mutex_t lock)
{
  int ret;
  struct pollfd poll_fd;

  memset(&poll_fd, 0, sizeof(poll_fd));
  poll_fd.fd = dvr_fd;
  poll_fd.events = POLLIN | POLLERR;

  pthread_mutex_unlock(&lock);
  ret = poll(&poll_fd, 1, DVR_TIMEOUT);
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

// Record clear pid stream to ringbuffer
static int normal_dvr_with_rb(int dvr_fd,
    DVR_RecordOutput_t *ringbuf,
    pthread_mutex_t lock)
{
  ssize_t len;

  if (dvr_poll(dvr_fd, lock) || dvr_fd < 0)
    return DVR_FAILURE;

  DVR_INFO("before read, r_offset: %#x, w_offset: %#x\n",
        ringbuf->r_offset, ringbuf->w_offset);

  // If the write offset is greater than or equal to the read offset,
  // then the data is recorded to from the write offset to the end of
  // the ring buffer. Otherwise the data is recorded at the position
  // from write offset to read offset.
  if (ringbuf->w_offset >= ringbuf->r_offset) {
    len = read(dvr_fd,
            ringbuf->buffer + ringbuf->w_offset,
            ringbuf->len - ringbuf->w_offset);
    ringbuf->size = ringbuf->w_offset - ringbuf->r_offset + len;;
    //ringbuf->size += len;
  } else {
    len = read(dvr_fd,
            ringbuf->buffer + ringbuf->w_offset,
            ringbuf->r_offset - ringbuf->w_offset);
    ringbuf->size = ringbuf->len -
            ringbuf->r_offset +
            ringbuf->w_offset + len;
  }
  if (len <= 0 || (len % 188) != 0) {
    DVR_ERROR("%s read failed: %s. fd: %d, r: %#x, w: %#x, len: %#x\n",
            __func__, strerror(errno), dvr_fd,
            ringbuf->r_offset,
            ringbuf->w_offset,
            len);
    return DVR_FAILURE;
  }

  ringbuf->w_offset =
          (ringbuf->w_offset + len) %
          ringbuf->len;

  DVR_INFO("%s record %#x bytes. r: %#x, w: %#x, size: %#x\n",
        __func__, len,
        ringbuf->r_offset,
        ringbuf->w_offset,
        ringbuf->size);

  return DVR_SUCCESS;
}

// Inject data to the dvr fd with a secure buffer and record to normal buffer
// The number of bytes written is returned
static ssize_t secure_inject_record2normal(
        size_t sects_sess,
        int inject_fd,
        int dvr_fd,
        uint8_t *sec_buf,
        size_t sec_buf_len,
        uint8_t *rec_buf,
        size_t rec_buf_len,
        pthread_mutex_t lock)
{
  int time = 50;
  int ret = 0;
  ssize_t act_rec_len = 0;
  size_t inj_buf = 0;
  struct dmx_sec_ts_data sec_ts_data;

#if 0
  inj_buf = (size_t)sec_buf;
#else
  if ( SECTS_Map2InjBuff_Func != NULL) {
    inj_buf = SECTS_Map2InjBuff_Func(sects_sess, (size_t)sec_buf, sec_buf_len);
  }

  if (inj_buf == 0) {
    DVR_ERROR("%s map2inject failed\n", __func__);
    return -1;
  }
#endif

  sec_ts_data.buf_start = inj_buf;
  sec_ts_data.buf_end = (size_t) (sec_ts_data.buf_start + sec_buf_len);
  DVR_INFO("secure inject %#x bytes, addr: %p\n",  sec_buf_len, (void *)sec_ts_data.buf_start);
  ret = write(inject_fd, (uint8_t *)&sec_ts_data, sizeof(struct dmx_sec_ts_data));
  if (ret <= 0) {
    DVR_ERROR("error!!!inject to dvr failed: %d, inj_fd: %d", errno, inject_fd);
    return -1;
  }

  do {
    act_rec_len += normal_dvr(dvr_fd,
                            rec_buf + act_rec_len,
                            rec_buf_len - act_rec_len,
                            lock);
    // The record data length must be equal with pusi len
    if (act_rec_len >= sec_buf_len) {
      break;
    }
    usleep(10*1000);
  } while (time-- > 0);

  if (act_rec_len < sec_buf_len) {
      DVR_ERROR("error!!! secure inject len: %#x, actual record len: %#x",
        sec_buf_len, act_rec_len);
  }

  DVR_INFO("%#x bytes read\n",  act_rec_len);
  return act_rec_len;
}

// Read a PUSI data from secure pusi ringbuffer to receive buffer.
// If non_pusi_rb have data and memory space in receive buffer, then fill
// the data in non_pusi_rb to the tail of the receive buffer.
// Update both the pusi_rb and non_pusi_rb
static ssize_t secure_pusi_read(
    size_t sects_sess,
    int inject_fd,
    int dvr_fd,
    DVR_RecordOutput_t *pusi_rb,
    SECTS_IndexerPusi_t *pusi_inf,
    DVR_RecordOutput_t *non_pusi_rb,
    DVR_RecordReceiveParams_t *params,
    pthread_mutex_t lock)
{
  // Secure inject descrambled stream(rb1) on dmx_dev_id[2] for re-encryption.
  // Record re-encrypted pid stream to receive buffer
  size_t inj_len = 0;
  size_t rec_len = 0;
  size_t pusi_len = 0;
  SECTS_IndexerPusi_t *pusi = pusi_inf;

  if (pusi->start == pusi->end
        || pusi->start >= pusi_rb->len
        || pusi->end >= pusi_rb->len ) {
    DVR_ERROR("%s wrong pusi. pusi_start: %#x, pusi_end: %#x, output len: %#x",
            __func__, pusi->start, pusi->end, pusi_rb->len);
    return -1;
  }

  if (pusi->flags & DVR_INDEX_IFRAME) {
    DVR_INFO("pusi read %#x ~ %#x, flags: %d",
             pusi->start, pusi->end, pusi->flags);
  }

  if (pusi->end > pusi->start) {
    // Buffer length MUST be enough
    pusi_len = pusi->end - pusi->start + 1;
    if (pusi_len > params->len) {
      DVR_ERROR("buffer is not enough! buffer len: %#x, data len:%#x. %#x-%#x",
            params->len, pusi_len, pusi->start, pusi->end);
      return -1;
    }

    rec_len = secure_inject_record2normal(
                    sects_sess,
                    inject_fd,
                    dvr_fd,
                    pusi_rb->buffer + pusi->start,
                    pusi_len,
                    params->buf,
                    params->len,
                    lock);
    DVR_CHECK(rec_len == pusi_len);
  } else {
    pusi_len = (pusi_rb->len - pusi->start) + pusi->end + 1;
    if (pusi_len > params->len) {
      DVR_ERROR("buffer is not enough! buffer len: %#x, data len:%#x. %#x-%#x",
            params->len, pusi_len, pusi->start, pusi->end);
      return -1;
    }

    inj_len = pusi_rb->len - pusi->start;
    // Inject from start to tail
    rec_len = secure_inject_record2normal(
                    sects_sess,
                    inject_fd,
                    dvr_fd,
                    pusi_rb->buffer + pusi->start,
                    inj_len,
                    params->buf,
                    params->len,
                    lock);
    DVR_CHECK(rec_len == inj_len);

    inj_len = pusi->end + 1;
    // Inject from head to end
    rec_len += secure_inject_record2normal(
                    sects_sess,
                    inject_fd,
                    dvr_fd,
                    pusi_rb->buffer,
                    inj_len,
                    params->buf + rec_len,
                    params->len - rec_len,
                    lock);
    DVR_CHECK(rec_len == pusi_len);
  }

  // Reset the PUSI state
  pusi->state = SECTS_INDEXER_PUSI_NONE;

  //DVR_INFO("none pusi data size: %#x", non_pusi_rb->size);
  if (params->len > pusi_len && non_pusi_rb->size > 0) {
    size_t left = params->len - pusi_len;
    size_t wanna_len = (left < non_pusi_rb->size) ? left : non_pusi_rb->size;
    uint8_t *src = non_pusi_rb->buffer + non_pusi_rb->r_offset;

    DVR_INFO("none pusi rp: %#x, wp: %#x, size: %#x", non_pusi_rb->r_offset,
                non_pusi_rb->w_offset, non_pusi_rb->size);
    // If non_pusi_rb have data and memory space in receive buffer, then fill
    // the data in non_pusi_rb to the tail of the receive buffer.
    if (non_pusi_rb->r_offset + wanna_len > non_pusi_rb->len) {
      size_t tail_len = 0;
      size_t head_len = 0;

      tail_len = non_pusi_rb->len - non_pusi_rb->r_offset;
      // Copy the tail data to receive buffer
      memcpy(params->buf + pusi_len, src, tail_len);

      head_len = wanna_len - tail_len;
      // Copy the head data to receive buffer
      memcpy(params->buf + pusi_len + tail_len,
                non_pusi_rb->buffer,
                head_len);

      non_pusi_rb->r_offset = head_len;
    } else {
      memcpy(params->buf + pusi_len, src, wanna_len);
      non_pusi_rb->r_offset += wanna_len;
    }
    non_pusi_rb->r_offset %= non_pusi_rb->len;
    DVR_INFO("%#x bytes non pusi data picked. 0x%02x 0x%02x 0x%02x 0x%02x",
          wanna_len, src[0], src[1], src[2], src[3]);

    non_pusi_rb->size -= wanna_len;

    // Update the read data length
    rec_len += wanna_len;
  }

  params->flags = pusi->flags;
  params->pts = pusi->pts;

  return rec_len;
}

// Read a PUSI data from normal pusi ringbuffer to receive buffer
static ssize_t normal_pusi_read(
    DVR_RecordOutput_t *pusi_rb,
    TS_Indexer_Pusi_t *pusi_inf,
    DVR_RecordReceiveParams_t *params)
{
  size_t pusi_len = 0;
  TS_Indexer_Pusi_t *pusi = pusi_inf;

  if (pusi->start == pusi->end
        || pusi->start >= pusi_rb->len
        || pusi->end >= pusi_rb->len ) {
    DVR_ERROR("%s wrong pusi. pusi_start: %#x, pusi_end: %#x, output len: %#x",
            __func__, pusi->start, pusi->end, pusi_rb->len);
    return -1;
  }

  if (pusi->flags & DVR_INDEX_IFRAME) {
    DVR_ERROR("pusi read %#x ~ %#x, flags: %d",
            pusi->start, pusi->end, pusi->flags);
  }

  if (pusi->end > pusi->start) {
    // Buffer length MUST be enough
    pusi_len = pusi->end - pusi->start + 1;
    if (pusi_len > params->len) {
      DVR_ERROR("buffer is not enough! buffer len: %#x, data len:%#x. %#x-%#x",
            params->len, pusi_len, pusi->start, pusi->end);
      return -1;
    }

    // Copy from start to end
    memcpy(&params->buf[0],
            &pusi_rb->buffer[pusi->start],
            pusi_len);
  } else {
    pusi_len = (pusi_rb->len - pusi->start) + pusi->end + 1;
    if (pusi_len > params->len) {
      DVR_ERROR("buffer is not enough! buffer len: %#x, data len:%#x. %#x-%#x",
            params->len, pusi_len, pusi->start, pusi->end);
      return -1;
    }
    // Copy from start to tail
    memcpy((void *)&params->buf[0],
            (void *)&pusi_rb->buffer[pusi->start],
            pusi_rb->len - pusi->start);

    // Copy from head to end
    memcpy(&params->buf[pusi_rb->len - pusi->start],
            &pusi_rb->buffer[0],
            pusi->end + 1);
  }
  params->flags = pusi->flags;
  params->pts = pusi->pts;

  memset(pusi, 0, sizeof(TS_Indexer_Pusi_t));

  return pusi_len;
}

#if 0 // For debug
static void pusi_print(DVR_RecordContext_t *p_ctx)
{
  DVR_INFO("#######################################");
  for (int i = 0; i < DVR_MAX_RECORD_PUSI_CNT; i++) {
    if (p_ctx->pusi[i].state == TS_INDEXER_PUSI_DONE) {
      DVR_INFO("pusi[%d]: %#x ~ %#x\n", i, p_ctx->pusi[i].start, p_ctx->pusi[i].end);
    }
  }
  DVR_INFO("#######################################");
}
#endif

// Get the head PUSI
static TS_Indexer_Pusi_t *pusi_get(DVR_RecordContext_t *p_ctx)
{
  int i;

  // Find the first valid PUSI
  for (i = 0; i < DVR_MAX_RECORD_PUSI_CNT; i++) {
    if (p_ctx->pusi[i].state == TS_INDEXER_PUSI_DONE) {
      DVR_INFO("get pusi[%d]: %#x ~ %#x, flags: %d\n",
            i, p_ctx->pusi[i].start,
            p_ctx->pusi[i].end, p_ctx->pusi[i].flags);
      break;
    }
  }

  // Return NULL if no PUSI found
  if (i >= DVR_MAX_RECORD_PUSI_CNT) {
    DVR_INFO("%s no PUSI\n", __func__);
    return NULL;
  }

  return &p_ctx->pusi[i];
}

// Move the left tail PUSI to head
static int pusi_move(DVR_RecordContext_t *p_ctx)
{
  int i;

  for (i = 0; i < DVR_MAX_RECORD_PUSI_CNT; i++) {
    if (p_ctx->pusi[i].state == TS_INDEXER_PUSI_PARSING)
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
  ssize_t len = -1;
  int ret;
  int max_pusi_cnt = DVR_MAX_RECORD_PUSI_CNT;
  TS_Indexer_Pusi_t *pusi = NULL;
  DVR_RecordContext_t *p_ctx = (DVR_RecordContext_t *)handle;

  DVR_CHECK(p_ctx != NULL);
  DVR_CHECK(params != NULL);
  DVR_CHECK(params->buf != NULL);
  DVR_CHECK(
        (params->len >= DVR_BLOCK_SIZE) &&
        (params->len % 188) == 0);

  DVR_CHECK(
        (params->mode == DVR_DIRECT_RECORD_MODE ||
        params->mode == DVR_PUSI_RECORD_MODE));

  pthread_mutex_lock(&p_ctx->lock);
  DVR_CHECK_WITH_UNLOCK(
                p_ctx->state == DVR_RECORD_STATE_STARTED,
                &p_ctx->lock);

  // It's direct dvr mode, not need ts indexer and ringbuffer
  if (params->mode == DVR_DIRECT_RECORD_MODE)
  {
    // Read away the data of the dvr device directly without using ringbuffer
    // and ts indexer
    len = normal_dvr(p_ctx->fd[0], params->buf, params->len, p_ctx->lock);
    goto exit;
  }

  if (p_ctx->is_secure_mode) {
    DVR_CHECK_WITH_UNLOCK(
                ca_ready_check(p_ctx) == 0,
                &p_ctx->lock);
  }
  // Get a DONE pusi and then retrieves the data from the ringbuffer
  // based on that pusi
  pusi = pusi_get(p_ctx);
  if (pusi != NULL) {
    // Read a pusi data and flags according to the pusi information
    // If the recording is secure mode
      // a. With REE ts indexer, combine rb0 + rb1 inject-rec, PUSIs are in rb0
      // b. With TEE ts indexer, combine rb0 + rb1 inject-rec, PUSIs are in rb1
    // If the recording is clear, read from rb0, PUSIs are in rb0
    if (p_ctx->is_secure_mode) {
      if (p_ctx->sects_sess != -1 & p_ctx->fd[2] >= 0) {
        // TEE ts indexer in secure mode, video is scrambled
        len = secure_pusi_read(p_ctx->sects_sess, p_ctx->fd[2], p_ctx->fd[3], &p_ctx->rb1,
                    (SECTS_IndexerPusi_t *)pusi, &p_ctx->rb0,
                    params, p_ctx->lock);
      } else {
        // TODO: REE ts indexer in secure moce, video is clear, audio is scrambled
        DVR_ERROR("clear ts indexer in secure mode, not support now!");
      }
      goto exit;
    }

    len = normal_pusi_read(&p_ctx->rb0, pusi, params);
    if (len > 0) {
      //DVR_INFO("%s data len: %#x, flags: %#x", __func__,
      //        len, params->flags);
      goto exit;
    }
    DVR_ERROR("%s should not come here", __func__);
  } else {
    // Move the left tail PUSI to head if it's TS_INDEXER_PUSI_PARSING state
    pusi_move(p_ctx);
  }

  do {
    if (p_ctx->is_secure_mode) {
      // Record clear pid stream like PAT/PMT etc. on dmx_dev_id[0]
      normal_dvr_with_rb(p_ctx->fd[0], &p_ctx->rb0, p_ctx->lock);

      // Record scrambled pid stream like video/audio/subtitle etc. in CAS
      // stream on dmx_dev_id[1]
      ret = secure_dvr_with_rb(p_ctx->fd[1], &p_ctx->rb1);
      if (ret != DVR_SUCCESS)
        break;

      // Secure ts indexer
      DVR_INFO("sects parse, wp: %#x, rp: %#x, size: %#x\n",
            p_ctx->rb1.w_offset, p_ctx->rb1.r_offset, p_ctx->rb1.size);
      ret = SECTS_IndexerParse_Func(p_ctx->sects_sess,
                                (SECTS_IndexerRingBuffer_t *)&p_ctx->rb1,
                                (SECTS_IndexerPusi_t *)&p_ctx->pusi[0],
                                max_pusi_cnt);
      DVR_INFO("sects parse done, wp: %#x, rp: %#x, size: %#x\n",
            p_ctx->rb1.w_offset, p_ctx->rb1.r_offset, p_ctx->rb1.size);
      if (ret != 0) {
        DVR_ERROR("%s sects parse failed", __func__);
        goto exit;
      }
    } else {
      ret = DVR_FAILURE;

      if (normal_dvr_with_rb(p_ctx->fd[0], &p_ctx->rb0, p_ctx->lock))
        break;

      if (ts_indexer_parse(&p_ctx->ts_indexer,
                        &p_ctx->rb0,
                        &p_ctx->pusi[0],
                        max_pusi_cnt)) {
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

