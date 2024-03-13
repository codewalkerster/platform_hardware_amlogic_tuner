#define LOG_TAG "AmCIModuleApi"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <utils/Log.h>
#include "AmCIModuleApi.h"
#include "dmx.h"

//define the offset of the media read/write buffer.
#define USB_CIMODULE_MEDIA_READ_MAPP_OFFSET  0x00
#define USB_CIMODULE_MEDIA_WRITE_MAPP_OFFSET 0x00

#define USB_CIMODULE_COMMAND_READ_MAPP_OFFSET  0x00
#define USB_CIMODULE_COMMAND_WRITE_MAPP_OFFSET 0x2000

static unsigned int gs_dwMaxCmdWriteSize;
static unsigned int gs_dwMaxCmdReadSize;
static unsigned char *gs_pbCmdMmapWriteBuf;
static unsigned char *gs_pbCmdMmapReadBuf;

static unsigned int gs_dwMaxMediaWriteSize;
static unsigned int gs_dwMaxMediaReadSize;
static unsigned char *gs_pbMediaMmapWriteBuf;
static unsigned char *gs_pbMediaMmapReadBuf;

//ci module ioctl command
#define CIMODULE_IOC_MAGIC          				'c'
#define CIMODULE_GET_DRIVER_VERSION  				_IOR(CIMODULE_IOC_MAGIC, 0, uint32_t)
#define CIMODULE_GET_USB_CIMODULE_INFO    			_IOR(CIMODULE_IOC_MAGIC, 1, struct usb_cimodule_info)
#define CIMODULE_RESET   		 	    		_IO(CIMODULE_IOC_MAGIC, 2)
#define CIMODULE_CANCEL_CMD_TRANSFER    			_IO(CIMODULE_IOC_MAGIC, 3)
#define CIMODULE_CANCEL_MEDIA_TRANSFER  			_IO(CIMODULE_IOC_MAGIC, 4)

AmCIModuleApi::AmCIModuleApi() {
}

AmCIModuleApi::~AmCIModuleApi() {

}

unsigned char * AmCIModuleApi::cimodule_cmd_intf_mmap_readbuf(int i_fd, unsigned int i_dwMaxCmdReadBufSize)
{
	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return NULL;
	}

	if (i_dwMaxCmdReadBufSize <= 0 || i_dwMaxCmdReadBufSize > USB_CIMODULE_COMMAND_MAX_SIZE)
	{
		ALOGE("parameter is error,the max command read buffer size must be less %08x", USB_CIMODULE_COMMAND_MAX_SIZE);
		return NULL;
	}

	if (gs_pbCmdMmapReadBuf)
	{
		ALOGE("command read buffer is mmaped before");
		return gs_pbCmdMmapReadBuf;
	}
	gs_pbCmdMmapReadBuf = (unsigned char *)mmap(NULL, i_dwMaxCmdReadBufSize, PROT_READ, MAP_SHARED, i_fd, USB_CIMODULE_COMMAND_READ_MAPP_OFFSET);
	if (MAP_FAILED == gs_pbCmdMmapReadBuf)
	{
		ALOGE("mmap command read buffer error: [%d]%s", -errno, strerror(errno));
		return NULL;
	}
	gs_dwMaxCmdReadSize = i_dwMaxCmdReadBufSize;

	ALOGD("command read buffer mapp successfully");
	return gs_pbCmdMmapReadBuf;
}

unsigned char * AmCIModuleApi::cimodule_cmd_intf_mmap_writebuf(int i_fd, unsigned int i_dwMaxCmdBufWriteSize)
{
	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return NULL;
	}

	if (i_dwMaxCmdBufWriteSize <= 0 || i_dwMaxCmdBufWriteSize > USB_CIMODULE_COMMAND_MAX_SIZE)
	{
		ALOGE("parameter is error,the command write buffer size must be less %08x", USB_CIMODULE_COMMAND_MAX_SIZE);
		return NULL;
	}

	if (gs_pbCmdMmapWriteBuf)
	{
		ALOGE("command write buffer is mmaped before");
		return gs_pbCmdMmapWriteBuf;
	}

	gs_pbCmdMmapWriteBuf = (unsigned char *)mmap(NULL, i_dwMaxCmdBufWriteSize, PROT_WRITE, MAP_SHARED, i_fd, USB_CIMODULE_COMMAND_WRITE_MAPP_OFFSET);
	if (MAP_FAILED == gs_pbCmdMmapWriteBuf)
	{
		ALOGE("mmap command write buffer error: [%d]%s", -errno, strerror(errno));
		return NULL;
	}
	gs_dwMaxCmdWriteSize = i_dwMaxCmdBufWriteSize;

	ALOGD("command write buffer mapp successfully");
	return gs_pbCmdMmapWriteBuf;
}

void AmCIModuleApi::cimodule_cmd_intf_munmap_readbuf(unsigned char *i_pbBuf)
{
	if (!i_pbBuf)
	{
		ALOGE("parameter is error,the input buffer is NULL");
		return;
	}

	if (i_pbBuf != gs_pbCmdMmapReadBuf)
	{
		ALOGE("input buffer is not mmaped before");
		return;
	}

	munmap(gs_pbCmdMmapReadBuf, gs_dwMaxCmdReadSize);
	gs_pbCmdMmapReadBuf = NULL;

	return;
}

void AmCIModuleApi::cimodule_cmd_intf_munmap_writebuf(unsigned char *i_pbBuf)
{
	if (!i_pbBuf)
	{
		ALOGE("parameter is error,the input buffer is NULL");
		return;
	}

	if (i_pbBuf != gs_pbCmdMmapWriteBuf)
	{
		ALOGE("input buffer is not mmaped before");
		return;
	}
	munmap(gs_pbCmdMmapWriteBuf, gs_dwMaxCmdWriteSize);
	gs_pbCmdMmapWriteBuf = NULL;
	return;
}

int AmCIModuleApi::cimodule_cmd_intf_read(int i_fd, unsigned char *o_pbBuf, unsigned int i_dwRqstLen, unsigned int* o_pdwActualLen,
unsigned int i_dwTimeout)
{
	int ret;

	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return ERROR_INVALID_HANDLE;
	}

	if (!o_pbBuf)
	{
		ALOGE("parameter is error,the buf is NULL");
		return ERROR_USB_PARAM;
	}

	if (i_dwTimeout != (unsigned int)-1)
	{
		ALOGE("invalid timeout param,must be -1");
		return ERROR_USB_PARAM;
	}

	if (i_dwRqstLen <= 0 || i_dwRqstLen > gs_dwMaxCmdReadSize)
	{
		ALOGE("parameter is error,the size must be less %08x", gs_dwMaxCmdReadSize);
		return ERROR_USB_PARAM;
	}

	if (o_pbBuf != gs_pbCmdMmapReadBuf)
	{
		ALOGE("the buffer must be mmaped before read command");
		return ERROR_USB_PARAM;
	}

	ret = read(i_fd, o_pbBuf, i_dwRqstLen);
	if (ret < 0)
	{
		ALOGE("read command error: ret = %d, [%d]%s", ret, -errno, strerror(errno));
	    return -errno;
	}
	*o_pdwActualLen = ret;

	return 0;
}

int AmCIModuleApi::cimodule_cmd_intf_write(int i_fd, unsigned char *i_pbBuf, unsigned int i_dwRqstLen, unsigned int* o_pdwActualLen,
unsigned int i_dwTimeout)
{
	int ret;

	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return ERROR_INVALID_HANDLE;
	}

	if (!i_pbBuf)
	{
		ALOGE("parameter is error,the buf is NULL");
		return ERROR_USB_PARAM;
	}

	if (i_dwTimeout != (unsigned int)-1)
	{
		ALOGE("invalid timeout param,must be -1");
		return ERROR_USB_PARAM;
	}

	if (i_dwRqstLen <= 0 || i_dwRqstLen > gs_dwMaxCmdWriteSize)
	{
		ALOGE("parameter is error,the size must be less %08x", gs_dwMaxCmdWriteSize);
		return ERROR_USB_PARAM;
	}

	if (i_pbBuf != gs_pbCmdMmapWriteBuf)
	{
		ALOGE("the buffer must be mmaped before write command");
		return ERROR_USB_PARAM;
	}

	ret = write(i_fd, i_pbBuf, i_dwRqstLen);
	if ((unsigned int)ret != i_dwRqstLen)
	{
		ALOGE("send command error: ret = %d, [%d]%s", ret, -errno, strerror(errno));
	    return -errno;
	}
	*o_pdwActualLen = ret;

	return 0;
}

int AmCIModuleApi::cimodule_media_intf_open(const char *i_pbDevpath, int i_nflags)
{
	int fd = -1;

	if (!i_pbDevpath)
	{
		ALOGE("parameter is error,the input Devpath is NULL");
		return -1;
	}

	fd = open(i_pbDevpath, i_nflags);
	if (fd < 0)
	{
		ALOGE("media interface open error: [%d]%s", -errno, strerror(errno));
		return -1;
	}

	return fd;
}

int AmCIModuleApi::cimodule_media_intf_close(int i_fd)
{
	int ret;

	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return ERROR_INVALID_HANDLE;
	}

	// ret = ioctl(i_fd, CIMODULE_CANCEL_MEDIA_TRANSFER, NULL);
	// if (ret < 0)
	// {
	// 	ALOGE("cancel media transfer failed: [%d]%s", -errno, strerror(errno));
	// 	return -errno;
	// }

	ret = close(i_fd);
	if (ret != 0)
	{
		ALOGE("close media interface handle(%d) failed: [%d]%s", i_fd, -errno, strerror(errno));
		return ret;
	}

	ALOGD("close media interface handle(%d) successfully", i_fd);
	return ret;
}

unsigned char * AmCIModuleApi::cimodule_media_intf_readbuf(int i_fd, unsigned int i_dwMaxMediaReadBufSize)
{
	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return NULL;
	}

	if (i_dwMaxMediaReadBufSize <= 0 || i_dwMaxMediaReadBufSize > USB_CIMODULE_MEDIA_MAX_SIZE)
	{
		ALOGE("parameter is error,the max media read buffer size must be less %08x", USB_CIMODULE_MEDIA_MAX_SIZE);
		return NULL;
	}

	gs_pbMediaMmapReadBuf = (unsigned char *)malloc(i_dwMaxMediaReadBufSize);
	if (MAP_FAILED == gs_pbMediaMmapReadBuf)
	{
		ALOGE("mmap media read buffer error: [%d]%s", -errno, strerror(errno));
		return NULL;
	}
	gs_dwMaxMediaReadSize = i_dwMaxMediaReadBufSize;

	ALOGD("media read buffer mapp successfully");
	return gs_pbMediaMmapReadBuf;
}

unsigned char * AmCIModuleApi::cimodule_media_intf_writebuf(int i_fd, unsigned int i_dwMaxMediaBufWriteSize)
{
	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return NULL;
	}

	if (i_dwMaxMediaBufWriteSize <= 0 || i_dwMaxMediaBufWriteSize > USB_CIMODULE_MEDIA_MAX_SIZE)
	{
		ALOGE("parameter is error,the media write buffer size must be less %08x", USB_CIMODULE_MEDIA_MAX_SIZE);
		return NULL;
	}

	gs_pbMediaMmapWriteBuf = (unsigned char *)malloc(i_dwMaxMediaBufWriteSize);
	if (MAP_FAILED == gs_pbMediaMmapWriteBuf)
	{
		ALOGE("mmap media write buffer error: [%d]%s", -errno, strerror(errno));
		return NULL;
	}
	gs_dwMaxMediaWriteSize = i_dwMaxMediaBufWriteSize;

	ALOGD("media write buffer mapp successfully");
	return gs_pbMediaMmapWriteBuf;
}

void AmCIModuleApi::cimodule_media_intf_readbuf(unsigned char *i_pbBuf)
{
	if (!i_pbBuf)
	{
		ALOGE("parameter is error,the input buffer is NULL");
		return;
	}

	if (i_pbBuf != gs_pbMediaMmapReadBuf)
	{
		ALOGE("the media read buffer is not mmaped before");
		return;
	}

	free(gs_pbMediaMmapReadBuf);
	gs_pbMediaMmapReadBuf = NULL;

	return;
}

void AmCIModuleApi::cimodule_media_intf_writebuf(unsigned char *i_pbBuf)
{
	if (!i_pbBuf)
	{
		ALOGE("parameter is error,the input buffer is NULL");
		return;
	}

	if (i_pbBuf != gs_pbMediaMmapWriteBuf)
	{
		ALOGE("the media write buffer is not mmaped before");
		return;
	}
	free(gs_pbMediaMmapWriteBuf);
	gs_pbMediaMmapWriteBuf = NULL;

	return;
}

int AmCIModuleApi::cimodule_media_intf_read(int i_fd, unsigned char *o_pbBuf, unsigned int i_dwRqstLen, unsigned int* o_pdwActualLen, unsigned int i_dwTimeout)
{
	int ret;

	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return ERROR_INVALID_HANDLE;
	}

	if (!o_pbBuf)
	{
		ALOGE("parameter is error,the buf is NULL");
		return ERROR_USB_PARAM;
	}

	if (i_dwTimeout != (unsigned int)-1)
	{
		ALOGE("invalid timeout param, must be -1");
		return ERROR_USB_PARAM;
	}

	if (i_dwRqstLen <= 0 || i_dwRqstLen > gs_dwMaxMediaReadSize)
	{
		ALOGE("parameter is error,the size must be less %08x", gs_dwMaxMediaReadSize);
		return ERROR_USB_PARAM;
	}

	if (o_pbBuf != gs_pbMediaMmapReadBuf)
	{
		ALOGE("the buffer must be mmaped before read media");
		return ERROR_USB_PARAM;
	}

	ret = read(i_fd, o_pbBuf, i_dwRqstLen);
	if (ret < 0)
	{
		ALOGE("read media error: ret = %d, [%d]%s", ret, -errno, strerror(errno));
	    return -errno;
	}

	*o_pdwActualLen = ret;
	return 0;
}

int AmCIModuleApi::cimodule_media_intf_write(int i_fd, unsigned char *i_pbBuf, unsigned int i_dwRqstLen, unsigned int* o_pdwActualLen, unsigned int i_dwTimeout)
{
	int ret;

	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return ERROR_INVALID_HANDLE;
	}

	if (!i_pbBuf)
	{
		ALOGE("parameter is error,the buf is NULL");
		return ERROR_USB_PARAM;
	}

	if (i_dwTimeout != (unsigned int)-1)
	{
		ALOGE("invalid timeout param,must be -1");
		return ERROR_USB_PARAM;
	}

	if (i_dwRqstLen <= 0 || i_dwRqstLen > gs_dwMaxMediaWriteSize)
	{
		ALOGE("parameter is error,the size must be less %08x", gs_dwMaxMediaWriteSize);
		return ERROR_USB_PARAM;
	}

	if (i_pbBuf != gs_pbMediaMmapWriteBuf)
	{
		ALOGE("the buffer must be mmaped before write media");
		return ERROR_USB_PARAM;
	}

	ret = write(i_fd, i_pbBuf, i_dwRqstLen);
	if (ret < 0)
	{
		ALOGE("write media error: ret = %d, [%d]%s", ret, -errno, strerror(errno));
	    return -errno;
	}

	*o_pdwActualLen = ret;
	return 0;
}

int AmCIModuleApi::cimodule_get_driver_version(int i_fd, unsigned int *o_pdwDriverVersion)
{
	int ret;

	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return ERROR_INVALID_HANDLE;
	}

	if (!o_pdwDriverVersion)
	{
		ALOGE("parameter is error,the buf is NULL");
		return ERROR_USB_PARAM;
	}

	ret = ioctl(i_fd, CIMODULE_GET_DRIVER_VERSION, o_pdwDriverVersion);
	if (ret < 0)
	{
		ALOGE("get ci module driver version error: ret = %d, [%d]%s", ret, -errno, strerror(errno));
	    return -errno;
	}
	return 0;
}

int AmCIModuleApi::cimodule_get_usb_cimodule_info(int i_fd, struct usb_cimodule_info *o_ptUsbCiModuleInfo)
{
	int ret;

	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return ERROR_INVALID_HANDLE;
	}

	if (!o_ptUsbCiModuleInfo)
	{
		ALOGE("parameter is error,the buf is NULL");
		return ERROR_USB_PARAM;
	}

	ret = ioctl(i_fd, CIMODULE_GET_USB_CIMODULE_INFO, o_ptUsbCiModuleInfo);
	if (ret < 0)
	{
		ALOGE("get usb cimodule info error: ret = %d, [%d]%s", ret, -errno, strerror(errno));
	    return -errno;
	}
	return 0;
}

int AmCIModuleApi::cimodule_reset(int i_fd)
{
	int ret;

	if (i_fd <= 0)
	{
		ALOGE("invalid fd");
		return ERROR_INVALID_HANDLE;
	}

	ret = ioctl(i_fd, CIMODULE_RESET, NULL);
	if (ret < 0)
	{
		ALOGE("reset usb ci module: ret = %d, [%d]%s", ret, -errno, strerror(errno));
		return -errno;
	}

	return 0;
}

int AmCIModuleApi::set_dvb_source(int id, int input, int source) {
    char node[32] = {0};
    int r = -1;
    ALOGD("set_dvb_source id= %d source %d", id,source);
    snprintf(node, sizeof(node), "/dev/dvb0.demux%d", id);
    int fd = open(node, O_WRONLY);
    if (fd != -1) {
        if (ioctl(fd, DMX_SET_INPUT, input) == -1)
        {
            ALOGD("dvb_set_demux_source ioctl DMX_SET_INPUT:%d error:%d", input, errno);
        }
        else
        {
            ALOGE("dvb_set_demux_source ioctl succeeded src:%d DMX_SET_INPUT:%d dmx_idx:%d", source, input, id);
            r = 0;
        }

          if (ioctl(fd, DMX_SET_HW_SOURCE, source) == -1)
          {
              ALOGD("dvb_set_demux_source ioctl DMX_SET_HW_SOURCE:%d error:%d", source, errno);
              r = -1;
          }
          else
          {
              ALOGE("dvb_set_demux_source ioctl succeeded DMX_SET_HW_SOURCE:%d dmx_idx:%d", source, id);
              r = 0;
          }
     }
     close(fd);
     return r;
}

