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
static uint32_t cas_create(void)
{
  uint32_t param = 0;
  int ret = -1;
  uint32_t token = -1;
  uint32_t dsm_handle;
  struct dsm_keyslot *dec_00_keyslot = &g_casinfo.dec_00_keyslot;

  memset(&g_casinfo, 0, sizeof(dvr_casinfo_t));
  g_casinfo.dsm_handle = -1;
  g_casinfo.token = -1;

  dsm_handle = DSM_OpenSession(param);
  ret = DSM_GenerateToken(dsm_handle, &token);

  // Prepare 00 kte/keyslot for cas dvr decryption
  dec_00_keyslot->id = 1;
  dec_00_keyslot->algo = DSM_ALGO_AES_CBC_CLR_END;
  dec_00_keyslot->parity = DSM_PARITY_NONE;
  dec_00_keyslot->is_enc = 0;
  ret |= DSM_AddKeySlot(dsm_handle, dec_00_keyslot);
  ret |= DSM_SetProperty(dsm_handle,
                    DSM_PROP_DEC_SLOT_READY,
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
  struct dsm_keyslot *dec_00_keyslot = &g_casinfo.dec_00_keyslot;

  if (dsm_handle == -1) {
    ERR("%s invalid DSM handle\n", __func__);
    return -1;
  }

  ret = DSM_RemoveKeySlot(dsm_handle, dec_00_keyslot->id);
  DSM_CloseSession(dsm_handle);
  dsm_handle = -1;

  INF("%s ret: %d\n", __func__, ret);
  return ret;
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
