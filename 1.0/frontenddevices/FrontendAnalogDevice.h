/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_ANALOG_DEVICE_H_
#define ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_ANALOG_DEVICE_H_

#include "FrontendDevice.h"

using namespace std;

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

class Frontend;

enum tvin_sig_fmt_e {
    TVIN_SIG_FMT_NULL = 0,
    //Video Formats
    TVIN_SIG_FMT_CVBS_NTSC_M                        = 0x601,
    TVIN_SIG_FMT_CVBS_NTSC_443                      = 0x602,
    TVIN_SIG_FMT_CVBS_PAL_I                         = 0x603,
    TVIN_SIG_FMT_CVBS_PAL_M                         = 0x604,
    TVIN_SIG_FMT_CVBS_PAL_60                        = 0x605,
    TVIN_SIG_FMT_CVBS_PAL_CN                        = 0x606,
    TVIN_SIG_FMT_CVBS_SECAM                         = 0x607,
    TVIN_SIG_FMT_CVBS_NTSC_50                       = 0x608,
    TVIN_SIG_FMT_CVBS_MAX                           = 0x609,
    TVIN_SIG_FMT_CVBS_THRESHOLD                     = 0x800,
    TVIN_SIG_FMT_MAX,
};

enum tvin_color_fmt_e {
    RGB444 = 0,
    YUV422, // 1
    YUV444, // 2
    YUYV422,// 3
    YVYU422,// 4
    UYVY422,// 5
    VYUY422,// 6
    NV12,   // 7
    NV21,   // 8
    BGGR,   // 9  raw data
    RGGB,   // 10 raw data
    GBRG,   // 11 raw data
    GRBG,   // 12 raw data
    COLOR_FMT_MAX,
};

enum tvin_sig_status_e {
    TVIN_SIG_STATUS_NULL = 0, // processing status from init to the finding of the 1st confirmed status
    TVIN_SIG_STATUS_NOSIG,    // no signal - physically no signal
    TVIN_SIG_STATUS_UNSTABLE, // unstable - physically bad signal
    TVIN_SIG_STATUS_NOTSUP,   // not supported - physically good signal & not supported
    TVIN_SIG_STATUS_STABLE,   // stable - physically good signal & supported
    TVIN_SIG_STATUS_BLOCKED,  // blocked - current channel is locked
};

typedef enum tvin_aspect_ratio_e {
    TVIN_ASPECT_NULL = 0,
    TVIN_ASPECT_1x1,
    TVIN_ASPECT_4x3_FULL,
    TVIN_ASPECT_14x9_FULL,
    TVIN_ASPECT_14x9_LB_CENTER,
    TVIN_ASPECT_14x9_LB_TOP,
    TVIN_ASPECT_16x9_FULL,
    TVIN_ASPECT_16x9_LB_CENTER,
    TVIN_ASPECT_16x9_LB_TOP,
    TVIN_ASPECT_MAX,
} tvin_aspect_ratio_t;

typedef enum tvin_trans_fmt {
    TVIN_TFMT_2D = 0,
    TVIN_TFMT_3D_LRH_OLOR,  // Primary: Side-by-Side(Half) Odd/Left picture, Odd/Right p
    TVIN_TFMT_3D_LRH_OLER,  // Primary: Side-by-Side(Half) Odd/Left picture, Even/Right picture
    TVIN_TFMT_3D_LRH_ELOR,  // Primary: Side-by-Side(Half) Even/Left picture, Odd/Right picture
    TVIN_TFMT_3D_LRH_ELER,  // Primary: Side-by-Side(Half) Even/Left picture, Even/Right picture
    TVIN_TFMT_3D_TB,   // Primary: Top-and-Bottom
    TVIN_TFMT_3D_FP,   // Primary: Frame Packing
    TVIN_TFMT_3D_FA,   // Secondary: Field Alternative
    TVIN_TFMT_3D_LA,   // Secondary: Line Alternative
    TVIN_TFMT_3D_LRF,  // Secondary: Side-by-Side(Full)
    TVIN_TFMT_3D_LD,   // Secondary: L+depth
    TVIN_TFMT_3D_LDGD, // Secondary: L+depth+Graphics+Graphics-depth
    /* normal 3D format */
    TVIN_TFMT_3D_DET_TB,
    TVIN_TFMT_3D_DET_LR,
    TVIN_TFMT_3D_DET_INTERLACE,
    TVIN_TFMT_3D_DET_CHESSBOARD,
    TVIN_TFMT_3D_MAX,
} tvin_trans_fmt_t;

typedef struct tvin_info_s {
    enum tvin_trans_fmt trans_fmt;
    enum tvin_sig_fmt_e fmt;
    enum tvin_sig_status_e status;
    enum tvin_color_fmt_e cfmt;
    unsigned int fps;
    unsigned int is_dvi;
    unsigned int signal_type;
    unsigned int input_colorimetry;
    enum tvin_aspect_ratio_e aspect_ratio;
    __u8 amdolby_vision;
    __u8 low_latency;
}tvin_info_t;

enum tvin_port_e {
    TVIN_PORT_CVBS3 = 0x00001003,
};

struct tvin_parm_s
{
    int index;                      // index of frontend for vdin
    enum tvin_port_e port;          // must set port in IOCTL
    struct tvin_info_s info;
    unsigned int hist_pow;
    unsigned int luma_sum;
    unsigned int pixel_sum;
    unsigned short histgram[64];
    unsigned int flag;
    unsigned short dest_width;      //for vdin horizontal scale down
    unsigned short dest_height;     //for vdin vertical scale down
    bool h_reverse;                 //for vdin horizontal reverse
    bool v_reverse;                 //for vdin vertical reverse
    unsigned int reserved;
};

#define TVIN_IOC_MAGIC 'T'
#define TVIN_IOC_OPEN               _IOW(TVIN_IOC_MAGIC, 0x01, struct tvin_parm_s)
#define TVIN_IOC_START_DEC          _IOW(TVIN_IOC_MAGIC, 0x02, struct tvin_parm_s)
#define TVIN_IOC_STOP_DEC           _IO( TVIN_IOC_MAGIC, 0x03)
#define TVIN_IOC_CLOSE              _IO( TVIN_IOC_MAGIC, 0x04)
#define TVIN_IOC_S_AFE_CVBS_STD     _IOW(TVIN_IOC_MAGIC, 0x1b, enum tvin_sig_fmt_e)

class FrontendAnalogDevice : public FrontendDevice {
public:
    FrontendAnalogDevice(uint32_t hwId, FrontendType type, const sp<Frontend>& context);
    virtual int getFrontendSettings(FrontendSettings *settings, void* fe_params);
    virtual int getFeDeliverySystem(FrontendType type);
    virtual FrontendModulationStatus getFeModulationStatus();
    virtual e_signal_status_t getsignalStatus(int fd, uint32_t &locked_freq);

private:
    ~FrontendAnalogDevice();
    int fd_tvafe;
    int fd_vdin;
    int open_tvafe();
    int close_tvafe();
    int set_tvafe(unsigned long std);
    int typeEnumToCvbsFmt (unsigned long feType);
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_TV_TUNER_V1_0_FRONTEND_ANALOG_DEVICE_H_
