#define LOG_TAG "AmPesFilter"
#include "AmPesFilter.h"
#include <utils/Log.h>

AmPesFilter::AmPesFilter(int fid, PES_DataCallback cb, void* user_data) {
    mPesFilter = (PES_Filter *)malloc(sizeof(PES_Filter));
    if (mPesFilter) {
        mPesFilter->fid       = fid;
        mPesFilter->cb        = cb;
        //mPesFilter->pid       = pid;
        mPesFilter->has_pusi  = 0;
        mPesFilter->cc        = -1;
        mPesFilter->pes_data  = NULL;
        mPesFilter->pes_len   = 0;
        mPesFilter->pes_cap   = 0;
        mPesFilter->user_data = user_data;
        mPesFilter->left_len  = 0;
    }
}

void AmPesFilter::postPesData() {
    ALOGD("[%s/%d]", __FUNCTION__, __LINE__);

    uint8_t *p = NULL;
    int      len;
    if (mPesFilter != NULL) {
        if (!mPesFilter->pes_len)
            return;

        if (mPesFilter->pes_len < 6) {
            ALOGE("PES is too short");
            return;
        }

        p = mPesFilter->pes_data;

        if ((p[0] != 0) || (p[1] != 0) || (p[2] != 1)) {
            ALOGE("PES is not start with 00 00 01");
            return;
        }

        len = (p[4] << 8) | p[5];
        if (len && (len + 6 > mPesFilter->pes_len)) {
            ALOGE("PES packet length error");
            return;
        }

        mPesFilter->cb(mPesFilter->user_data, mPesFilter->fid, mPesFilter->pes_data, mPesFilter->pes_len);
    }
}

void AmPesFilter::ts_payload(int pusi, uint8_t * data, int len) {
    int size;
    if (pusi) {
        postPesData();
        if (mPesFilter != NULL) {
            mPesFilter->pes_len = 0;
        }
    }

    if (mPesFilter) {
        size = mPesFilter->pes_len + len;
        if (size > mPesFilter->pes_cap) {
            int cap = mPesFilter->pes_cap * 2;

            if (cap < size)
                cap = size;

            mPesFilter->pes_data = (uint8_t *)realloc(mPesFilter->pes_data, cap);

            mPesFilter->pes_cap = cap;
        }
        memcpy(mPesFilter->pes_data + mPesFilter->pes_len, data, len);
        mPesFilter->pes_len += len;
    }
    if (mPesFilter != NULL && mPesFilter->pes_len >= 6) {
        int      len;
        uint8_t *p = mPesFilter->pes_data;

        len = (p[4] << 8) | p[5];

        if (mPesFilter != NULL && len && (mPesFilter->pes_len >= len + 6)) {
            postPesData();
            if (mPesFilter != NULL) {
                mPesFilter->pes_len  = 0;
                mPesFilter->has_pusi = 0;
            }
        }
    }

}

void AmPesFilter::ts_packet(uint8_t *pkt) {
    if (mPesFilter == NULL) return;
    uint8_t *p    = pkt;
    int      left = 188;
    int      tei, pusi, tsc, afc, cc;
    int      pid;
    int      discon = 0;

    pid = ((p[1] << 8) | p[2]) & 0x1fff;
    if (mPesFilter != NULL && pid != mPesFilter->pid)
        return;

    tei  = p[1] & 0x80;
    pusi = p[1] & 0x40;
    tsc  = (p[3] & 0xc0) >> 6;
    afc  = (p[3] & 0x30) >> 4;
    cc   = p[3] & 0xf;

    if (mPesFilter != NULL && mPesFilter->cc != -1) {
        if (((mPesFilter->cc + 1) & 0xf) != cc) {
            discon = 1;
        }
    }

    if (mPesFilter != NULL) {
        mPesFilter->cc = cc;
        if (pusi) {
            mPesFilter->has_pusi = 1;
        } else if (discon) {
            ALOGE("CC discontinuity");
            mPesFilter->has_pusi = 0;
        }
        if (tsc || tei) {
            mPesFilter->has_pusi = 0;
        }
        if (!mPesFilter->has_pusi || !(afc & 1))
            return;
    } else {
        ALOGE("mPesFilter is null");
        return;
    }
    p    += 4;
    left -= 4;

    if (afc & 2) {
        int alen = *p;

        p    += alen + 1;
        left -= alen - 1;
    }

    if (left <= 0)
        return;

    ts_payload(pusi, p, left);
}

void AmPesFilter::extractPesDataFromTsPacket(int pid, uint8_t * ts, int len) {
    if (ts == NULL || mPesFilter == NULL) return;
    if (!len) return;
    ALOGD("[%s/%d] pid = %d", __FUNCTION__, __LINE__, pid);
    uint8_t *p;
    int left;
    p    = ts;
    left = len;
    mPesFilter->pid = pid;
    if (mPesFilter != NULL && mPesFilter->left_len) {
        int n = 188 - mPesFilter->left_len;
        if (n > len)
            n = len;
        memcpy(mPesFilter->left_data + mPesFilter->left_len, p, n);
        mPesFilter->left_len += n;
        p                    += n;
        left                 -= n;
        if (mPesFilter != NULL && mPesFilter->left_len == 188) {
            ts_packet(mPesFilter->left_data);
            mPesFilter->left_len = 0;
        } else {
            return;
        }
    }

    while (left) {
        if (*p == 0x47) {
            if (left < 188)
                break;

            ts_packet(p);

            p    += 188;
            left -= 188;
        } else {
            p    ++;
            left --;
        }
    }

    if (left && mPesFilter != NULL) {
        memcpy(mPesFilter->left_data, p, left);
        mPesFilter->left_len = left;
    }
}

void AmPesFilter::release() {
    if (mPesFilter != NULL) {
        if (mPesFilter->pes_data != NULL) {
            free(mPesFilter->pes_data);
            mPesFilter->pes_data = NULL;
            mPesFilter->cb = NULL;
        }
        free(mPesFilter);
        mPesFilter= NULL;
    }
}

AmPesFilter::~AmPesFilter() {

}
