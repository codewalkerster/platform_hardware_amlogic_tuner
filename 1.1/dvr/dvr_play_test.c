/**
 * \page dvr_play_test
 * \section Introduction
 * test code with dvr_xxx_xxx APIs.
 * It supports:
 * \li DVR playback clear and re-encrypted recording file
 *
 * \section Usage
 *
 * \li ts:  the dvr recording file path
 * \li vpid: the pid of video bitstream
 * \li apid: the pid of audio bitstream
 *
 * \code
 *    dvr_play_test [ts=] [dmx=] [vpid=] [apid=] [dump=]
 * \endcode
 *
 * \endsection
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "dvr_playback.h"
#include "libdsm.h"
#include "spi_dsm_kte_test.h"

#define INF(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)
#define ERR(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

#define DVR_BLOCK_SIZE    (188*1024)

typedef struct {
  int dsm_handle;
  uint32_t token;
  struct dsm_keyslot dec_00_keyslot;
} dvr_casinfo_t;

static dvr_casinfo_t g_casinfo;

// Manage KTE/DSM
// Return keytoken
static uint8_t CRYPTO_KEY[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
static uint32_t cas_create(void)
{
  int ret = -1;
  uint32_t token = -1;

  ret = cas_dvr_playback_open_ext(&token, CRYPTO_KEY, 16);
  g_casinfo.token = token;

  INF("%s ret: %d, token: %#x\n", __func__, ret, token);
  return token;
}

static int cas_destroy(void)
{
  int ret = -1;
  uint32_t token = g_casinfo.token;

  if (token == -1)
    return 0;

  ret = cas_dvr_playback_close(token);
  INF("%s ret: %d, token: %#x\n", __func__, ret, token);
  if (ret == 0)
    g_casinfo.token = -1;
  return ret;
}

static void handle_signal(int signal)
{
  cas_destroy();
  exit(0);
}

static void init_signal_handler(void)
{
  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = handle_signal;
  sigaction(SIGINT, &act, NULL);
}

static void usage(int argc, char *argv[])
{
  INF("Usage: %s [ts=] [dmx=] [vpid=] [apid=] [dump=] [is_cas=]\n", argv[0]);
}

int main(int argc, char **argv)
{
  int i;
  char play_file_path[512];
  char dump_file_path[512];
  int vpid = 0x1fff;
  int apid = 0x1fff;
  int dmx = 0;
  int is_cas = 0;

  uint8_t *buf = NULL;
  DVR_Result_t ret;
  DVR_PlaybackHandle_t play_handle;
  DVR_PlaybackOpenParams_t open_params;

  init_signal_handler();
  memset(&play_file_path[0], 0, sizeof(play_file_path));
  memset(&dump_file_path[0], 0, sizeof(dump_file_path));
  for (i = 1; i < argc; i++) {
    if (!strncmp(argv[i], "ts=", 3))
      sscanf(argv[i], "ts=%s", &play_file_path[0]);
    else if (!strncmp(argv[i], "dmx=", 4))
      sscanf(argv[i], "dmx=%d", &dmx);
    else if (!strncmp(argv[i], "dump=", 5))
      sscanf(argv[i], "dump=%s", &dump_file_path[0]);
    else if (!strncmp(argv[i], "vpid=", 5))
      sscanf(argv[i], "vpid=%i", &vpid);
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

  FILE *play_fp = fopen(play_file_path, "rb+");
  if (play_fp == NULL) {
    INF("open playback file %s failed\n", play_file_path);
    exit(0);
  }

  memset(&open_params, 0, sizeof(DVR_PlaybackOpenParams_t));
  FILE *dump_fp = fopen(dump_file_path, "wb+");
  if (dump_fp == NULL) {
    INF("open dump file %s failed\n", dump_file_path);
    open_params.reserved[0]= -1;
  } else {
    open_params.reserved[0]= fileno(dump_fp);
  }

  open_params.dmx_dev_id = dmx;
  ret = dvr_playback_open(&play_handle, &open_params);
  if (ret != DVR_SUCCESS) {
    ERR("open dvr playback failed!\n");
    return -1;
  }

  if (is_cas) {
    uint32_t token = cas_create();
    if (token != -1) {
      dvr_playback_set_key_token(play_handle, vpid, token);
      dvr_playback_set_key_token(play_handle, apid, token);
    }
  }

  ret = dvr_playback_start(play_handle);
  if (ret != DVR_SUCCESS) {
    ERR("start dvr playback failed!\n");
    return -1;
  }

  INF("start dvr playback %s, vpid: %#x, apid: %#x\n", play_file_path, vpid, apid);
  buf = malloc(DVR_BLOCK_SIZE);
  assert(buf);

  while (1) {
    size_t actual = fread(buf, 1, DVR_BLOCK_SIZE, play_fp);
    if (actual <= 0) {
      INF("dvr playback done, exit\n");
      break;
    }

    do {
      size_t act_write = dvr_playback_write(play_handle, buf, actual);
      if (act_write == -1)
        break;
      actual -= act_write;
    } while (actual > 0);
  }

  if (is_cas) {
    cas_destroy();
  }

  dvr_playback_stop(play_handle);
  dvr_playback_close(play_handle);

  if (buf)
    free(buf);

  if (play_fp)
    fclose(play_fp);

  if (dump_fp)
    fclose(dump_fp);

  return 0;
}
