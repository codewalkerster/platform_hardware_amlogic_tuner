#include <bitset>
#include "ParseTEMIData.h"

#define LOG_TAG "ParseTEMIData"

  /**Log facilities.*/
#define LOG_LV_DEFAULT  3

#define LOG_LV_DEBUG    1
#define LOG_LV_INFO     2
#define LOG_LV_ERROR    3

#include <android/log.h>
#define TEMI_LOG_PRINT(level, tag, ...) \
  do { \
    if (level >= LOG_LV_DEFAULT) { \
      __android_log_print(level, tag, __VA_ARGS__); \
    } \
  } while(0)


#define PARSE_TEMI_DEBUG
#ifdef PARSE_TEMI_DEBUG
#define PARSETEMI_LOGD( ... )                                               \
     TEMI_LOG_PRINT(LOG_LV_DEBUG, LOG_TAG, __VA_ARGS__)
#define PARSETEMI_LOGI( ... )                                               \
    TEMI_LOG_PRINT(LOG_LV_INFO, LOG_TAG, __VA_ARGS__)
#define PARSETEMI_LOGE( ... )                                               \
    TEMI_LOG_PRINT(LOG_LV_ERROR, DVR_LOG_TAG, __VA_ARGS__)
#else
#define PARSETEMI_LOGD( ... )
#define PARSETEMI_LOGI( ... )
#define PARSETEMI_LOGE( ... )
#endif

typedef struct
{
    unsigned int discontinuity_indicator               : 1;
    unsigned int random_access_indicator               : 1;
    unsigned int elementary_stream_priority_indicator  : 1;
    unsigned int PCR_flag                              : 1;
    unsigned int OPCR_flag                             : 1;
    unsigned int splicing_point_flag                   : 1;
    unsigned int transport_private_data_flag           : 1;
    unsigned int adaptation_field_extension_flag       : 1;
} ADAPTATION_FIELD_HEADER;

typedef struct
{
    unsigned int has_timestamp : 2;
    unsigned int has_ntp       : 1;
    unsigned int has_ptp       : 1;
    unsigned int has_timecode  : 2;
    unsigned int force_reload  : 1;
    unsigned int pause         : 1;
    unsigned int discontinuity : 1;
    unsigned int reserved      : 7;
    uint8_t      timeline_id   : 8;
} TEMI_TIMELINE_DESCRIPTOR_HEADER;

typedef struct
{
    unsigned int check_flag                : 2;
    unsigned int PES_scrambling_control    : 2;
    unsigned int PES_priority              : 1;
    unsigned int data_alignment_indicator  : 1;
    unsigned int copyright                 : 1;
    unsigned int original_or_copy          : 1;
    unsigned int PTS_DTS_flag              : 2;
    unsigned int ESCR_flag                 : 1;
    unsigned int ES_rate_flag              : 1;
    unsigned int DSM_trick_mode_flag       : 1;
    unsigned int additional_copy_info_flag : 1;
    unsigned int PES_CRC_flag              : 1;
    unsigned int PES_extension_flag        : 1;
    unsigned int PES_header_data_length    : 8;
} PES_PACKET_CONTAIN_PTS;

typedef struct
{
    unsigned int check_flag   : 4 ;
    unsigned int PTS_hour     : 3 ;
    unsigned int market_bit_1 : 1 ;
    unsigned int PTS_minute   : 15;
    unsigned int market_bit_2 : 1 ;
    unsigned int PTS_second   : 15;
    unsigned int market_bit_3 : 1 ;
} PES_HEADER_CONTAIN_PTS_ONLY;

typedef struct
{
    unsigned int check_flag_1 : 4 ;
    unsigned int PTS_hour     : 3 ;
    unsigned int market_bit_1 : 1 ;
    unsigned int PTS_minute   : 15;
    unsigned int market_bit_2 : 1 ;
    unsigned int PTS_second   : 15;
    unsigned int market_bit_3 : 1 ;
    unsigned int check_flag_2 : 4 ;
    unsigned int DTS_hour     : 3 ;
    unsigned int market_bit_4 : 1 ;
    unsigned int DTS_minute   : 15;
    unsigned int market_bit_5 : 1 ;
    unsigned int DTS_second   : 15;
    unsigned int market_bit_6 : 1 ;
} PES_HEADER_CONTAIN_PTS_DTS;

uint16_t pidOfTEMI        = 0;
uint8_t  PCR_length       = 6;      //bytes
uint8_t  TS_header_length = 4;      //bytes
uint8_t  OPCR_length      = 6;      //bytes
uint8_t  splicing_point_length  = 1;//byte
uint8_t  transport_private_data = 1 ;

TRANSPORT_PACKET_HEADER TurnByteToTSHeader(uint8_t* pidData)
{
    TRANSPORT_PACKET_HEADER tsHeader;
    tsHeader.tsHeaderExist = false;
    if (pidData[0] != 0x47)
    {
        tsHeader.tsHeaderExist = false;
        PARSETEMI_LOGI("sync_byte is not 0x47.");
        return tsHeader;
    }
    tsHeader.sync_byte                    = pidData[0];
    tsHeader.transport_error_indicator    = ((pidData[1] >> 7) & 0x01);
    tsHeader.payload_unit_start_indicator = ((pidData[1] >> 6) & 0x01);
    tsHeader.transport_priority           = ((pidData[1] >> 5) & 0x01);
    tsHeader.pid                          = (((pidData[1] & 0x1f) << 8) | pidData[2]);
    tsHeader.transport_scrambling_control = ((pidData[3] >> 6) & 0x03);
    tsHeader.adaptation_field_control     = ((pidData[3] >> 4) & 0x03);
    tsHeader.continuity_counter           = (pidData[3] & 0x0f);
    tsHeader.tsHeaderExist                = true;
    return tsHeader;
}

ADAPTATION_FIELD_DATA GetAdaptationFieldFromTS(uint8_t *pidData, int length)
{
    ADAPTATION_FIELD_DATA adaptation_field;
    int flag = 0;
    adaptation_field.adaptation_field_exist = false;
    if (length != 188)
    {
        adaptation_field.adaptation_field_exist = false;
        PARSETEMI_LOGI("TS packet's length is not 188 bytes.");
        return adaptation_field;
    }
    TRANSPORT_PACKET_HEADER ts_header = TurnByteToTSHeader(pidData);
    flag += TS_header_length;
    if (ts_header.tsHeaderExist == true)
    {
        PARSETEMI_LOGI("ts header exists.");
        //Judge whether it exists adaptation_field and PES Header
        if ((ts_header.adaptation_field_control == 2) || (ts_header.adaptation_field_control == 3))
        {
            PARSETEMI_LOGI("Adaptation_field exists.");
            uint8_t adaptation_field_length          = pidData[flag];
            flag ++;
            adaptation_field.adaptation_field_exist  = true;
            adaptation_field.adaptation_field_length = adaptation_field_length;
            adaptation_field.flag                    = flag; //beginning follow by adaption_field_length
        }
        else
        {
            PARSETEMI_LOGI("No adaptation_field.");
            adaptation_field.adaptation_field_exist  = false;
        }
    }
    else
    {
        PARSETEMI_LOGI("ts header do not exist.");
    }
    return adaptation_field;
}

AF_DESC_DATA GetAFDescriptorFromAdaptationField(uint8_t *pidData, ADAPTATION_FIELD_DATA adaptation_field_data)
{
    AF_DESC_DATA af_desc_data;
    af_desc_data.af_descr_exist = false;
    if (adaptation_field_data.adaptation_field_exist == true)
    {
        int flag = adaptation_field_data.flag;
        int adaptation_field_length = adaptation_field_data.adaptation_field_length;
        if (adaptation_field_length > 0)
        {
            //get adaptation field header
            PARSETEMI_LOGI("adaptation_field_length > 0");
            ADAPTATION_FIELD_HEADER af_Header;
            af_Header.discontinuity_indicator              = (pidData[flag] >> 7) & 0x01;
            af_Header.random_access_indicator              = (pidData[flag] >> 6) & 0x01;
            af_Header.elementary_stream_priority_indicator = (pidData[flag] >> 5) & 0x01;
            af_Header.PCR_flag                             = (pidData[flag] >> 4) & 0x01;
            af_Header.OPCR_flag                            = (pidData[flag] >> 3) & 0x01;
            af_Header.splicing_point_flag                  = (pidData[flag] >> 2) & 0x01;
            af_Header.transport_private_data_flag          = (pidData[flag] >> 1) & 0x01;
            af_Header.adaptation_field_extension_flag      = (pidData[flag] >> 0) & 0x01;
            flag ++;
            if (af_Header.PCR_flag == 1)
            {
                PARSETEMI_LOGI("PCR exists");
                flag += PCR_length;
            }
            if (af_Header.OPCR_flag == 1)
            {
                PARSETEMI_LOGI("OPCR exists");
                flag += OPCR_length;
            }
            if (af_Header.splicing_point_flag == 1)
            {
                PARSETEMI_LOGI("splicing_point exists");
                flag += splicing_point_length;
            }
            if (af_Header.transport_private_data_flag == 1)
            {
                PARSETEMI_LOGI("transport_private_data exists");
                uint8_t transport_private_data_length = pidData[flag];
                flag ++;
                flag += transport_private_data_length;
            }
            if (af_Header.adaptation_field_extension_flag == 1)
            {
                PARSETEMI_LOGI("adaptation_field_extension exists.");
                //uint8_t adaptation_field_extension_length = pidData[flag];
                flag ++;
                unsigned int ltw_flag                       = ((pidData[flag] >> 7) & 0x01) ;
                unsigned int piecewise_rate_flag            = ((pidData[flag] >> 6) & 0x01) ;
                unsigned int seamless_splice_flag           = ((pidData[flag] >> 5) & 0x01) ;
                unsigned int af_descriptor_not_present_flag = ((pidData[flag] >> 4) & 0x01) ;
                flag ++;
                if (ltw_flag == 1)
                {
                    flag += 2;//ltw_valid_flag and ltw_offset data length is 2 bytes.
                }
                if (piecewise_rate_flag == 1)
                {
                    flag += 3;//piecewise_rate and reserved data length is 3 bytes
                }
                if (seamless_splice_flag == 1)
                {
                    flag += 5;//data length is 5 bytes
                }
                if (af_descriptor_not_present_flag == 0)
                {
                    PARSETEMI_LOGI("af_descriptor exists.");
                    uint8_t af_descr_tag         = pidData[flag ++];
                    uint8_t af_descr_length      = pidData[flag ++];
                    af_desc_data.af_descr_exist  = true;
                    af_desc_data.af_descr_tag    = af_descr_tag;
                    af_desc_data.af_descr_length = af_descr_length;
                    af_desc_data.flag = flag;
                }
                else
                {
                    PARSETEMI_LOGI("af_descriptor does not exist.");
                    af_desc_data.af_descr_exist  = false;
                }
            }
            else
            {
                PARSETEMI_LOGI("adaptation_field_extension_flag = 0. It means no af_extension_data.");
            }
        }
        else
        {
            PARSETEMI_LOGI("adaptation_field_length = 0");
        }
    }
    else
    {
        PARSETEMI_LOGI("adaptation_field_data.adaptation_field_exist = false.");
    }
    return af_desc_data;
}

PES_PACKET_DATA GetPESHeaderFromTS(uint8_t *pidData, int length)
{
    PES_PACKET_DATA pes_packet_data;
    pes_packet_data.PES_exist = false;
    if (length != 188)
    {
        PARSETEMI_LOGI("length of TS Packet is not 188 bytes.");
        return pes_packet_data;
    }
    int flag = 0;
    TRANSPORT_PACKET_HEADER ts_header = TurnByteToTSHeader(pidData);
    flag += TS_header_length;
    if (ts_header.tsHeaderExist == true)
    {
        PARSETEMI_LOGI("ts header exists.");
        //Judge whether it exists adaptation_field and PES Header
        if (ts_header.adaptation_field_control == 1)
        {
            PARSETEMI_LOGI("No adaptation_field, payload only.");
            if (ts_header.payload_unit_start_indicator == 1)
            {
                PARSETEMI_LOGI("payload data includes PES Header.");
                uint32_t packet_start_code_prefix = (pidData[flag] << 16 | pidData[flag + 1] << 8 | pidData [flag + 2]);
                flag += 3;
                uint8_t stream_id                 = pidData[flag];
                flag ++;
                uint16_t PES_packet_length        = ((pidData[flag] << 8) | pidData[flag + 1]);
                flag += 2;
                if (packet_start_code_prefix == 0x000001)
                {
                    pes_packet_data.PES_exist         = true;
                    pes_packet_data.stream_id         = stream_id;
                    pes_packet_data.flag              = flag; //beginning follow by PES_packet_length
                    pes_packet_data.PES_packet_length = PES_packet_length;
                }
                else
                {
                    pes_packet_data.PES_exist         = false;
                    PARSETEMI_LOGI("packet_start_code_prefix != 0x000001.");
                }
            }
            else
            {
                pes_packet_data.PES_exist         = false;
                PARSETEMI_LOGI("payload data do not include PES Header.");
            }
        }
        else if (ts_header.adaptation_field_control == 2)
        {
            PARSETEMI_LOGI("Adaptation_field only, no payload.");
        }
        else if (ts_header.adaptation_field_control == 3)
        {
            PARSETEMI_LOGI("Adaptation_field followed by payload.");
            uint8_t adaptation_field_length   = pidData[flag];
            flag ++;
            flag += adaptation_field_length;
            if (ts_header.payload_unit_start_indicator == 1)
            {
                PARSETEMI_LOGI("payload data includes PES Header.");
                uint32_t packet_start_code_prefix = (pidData[flag] << 16 | pidData[flag + 1] << 8 | pidData [flag + 2]);
                flag += 3;
                uint8_t stream_id = pidData[flag];
                flag ++;
                uint16_t PES_packet_length        = ((pidData[flag] << 8) | pidData[flag + 1]);
                flag += 2;
                if (packet_start_code_prefix == 0x000001)
                {
                    pes_packet_data.PES_exist         = true;
                    pes_packet_data.stream_id         = stream_id;
                    pes_packet_data.flag              = flag; //beginning follow by PES_packet_length
                    pes_packet_data.PES_packet_length = PES_packet_length;
                }
                else
                {
                    pes_packet_data.PES_exist         = false;
                    PARSETEMI_LOGI("packet_start_code_prefix != 0x000001.");
                }
            }
            else
            {
                pes_packet_data.PES_exist         = false;
                PARSETEMI_LOGI("payload data do not include PES Header.");
            }
        }
        else
        {
            PARSETEMI_LOGI("Reserved for future use by ISO/IEO.");
        }
    }
    else
    {
        PARSETEMI_LOGI("ts header do not exist.");
    }
    return pes_packet_data;
}

PTS_VALUE GetPTSValueFromPESPacket(uint8_t *pidData, PES_PACKET_DATA pes_packet_data)
{
    PTS_VALUE PTS;
    PTS.PTSExist = false;
    uint8_t stream_id = pes_packet_data.stream_id;
    uint8_t flag      = pes_packet_data.flag;
    if ((stream_id != 0xbc) && (stream_id != 0xbe) && (stream_id != 0xbf) && (stream_id != 0xf0) &&
        (stream_id != 0xf1) && (stream_id != 0xff) && (stream_id != 0xf2) && (stream_id != 0xf8))
    {
        PES_PACKET_CONTAIN_PTS PES_Header;
        PES_Header.check_flag                = ((pidData[flag] >> 6) & 0x03);
        PES_Header.PES_scrambling_control    = ((pidData[flag] >> 4) & 0x03);
        PES_Header.PES_priority              = ((pidData[flag] >> 3) & 0x01);
        PES_Header.data_alignment_indicator  = ((pidData[flag] >> 2) & 0x01);
        PES_Header.copyright                 = ((pidData[flag] >> 1) & 0x01);
        PES_Header.original_or_copy          = ((pidData[flag] >> 0) & 0x01);
        flag ++;
        PES_Header.PTS_DTS_flag              = ((pidData[flag] >> 6) & 0x03);
        PES_Header.ESCR_flag                 = ((pidData[flag] >> 5) & 0x01);
        PES_Header.ES_rate_flag              = ((pidData[flag] >> 4) & 0x01);
        PES_Header.DSM_trick_mode_flag       = ((pidData[flag] >> 3) & 0x01);
        PES_Header.additional_copy_info_flag = ((pidData[flag] >> 2) & 0x01);
        PES_Header.PES_CRC_flag              = ((pidData[flag] >> 1) & 0x01);
        PES_Header.PES_extension_flag        = ((pidData[flag] >> 0) & 0x01);
        flag ++;
        PES_Header.PES_header_data_length    = pidData[flag];
        PARSETEMI_LOGI("PES_header_data_length is %u.", PES_Header.PES_header_data_length);
        flag ++;
        if (PES_Header.check_flag == 0x02)
        {
            PARSETEMI_LOGI("check_flag is '10'.");
            if (PES_Header.PTS_DTS_flag == 0x00)
            {
                PARSETEMI_LOGI("PES packet header does not contain PTS or DTS.");
            }
            else if (PES_Header.PTS_DTS_flag == 0x02)
            {
                PARSETEMI_LOGI("PES packet header only contains PTS.");
                PES_HEADER_CONTAIN_PTS_ONLY PESHeaderPTS;
                PESHeaderPTS.check_flag   = ((pidData[flag] >> 4) & 0x0f);
                PESHeaderPTS.PTS_hour     = ((pidData[flag] >> 1) & 0x07);
                PESHeaderPTS.market_bit_1 = ((pidData[flag] >> 0) & 0x01);
                flag ++;
                PESHeaderPTS.PTS_minute   = ((pidData[flag] << 8) | pidData[flag + 1]) >> 1;
                PESHeaderPTS.market_bit_2 = ((pidData[flag + 1] >> 0) & 0x01);
                flag += 2;
                PESHeaderPTS.PTS_second   = ((pidData[flag] << 8) | pidData[flag + 1]) >> 1;
                PESHeaderPTS.market_bit_3 = ((pidData[flag + 1] >> 0) & 0x01);
                flag += 2;
                if (PESHeaderPTS.check_flag == 0x02 && PESHeaderPTS.market_bit_1 == 0x01 && PESHeaderPTS.market_bit_2 == 0x01 && PESHeaderPTS.market_bit_2 == 0x01)
                {
                    PTS.PTS_1     = PESHeaderPTS.PTS_hour;
                    PTS.PTS_2     = PESHeaderPTS.PTS_minute;
                    PTS.PTS_3     = PESHeaderPTS.PTS_second;
                    PTS.PTSExist  = true;
                    PARSETEMI_LOGI("PTS_hour is %u, PTS_minute is %u, PTS_second is %u", PESHeaderPTS.PTS_hour, PESHeaderPTS.PTS_minute, PESHeaderPTS.PTS_second);
                    PTS.PTS_value = (((uint64_t)PESHeaderPTS.PTS_hour) << 30) | (PESHeaderPTS.PTS_minute << 15) | PESHeaderPTS.PTS_second;
                    PARSETEMI_LOGI("PTS value is %llu", PTS.PTS_value);
                }
                else
                {
                    PARSETEMI_LOGI("check failed.");
                    PTS.PTSExist  = false;
                }
            }
            else if (PES_Header.PTS_DTS_flag == 0x03)
            {
                PARSETEMI_LOGI("PES packet header contains PTS and DTS.");
                PES_HEADER_CONTAIN_PTS_DTS PESHeaderPTSandDTS;
                PESHeaderPTSandDTS.check_flag_1 = ((pidData[flag] >> 4) & 0x0f);
                PESHeaderPTSandDTS.PTS_hour     = ((pidData[flag] >> 1) & 0x07);
                PESHeaderPTSandDTS.market_bit_1 = ((pidData[flag] >> 0) & 0x01);
                flag ++;
                PESHeaderPTSandDTS.PTS_minute   = ((pidData[flag] << 8) | pidData[flag + 1]) >> 1;
                PESHeaderPTSandDTS.market_bit_2 = ((pidData[flag + 1] >> 0) & 0x01);
                flag += 2;
                PESHeaderPTSandDTS.PTS_second   = ((pidData[flag] << 8) | pidData[flag + 1]) >> 1;
                PESHeaderPTSandDTS.market_bit_3 = ((pidData[flag + 1] >> 0) & 0x01);
                flag += 2;
                PESHeaderPTSandDTS.check_flag_2 = ((pidData[flag] >> 4) & 0x0f);
                PESHeaderPTSandDTS.DTS_hour     = ((pidData[flag] >> 1) & 0x07);
                PESHeaderPTSandDTS.market_bit_4 = ((pidData[flag] >> 0) & 0x01);
                flag ++;
                PESHeaderPTSandDTS.DTS_minute   = ((pidData[flag] << 8) | pidData[flag + 1]) >> 1;
                PESHeaderPTSandDTS.market_bit_5 = ((pidData[flag + 1] >> 0) & 0x01);
                flag += 2;
                PESHeaderPTSandDTS.DTS_second   = ((pidData[flag] << 8) | pidData[flag + 1]) >> 1;
                PESHeaderPTSandDTS.market_bit_6 = ((pidData[flag + 1] >> 0) & 0x01);
                flag += 2;
                if (PESHeaderPTSandDTS.check_flag_1 == 0x03 && PESHeaderPTSandDTS.market_bit_1 == 0x01 && PESHeaderPTSandDTS.market_bit_2 == 0x01 && PESHeaderPTSandDTS.market_bit_2 == 0x01)
                {
                    PTS.PTS_1     = PESHeaderPTSandDTS.PTS_hour;
                    PTS.PTS_2     = PESHeaderPTSandDTS.PTS_minute;
                    PTS.PTS_3     = PESHeaderPTSandDTS.PTS_second;
                    PTS.PTSExist  = true;
                    PTS.PTS_value = (((uint64_t)PESHeaderPTSandDTS.PTS_hour) << 30) | (PESHeaderPTSandDTS.PTS_minute << 15) | PESHeaderPTSandDTS.PTS_second;
                    PARSETEMI_LOGI("PTS value is %llu", PTS.PTS_value);
                }
                else
                {
                    PARSETEMI_LOGI("market_bit is wrong.");
                    PTS.PTSExist  = false;
                }
            }
            else
            {
                PARSETEMI_LOGI("the value 0x01 is forbidden.");
                PTS.PTSExist  = false;
            }
        }
        else
        {
            PARSETEMI_LOGI("check_flag is not '0000,0010' and it should be '0000,0010'.");
            PTS.PTSExist  = false;
        }
    }
    else
    {
        PARSETEMI_LOGI("PES packet donot contain PTS.");
        PTS.PTSExist  = false;
    }
    return PTS;
}

TEMI_VALUE GetTEMIFromAFDescriptor(uint8_t *pidData, AF_DESC_DATA af_desc_data)
{
    TEMI_VALUE TEMI;
    TEMI.TEMIExist = false;
    if (af_desc_data.af_descr_exist == true)
    {
        uint8_t af_descr_tag = af_desc_data.af_descr_tag;
        uint8_t flag         = af_desc_data.flag;
        if (af_descr_tag == 0x04)
        {
            PARSETEMI_LOGI("af_descriptor is temi_timeline_descriptor.");
            TEMI_TIMELINE_DESCRIPTOR_HEADER timelineDescriptorHeader ;
            timelineDescriptorHeader.has_timestamp = (pidData[flag] >> 6) & 0x03;
            timelineDescriptorHeader.has_ntp       = (pidData[flag] >> 5) & 0x01;
            timelineDescriptorHeader.has_ptp       = (pidData[flag] >> 4) & 0x01;
            timelineDescriptorHeader.has_timecode  = (pidData[flag] >> 2) & 0x03;
            timelineDescriptorHeader.force_reload  = (pidData[flag] >> 1) & 0x01;
            timelineDescriptorHeader.pause         = (pidData[flag ++])   & 0x01;
            timelineDescriptorHeader.discontinuity = (pidData[flag] >> 7) & 0x01;
            flag ++;
            timelineDescriptorHeader.timeline_id   = pidData[flag ++];
            TEMI.timelineId = timelineDescriptorHeader.timeline_id;
            TEMI.paused     = timelineDescriptorHeader.pause;
            if (timelineDescriptorHeader.has_timestamp != 0)
            {
                PARSETEMI_LOGI("media_timestamp exists.");
                uint32_t timescale = (((((pidData[flag] << 8) | pidData[flag + 1]) << 8 )  | pidData[flag + 2]) << 8 ) | pidData[flag + 3];
                PARSETEMI_LOGI("timescale is %u", timescale);
                flag += 4;
                if (timelineDescriptorHeader.has_timestamp == 1)
                {
                    PARSETEMI_LOGI("media_timestamp is 32 bits.");
                    uint32_t media_timestamp = (((((pidData[flag] <<   8) | pidData[flag + 1]) << 8) | pidData[flag + 2]) << 8) | pidData[flag + 3];
                    flag += 4;
                    TEMI.timescale       = timescale;
                    TEMI.media_timestamp = media_timestamp;
                    TEMI.TEMI            = (double)media_timestamp/(double)timescale;
                    PARSETEMI_LOGI("media_timestamp is %llu", TEMI.media_timestamp);
                }
                else
                {
                    PARSETEMI_LOGI("media_timestamp is 64 bits.");
                    uint64_t media_timestamp = 0;
                    for (uint8_t i=0; i<8; i++) {
                        media_timestamp <<= 8;
                        media_timestamp |= pidData[flag+i];
                    }
                    TEMI.timescale       = timescale;
                    TEMI.media_timestamp = media_timestamp;
                    TEMI.TEMI            = (double)media_timestamp/(double)timescale;
                    PARSETEMI_LOGI("media_timestamp is %llu", TEMI.media_timestamp);
                }
                TEMI.TEMIExist = true;
            }
            else
            {
                PARSETEMI_LOGI("media_timestamp does not exist.");
                TEMI.TEMIExist = false;
            }
        }
        else
        {
            PARSETEMI_LOGI("af_descriptor is not temi_timeline_descriptor.");
            TEMI.TEMIExist = false;
        }
    }
    return TEMI;
}

TEMI_PTS_PAIR GetTEMIAndPTSFromTSPacket(uint8_t *pidData, int length)
{
    PARSETEMI_LOGI("GetTEMIAndPTSFromTSPacket in.");
    TEMI_PTS_PAIR pairData;
    pairData.pairedValue                   = false;
    PES_PACKET_DATA pes_packet             = GetPESHeaderFromTS(pidData, length);
    pairData.PTS                           = GetPTSValueFromPESPacket(pidData, pes_packet);
    ADAPTATION_FIELD_DATA adaptation_filed = GetAdaptationFieldFromTS(pidData, length);
    AF_DESC_DATA af_descriptor             = GetAFDescriptorFromAdaptationField(pidData, adaptation_filed);
    pairData.TEMI                          = GetTEMIFromAFDescriptor(pidData, af_descriptor);
    if ((pairData.TEMI.TEMIExist == true) && (pairData.PTS.PTSExist == true))
    {
        pairData.pairedValue = true;
    }
    PARSETEMI_LOGI("GetTEMIAndPTSFromTSPacket out.");
    return pairData;
}

