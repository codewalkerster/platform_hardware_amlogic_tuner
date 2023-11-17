/**
 * \page dvr_rec_test
 * \section Introduction
 * test code with dvr_xxx_xxx APIs.
 * It supports:
 * \li DVR recording and playback on both FTA and Scrambled program
 *
 * \section Usage
 *
 * \li vpid: the pid of video bitstream
 * \li apid: the pid of audio bitstream
 * \li vfmt: the video format
 * \li rec:  the dvr recording file path
 *
 * \code
 *    dvr_rec_test [src=] [dmx=] [vpid=] [vfmt=] [apid=] [rec=]
 * \endcode
 *
 * \endsection
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "dvr_record.h"
#include "libdsm.h"

#define INF(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)
#define ERR(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

#define has_pusi(_m_)    ((_m_) & DVR_INDEX_PUSI)
#define has_iframe(_m_) ((_m_) & DVR_INDEX_IFRAME)
#define has_pts(_m_)    ((_m_) & DVR_INDEX_PTS)

#define DVR_BUFFER_LEN    (20*188*1024)

static char *help_vfmt =
  "\n\t0:DVR_VIDEO_FORMAT_MPEG2" /**< MPEG2 video.*/
  "\n\t1:DVR_VIDEO_FORMAT_H264" /**< H264.*/
  "\n\t2:DVR_VIDEO_FORMAT_HEVC"; /**<HEVC.*/

typedef struct {
  int dsm_handle;
  uint32_t token;
  struct dsm_keyslot dec_even_keyslot;
  struct dsm_keyslot dec_odd_keyslot;
  struct dsm_keyslot enc_00_keyslot;
} dvr_casinfo_t;

static dvr_casinfo_t g_casinfo;

// Manage KTE/DSM
// Return keytoken
static uint32_t cas_create(void)
{
  uint32_t param = 0;
  int ret = -1;
  uint32_t token = -1;
  uint32_t dsm_handle;
  struct dsm_keyslot *enc_00_keyslot = &g_casinfo.enc_00_keyslot;
  struct dsm_keyslot *dec_even_keyslot = &g_casinfo.dec_even_keyslot;
  struct dsm_keyslot *dec_odd_keyslot = &g_casinfo.dec_odd_keyslot;

  memset(&g_casinfo, 0, sizeof(dvr_casinfo_t));
  g_casinfo.dsm_handle = -1;
  g_casinfo.token = -1;

  dsm_handle = DSM_OpenSession(param);
  ret = DSM_GenerateToken(dsm_handle, &token);

  // Prepare even/odd kte/keyslot for descrambling
  dec_even_keyslot->id = 0;
  dec_even_keyslot->algo = DSM_ALGO_CSA2;
  dec_even_keyslot->parity = DSM_PARITY_EVEN;
  dec_even_keyslot->is_enc = 0;
  ret = DSM_AddKeySlot(dsm_handle, dec_even_keyslot);

  dec_odd_keyslot->id = 1;
  dec_odd_keyslot->algo = DSM_ALGO_CSA2;
  dec_odd_keyslot->parity = DSM_PARITY_ODD;
  dec_odd_keyslot->is_enc = 0;
  ret |= DSM_AddKeySlot(dsm_handle, dec_odd_keyslot);
  ret |= DSM_SetProperty(dsm_handle,
                    DSM_PROP_DEC_SLOT_READY,
                    DSM_PROP_SLOT_IS_READY);

  // Prepare 00 kte/keyslot for dvr re-encryption
  enc_00_keyslot->id = 2;
  enc_00_keyslot->algo = DSM_ALGO_AES_CBC_CLR_END;
  enc_00_keyslot->parity = DSM_PARITY_NONE;
  enc_00_keyslot->is_enc = 1;
  ret |= DSM_AddKeySlot(dsm_handle, enc_00_keyslot);
  ret |= DSM_SetProperty(dsm_handle,
                    DSM_PROP_ENC_SLOT_READY,
                    DSM_PROP_SLOT_IS_READY);

  g_casinfo.dsm_handle = dsm_handle;
  g_casinfo.token = token;
  INF("%s ret: %d\n", __func__, ret);

  if (!ret) {
    return token;
  } else {
    return -1;
  }
}

static int cas_destroy(void)
{
  int ret = -1;
  uint32_t dsm_handle = g_casinfo.dsm_handle;
  struct dsm_keyslot *enc_00_keyslot = &g_casinfo.enc_00_keyslot;
  struct dsm_keyslot *dec_even_keyslot = &g_casinfo.dec_even_keyslot;
  struct dsm_keyslot *dec_odd_keyslot = &g_casinfo.dec_odd_keyslot;

  if (dsm_handle == -1) {
    ERR("%s invalid DSM handle\n", __func__);
    return -1;
  }

  ret = DSM_RemoveKeySlot(dsm_handle, enc_00_keyslot->id);
  ret |= DSM_RemoveKeySlot(dsm_handle, dec_even_keyslot->id);
  ret |= DSM_RemoveKeySlot(dsm_handle, dec_odd_keyslot->id);
  DSM_CloseSession(dsm_handle);
  dsm_handle = -1;

  INF("%s ret: %d\n", __func__, ret);
  return ret;
}

static void usage(int argc, char *argv[])
{
  INF("Usage: %s [src=] [dmx=] [vpid=] [vfmt=] [apid=] [rec=] [cas=]\n", argv[0]);
  INF("Usage: %s\n", help_vfmt);
}

int main(int argc, char **argv)
{
  int i;
  char in_file_path[512];
  char out_file_path[512];
  int vpid = 0x1fff;
  int apid = 0x1fff;
  int vfmt = -1;
  int dmx = 0;
  int src = 0;
  int is_cas = 0;

  DVR_Result_t ret;
  int v_filter_idx = -1;
  int a_filter_idx = -1;
  DVR_RecordHandle_t rec_handle;
  DVR_RecordOpenParams_t open_params;
  DVR_RecordFilterParams_t filter_params;
  DVR_RecordReceiveParams_t receive_params;

  memset(&in_file_path[0], 0, sizeof(in_file_path));
  memset(&out_file_path[0], 0, sizeof(out_file_path));
  for (i = 1; i < argc; i++) {
    if (!strncmp(argv[i], "src=", 4))
      sscanf(argv[i], "src=%d", &src);
    else if (!strncmp(argv[i], "dmx=", 4))
      sscanf(argv[i], "dmx=%d", &dmx);
    else if (!strncmp(argv[i], "ts=", 3))
      sscanf(argv[i], "ts=%s", &in_file_path[0]);
    else if (!strncmp(argv[i], "rec=", 4))
      sscanf(argv[i], "rec=%s", &out_file_path[0]);
    else if (!strncmp(argv[i], "vpid=", 5))
      sscanf(argv[i], "vpid=%i", &vpid);
    else if (!strncmp(argv[i], "vfmt=", 5))
      sscanf(argv[i], "vfmt=%i", &vfmt);
    else if (!strncmp(argv[i], "apid=", 5))
      sscanf(argv[i], "apid=%i", &apid);
    else if (!strncmp(argv[i], "cas=", 4))
      sscanf(argv[i], "cas=%i", &is_cas);
    else if (!strncmp(argv[i], "help", 4)) {
      usage(argc, argv);
      exit(0);
    }
  }

  if (argc == 1 ||
    (vpid == 0x1fff && apid == 0x1fff))
  {
    usage(argc, argv);
    exit(0);
  }

  memset(&open_params, 0, sizeof(DVR_RecordOpenParams_t));
  open_params.src = DVB_DEMUX_SOURCE_TS0 + src;
  // FTA only use one demux device
  open_params.dmx_dev_id[0] = dmx;
  open_params.dmx_dev_id[1] = dmx + 1;
  open_params.dmx_dev_id[2] = dmx + 2;
  open_params.non_sec_ringbuf_size = DVR_BUFFER_LEN;
  open_params.sec_buf_size = DVR_BUFFER_LEN;

#ifdef DEBUG_ON_PC
  FILE *in_fp = fopen(in_file_path, "rb");
  if (in_fp == NULL) {
    ERR("open %s failed!\n", in_file_path);
    return -1;
  }

  open_params.reserved[0] = fileno(in_fp);
#endif

  ret = dvr_record_open(&rec_handle, &open_params);
  if (ret != DVR_SUCCESS) {
    ERR("open record failed!\n");
    return -1;
  }

  if (is_cas) {
    uint32_t token;
    token = cas_create();
    if (token != -1) {
      dvr_record_set_key_token(rec_handle, vpid, token);
      dvr_record_set_key_token(rec_handle, apid, token);
    }
  }

  memset(&filter_params, 0, sizeof(DVR_RecordFilterParams_t));
  filter_params.pid = vpid;
  filter_params.type = DVR_STREAM_VIDEO_TYPE;
  filter_params.vfmt = vfmt;
  v_filter_idx = dvr_record_open_filter(rec_handle, &filter_params);
  if (v_filter_idx != -1) {
    ret = dvr_record_start_filter(rec_handle, v_filter_idx);
    if (ret != DVR_SUCCESS) {
      ERR("start video filter failed!\n");
      return -1;
    }
  }

  filter_params.pid = apid;
  filter_params.type = DVR_STREAM_AUDIO_TYPE;
  a_filter_idx = dvr_record_open_filter(rec_handle, &filter_params);
  if (a_filter_idx != -1) {
    ret = dvr_record_start_filter(rec_handle, a_filter_idx);
    if (ret != DVR_SUCCESS) {
      ERR("start audio filter failed!\n");
      return -1;
    }
  }

  INF("v_filter_idx: %d, a_filter_idx: %d\n", v_filter_idx, a_filter_idx);
  if (v_filter_idx == -1 && a_filter_idx == -1) {
    ERR("both video filter and audio filter are invalid\n");
    return -1;
  }

  filter_params.pid = 0;
  filter_params.type = DVR_STREAM_SECTION_TYPE;
  int filter_idx = dvr_record_open_filter(rec_handle, &filter_params);
  if (filter_idx != -1) {
    ret = dvr_record_start_filter(rec_handle, filter_idx);
    if (ret != DVR_SUCCESS) {
      ERR("start section filter failed!\n");
      return -1;
    }
  }

  ret = dvr_record_start(rec_handle);
  if (ret != DVR_SUCCESS) {
    ERR("start record failed!\n");
    return -1;
  }

  INF("vpid: %#x, vfmt: %d, apid:%#x\n", vpid, vfmt, apid);
  INF("save dvr recording to %s\n", out_file_path);
  FILE *dvr_fp = fopen(out_file_path, "wb+");
  if (dvr_fp == NULL) {
    ERR("open dump file failed\n");
    return -1;
  }

  memset(&receive_params, 0, sizeof(DVR_RecordReceiveParams_t));
  receive_params.buf = malloc(DVR_BLOCK_SIZE);
  receive_params.len = DVR_BLOCK_SIZE;
  receive_params.mode = DVR_PUSI_RECORD_MODE;
  while (1) {
    ssize_t len;
    static int time = 0;
    size_t speed = 2; //speed 2x
    static int cnt = 0;
    //if (time == 200)
      //break;
    len = dvr_record_read(rec_handle, &receive_params);
    if (len <= 0) {
      usleep(100*1000);
      time++;
      //ERR("dvr no data\n");
      continue;
    }

    time = 0;

#if 0
    if (receive_params.flags & 0x02) {
       INF("I-frame, flags: %d\n", receive_params.flags);
    }
    if (vpid != 0x1fff && !has_iframe(receive_params.flags))
      continue;
    if (vpid != 0x1fff && (cnt++ % speed) != 0)
      continue;
#endif
    if (fwrite(receive_params.buf, 1, len, dvr_fp) != len) {
      ERR("dvr write to file failed\n");
    } else {
      INF("%#zx bytes written\n", len);
    }
  }

  if (is_cas) {
    cas_destroy();
  }

  dvr_record_close_filter(rec_handle, v_filter_idx);
  dvr_record_close_filter(rec_handle, a_filter_idx);
  dvr_record_stop(rec_handle);
  dvr_record_close(rec_handle);

  if (receive_params.buf)
    free(receive_params.buf);

  fflush(dvr_fp);
  fclose(dvr_fp);

#ifdef DEBUG_ON_PC
  fclose(in_fp);
#endif

  return 0;
}
