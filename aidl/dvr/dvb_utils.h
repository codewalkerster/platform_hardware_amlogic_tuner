/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * dvb utility functions
 */
#ifndef DVB_UTILS_H_
#define DVB_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**Demux input source.*/
typedef enum
{
    DVB_DEMUX_SOURCE_TS0,  /**< Hardware TS input port 0.*/
    DVB_DEMUX_SOURCE_TS1,  /**< Hardware TS input port 1.*/
    DVB_DEMUX_SOURCE_TS2,  /**< Hardware TS input port 2.*/
    DVB_DEMUX_SOURCE_TS3,  /**< Hardware TS input port 3.*/
    DVB_DEMUX_SOURCE_TS4,  /**< Hardware TS input port 4.*/
    DVB_DEMUX_SOURCE_TS5,  /**< Hardware TS input port 5.*/
    DVB_DEMUX_SOURCE_TS6,  /**< Hardware TS input port 6.*/
    DVB_DEMUX_SOURCE_TS7,  /**< Hardware TS input port 7.*/
    DVB_DEMUX_SOURCE_DMA0, /**< DMA input port 0.*/
    DVB_DEMUX_SOURCE_DMA1, /**< DMA input port 1.*/
    DVB_DEMUX_SOURCE_DMA2, /**< DMA input port 2.*/
    DVB_DEMUX_SOURCE_DMA3, /**< DMA input port 3.*/
    DVB_DEMUX_SOURCE_DMA4, /**< DMA input port 4.*/
    DVB_DEMUX_SOURCE_DMA5, /**< DMA input port 5.*/
    DVB_DEMUX_SOURCE_DMA6, /**< DMA input port 6.*/
    DVB_DEMUX_SOURCE_DMA7,  /**< DMA input port 7.*/
    DVB_DEMUX_SECSOURCE_DMA0, /**< DMA secure port 0.*/
    DVB_DEMUX_SECSOURCE_DMA1, /**< DMA secure port 1.*/
    DVB_DEMUX_SECSOURCE_DMA2, /**< DMA secure port 2.*/
    DVB_DEMUX_SECSOURCE_DMA3, /**< DMA secure port 3.*/
    DVB_DEMUX_SECSOURCE_DMA4, /**< DMA secure port 4.*/
    DVB_DEMUX_SECSOURCE_DMA5, /**< DMA secure port 5.*/
    DVB_DEMUX_SECSOURCE_DMA6, /**< DMA secure port 6.*/
    DVB_DEMUX_SECSOURCE_DMA7,  /**< DMA secure port 7.*/
    DVB_DEMUX_SOURCE_DMA0_1,  /**< DMA input port 0_1.*/
    DVB_DEMUX_SOURCE_DMA1_1,   /**< DMA input port 1_1.*/
    DVB_DEMUX_SOURCE_DMA2_1,  /**< DMA input port 2_1.*/
    DVB_DEMUX_SOURCE_DMA3_1,   /**< DMA input port 3_1.*/
    DVB_DEMUX_SOURCE_DMA4_1,  /**< DMA input port 4_1.*/
    DVB_DEMUX_SOURCE_DMA5_1,   /**< DMA input port 5_1.*/
    DVB_DEMUX_SOURCE_DMA6_1,  /**< DMA input port 6_1.*/
    DVB_DEMUX_SOURCE_DMA7_1,   /**< DMA input port 7_1.*/
    DVB_DEMUX_SECSOURCE_DMA0_1, /**< DMA secure port 0_1.*/
    DVB_DEMUX_SECSOURCE_DMA1_1, /**< DMA secure port 1_1.*/
    DVB_DEMUX_SECSOURCE_DMA2_1, /**< DMA secure port 2_1.*/
    DVB_DEMUX_SECSOURCE_DMA3_1, /**< DMA secure port 3_1.*/
    DVB_DEMUX_SECSOURCE_DMA4_1, /**< DMA secure port 4_1.*/
    DVB_DEMUX_SECSOURCE_DMA5_1, /**< DMA secure port 5_1.*/
    DVB_DEMUX_SECSOURCE_DMA6_1, /**< DMA secure port 6_1.*/
    DVB_DEMUX_SECSOURCE_DMA7_1,  /**< DMA secure port 7_1.*/
    DVB_DEMUX_SOURCE_TS0_1, /**< DMA secure port 0_1.*/
    DVB_DEMUX_SOURCE_TS1_1, /**< DMA secure port 1_1.*/
    DVB_DEMUX_SOURCE_TS2_1, /**< DMA secure port 2_1.*/
    DVB_DEMUX_SOURCE_TS3_1, /**< DMA secure port 3_1.*/
    DVB_DEMUX_SOURCE_TS4_1, /**< DMA secure port 4_1.*/
    DVB_DEMUX_SOURCE_TS5_1, /**< DMA secure port 5_1.*/
    DVB_DEMUX_SOURCE_TS6_1, /**< DMA secure port 6_1.*/
    DVB_DEMUX_SOURCE_TS7_1, /**< DMA secure port 7_1.*/
} DVB_DemuxSource_t;

/**
 * Set the demux's input source.
 * \param dmx_idx Demux device's index.
 * \param src The demux's input source.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int dvb_set_demux_source(int dmx_idx, DVB_DemuxSource_t src);

/**
 * Set the demux's secure buffer.
 * \param dmx_idx Demux device's index.
 * \param sec_buf The secure buffer.
 * \param len The secure buffer length.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int dvb_set_secure_buffer(int dmx_idx, uint8_t *sec_buf, size_t len);

/**
 * Open the dvr device.
 * \param dev_dev_id Dvr device's index.
 * \param rw 1 means read only, 0 means write only
 * \retval fd On success.
 * \retval -1 On error.
 */
int dvb_dvr_device_open(int dvr_dev_id, int rw);

/**
 * Setting record ringbuffer for the normal dvr device.
 * \param fd Dvr device's file descriptor.
 * \param len Dvr ringbuffer length.
 * \retval 0 On success.
 * \retval -1 On error.
 */
int dvb_dvr_set_ringbuffer(int fd, size_t len);
#ifdef __cplusplus
}
#endif

#endif /*DVB_UTILS_H_*/

