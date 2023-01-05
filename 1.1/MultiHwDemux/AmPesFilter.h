#ifndef AMPESFILTER_H
#define AMPESFILTER_H
#include <inttypes.h>
#include <utils/RefBase.h>

typedef void (*PES_DataCallback) (void *device, int fid, uint8_t *pes, int len);

typedef struct {
    int      fid;
    int      pid;
    int      cc;
    int      has_pusi;
    uint8_t *pes_data;
    int      pes_len;
    int      pes_cap;
    uint8_t  left_data[188];
    int      left_len;
    PES_DataCallback cb;
    void    *user_data;
} PES_Filter;

using namespace android;
class AmPesFilter : public RefBase {
public:
    AmPesFilter(int fid, PES_DataCallback cb, void *user_data);
    ~AmPesFilter();
    void extractPesDataFromTsPacket(int pid, uint8_t *ts, int len);
    void release();
private:
    void ts_packet(uint8_t *pkt);
    void ts_payload (int pusi, uint8_t *data, int len);
    void postPesData();
    PES_Filter *mPesFilter;
};


#endif

