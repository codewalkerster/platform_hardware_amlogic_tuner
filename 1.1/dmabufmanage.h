/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#ifndef _DMABUF_MANAGE_H_
#define _DMABUF_MANAGE_H_

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/types.h>

#define AML_DMA_BUF_MANAGER_VERSION  1
#define DMA_BUF_MANAGER_MAX_BUFFER_LEN 320

enum dmabuf_manage_type {
	DMA_BUF_TYPE_INVALID,
	DMA_BUF_TYPE_SECMEM,
	DMA_BUF_TYPE_DMX_ES,
	DMA_BUF_TYPE_DMABUF,
	DMA_BUF_TYPE_MAX
};

struct filter_info {
	__u32 token;
	__u32 filter_fd;
	__u32 release;
};

struct dmabuf_dmx_sec_es_data {
	__u8 pts_dts_flag;
	__u64 video_pts;
	__u64 video_dts;
	__u32 buf_start;
	__u32 buf_end;
	__u32 data_start;
	__u32 data_end;
	__u32 buf_rp;
	__u64 av_handle;
	__u32 token;
	__u32 extend0;
	__u32 extend1;
	__u32 extend2;
	__u32 extend3;
};

struct secmem_block {
	__u32 paddr;
	__u32 size;
	__u32 handle;
};

struct dmabuf_manage_buffer {
	__u32 type;
	__u32 fd;
	__u32 paddr;
	__u32 size;
	__u32 handle;
	__u32 flags;
	__u32 extend;
	union {
		struct dmabuf_dmx_sec_es_data dmx_es;
		__u8 data[DMA_BUF_MANAGER_MAX_BUFFER_LEN];
	} buffer;
};

/**Function:            dmabufmanage_support
 * Input parameters:    void
 * Return:              1 support 0 not support
*/
int dmabufmanage_support(void);

/**Function:            dmabufmanage_init
 * Input parameters:    void
 * Return:              failure null success return context
*/
void * dmabufmanage_init();

/**Function:            dmabufmanage_uninit
 * Input parameters:    context [IN/OUT]: which return from dmabufmanage_init
 * Return:              success 0 failure -1
*/
int dmabufmanage_uninit(const void *context);

/**Function:            dmabufmanage_export
 * Input parameters:    context     [IN]: which return from dmabufmanage_init
 *                      paddr       [IN]: paddr of export buffer
 *                      size        [IN]: size of the paddr
 *                      privHandle  [IN]: privHandle can be 0
 * Return:              success valid fd failure -1
*/
int dmabufmanage_export(const void *context, int paddr, int size, int privHandle);

/**Function:            dmabufmanage_exportByInfo
 * Input parameters:    context     [IN]: which return from dmabufmanage_init
 *                      type        [IN]: type of export buffer from dmabuf_manage_type
 *                      info        [IN]: private information of dmabuf_manage's type
 * Return:              success valid fd failure -1
*/
int dmabufmanage_exportByInfo(const void *context, int type, const void *info);

/**Function:            dmabufmanage_getPrivHandle
 * Input parameters:    context     [IN]: which return from dmabufmanage_init
 *                      type        [IN]: type of export buffer from dmabuf_manage_type
 *                      fd          [IN]: which return from export interface
 *                      privHandle  [IN/OUT]: Which setting when export
 * Return:              success 0 failure -1
*/
int dmabufmanage_getPrivHandle(const void *context, int type, int fd, int *privHandle);

/**Function:            dmabufmanage_getPaddr
 * Input parameters:    context     [IN]: which return from dmabufmanage_init
 *                      type        [IN]: type of export buffer from dmabuf_manage_type
 *                      fd          [IN]: which return from export interface
 *                      paddr       [IN/OUT]: Which setting when export
 *                      size        [IN/OUT]: Which setting when export
 * Return:              success 0 failure -1
*/
int dmabufmanage_getPaddr(const void *context, int type, int fd, int *paddr, int *size);

/**Function:            dmabufmanage_getBufInfo
 * Input parameters:    context     [IN]: which return from dmabufmanage_init
 *                      type        [IN]: type of export buffer from dmabuf_manage_type
 *                      fd          [IN]: which return from export interface
 *                      info        [IN/OUT]: Which setting when export
 * Return:              success 0 failure -1
*/
int dmabufmanage_getBufInfo(const void *context, int type, int fd, void *info);

/**Function:            dmabufmanage_setFilterInfo
 * Input parameters:    context     [IN]: which return from dmabufmanage_init
 *                      info        [IN]: struct filter_info * info
 * Return:              success 0 failure -1
*/
int dmabufmanage_setFilterInfo(const void *context, void *info);

/**Function:            dmabufmanage_allocDmabuf
 * Input parameters:    context     [IN]: which return from dmabufmanage_init
 *                      size        [IN]: Allocate continuous memory size 4K aligned
 *                      flags  	    [IN]: not used now
 * Return:              success return the correct paddress failed <= 0
*/
unsigned long dmabufmanage_allocDmabuf(const void *context, unsigned int size, int flags);

/**Function:            dmabufmanage_freeDmabuf
 * Input parameters:    context     [IN]: which return from dmabufmanage_init
 *                      paddr       [IN]: Allocated paddress
 * Return:              success 0 failure -1
*/
int dmabufmanage_freeDmabuf(const void *context, unsigned long paddr);

#ifdef  __cplusplus
}
#endif

#endif /** _DMABUF_MANAGE_H_ **/
