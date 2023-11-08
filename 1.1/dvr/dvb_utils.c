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
#endif
