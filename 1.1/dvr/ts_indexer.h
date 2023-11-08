/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * ts_indexer.h
 */

#ifndef _TS_INDEXER_H_
#define _TS_INDEXER_H_

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef DVR_VideoFormat_t TS_Indexer_StreamFormat_t;

/**Event type.*/
typedef enum {
  TS_INDEXER_EVENT_TYPE_START_INDICATOR,            /**< TS start indicator.*/
  TS_INDEXER_EVENT_TYPE_DISCONTINUITY_INDICATOR,    /**< TS discontinuity indicator.*/
  TS_INDEXER_EVENT_TYPE_MPEG2_I_FRAME,              /**< MPEG2 I frame.*/
  TS_INDEXER_EVENT_TYPE_MPEG2_P_FRAME,              /**< MPEG2 P frame.*/
  TS_INDEXER_EVENT_TYPE_MPEG2_B_FRAME,              /**< MPEG2 B frame.*/
  TS_INDEXER_EVENT_TYPE_MPEG2_SEQUENCE,             /**< MPEG2 Video Sequence header.*/
  TS_INDEXER_EVENT_TYPE_AVC_I_SLICE,                /**< AVC I slice.*/
  TS_INDEXER_EVENT_TYPE_AVC_B_SLICE,                /**< AVC B slice.*/
  TS_INDEXER_EVENT_TYPE_AVC_P_SLICE,                /**< AVC P slice.*/
  TS_INDEXER_EVENT_TYPE_AVC_SI_SLICE,               /**< AVC SI slice.*/
  TS_INDEXER_EVENT_TYPE_AVC_SP_SLICE,               /**< AVC SP slice.*/
  TS_INDEXER_EVENT_TYPE_HEVC_SPS,                   /**< HEVC NAL unit type SPS_NUT.*/
  TS_INDEXER_EVENT_TYPE_HEVC_AUD,                   /**< HEVC NAL unit type AUD_NUT.*/
  TS_INDEXER_EVENT_TYPE_HEVC_BLA_W_LP,              /**< HEVC NAL unit type BLA_W_LP.*/
  TS_INDEXER_EVENT_TYPE_HEVC_BLA_W_RADL,            /**< HEVC NAL unit type BLA_W_RADL.*/
  TS_INDEXER_EVENT_TYPE_HEVC_BLA_N_LP,              /**< HEVC NAL unit type BLA_N_LP.*/
  TS_INDEXER_EVENT_TYPE_HEVC_IDR_W_RADL,            /**< HEVC NAL unit type IDR_W_RADL.*/
  TS_INDEXER_EVENT_TYPE_HEVC_IDR_N_LP,              /**< HEVC NAL unit type IDR_N_LP.*/
  TS_INDEXER_EVENT_TYPE_HEVC_TRAIL_CRA,             /**< HEVC NAL unit type TRAIL_CRA.*/
  TS_INDEXER_EVENT_TYPE_VIDEO_PTS,                  /**< MEPG2/AVC/HEVC PTS.*/
  TS_INDEXER_EVENT_TYPE_AUDIO_PTS                   /**< Audio PTS.*/
} TS_Indexer_EventType_t;

/**Stream Parser state.*/
typedef enum {
  TS_INDEXER_STATE_INIT,        /**< Init state.*/
  TS_INDEXER_STATE_TS_START,    /**< TS header state with start_indicator==1.*/
  TS_INDEXER_STATE_PES_HEADER,  /**< PES header state.*/
  TS_INDEXER_STATE_PES_PTS,     /**< PES pts state.*/
  TS_INDEXER_STATE_PES_I_FRAME  /**< PES I-frame state.*/
} TS_Indexer_State_t;

/**TS indexer PUSI state.*/
typedef enum {
  TS_INDEXER_PUSI_NONE,        /**< PUSI Init state.*/
  TS_INDEXER_PUSI_PARSING,     /**< PUSI Parsing state.*/
  TS_INDEXER_PUSI_DONE,        /**< PUSI Parse done state.*/
  TS_INDEXER_PUSI_INVALID      /**< PUSI Invalid state.*/
} TS_Indexer_Pusi_State_t;

/**TS indexer ringbuffer.*/
typedef struct
{
  uint8_t *buffer;              /**< DVR record output buffer*/
  size_t len;                   /**< DVR record output buffer length*/
  size_t w_offset;              /**< DVR record output write offset*/
  size_t r_offset;              /**< TS indexer read offset*/
  size_t size;                  /**< DVR record data length*/
} TS_Indexer_RingBuffer_t;

/**PUSI.*/
typedef struct
{
  size_t start;         /**< start position of PUSI */
  size_t end;           /**< end position of PUSI*/
  int flags;            /**< index INDEX_PUSI/INDEX_PTS/INDEX_IFRAME etc.*/
  uint64_t pts;         /**< PTS of the PUSI*/
  uint8_t state;        /**< index valid flag*/
} TS_Indexer_Pusi_t;

/**TS indexer.*/
typedef struct TS_Indexer_s TS_Indexer_t;

/**PES parser.*/
typedef struct {
  uint64_t                      pts;            /**< The a/v PTS.*/
  size_t                      offset;           /**< The current offset.*/
  uint8_t                       data[184+16];   /**< The PES data.*/
  int                           len;            /**< The length of PES data`.*/
  TS_Indexer_State_t            state;          /**< The stream state.*/
} PESParser;

/**TS parser.*/
typedef struct {
  int                           pid;    /**< The a/v PID.*/
  TS_Indexer_StreamFormat_t     format; /**< The a/v format.*/
  PESParser                     PES;    /**< The PES parser.*/
  size_t                      offset;   /**< The offset of packet with start indicator.*/
} TSParser;

/**TS indexer.*/
struct TS_Indexer_s {
  TSParser                  video_parser;   /**< The video parser.*/
  TSParser                  audio_parser;   /**< The audio parser.*/
  uint64_t                  offset;         /**< The current offset.*/
  TS_Indexer_RingBuffer_t   *ringbuffer;    /**< The ringbuffer.*/
  TS_Indexer_Pusi_t         *pusi;          /**< The PUSI list*/
  int                       pusi_cur_idx;   /**< The PUSI current index*/
  int                       pusi_max_cnt;   /**< The PUSI count*/
};

/**
 * Initialize the TS indexer.
 * \param ts_indexer The TS indexer to be initialized.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int ts_indexer_init (TS_Indexer_t *ts_indexer);

/**
 * Release the TS indexer.
 * \param ts_indexer The TS indexer to be released.
 * \retval 0 On success.
 * \retval -1 On error.
 */
void ts_indexer_destroy (TS_Indexer_t *ts_indexer);

/**
 * Set the video format.
 * \param ts_indexer The TS indexer.
 * \param format The video format.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int ts_indexer_set_video_format (TS_Indexer_t *ts_indexer, TS_Indexer_StreamFormat_t format);

/**
 * Set the video PID.
 * \param ts_indexer The TS indexer.
 * \param pid The video PID.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int ts_indexer_set_video_pid (TS_Indexer_t *ts_indexer, int pid);

/**
 * Set the audio PID.
 * \param ts_indexer The TS indexer.
 * \param pid The audio PID.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int ts_indexer_set_audio_pid (TS_Indexer_t *ts_indexer, int pid);

/**
 * Parse the TS stream and generate the index data.
 * \param ts_indexer The TS indexer.
 * \param ringbuf The TS ring buffer.
 * \param pusi The PUSI list
 * \param pusi_max_cnt The maximum PUSI count
 * \return The left TS data length of bytes.
 */
int ts_indexer_parse (
    TS_Indexer_t *ts_indexer,
    TS_Indexer_RingBuffer_t *ringbuf,
    TS_Indexer_Pusi_t *pusi,
    int pusi_max_cnt);

#ifdef __cplusplus
}
#endif

#endif /*_TS_INDEXER_H_*/

