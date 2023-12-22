#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

#ifndef DEBUG_ON_PC
#include "dmx.h"
#endif
#include "dvr_types.h"
#include "dvb_utils.h"

#ifndef DEBUG_ON_PC
/**
 * Set the demux's input source.
 * \param dmx_idx Demux device's index.
 * \param src The demux's input source.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int dvb_set_demux_source(int dmx_idx, DVB_DemuxSource_t src)
{
    char node[20] = {0};
    int r = 0;
    int source = 0;
    int input = 0;

    snprintf(node, sizeof(node), "/dev/dvb0.demux%d", dmx_idx);
    int fd = open(node, O_RDONLY);
    if (fd == -1) {
      DVR_ERROR("dvb_set_demux_source open \"%s\" failed, error:%d", node, errno);
      return -1;
    }

    if (src <= DVB_DEMUX_SOURCE_TS7) {
      source = FRONTEND_TS0 + src - DVB_DEMUX_SOURCE_TS0;
      input = INPUT_DEMOD;
    } else if (src >= DVB_DEMUX_SOURCE_DMA0 &&
      src <= DVB_DEMUX_SOURCE_DMA7) {
      source = DMA_0 + src - DVB_DEMUX_SOURCE_DMA0;
      input = INPUT_LOCAL;
    } else if (src >= DVB_DEMUX_SECSOURCE_DMA0 &&
      src <= DVB_DEMUX_SECSOURCE_DMA7) {
      source = DMA_0 + src - DVB_DEMUX_SECSOURCE_DMA0;
      input = INPUT_LOCAL_SEC;
    } else if (src >= DVB_DEMUX_SOURCE_DMA0_1 &&
      src <= DVB_DEMUX_SOURCE_DMA7_1) {
      source = DMA_0_1 + src - DVB_DEMUX_SOURCE_DMA0_1;
      input = INPUT_LOCAL;
    } else if (src >= DVB_DEMUX_SECSOURCE_DMA0_1 &&
      src <= DVB_DEMUX_SECSOURCE_DMA7_1) {
      source = DMA_0_1 + src - DVB_DEMUX_SECSOURCE_DMA0_1;
      input = INPUT_LOCAL_SEC;
    } else if (src >= DVB_DEMUX_SOURCE_TS0_1 &&
      src <= DVB_DEMUX_SOURCE_TS7_1) {
      source = FRONTEND_TS0_1 + src - DVB_DEMUX_SOURCE_TS0_1;
      input = INPUT_DEMOD;
    } else {
      assert(0);
    }

    if (ioctl(fd, DMX_SET_INPUT, input) == -1)
    {
      DVR_INFO("dvb_set_demux_source ioctl DMX_SET_INPUT:%d error:%d", input, errno);
      r = -1;
    }
    else
    {
      DVR_INFO("dvb_set_demux_source ioctl succeeded src:%d DMX_SET_INPUT:%d dmx_idx:%d", src, input, dmx_idx);
      r = 0;
    }
    if (ioctl(fd, DMX_SET_HW_SOURCE, source) == -1)
    {
      DVR_INFO("dvb_set_demux_source ioctl DMX_SET_HW_SOURCE:%d error:%d", source, errno);
      r = -1;
    }
    else
    {
      DVR_INFO("dvb_set_demux_source ioctl succeeded src:%d DMX_SET_HW_SOURCE:%d dmx_idx:%d", src, source, dmx_idx);
      r = 0;
    }
    close(fd);

    return r;
}

/**
 * Set the demux's secure buffer.
 * \param dmx_idx Demux device's index.
 * \param sec_buf The secure buffer.
 * \param len The secure buffer length.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int dvb_set_secure_buffer(int dmx_idx, uint8_t *sec_buf, size_t len)
{
    char node[20] = {0};
    int ret = 0;

    snprintf(node, sizeof(node), "/dev/dvb0.demux%d", dmx_idx);
    int fd = open(node, O_RDONLY);
    if (fd == -1) {
      DVR_ERROR("%s open \"%s\" failed, error:%d", __func__, node, errno);
      return -1;
    }

    struct dmx_sec_mem sec_mem;
    sec_mem.buff = (uint32_t)sec_buf;
    sec_mem.size = len;
    ret = ioctl(fd, DMX_SET_SEC_MEM, &sec_mem);
    close(fd);
    if (ret == -1) {
      DVR_ERROR("%s ioctl DMX_SET_SEC_MEM error: %d", __func__, errno);
      return -1;
    } else {
      DVR_INFO("%s ioctl DMX_SET_SEC_MEM succeed. sec_mem: %#x, size: %#x",
        __func__, (size_t)sec_buf, len);
      return 0;
    }
}

/**
 * Open the dvr device.
 * \param dev_dev_id Dvr device's index.
 * \param rw 1 means read only, 0 means write only
 * \retval fd On success.
 * \retval -1 On error.
 */
int dvb_dvr_device_open(int dvr_dev_id, int rw)
{
  int fd;
  int flags = 0;
  char dev_name[32];

  memset(dev_name, 0, sizeof(dev_name));
  snprintf(dev_name, sizeof(dev_name), "/dev/dvb0.dvr%d", dvr_dev_id);
  if (rw) {
    flags = O_RDONLY;
  } else {
    flags = O_WRONLY;
  }
  fd = open(dev_name, flags);
  if (fd == -1) {
    DVR_ERROR("%s cannot open \"%s\" (%s)", __func__, dev_name, strerror(errno));
    return fd;
  }

  DVR_INFO("%s open %s succeed, fd: %d, rw: %d", __func__, dev_name, fd, rw);

  if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK, 0) < 0) {
    DVR_ERROR("%s set nonblock flag failed \"%s\"", __func__ ,strerror(errno));
  }

  return fd;
}

/**
 * Setting record ringbuffer for the normal dvr device.
 * \param fd Dvr device's file descriptor.
 * \param len Dvr ringbuffer length.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int dvb_dvr_set_ringbuffer(int fd, size_t len)
{
  if (fd < 0)
    return -1;

  if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK, 0) < 0) {
    DVR_ERROR("%s set nonblock flag failed \"%s\"", __func__ ,strerror(errno));
    return -1;
  }

  if (ioctl(fd, DMX_SET_BUFFER_SIZE, len) == -1) {
    DVR_ERROR("%s set dvr ringbuf size failed (%s) buf_size:%d",
      __func__, strerror(errno), len);
    return -1;
  }

  DVR_INFO("%s set fd: %d ringbuf size success buf_size:%#x",
      __func__, fd, len);

  return 0;
}
#endif
