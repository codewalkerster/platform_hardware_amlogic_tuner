/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * ts_indexer.c
 */

#include <stdio.h>
#include <string.h>
#include "dvr_record.h"
#include "dvr_types.h"
#include "ts_indexer.h"
#include "bitstrm.h"

#define TS_PKT_SIZE (188)
#define NAL_TYPE_NON_IDR    1  // 非 IDR NALU 类型
#define NAL_TYPE_IDR        5  // IDR NALU 类型

#define HEVC_NALU_BLA_W_LP      16
#define HEVC_NALU_BLA_W_RADL    17
#define HEVC_NALU_BLA_N_LP      18
#define HEVC_NALU_IDR_W_RADL    19
#define HEVC_NALU_IDR_N_LP      20
#define HEVC_NALU_TRAIL_CRA     21
#define HEVC_NALU_SPS           33
#define HEVC_NALU_AUD           35

/**
 * Initialize the TS indexer.
 * \param ts_indexer The TS indexer to be initialized.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int
ts_indexer_init (TS_Indexer_t *ts_indexer)
{
  TSParser init_parser;

  if (ts_indexer == NULL) {
    return -1;
  }

  memset(&init_parser, 0, sizeof(TSParser));
  init_parser.pid = 0x1fff;
  init_parser.format = -1;
  init_parser.PES.pts = -1;
  init_parser.PES.offset = 0;
  init_parser.PES.len = 0;
  init_parser.PES.state = TS_INDEXER_STATE_INIT;

  memcpy(&ts_indexer->video_parser, &init_parser, sizeof(TSParser));
  memcpy(&ts_indexer->audio_parser, &init_parser, sizeof(TSParser));
  ts_indexer->offset       = 0;

  return 0;
}

/**
 * Release the TS indexer.
 * \param ts_indexer The TS indexer to be released.
 */
void
ts_indexer_destroy (TS_Indexer_t *ts_indexer)
{
  return;
}

/**
 * Set the video format.
 * \param ts_indexer The TS indexer.
 * \param format The stream format.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int
ts_indexer_set_video_format (TS_Indexer_t *ts_indexer, TS_Indexer_StreamFormat_t format)
{
  if (ts_indexer == NULL)
    return -1;

  ts_indexer->video_parser.format = format;

  return 0;
}

/**
 * Set the video PID.
 * \param ts_indexer The TS indexer.
 * \param pid The video PID.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int
ts_indexer_set_video_pid (TS_Indexer_t *ts_indexer, int pid)
{
  if (ts_indexer == NULL)
    return -1;


  TSParser *parser = &ts_indexer->video_parser;
  parser->pid = pid;
  parser->offset = 0;
  parser->PES.pts = -1;
  parser->PES.offset = 0;
  parser->PES.len = 0;
  parser->PES.state = TS_INDEXER_STATE_INIT;

  return 0;
}

/**
 * Set the audio PID.
 * \param ts_indexer The TS indexer.
 * \param pid The audio PID.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int
ts_indexer_set_audio_pid (TS_Indexer_t *ts_indexer, int pid)
{
  if (ts_indexer == NULL)
    return -1;

  TSParser *parser = &ts_indexer->audio_parser;
  parser->pid = pid;
  parser->offset = 0;
  parser->PES.pts = -1;
  parser->PES.offset = 0;
  parser->PES.len = 0;
  parser->PES.state = TS_INDEXER_STATE_INIT;

  return 0;
}

static void find_mpeg(uint8_t *data, int len, TS_Indexer_t *indexer, TSParser *stream)
{
  int i;
  uint32_t needle = 0;
  uint32_t needle1 = 0;
  uint8_t *haystack = data;
  int haystack_len = len;
  int left = len;
  // start code of picture header
  uint8_t arr[4] = {0x00, 0x00, 0x01, 0x00};
  // start code of sequence header
  uint8_t arr1[4] = {0x00, 0x00, 0x01, 0xb3};

  /* mpeg header needs at least 4 bytes */
  if (left < 4) {
    memcpy(&stream->PES.data[0], &haystack, left);
    stream->PES.len = left;
    return;
  }

  for (i = 0; i < 4; ++i) {
    needle += (arr[i] << (8 * i));
  }

  for (i = 0; i < 4; ++i) {
    needle1 += (arr1[i] << (8 * i));
  }

  for (i = 0; i < haystack_len - sizeof(needle) + 1;) {
    if (left < 5) {
      //DVR_INFO("MPEG2 picture header across TS Packet\n");

      /* MEPG2 picture header across TS packet, should cache the left data */
      memcpy(&stream->PES.data[0], haystack + i, left);
      stream->PES.len = left;
      return;
    }

    if (*(uint32_t *)(haystack + i) == needle) {
      // picture header found
      int frame_type = (haystack[i + 5] >> 3) & 0x7;
      switch (frame_type) {
        case 1:
            indexer->pusi[indexer->pusi_cur_idx].flags |= DVR_INDEX_IFRAME;
            DVR_INFO("I frame found, offset: %zx, pusi[%d] flags: %d\n",
                stream->offset,
                indexer->pusi_cur_idx,
                indexer->pusi[indexer->pusi_cur_idx].flags);
            break;

        case 2:
            DVR_INFO("P frame found, offset: %zx\n", stream->offset);
            break;

        case 3:
            DVR_INFO("B frame found, offset: %zx\n", stream->offset);
            break;

        default:
            i += 5;
            left -= 5;
            continue;

      }

      i += 5;
      left -= 5;
    } else if (*(uint32_t *)(haystack + i) == needle1) {
      // sequence header found
      i += 5;
      left -= 5;
    } else {
      i ++;
      left --;
    }
  }

  if (left > 0) {
    memcpy(&stream->PES.data[0], &haystack[i], left);
    stream->PES.len = left;
  } else {
    stream->PES.len = 0;
  }
}

static uint8_t *get_nalu(uint8_t *data, size_t len, size_t *nalu_len)
{
  size_t i = 0;
  uint8_t *p = data;

  if (len == 0)
    return NULL;

  //DVR_INFO("%s enter, len:%#x\n", __func__, len);
  while (i < len - 4) {
    if (p[i] == 0x00 && p[i+1] == 0x00 && p[i+2] == 0x01) {
      uint8_t *frame_data = data + i;
      size_t frame_data_len = 0;

      i += 4;
      //DVR_INFO("%s start code prefix\n", __func__);
      for (size_t j = i ; j < len - 4; ++j) {
        if (p[j] == 0x00 && p[j+1] == 0x00 && p[j+2] == 0x01) {
          frame_data_len = j - i;
          break;
        }
      }

      if (frame_data_len > 0) {
        *nalu_len = frame_data_len;
        return frame_data;
      } else {
        frame_data_len = len - i;
        *nalu_len = frame_data_len;
        return frame_data;
      }
    } else {
      i ++;
    }
  }

  return NULL;
}

uint32_t golomb_uev(uint32_t *pu4_bitstrm_ofst, uint32_t *pu4_bitstrm_buf)
{
  uint32_t u4_bitstream_offset = *pu4_bitstrm_ofst;
  uint32_t u4_word, u4_ldz;

  /* Find leading zeros in next 32 bits */
  NEXTBITS_32(u4_word, u4_bitstream_offset, pu4_bitstrm_buf);
  u4_ldz = CLZ(u4_word);
  //printf("u4_ldz: %d, u4_word: %#x, offset: %d, pu4_bitstrm_buf: %#x\n",
  //          u4_ldz, u4_word, u4_bitstream_offset, *pu4_bitstrm_buf);

  /* Flush the ps_bitstrm */
  u4_bitstream_offset += (u4_ldz + 1);

  /* Read the suffix from the ps_bitstrm */
  u4_word = 0;
  if (u4_ldz)
    GETBITS(u4_word, u4_bitstream_offset, pu4_bitstrm_buf, u4_ldz);

  *pu4_bitstrm_ofst = u4_bitstream_offset;

  return ((1 << u4_ldz) + u4_word - 1);
}

uint32_t reverseBytes(uint32_t num)
{
  uint32_t result = 0;

  result |= (num & 0xff) << 24;
  result |= (num & 0xff00) << 8;
  result |= (num & 0xff0000) >> 8;
  result |= (num & 0xff000000) >> 24;

  return result;
}

static void find_h264(uint8_t *data, size_t len, TS_Indexer_t *indexer, TSParser *stream)
{
  uint8_t *nalu = data;
  size_t pes_data_len = len;
  size_t nalu_len;
  uint8_t *p = NULL;

  for (;;) {
    int left = pes_data_len - (nalu - data);
    if (left <= 5) {
      memcpy(&stream->PES.data[0], nalu, left);
      stream->PES.len = left;
      break;
    }

    nalu = get_nalu(nalu, left, &nalu_len);
    if (nalu == NULL)
      break;

    if (nalu[0] == 0x00 && nalu[1] == 0x00 && nalu[2] == 0x01) {
      p = &nalu[3];
    }

    uint32_t offset = 0;
    uint32_t *pu4_bitstrm_buf = (uint32_t *)&p[1];
    uint32_t *pu4_bitstrm_ofst = &offset;
    if (p != NULL)
    {
      uint8_t nal_unit_type = (p[0] & 0x1f);
      uint16_t u2_first_mb_in_slice;
      uint8_t slice_type;

      uint32_t reverseNum = reverseBytes(*pu4_bitstrm_buf);
      u2_first_mb_in_slice = golomb_uev(pu4_bitstrm_ofst, &reverseNum);
      slice_type = golomb_uev(pu4_bitstrm_ofst, &reverseNum);

      if (nal_unit_type == NAL_TYPE_IDR) {
        if (slice_type == 2 || slice_type == 7) {
          //event.type = TS_INDEXER_EVENT_TYPE_AVC_I_SLICE;
          indexer->pusi[indexer->pusi_cur_idx].flags |= DVR_INDEX_IFRAME;
        } else if (slice_type == 4 || slice_type == 9) {
          //event.type = TS_INDEXER_EVENT_TYPE_AVC_SI_SLICE;
        } else {
          DVR_ERROR("0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
                nalu[0], nalu[1], nalu[2], nalu[3],
                nalu[4], nalu[5], nalu[6], nalu[7]);
          DVR_ERROR("%s line%d invalid slice_type: %d, offset: %zx, first_mb: %d\n",
                __func__,
                __LINE__,
                slice_type,
                stream->offset,
                u2_first_mb_in_slice);
          nalu += nalu_len;
          continue;
        }
      } else if (nal_unit_type == NAL_TYPE_NON_IDR) {
          //DVR_ERROR("0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
          //      nalu[0], nalu[1], nalu[2], nalu[3],
          //      nalu[4], nalu[5], nalu[6], nalu[7]);
        if (slice_type == 0 || slice_type == 5) {
            //event.type = TS_INDEXER_EVENT_TYPE_AVC_P_SLICE;
        } else if (slice_type == 1 || slice_type == 6) {
            //event.type = TS_INDEXER_EVENT_TYPE_AVC_B_SLICE;
        } else if (slice_type == 2 || slice_type == 7) {
            //event.type = TS_INDEXER_EVENT_TYPE_AVC_I_SLICE;
            indexer->pusi[indexer->pusi_cur_idx].flags |= DVR_INDEX_IFRAME;
        } else if (slice_type == 3 || slice_type == 8) {
            //event.type = TS_INDEXER_EVENT_TYPE_AVC_SP_SLICE;
        } else if (slice_type == 4 || slice_type == 9) {
            //event.type = TS_INDEXER_EVENT_TYPE_AVC_SI_SLICE;
        } else {
            DVR_ERROR("%s line%d invalid slice_type: %d\n", __func__, __LINE__, slice_type);
            nalu += nalu_len;
            continue;
        }
      } else {
        nalu += nalu_len;
        continue;
      }
    }

    nalu += nalu_len;
  }

  stream->PES.len = 0;
}

static void find_h265(uint8_t *data, int len, TS_Indexer_t *indexer, TSParser *stream)
{
  uint8_t *nalu = data;
  size_t pes_data_len = len;
  size_t nalu_len;

  while (nalu != NULL) {
    int left = pes_data_len - (nalu - data);
    if (left <= 4) {
      memcpy(&stream->PES.data[0], nalu, left);
      stream->PES.len = left;
      break;
    }

    nalu = get_nalu(nalu, left, &nalu_len);
    if (nalu == NULL)
      break;

    if (nalu[0] == 0x00 && nalu[1] == 0x00 && nalu[2] == 0x01) {
      int nalu_type = (nalu[3] & 0x7E) >> 1;
      DVR_INFO("0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x, nalu_type: %d, offset: %#zx\n",
          nalu[3], nalu[4], nalu[5], nalu[6], nalu[7], nalu[8], nalu_type, stream->offset);
      switch (nalu_type) {
        case HEVC_NALU_BLA_W_LP:
            //event.type = TS_INDEXER_EVENT_TYPE_HEVC_BLA_W_LP;
            break;

        case HEVC_NALU_BLA_W_RADL:
            //event.type = TS_INDEXER_EVENT_TYPE_HEVC_BLA_W_RADL;
            break;

        case HEVC_NALU_BLA_N_LP:
            //event.type = TS_INDEXER_EVENT_TYPE_HEVC_BLA_N_LP;
            break;

        case HEVC_NALU_IDR_W_RADL:
            //event.type = TS_INDEXER_EVENT_TYPE_HEVC_IDR_W_RADL;
            indexer->pusi[indexer->pusi_cur_idx].flags |= DVR_INDEX_IFRAME;
            DVR_INFO("HEVC I-frame found\n");
            break;

        case HEVC_NALU_IDR_N_LP:
            //event.type = TS_INDEXER_EVENT_TYPE_HEVC_IDR_N_LP;
            indexer->pusi[indexer->pusi_cur_idx].flags |= DVR_INDEX_IFRAME;
            DVR_INFO("HEVC IDR_N_LP frame found\n");
            break;

        case HEVC_NALU_TRAIL_CRA:
            //event.type = TS_INDEXER_EVENT_TYPE_HEVC_TRAIL_CRA;
            indexer->pusi[indexer->pusi_cur_idx].flags |= DVR_INDEX_IFRAME;
            DVR_INFO("HEVC CRA frame found\n");
            break;

        case HEVC_NALU_SPS:
            //event.type = TS_INDEXER_EVENT_TYPE_HEVC_SPS;
            break;

        case HEVC_NALU_AUD:
            //event.type = TS_INDEXER_EVENT_TYPE_HEVC_AUD;
            break;

        default:
            nalu += nalu_len;
            continue;
      }
    }

    nalu += nalu_len;
  }

  stream->PES.len = 0;
}

/*Parse the PES packet*/
static void
pes_packet(TS_Indexer_t *ts_indexer, uint8_t *data, int len, TSParser *stream)
{
  uint8_t *p = data;
  TS_Indexer_t *pi = ts_indexer;
  int left = len;

  //DVR_INFO("stream: %p, state: %d\n", stream, stream->PES.state);
  if (stream->PES.state <= TS_INDEXER_STATE_INIT) {
    //DVR_INFO("%s, invalid state\n", __func__);
    stream->PES.len = 0;
    return;
  }

  /* needs splice two pieces of data together if have cache data */
  if (stream->PES.len > 0) {
    //DVR_INFO("%s have cache data %d bytes\n", __func__, stream->PES.len);
    memcpy(&stream->PES.data[stream->PES.len], data, len);
    p = &stream->PES.data[0];
    left = stream->PES.len + len;
    stream->PES.len = left;
  }

  if (stream->PES.state == TS_INDEXER_STATE_TS_START) {
    /* needs cache data if no enough data to parse PES header */
    if (left < 6) {
      if (stream->PES.len <= 0) {
        memcpy(&stream->PES.data[0], p, left);
        stream->PES.len = left;
      }
      DVR_INFO("not enough ts payload len: %#x\n", left);
      return;
    }

    // chect the PES packet start code prefix
    if ((p[0] != 0) || (p[1] != 0) || (p[2] != 1)) {
      stream->PES.len = 0;
      stream->PES.state = TS_INDEXER_STATE_INIT;
      DVR_INFO("%s, not the expected start code!\n", __func__);
      return;
    }

    p += 6;
    left -= 6;
    stream->PES.state = TS_INDEXER_STATE_PES_HEADER;
  }

  if (stream->PES.state == TS_INDEXER_STATE_PES_HEADER) {
    if (left < 8) {
      if (stream->PES.len <= 0) {
        memcpy(&stream->PES.data[0], p, left);
        stream->PES.len = left;
      }
      DVR_INFO("not enough optional pes header len: %#x\n", left);
      return;
    }

    int header_length = p[2];
    if (p[1] & 0x80) {
      // parser pts
      p += 3;
      left -= 3;
      stream->PES.pts = (((uint64_t)(p[0] & 0x0E) << 29) |
                                    ((uint64_t)p[1] << 22) |
                                    ((uint64_t)(p[2] & 0xFE) << 14) |
                                    ((uint64_t)p[3] << 7) |
                                    (((uint64_t)p[4] & 0xFE) >> 1));
      if (stream == &pi->video_parser) {
        //event.type = TS_INDEXER_EVENT_TYPE_VIDEO_PTS;
        pi->pusi[pi->pusi_cur_idx].flags |= DVR_INDEX_PTS;
      } else {
        //event.type = TS_INDEXER_EVENT_TYPE_AUDIO_PTS;
      }
      pi->pusi[pi->pusi_cur_idx].pts = stream->PES.pts;
      DVR_INFO("pts = %" PRIu64 " in dvr\n", stream->PES.pts);
    }
    if (stream->format != -1) {
      stream->PES.state = TS_INDEXER_STATE_PES_PTS;

      p += header_length;
      left -= header_length;
    } else {
      stream->PES.state = TS_INDEXER_STATE_INIT;
      left = 0;
    }
  }

  stream->PES.len = left;
  if (left <= 0
    || stream->PES.state < TS_INDEXER_STATE_PES_PTS) {
    return;
  }

  //DVR_INFO("stream->format: %d, left: %d\n", stream->format, left);
  switch (stream->format) {
    case DVR_VIDEO_FORMAT_MPEG2:
      find_mpeg(p, left, pi, &pi->video_parser);
      break;

    case DVR_VIDEO_FORMAT_H264:
      find_h264(p, left, pi, &pi->video_parser);
      break;

    case DVR_VIDEO_FORMAT_HEVC:
      find_h265(p, left, pi, &pi->video_parser);
      break;

    default:
      stream->PES.state = TS_INDEXER_STATE_INIT;
      stream->PES.len = 0;
      break;
  }
}

/*Parse the TS packet.*/
static void
ts_packet(TS_Indexer_t *ts_indexer, uint8_t *data, size_t rp)
{
  uint16_t pid;
  uint8_t afc;
  uint8_t *p = data;
  TS_Indexer_t *pi = ts_indexer;
  int len;
  int is_start;

  is_start = p[1] & 0x40;
  pid = ((p[1] & 0x1f) << 8) | p[2];
  if (pid == 0x1fff)
    return;

  if ((pid != pi->video_parser.pid) &&
      (pid != pi->audio_parser.pid)) {
    return;
  }

  if (is_start) {
    //event.type = TS_INDEXER_EVENT_TYPE_START_INDICATOR;
    if (pid == pi->video_parser.pid) {
      pi->video_parser.offset = pi->offset;
      pi->video_parser.PES.len = 0;
      pi->video_parser.PES.state = TS_INDEXER_STATE_TS_START;
    }
    else if (pid == pi->audio_parser.pid) {
      pi->audio_parser.offset = pi->offset;
      pi->audio_parser.PES.len = 0;
      pi->audio_parser.PES.state = TS_INDEXER_STATE_TS_START;
    }

    if (pid == pi->video_parser.pid ||
        (pi->video_parser.pid == 0x1fff && pid == pi->audio_parser.pid)) {
      if (pi->pusi_cur_idx >= pi->pusi_max_cnt - 1) {
        DVR_ERROR("error! pusi_cur_idx: %d, pusi_max_cnt: %d",
                pi->pusi_cur_idx,
                pi->pusi_max_cnt);
        return;
      }
      pi->pusi_cur_idx++;

      if (pi->pusi_cur_idx >= 1) {
        if (rp > 0) {
          pi->pusi[pi->pusi_cur_idx - 1].end = rp - 1;
        } else {
          pi->pusi[pi->pusi_cur_idx - 1].end = ts_indexer->ringbuffer->len - 1;
        }
        pi->pusi[pi->pusi_cur_idx - 1].state = TS_INDEXER_PUSI_DONE;
      }

      pi->pusi[pi->pusi_cur_idx].flags = DVR_INDEX_PUSI;
      DVR_INFO("pusi[%d] found, pos: %#zx\n", pi->pusi_cur_idx, rp);

      // Ringbuffer's read offset is the position of lastest PUSI
      // Don't care audio
      pi->ringbuffer->r_offset = rp;
      pi->pusi[pi->pusi_cur_idx].start = rp;
      pi->pusi[pi->pusi_cur_idx].state = TS_INDEXER_PUSI_PARSING;
    }
  }

  afc = (p[3] >> 4) & 0x03;

  p += 4;
  len = 184;

  if (afc & 2) {
    int adp_field_len = p[0];
    if (p[1] & 0x80) {
      //event.type = TS_INDEXER_EVENT_TYPE_DISCONTINUITY_INDICATOR;
    }
    p++;
    len--;

    p += adp_field_len;
    len -= adp_field_len;

    if (len < 0) {
      DVR_ERROR("illegal adaption field length!");
      return;
    }
  }

  // has payload
  if ((afc & 1) && (len > 0)) {
    // parser pes packet
    pes_packet(pi, p, len, (pid == pi->video_parser.pid) ? &pi->video_parser : &pi->audio_parser);
  }
}

// Return left data length
int
ringbuf_parse(
    TS_Indexer_t *ts_indexer,
    uint8_t *data,
    size_t *rp,
    size_t wp,
    size_t size)
{
  uint8_t *p = data;
  size_t left = size;

  DVR_INFO("ts parse %#zx ~ %#zx, size: %#zx\n", *rp , wp, size);
  while (left) {
    // Find the sync byte
    if (*p == 0x47) {
      if (left < TS_PKT_SIZE) {
        DVR_ERROR("%s data length may not be 188-byte aligned. rp: %#zx, wp: %#zx\n",
               __func__, *rp, wp);
        return left;
      }
    } else {
      p++;
      left--;
      *rp = (*rp + 1) % ts_indexer->ringbuffer->len;
      ts_indexer->offset++;
    }

    // Parse one ts packe, sizet
    ts_packet(ts_indexer, p, *rp);
    p += TS_PKT_SIZE;
    left -= TS_PKT_SIZE;
    *rp = (*rp + TS_PKT_SIZE) % ts_indexer->ringbuffer->len;
    ts_indexer->offset += TS_PKT_SIZE;
  }

  return left;
}

/**
 * Parse the TS stream and generate the index data.
 * \param ts_indexer The TS indexer.
 * \return The left TS data length of bytes.
 */
int
ts_indexer_parse (
    TS_Indexer_t *ts_indexer,
    TS_Indexer_RingBuffer_t *ringbuf,
    TS_Indexer_Pusi_t *pusi,
    int pusi_max_cnt)
{
  uint8_t *p;
  size_t left = 0;
  size_t rp, wp, end;

  if (ts_indexer == NULL
        || ringbuf == NULL
        || pusi == NULL
        || pusi_max_cnt < 1) {
    DVR_ERROR("%s invalid param\n", __func__);
    return -1;
  }

  //memcpy(&ts_indexer->ringbuffer, ringbuf, sizeof(TS_Indexer_RingBuffer_t));

  rp = ringbuf->r_offset;
  wp = ringbuf->w_offset;
  end = ringbuf->len - 1;
  left = ringbuf->size;
  ts_indexer->ringbuffer = ringbuf;

  ts_indexer->pusi = pusi;
  ts_indexer->pusi_max_cnt = pusi_max_cnt;
  ts_indexer->pusi_cur_idx = -1;

  DVR_INFO("%s parse: %#zx ~ %#zx, end: %#zx, size: %#zx\n",
      __func__, rp, wp, end, left);
  if (left <= 0) {
    DVR_ERROR("%s data size is zero\n", __func__);
    return 0;
  }

  while (left) {
    p = &ringbuf->buffer[rp];
    // Find the sync byte
    if (*p == 0x47) {
      if (left < TS_PKT_SIZE) {
        DVR_ERROR("%s data length may not be 188-byte aligned. rp: %#zx, wp: %#zx\n",
               __func__, rp, wp);
        break;
      }
    } else {
      left--;
      rp = (rp + 1) % ringbuf->len;
      ts_indexer->offset++;
    }

    // Parse one ts packe, sizet
    ts_packet(ts_indexer, p, rp);
    left -= TS_PKT_SIZE;
    rp = (rp + TS_PKT_SIZE) % ringbuf->len;
    ts_indexer->offset += TS_PKT_SIZE;
  }

  ringbuf->size = left;
#if 0
  int j;
  for (j = 0; j <= ts_indexer->pusi_cur_idx; j++) {
    if (pusi[j].flags & 0x02) {
      DVR_INFO("ts indexer pusi[%d] %#x ~ %#x, flags = %d, \n",
            j, pusi[j].start, pusi[j].end, pusi[j].flags);
    }
  }
#endif
  DVR_INFO("%s pusi count: %d. rp: %#zx, wp: %#zx, size: %#zx\n",
		__func__,
		ts_indexer->pusi_cur_idx,
		rp, wp, left);

  return left;
}
