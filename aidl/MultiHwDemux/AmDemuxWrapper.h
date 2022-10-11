/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef AMDEMUX_WRAPPER_H
#define AMDEMUX_WRAPPER_H
//#include <VideodecWrapper.h>
#include <Mutex.h>
#include <TSPMessage.h>

extern "C" {

}

typedef int AM_DevSource_t;

/**\brief AV stream package format*/
typedef enum
{
	PFORMAT_ES = 0, /**< ES stream*/
	PFORMAT_PS,     /**< PS stream*/
	PFORMAT_TS,     /**< TS stream*/
	PFORMAT_REAL    /**< REAL file*/
} AM_AV_PFormat_t;

/*Data input source type*/
typedef enum
{
    TS_DEMOD = 0,                          // TS Data input from demod
    TS_MEMORY = 1,                         // TS Data input from memory
} input_source_type;

typedef enum {
    VFORMAT_UNKNOWN = -1,
    VFORMAT_MPEG12 = 0,
    VFORMAT_MPEG4,
    VFORMAT_H264,
    VFORMAT_MJPEG,
    VFORMAT_REAL,
    VFORMAT_JPEG,
    VFORMAT_VC1,
    VFORMAT_AVS,
    VFORMAT_SW,
    VFORMAT_H264MVC,
    VFORMAT_H264_4K2K,
    VFORMAT_HEVC,
    VFORMAT_H264_ENC,
    VFORMAT_JPEG_ENC,
    VFORMAT_VP9,
    VFORMAT_AVS2,
    VFORMAT_DVES_AVC,
    VFORMAT_DVES_HEVC,
    VFORMAT_MPEG2TS,
    VFORMAT_UNSUPPORT,
    VFORMAT_MAX
} vformat_t;

typedef enum {
    AFORMAT_UNKNOWN = -1,
    AFORMAT_MPEG = 0,
    AFORMAT_PCM_S16LE = 1,
    AFORMAT_AAC = 2,
    AFORMAT_AC3 = 3,
    AFORMAT_ALAW = 4,
    AFORMAT_MULAW = 5,
    AFORMAT_DTS = 6,
    AFORMAT_PCM_S16BE = 7,
    AFORMAT_FLAC = 8,
    AFORMAT_COOK = 9,
    AFORMAT_PCM_U8 = 10,
    AFORMAT_ADPCM = 11,
    AFORMAT_AMR = 12,
    AFORMAT_RAAC = 13,
    AFORMAT_WMA = 14,
    AFORMAT_WMAPRO = 15,
    AFORMAT_PCM_BLURAY = 16,
    AFORMAT_ALAC = 17,
    AFORMAT_VORBIS = 18,
    AFORMAT_AAC_LATM = 19,
    AFORMAT_APE = 20,
    AFORMAT_EAC3 = 21,
    AFORMAT_PCM_WIFIDISPLAY = 22,
    AFORMAT_DRA = 23,
    AFORMAT_SIPR = 24,
    AFORMAT_TRUEHD = 25,
    AFORMAT_MPEG1 = 26, //AFORMAT_MPEG-->mp3,AFORMAT_MPEG1-->mp1,AFROMAT_MPEG2-->mp2
    AFORMAT_MPEG2 = 27,
    AFORMAT_WMAVOI = 28,
    AFORMAT_UNSUPPORT,
    AFORMAT_MAX

} aformat_t;

typedef aformat_t AM_AV_AFormat_t;
typedef vformat_t AM_AV_VFormat_t;

typedef enum
{
	AM_AV_NO_DRM,
	AM_AV_DRM_WITH_SECURE_INPUT_BUFFER, /**< Use for HLS, input buffer is clear and protected*/
	AM_AV_DRM_WITH_NORMAL_INPUT_BUFFER  /**< Use for IPTV, input buffer is normal and scramble*/
} AM_AV_DrmMode_t;

typedef struct Am_DemuxWrapper_OpenPara
{
    int device_type;              // dmx ,dsc,av
    int dev_no;
	AM_AV_VFormat_t  vid_fmt;     /**< Video format*/
	AM_AV_AFormat_t  aud_fmt;     /**< Audio format*/
	AM_AV_PFormat_t  pkg_fmt;     /**< Package format*/
	int              vid_id;      /**< Video ID, -1 means no video data*/
	int              aud_id;      /**< Audio ID, -1 means no audio data*/
	int              sub_id;      /**< Subtitle ID, -i means no subtitle data*/
	int              sub_type;    /**< Subtitle type.*/
	int              cntl_fd;     /*control fd*/
	int              aud_fd;      // inject /dev/amstream_mpts or dev/amstream_mpts_schedfd  handle
	int              vid_fd;      // inject /dev/amstream_mpts or dev/amstream_mpts_schedfd  handle
	void *           dsc_fd;
	AM_AV_DrmMode_t  drm_mode;
} Am_DemuxWrapper_OpenPara_t;

typedef struct Am_TsPlayer_Input_buffer
{
   uint8_t * data;
   int size;  //input: data size ,output : the real input size
} Am_TsPlayer_Input_buffer_t;

typedef enum
{
    AM_Dmx_SUCCESS,
	AM_Dmx_ERROR,
    AM_Dmx_DEVOPENFAIL,
    AM_Dmx_SETSOURCEFAIL,
    AM_Dmx_NOT_SUPPORTED,
    AM_Dmx_CANNOT_OPEN_FILE,
    AM_Dmx_ERR_SYS,
    AM_Dmx_MAX,
} AM_DmxErrorCode_t;


struct mEsDataInfo {
    uint8_t *data;
    int size;
    int64_t pts;
};
enum {
	kWhatReadVideo,
	kWhatReadAudio,
};

class  AmDemuxWrapper {
public:
   AmDemuxWrapper() {
   }
   virtual ~AmDemuxWrapper() {

   }
   virtual  AM_DmxErrorCode_t AmDemuxWrapperOpen(Am_DemuxWrapper_OpenPara_t *para);
   virtual  AM_DmxErrorCode_t AmDemuxWrapperSetTSSource(Am_DemuxWrapper_OpenPara_t *para,const AM_DevSource_t src);
   virtual  AM_DmxErrorCode_t AmDemuxWrapperStart();
   virtual  AM_DmxErrorCode_t AmDemuxWrapperWriteData(Am_TsPlayer_Input_buffer_t* Pdata, int *pWroteLen, uint64_t timeout);
   virtual  AM_DmxErrorCode_t AmDemuxWrapperReadData(int pid, mEsDataInfo **mEsdata,uint64_t timeout); //???????
   virtual  AM_DmxErrorCode_t AmDemuxWrapperFlushData(int pid); //???
   virtual  AM_DmxErrorCode_t AmDemuxWrapperPause();
   virtual  AM_DmxErrorCode_t AmDemuxWrapperResume();
   virtual  AM_DmxErrorCode_t AmDemuxWrapperSetAudioParam(int aid, AM_AV_AFormat_t afmt);
   virtual  AM_DmxErrorCode_t AmDemuxWrapperSetAudioDescParam(int aid, AM_AV_AFormat_t afmt);
   virtual  AM_DmxErrorCode_t AmDemuxWrapperSetSubtitleParam(int sid, int stype);
   virtual  AM_DmxErrorCode_t AmDemuxWrapperSetSectionParam(int secpid, int table_id);
   virtual  AM_DmxErrorCode_t AmDemuxWrapperSetVideoParam(int vid, AM_AV_VFormat_t vfmt);
   virtual  AM_DmxErrorCode_t AmDemuxWrapperGetStates (int * value , int statetype);
   virtual  AM_DmxErrorCode_t AmDemuxWrapperGetMenInfo(uint8_t* buffer) {
        (void) buffer;
        return AM_Dmx_SUCCESS;
   }
   virtual  AM_DmxErrorCode_t AmDemuxWrapperStop();
   virtual  AM_DmxErrorCode_t AmDemuxWrapperClose();
   virtual  AmDemuxWrapper* get(void) {
        return this;
   }
   virtual void AmDemuxSetNotify(const sp<TSPMessage> & msg) {
	(void) msg;
   }
};

#endif
