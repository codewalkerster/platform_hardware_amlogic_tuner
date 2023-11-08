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

#define INF(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)
#define ERR(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

#define has_pusi(_m_)    ((_m_) & DVR_INDEX_PUSI)
#define has_iframe(_m_) ((_m_) & DVR_INDEX_IFRAME)
#define has_pts(_m_)    ((_m_) & DVR_INDEX_PTS)

#define DVR_MAX_PUSI_LEN    (20*188*1024)

static char *help_vfmt =
  "\n\t0:DVR_VIDEO_FORMAT_MPEG2" /**< MPEG2 video.*/
  "\n\t1:DVR_VIDEO_FORMAT_H264" /**< H264.*/
  "\n\t2:DVR_VIDEO_FORMAT_HEVC"; /**<HEVC.*/

static void usage(int argc, char *argv[])
{
  INF("Usage: %s [src=] [dmx=] [vpid=] [vfmt=] [apid=] [rec=]\n", argv[0]);
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
  open_params.none_sec_ringbuf_size = DVR_MAX_PUSI_LEN;

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
  receive_params.buf = malloc(DVR_MAX_PUSI_LEN);
  receive_params.len = DVR_MAX_PUSI_LEN;
  receive_params.mode = DVR_PUSI_RECORD_MODE;
  while (1) {
    ssize_t len;
    static int time = 0;
    size_t speed = 2; //speed 2x
    static int cnt = 0;
    if (time == 50)
      break;
    len = dvr_record_read(rec_handle, &receive_params);
    if (len <= 0) {
      usleep(100*1000);
      time++;
      ERR("dvr no data\n");
      continue;
    }

    time = 0;

#if 0
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
