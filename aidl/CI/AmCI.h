#ifndef __AMCI_H
#define __AMCI_H
#include <utils/RefBase.h>
#include "AmCIModuleApi.h"

using namespace android;
typedef struct DataBlock_s DataBlock;
struct DataBlock_s
{
    size_t left;
    size_t start;
    DataBlock *next;
    uint8_t data[2048];
};

class AmCI: public RefBase  {
public:
    AmCI(int dmxId, int ts_input, int source);
    ~AmCI();

    /**
     * \brief   When ts data route using usbcam, play/record etc demux
     *          need set source to usbcam demux.
     *          This function will return usbcam demux number.
     * \param live if requirement is called by live path.
     * \return  demux source in code.
     */
    uint8_t CIUsbGetDmxSource(bool live);

    /**
     * \brief   Check if usbcam is plugged.
     * \return  TRUE if cam is inserted.
     */
    bool CIUsbModuleInserted();

    /**
     * \brief STB_CIUsbOpen
     *        called by usbt, usb monitor thread will call this function
     *          to see if usb cam is plug in/unplug.
     * \return TRUE if success
     */
    int CIUsbOpen();

    /**
     * \brief STB_CIUsbClose
     *        called by usbt, if device node open failed, or spdu transfer failed
     *          then this function will be called to release resource.
     * \return 0 if success
     */
    int CIUsbClose();

    int32_t CIUsbWrite(uint8_t *buffer, uint32_t len);

    /**
     * \brief STB_CIUsbRead
     *        Read data from usbcam.
     * \param buffer, read buffer
     * \param len read len.
     * \return readlen.
     */
    int32_t CIUsbRead(uint8_t *buffer, uint32_t len);

    uint8_t CIUsbCamTotal(void);

    /**
     * \brief STB_DMXUsbGetTsDemux
     *        get the inject usb demux number, and set other
     *        demux source to DMA.
     * \return 0 if success
     */
    int DMXUsbGetTsDemux();

    /**
     * \brief STB_DMXUsbIsEnable
     *        if usb cam function is valid.
     * \return 0 if success
     */
    bool DmxUsbIsEnable();//STB_DMXUsbIsEnable()
    static void *cimodule_media_read_task(void *args);
    static void *cimodule_media_write_task(void *args);
    static void *cimodule_cmd_read_task(void *args);
    AmCIModuleApi* getCIModuelApi();
    int setDvbSource(int dmxId, int input, int source);
    int getUsbcamDriverStatus();

    /*********/

private:
    void init_mutex();
    bool set_usbcam_recording_demux(int source);
    void prepare_working_demuxes();
    void dev_close();
    int ci_ts_read_open();
    int ci_ts_write_open();
    int ci_ts_read_close(int fd, unsigned char *data);
    int ci_ts_write_close(int fd, unsigned char *data);
    int record_from_tsin(void* buff, int buff_len);
    int inject_usbcam_source_demux(void* data, int data_len);
//     int fdMedia = -1;
    int mDemuxId = -1;
    int mTsInput = -1;
    int mSource  = -1;
    int rec_dev_id = 4;
    int inj_dev_id = 5;
    int rec_dvr_fd = -1;
    int rec_dmx_fd = -1;
    int inj_dvr_fd = -1;
    int ev_fd;
    bool thread_running = false;
    pthread_mutex_t gs_tMediaReadCondMut;
    pthread_cond_t gs_tMediaReadCond;
    pthread_mutex_t gs_tMediaWriteCondMut;
    pthread_cond_t gs_tMediaWriteCond;

    pthread_t tMediaReadTaskId;
    pthread_t tMediaWriteTaskId;
    pthread_t tCmdReadTaskId;

    unsigned char *g_pCmdWriteBuf = NULL;
    unsigned char *g_pCmdReadBuf = NULL;
    int g_pCmdFd = -1;
    pthread_mutex_t cmd_read_mutex;
    //char ci_cmd_buffer[1024];
    //int ci_cmd_len;
    bool module_inserted = true;
    bool mutex_init = true;
    DataBlock *data_block_head = NULL;
    AmCIModuleApi *mpCIApi = NULL;
};


#endif

