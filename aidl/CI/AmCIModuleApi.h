#ifndef __AMUSBCIMODULEAPI_H
#define __AMUSBCIMODULEAPI_H
#include <stdint.h>
#include <utils/RefBase.h>
#define USB_CIMODULE_COMMAND_MAX_SIZE    0x2000
#define USB_CIMODULE_MEDIA_MAX_SIZE      (47 * 1024)
#define ERROR_USB_REMOVE			-19
#define ERROR_INVALID_ARGUMENT		-22
#define ERROR_PROTOCOL_ERROR		-71
#define ERROR_ENDPOINT_SHUTDOWN		-108
#define ERROR_INVALID_HANDLE		-200
#define ERROR_USB_PARAM				-201
struct usb_cimodule_info
{
   unsigned short m_wVendorId;
   unsigned short m_wProductId;
   uint32_t m_dwCiCompatibility;
   unsigned char m_bIsCI20Detected;
   unsigned char m_arReserve[31];
};

enum aml_usbcam_device_state {
	DEVICE_CONNECT = 0,
	DEVICE_DISCONNECT = 1
};

#define AML_USBCAM_IOC_MAGIC                  'c'
#define AML_USBCAM_IOC_GET_DRIVER_VERSION     _IOR(AML_USBCAM_IOC_MAGIC, 0, uint32_t)
#define AML_USBCAM_IOC_GET_INFO               _IOR(AML_USBCAM_IOC_MAGIC, 1, struct usb_cimodule_info)
#define AML_USBCAM_IOC_RESET                  _IO(AML_USBCAM_IOC_MAGIC, 2)
#define AML_USBCAM_IOC_CANCEL_TRANSFER        _IO(AML_USBCAM_IOC_MAGIC, 3)
#define AML_USBCAM_IOC_MODULE_CAPABILITIES    _IOR(AML_USBCAM_IOC_MAGIC, 4, usbci_module_capabilities_t)
#define AML_USBCAM_IOC_GET_MODULE_STATE       _IOR(AML_USBCAM_IOC_MAGIC, 5, uint32_t)
#define AML_USBCAM_IOC_SET_MODULE_STATE       _IOR(AML_USBCAM_IOC_MAGIC, 6, uint32_t)

class AmCIModuleApi {
    public:
    AmCIModuleApi();
    ~AmCIModuleApi();
    int cimodule_cmd_intf_open(const char *i_pbDevpath, int i_nflags);
    int cimodule_cmd_intf_close(int i_fd);
    unsigned char * cimodule_cmd_intf_mmap_readbuf(int i_fd, unsigned int i_dwMaxCmdReadBufSize);
    unsigned char * cimodule_cmd_intf_mmap_writebuf(int i_fd, unsigned int i_dwMaxCmdBufWriteSize);
    void cimodule_cmd_intf_munmap_readbuf(unsigned char *i_pbBuf);
    void cimodule_cmd_intf_munmap_writebuf(unsigned char *i_pbBuf);
    int cimodule_cmd_intf_read(int i_fd, unsigned char *o_pbBuf, unsigned int i_dwRqstLen, unsigned int* o_pdwActualLen,
    unsigned int i_dwTimeout);
    int cimodule_cmd_intf_write(int i_fd, unsigned char *i_pbBuf, unsigned int i_dwRqstLen, unsigned int* o_pdwActualLen,
    unsigned int i_dwTimeout);
    int cimodule_media_intf_open(const char *i_pbDevpath, int i_nflags);
    int cimodule_media_intf_close(int i_fd);
    unsigned char * cimodule_media_intf_readbuf(int i_fd, unsigned int i_dwMaxMediaReadBufSize);
    unsigned char * cimodule_media_intf_writebuf(int i_fd, unsigned int i_dwMaxMediaBufWriteSize);
    void cimodule_media_intf_readbuf(unsigned char *i_pbBuf);
    void cimodule_media_intf_writebuf(unsigned char *i_pbBuf);
    int cimodule_media_intf_read(int i_fd, unsigned char *o_pbBuf, unsigned int i_dwRqstLen, unsigned int *o_pdwActualLen, unsigned int i_dwTimeout);
    int cimodule_media_intf_write(int i_fd, unsigned char *i_pbBuf, unsigned int i_dwRqstLen, unsigned int *o_pdwActualLen, unsigned int i_dwTimeout);
    int cimodule_get_driver_version(int i_fd, unsigned int *o_pdwDriverVersion);
    int cimodule_get_usb_cimodule_info(int i_fd, struct usb_cimodule_info *o_ptUsbCiModuleInfo);
    int cimodule_reset(int i_fd);
    int set_dvb_source(int id, int input, int source);
};

#endif
