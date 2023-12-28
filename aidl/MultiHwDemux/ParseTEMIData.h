#ifndef PARSETEMIDATA_H
#define PARSETEMIDATA_H

//#include "techtype.h"


typedef struct
{
    uint8_t  sync_byte;
    bool     transport_error_indicator;
    bool     payload_unit_start_indicator;
    bool     transport_priority;
    uint16_t pid;
    uint8_t  transport_scrambling_control;
    uint8_t  adaptation_field_control;
    uint8_t  continuity_counter;
    bool     tsHeaderExist;
} TRANSPORT_PACKET_HEADER;

typedef struct
{
    uint8_t  PTS_1;
    uint16_t PTS_2;
    uint16_t PTS_3;
    uint64_t PTS_value;
    bool     PTSExist;
} PTS_VALUE;

typedef struct
{
    uint32_t timescale;
    uint64_t media_timestamp;
    uint16_t timelineId;
    bool     paused;
    double   TEMI;
    bool     TEMIExist;
} TEMI_VALUE;

typedef struct
{
    TEMI_VALUE TEMI;
    bool       pairedValue;
    PTS_VALUE  PTS;
} TEMI_PTS_PAIR;

typedef struct
{
    uint8_t flag;
    uint8_t adaptation_field_length;
    bool    adaptation_field_exist;
} ADAPTATION_FIELD_DATA;

typedef struct
{
    uint8_t flag;
    uint8_t af_descr_tag;
    uint8_t af_descr_length;
    bool    af_descr_exist;
} AF_DESC_DATA;

typedef struct
{
    uint8_t flag;
    uint8_t stream_id;
    uint8_t PES_packet_length;
    bool    PES_exist;
} PES_PACKET_DATA;

TEMI_PTS_PAIR GetTEMIAndPTSFromTSPacket(uint8_t *pidData, int length);
TRANSPORT_PACKET_HEADER TurnByteToTSHeader(uint8_t* pidData, int length);
ADAPTATION_FIELD_DATA GetAdaptationFieldFromTS(uint8_t *pidData, int length);
AF_DESC_DATA GetAFDescriptorFromAdaptationField(uint8_t *pidData, ADAPTATION_FIELD_DATA adaptation_field_data);
PES_PACKET_DATA GetPESHeaderFromTS(uint8_t *pidData, int length);
PTS_VALUE GetPTSValueFromPESPacket(uint8_t *pidData, PES_PACKET_DATA pes_packet_data);
TEMI_VALUE GetTEMIFromAFDescriptor(uint8_t *pidData, AF_DESC_DATA af_desc_data);

#endif //PARSETEMIDATA_H
