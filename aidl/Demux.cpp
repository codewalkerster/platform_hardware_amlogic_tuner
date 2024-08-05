/*
 * Copyright 2021 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "tunerhal2.0-Demux"

#include <aidl/android/hardware/tv/tuner/DemuxQueueNotifyBits.h>
#include <aidl/android/hardware/tv/tuner/Result.h>

#include <utils/Log.h>
#include "Demux.h"
#include <cutils/properties.h>
#include <sys/prctl.h>
#include "FileSystemIo.h"

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

namespace {
bool isValidTsPacket(const vector<uint8_t>& tsPacket) {
    return tsPacket[0] == 0x47;
}
}  // namespace

#define WAIT_TIMEOUT 3000000000
#define PSI_MAX_SIZE 4096
#define PES_RAW_DATA_SIZE 64 * 1024
#define TEMI_DATA_SIZE 4 * 1024
#define PRIVATE_STREAM_1   0x1bd
#define PRIVATE_STREAM_2   0x1bf
#define SUPPORT_SOFTWARE_DEMUX_SUBTITLE "vendor.tunerhal.softwaredemux.subtitle"
#define VIDEO_BUFFER_SIZE  "/sys/module/amlogic_dvb_demux/parameters/video_buf_size"
#define AUDIO_BUFFER_SIZE  "/sys/module/amlogic_dvb_demux/parameters/audio_buf_size"
#define TUNERHAL_DUMP_TS_DATA "vendor.tf.dump.ts"
#define TSO_SOURCE    "/sys/class/stb/tso_source"
#define SUPPORT_SOFTWARE_DEMUX_TEMI "vendor.tunerhal.softwaredemux.temi"

static int connectcicam_ref;
static sp<AmCI> mAmCI = NULL;
static void dump(uint8_t* data, int len) {
    int i;
    string strData;
    char ch[3];
    for (i = 0; i < len; i++)
    {
        snprintf(ch, 3, "%02x", data[i]);
        strData += ch;
    }
    ALOGD("dump bytes: %s", strData.c_str());
}

static vector<int8_t> uint8DataToInt8Data(vector<uint8_t> uInt8Data) {
    vector<int8_t> int8Data;
    int len = uInt8Data.size();
    int8Data.resize(len);
    memcpy(int8Data.data(), uInt8Data.data(), len * sizeof(uint8_t));
    return int8Data;
}

#if PLATFORM_SDK_VERSION > 33
Demux::Demux(int32_t demuxId, uint32_t filterTypes) {
    FileSystem_create();

    if (FileSystem_writeFile(VIDEO_BUFFER_SIZE,"15728640") != 0) {
        ALOGE("set video_buf_size erro %p\n",this);
    }
    if (FileSystem_writeFile(AUDIO_BUFFER_SIZE,"3145728") != 0) {
        ALOGE("set audio_buf_size erro %p\n",this);
    }
    mDemuxId = demuxId;
    mFilterTypes = filterTypes;
    mCiCamId = 0;
    mFrontendInputThreadRunning = false;
    mKeepFetchingDataFromFrontend = false;

}

void Demux::setTunerService(std::shared_ptr<Tuner> tuner) {
    ALOGD("setTunerService");
    mTuner = tuner;
    bSupportSoftDemuxForSubtitle =  property_get_bool(SUPPORT_SOFTWARE_DEMUX_SUBTITLE, true);
    bSupportSoftDemuxForTemi = property_get_bool(SUPPORT_SOFTWARE_DEMUX_TEMI, true);
    ALOGD("mDemuxId:%d, bSupportSoftDemuxForSubtitle = %d, bSupportSoftDemuxForTemi = %d", mDemuxId, bSupportSoftDemuxForSubtitle, bSupportSoftDemuxForTemi);
    AmDmxDevice = new AM_DMX_Device(mDemuxId);
    AmDmxDevice->AM_DMX_Open();

    mHwDemuxOps = new HwDemuxOpsSCWrap();
    if (mHwDemuxOps != nullptr) {
        mDemuxHandle = mHwDemuxOps->AmHwDemux_Create(0, NULL);
    }
}
#else
Demux::Demux(int32_t demuxId, std::shared_ptr<Tuner> tuner) {
    FileSystem_create();

    if (FileSystem_writeFile(VIDEO_BUFFER_SIZE,"15728640") != 0) {
        ALOGE("set video_buf_size erro %p\n",this);
    }
    if (FileSystem_writeFile(AUDIO_BUFFER_SIZE,"3145728") != 0) {
        ALOGE("set audio_buf_size erro %p\n",this);
    }

    mDemuxId = demuxId;
    mTuner = tuner;
    mCiCamId = 0;
    mFrontendInputThreadRunning = false;
    mKeepFetchingDataFromFrontend = false;
    bSupportSoftDemuxForSubtitle =  property_get_bool(SUPPORT_SOFTWARE_DEMUX_SUBTITLE, true);
    bSupportSoftDemuxForTemi = property_get_bool(SUPPORT_SOFTWARE_DEMUX_TEMI, true);
    ALOGD("mDemuxId:%d, bSupportSoftDemuxForSubtitle = %d, bSupportSoftDemuxForTemi = %d", mDemuxId, bSupportSoftDemuxForSubtitle, bSupportSoftDemuxForTemi);
    AmDmxDevice = new AM_DMX_Device(mDemuxId);
    AmDmxDevice->AM_DMX_Open();

    mHwDemuxOps = new HwDemuxOpsSCWrap();
    if (mHwDemuxOps != nullptr) {
        mDemuxHandle = mHwDemuxOps->AmHwDemux_Create(0, NULL);
    }
}
#endif

Demux::~Demux() {
    ALOGD("~Demux");
    //close();
}

void Demux::pesDataCallback(void* demux, int fid, uint8_t *pes, int len) {
    ALOGD("[%s/%d] fid = %d", __FUNCTION__, __LINE__, fid);

    int i;
    string strData;
    char ch[3];
    for (i = 0; i < len; i++)
    {
        snprintf(ch, 3, "%02x", pes[i]);
        strData += ch;
    }
    if ((pes[0] != 0) || (pes[1] != 0) || (pes[2] != 1) || (pes[3] != 0xbd)) {
        ALOGE("PES is not start with 00 00 01 bd");
        return;
    }
    //ALOGD("dump bytes: %s", strData.c_str());

    Demux *dmxDev = (Demux*)demux;
    vector<int8_t> pesData;
    pesData.resize(len);
    memcpy(pesData.data(), pes, len * sizeof(uint8_t));
    dmxDev->updateFilterOutput(fid, pesData);
    dmxDev->startFilterHandler(fid);
}

void Demux::combinePesData(int64_t filterId) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);
    uint8_t tmpbuf[8] = {0};
    uint8_t tmpbuf1[8] = {0};
    int64_t pts = 0, dts = 0;
    int64_t tempPts = 0, tempDts = 0;
    int result = -1;
    int packetLen = 0, pesHeaderLen = 0;
    bool needSkipData = false;
    int64_t packetHeader = 0;
    int stream_id = 0;
    vector<uint8_t> pesData;
    int size = 1;
    while (AmDmxDevice->AM_DMX_Read(filterId, tmpbuf, &size) == 0) {
        packetHeader = ((packetHeader<<8) & 0x000000ffffffff00) | tmpbuf[0];
        //ALOGD("[Demux] packetHeader = %llx", packetHeader);
        stream_id = packetHeader & 0xffffffff;
        if (stream_id == PRIVATE_STREAM_1 || stream_id == PRIVATE_STREAM_2) {
            //ALOGD("## [Demux] combinePesData %x,%llx,-----------\n", tmpbuf[0], packetHeader & 0xffffffffff);
            size = 2;
            result = AmDmxDevice->AM_DMX_Read(filterId, tmpbuf1, &size);
            packetLen = (tmpbuf1[0] << 8) | tmpbuf1[1];
            ALOGD("[Demux] packetLen = %d", packetLen);
            if (packetLen >= 3) {
                pesData.resize(packetLen + 6);
                pesData[0] = 0x0;
                pesData[1] = 0x0;
                pesData[2] = 0x01;
                pesData[3] = tmpbuf[0];
                pesData[4] = tmpbuf1[0];
                pesData[5] = tmpbuf1[1];
                size = 3;
                result =  AmDmxDevice->AM_DMX_Read(filterId, pesData.data() + 6, &size);
                packetLen -= 3;
                pesHeaderLen = pesData[8];
                ALOGD("[Demux] pesHeaderLen = %d", pesHeaderLen);
                if (packetLen >= pesHeaderLen) {
                    if ((pesData[7] & 0xc0) == 0x80) {
                        result = AmDmxDevice->AM_DMX_Read(filterId, pesData.data() + 6 + 3, &pesHeaderLen);
                        if (result == 0) {
                            tempPts = (int64_t)(pesData[9] & 0xe) << 29;
                            tempPts = tempPts | ((pesData[10] & 0xff) << 22);
                            tempPts = tempPts | ((pesData[11] & 0xfe) << 14);
                            tempPts = tempPts | ((pesData[12] & 0xff) << 7);
                            tempPts = tempPts | ((pesData[13] & 0xfe) >> 1);
                            pts = tempPts;
                            packetLen -= pesHeaderLen;
                        }
                    } else if ((pesData[7] & 0xc0) == 0xc0) {
                        result = AmDmxDevice->AM_DMX_Read(filterId, pesData.data() + 6 + 3, &pesHeaderLen);
                        if (result == 0) {
                            tempPts = (int64_t)(pesData[9] & 0xe) << 29;
                            tempPts = tempPts | ((pesData[10] & 0xff) << 22);
                            tempPts = tempPts | ((pesData[11] & 0xfe) << 14);
                            tempPts = tempPts | ((pesData[12] & 0xff) << 7);
                            tempPts = tempPts | ((pesData[13] & 0xfe) >> 1);
                            pts = tempPts; // - pts_aligned;
                            tempDts = (int64_t)(pesData[14] & 0xe) << 29;
                            tempDts = tempDts | ((pesData[15] & 0xff) << 22);
                            tempDts = tempDts | ((pesData[16] & 0xfe) << 14);
                            tempDts = tempDts | ((pesData[17] & 0xff) << 7);
                            tempDts = tempDts | ((pesData[18] & 0xfe) >> 1);
                            dts = tempDts; // - pts_aligned;
                            packetLen -= pesHeaderLen;
                        }
                    } else {
                        needSkipData = true;
                    }
                } else {
                    needSkipData = true;
                }
            } else {
                needSkipData = true;
            }

            if (needSkipData) {
                ALOGD("[Demux] need to skip pes data");
                return;
            } else if ((pts) && (packetLen > 0)) {
                int readLen = 0;
                int dataLen = 0;
                do {
                    dataLen = packetLen - readLen;
                    result = AmDmxDevice->AM_DMX_Read(filterId, pesData.data() + 6 + 3 + pesHeaderLen +
readLen, &dataLen);
                    //ALOGD("[Demux] result = 0x%x", result);
                    if (result == AM_SUCCESS) {
                        readLen += dataLen;
                    } else if (result == AM_FAILURE) {
                        ALOGD("[Demux] pes data read fail");
                        return;
                    }
                } while(readLen < packetLen);
            }

            updateFilterOutput(filterId, uint8DataToInt8Data(pesData));
            startFilterHandler(filterId);
            return;
        } else {
            // advance header, not report error if no problem.
            if (tmpbuf[0] == 0xFF) {
                if (packetHeader == 0xFF || packetHeader == 0xFFFF || packetHeader == 0xFFFFFF
                    || packetHeader == 0xFFFFFFFF || packetHeader == 0xFFFFFFFFFF) {
                    continue;
                }
            } else if (tmpbuf[0] == 0) {
                if (packetHeader == 0xff00 || packetHeader == 0xff0000 || packetHeader == 0xffffffff00 ||
packetHeader == 0xffffff0000) {
                    continue;
                }
            } else if (tmpbuf[0] == 1 && (packetHeader == 0xffff000001 || packetHeader == 0xff000001)) {
                continue;
            }
        }
    }
}

void Demux::getSectionData(int64_t filterId) {
    vector<uint8_t> sectionData;
    int sectionSize = PSI_MAX_SIZE;

    sectionData.resize(sectionSize);
    int readRet = AmDmxDevice->AM_DMX_Read(filterId, sectionData.data(), &sectionSize);
    if (readRet != 0) {
        ALOGE("AM_DMX_Read failed! readRet:0x%x", readRet);
        return;
    } else {
        ALOGV("fid =%" PRIu64 " section data size:%d", filterId, sectionSize);
        sectionData.resize(sectionSize);
        /*
        //for debug
        uint16_t tableId = sectionData[0];
        if (tableId == 0x0) {
            ALOGD("received PAT table tableId = %d, fid = %lld", tableId, filterId);
            int i;
            string strData;
            char ch[3];
            for (i = 0; i < sectionData.size(); i++)
            {
                snprintf(ch, 3, "%02x", sectionData[i]);
                strData += ch;
            }
            ALOGD("dmxid = %d, fid = %lld, dump PAT data bytes: %s", mDemuxId, filterId, strData.c_str());
        }
        if (tableId == 0x2) {
            ALOGD("received PMT table tableId = %d, fid = %lld", tableId, filterId);
        }*/
        updateFilterOutput(filterId, uint8DataToInt8Data(sectionData));
        startFilterHandler(filterId);
    }

}

void Demux::getPesRawData(int64_t filterId) {
    vector<uint8_t> pesRawData;
    int pesRawDataSize = PES_RAW_DATA_SIZE;
    pesRawData.resize(pesRawDataSize);
    int readRet = AmDmxDevice->AM_DMX_Read(filterId, pesRawData.data(), &pesRawDataSize);
    if (readRet != 0) {
        ALOGE("AM_DMX_Read failed! readRet:0x%x", readRet);
        return;
    } else {
        ALOGD("fid =%" PRIu64 " pes raw data size:%d", filterId, pesRawDataSize);
        pesRawData.resize(pesRawDataSize);
        updateFilterOutput(filterId, uint8DataToInt8Data(pesRawData));
        startFilterHandler(filterId);
    }
}

void Demux::getTemiData(int64_t filterId) {
    vector<uint8_t> temiData;
    int temiDataSize = TEMI_DATA_SIZE;
    temiData.resize(temiDataSize);
    int readRet = AmDmxDevice->AM_DMX_Read(filterId, temiData.data(), &temiDataSize);
    if (readRet != 0) {
        ALOGE("AM_DMX_Read failed! readRet:0x%x", readRet);
        return;
    } else {
        ALOGD("fid =%" PRIu64 " Temi data size:%d", filterId, temiDataSize);
        temiData.resize(temiDataSize);
        updateFilterOutput(filterId, uint8DataToInt8Data(temiData));
        startFilterHandler(filterId);
    }
}

void Demux::postData(void* demux, int fid, bool esOutput, bool passthrough) {
    vector<uint8_t> tmpData;
    Demux *dmxDev = (Demux*)demux;
    if (dmxDev == NULL) {
        ALOGD("get demux device is NULL in demux thread");
        return;
    }
    ALOGV("[Demux] postData fid =%d esOutput:%d dev_no:%d", fid, esOutput, dmxDev->getAmDmxDevice()->dev_no);

#ifdef TUNERHAL_DBG
    static int postDataSize = 0;
    if (esOutput == true && postDataSize != mFilterOutputTotalLen/1024/1024) {
        postDataSize = mFilterOutputTotalLen/1024/1024;
        ALOGD("postData fid =%d Total:%d MB", fid, postDataSize);
    }
#endif

    if (esOutput) {
        if (passthrough) {
            int size = sizeof(dmx_sec_es_data) * 500;
            tmpData.resize(size);
            int readRet = dmxDev->getAmDmxDevice()
                          ->AM_DMX_Read(fid, tmpData.data(), &size);
            if (readRet != 0) {
                return;
            } else {
                dmxDev->updateFilterOutput(fid, uint8DataToInt8Data(tmpData));
                dmxDev->startFilterHandler(fid);
            }
        } else {
            int headerLen = sizeof(dmx_non_sec_es_header);
            tmpData.resize(headerLen);
            int read_len = 0;
            int data_len = 0;
            int readRet  = 0;
            do {
                data_len = headerLen - read_len;
                readRet = dmxDev->getAmDmxDevice()
                              ->AM_DMX_Read(fid, tmpData.data(), &data_len);
                if (readRet == AM_FAILURE) {
                    ALOGD("maybe filter has been closed, readRet = %d", readRet);
                    return;
                }
                if (readRet == 0) {
                    read_len += data_len;
                }
            } while(read_len < headerLen);

            dmx_non_sec_es_header* esHeader = (dmx_non_sec_es_header*)(tmpData.data());
            uint32_t dataLen = esHeader->len;
            //tmpData.resize(headerLen + dataLen);
            tmpData.resize(dataLen);
            readRet = 1;
            uint32_t readLen = dataLen;
            uint32_t totalLen = 0;
            while (readRet) {
                readRet = dmxDev->getAmDmxDevice()
                      ->AM_DMX_Read(fid, tmpData.data()/* + headerLen*/, (int*)(&readLen));
                if (readRet == AM_FAILURE) {
                    ALOGD("maybe filter has been closed, readRet = %d", readRet);
                    return;
                }
                if (readRet)
                    continue;
                totalLen += readLen;
                if (totalLen < dataLen) {
                    ALOGD("totalLen= %d, dataLen = %d", totalLen, dataLen);
                    readLen = dataLen - totalLen;
                    readRet = 1;
                    continue;
                }
#ifdef TUNERHAL_DBG
                mFilterOutputTotalLen += dataLen;
                mDropLen += dataLen;
                if (mDropLen > mDropTsPktNum * 188) {
                    //insert tmpData to mFilterOutput
                    dmxDev->updateFilterOutput(fid, uint8DataToInt8Data(tmpData));
                    //Copy mFilterOutput to av ion buffer and create mFilterEvent
                    dmxDev->startFilterHandler(fid);
                    static int tempMB = 0;
                    if (mFilterOutputTotalLen/1024/1024 % 2 == 0 && tempMB != mFilterOutputTotalLen/1024/1024) {
                        tempMB = mFilterOutputTotalLen/1024/1024;
                        ALOGD("mFilterOutputTotalLen:%d MB", tempMB);
                    }
                } else {
                    ALOGW("mDropLen:%d KB [%d ts pkts]", mDropLen/1024, mDropLen/188);
                }
                if (mDumpEsData == 1) {
                    FILE *filedump = fopen("/data/dump/demux_out.es", "ab+");
                    if (filedump != NULL) {
                        fwrite(tmpData.data(), 1, dataLen, filedump);
                        fflush(filedump);
                        fclose(filedump);
                        filedump = NULL;
                        ALOGD("Dump dataLen:%d", dataLen);
                    } else {
                       ALOGE("Open demux_out.es failed!\n");
                    }
                }
#else
            //insert tmpData to mFilterOutput
            dmxDev->updateFilterOutput(fid, uint8DataToInt8Data(tmpData));
            //Copy mFilterOutput to av ion buffer and create mFilterEvent
            dmxDev->startFilterHandler(fid);
#endif
            }
        }
    } else {
        bool bTemiFilterId = dmxDev->checkTemiFilterId(fid);
        if (bTemiFilterId) {
            dmxDev->getTemiData(fid);
            return;
        }
        bool isPesFilterId = dmxDev->checkPesFilterId(fid);
        if (isPesFilterId) {
            if (dmxDev->isRawData(fid)) {
                dmxDev->getPesRawData(fid);
            } else {
                if (!dmxDev->checkSoftDemuxForSubtitle()) {
                    ALOGD("start pes data combine fid = %d", fid);
                    dmxDev->combinePesData(fid);
                } else {
                    ALOGD("record ts packet filterid = %d", fid);
                    dmxDev->recordTsPacketForPesData(fid);
                }
            }
        } else {
            dmxDev->getSectionData(fid);
        }
    }
}

::ndk::ScopedAStatus Demux::setFrontendDataSource(int32_t in_frontendId) {
    ALOGD("%s", __FUNCTION__);

    if (mTuner == nullptr) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::NOT_INITIALIZED));
    }

    mFrontend = mTuner->getFrontendById(in_frontendId);
    if (mFrontend == nullptr) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_STATE));
    }

    mTuner->setFrontendAsDemuxSource(in_frontendId, mDemuxId);
    if (AmDmxDevice) {
        mTsInput = mTuner->getTsInput(in_frontendId);

        if (checkCiCamInsert()) {
                if (getDemuxSource() < 0x0F)/*0~15 for PCMCIA type,16~31 USB type */
                    AmDmxDevice->AM_DMX_SetSource(mDemuxId, INPUT_DEMOD, FRONTEND_TS1);
                else
                    AmDmxDevice->AM_DMX_SetSource(mDemuxId, INPUT_LOCAL, DMA_4);
        }
        else
            AmDmxDevice->AM_DMX_SetSource(mDemuxId, INPUT_DEMOD, mTsInput);

        if (!mAmCI)
            mAmCI = new AmCI(mDemuxId,mTsInput, INPUT_DEMOD);

    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Demux::openFilter(const DemuxFilterType& in_type, int32_t in_bufferSize,
                                       const std::shared_ptr<IFilterCallback>& in_cb,
                                       std::shared_ptr<IFilter>* _aidl_return) {
    int64_t filterId;
    int32_t dmxFilterIdx;
    bool hasTsFilterType = false;
    DemuxTsFilterType tsFilterType = DemuxTsFilterType::UNDEFINED;
    switch (in_type.mainType) {
        case DemuxFilterMainType::TS:
            tsFilterType = in_type.subType.get<DemuxFilterSubType::Tag::tsFilterType>();
            hasTsFilterType = true;
            break;
        case DemuxFilterMainType::MMTP:
            break;
        case DemuxFilterMainType::IP:
            break;
        case DemuxFilterMainType::TLV:
            break;
        case DemuxFilterMainType::ALP:
            break;
        default:
            break;

    }
    if (in_cb == nullptr) {
        ALOGW("[Demux] callback can't be null");
        *_aidl_return = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (AmDmxDevice->AM_DMX_AllocateFilter(&dmxFilterIdx) != 0) {
        ALOGE("Allocate filterid fail");
        *_aidl_return = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }
    filterId = dmxFilterIdx;
    std::shared_ptr<Filter> filter = ndk::SharedRefBase::make<Filter>(
            in_type, filterId, in_bufferSize, in_cb, this->ref<Demux>());
    ALOGD("[%s/%d] Allocate filter subType:%d filterIdx:%" PRIu64 ", bufferSize:%d KB", __FUNCTION__, __LINE__, tsFilterType, filterId, in_bufferSize/1024);
    if (!filter->createFilterMQ()) {
        *_aidl_return = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNKNOWN_ERROR));
    }

    if (hasTsFilterType) {
        if (tsFilterType == DemuxTsFilterType::SECTION
            || tsFilterType == DemuxTsFilterType::VIDEO
            || tsFilterType == DemuxTsFilterType::AUDIO
            || tsFilterType == DemuxTsFilterType::PES
            || tsFilterType == DemuxTsFilterType::TEMI) {
            AmDmxDevice->AM_DMX_SetCallback(dmxFilterIdx, postData, this);
        } else if (tsFilterType == DemuxTsFilterType::PCR) {
            AmDmxDevice->AM_DMX_SetCallback(dmxFilterIdx, NULL, NULL);
        } else if (tsFilterType == DemuxTsFilterType::RECORD) {
            //mAmDvrDevice[mDemuxId]->AM_DVR_SetCallback(postDvrData, this);
        }
    }

    if (hasTsFilterType && tsFilterType == DemuxTsFilterType::PES) {
        mAmPesFilter = new AmPesFilter(dmxFilterIdx, pesDataCallback, this);
        mPesFilterIds.insert(dmxFilterIdx);
        ALOGD("Insert PES filter mPesFid = %d", dmxFilterIdx);
    }

    if (hasTsFilterType && filter->isPcrFilter()) {
        int32_t pcrFid = -1;
        filter->getId(&pcrFid);
        mPcrFilterIds.insert(pcrFid);
        ALOGD("Insert pcr filter  pcrFid = %d", pcrFid);
    }

    if (hasTsFilterType && tsFilterType == DemuxTsFilterType::TEMI) {
        mTemiFilterIds.insert(dmxFilterIdx);
        ALOGD("Insert Temi filter");
        if (bSupportSoftDemuxForTemi) {
            mTemiRecordThreadRunning = true;
            mTemiRecordThread = std::thread(&Demux::TemiRecordThreadLoop, this);
            ALOGD("create Temi Record Thread");
        }
    }

    bool result = true;
    if (hasTsFilterType && tsFilterType != DemuxTsFilterType::RECORD && tsFilterType != DemuxTsFilterType::PCR) {
        // Only save non-record filters for now. Record filters are saved when the
        // IDvr.attacheFilter is called.
        mPlaybackFilterIds.insert(filterId);
        if (mDvrPlayback != nullptr) {
            ALOGD("[%s/%d] addPlaybackFilter filterIdx:%" PRIu64 "", __FUNCTION__, __LINE__, filterId);
            result = mDvrPlayback->addPlaybackFilter(filterId, filter);
        }
    }

    if (!result) {
        *_aidl_return = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }
    mFilters[filterId] = filter;
    *_aidl_return = filter;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Demux::openTimeFilter(std::shared_ptr<ITimeFilter>* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    mTimeFilter = ndk::SharedRefBase::make<TimeFilter>(this->ref<Demux>());

    *_aidl_return = mTimeFilter;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Demux::getAvSyncHwId(const std::shared_ptr<IFilter>& in_filter,
                                          int32_t* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    int64_t fid;
    ::ndk::ScopedAStatus status;
    int mode = 0;

    status = in_filter->getId64Bit(&fid);
    if (!status.isOk()) {
        ALOGE("[Demux] Can't get filter Id.");
        *_aidl_return = -1;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_STATE));
    }

    if (fid > DMX_FILTER_COUNT + START_FILTERID_FROM_ONE) {
        fid = findFilterIdByfakeFilterId(fid);
    }

    if (mMediaSync == nullptr) {
        ALOGD("[debuglevel] new mediasync");
        mMediaSync = new MediaSyncWrap();
    }

    ALOGD("%s/%d fid = %" PRIu64 "", __FUNCTION__, __LINE__, fid);
    std::lock_guard<std::mutex> lock(mFilterLock);
    set<int64_t>::iterator it;
    if (mDvrPlayback != nullptr) {
       uint16_t avPid;
       for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
           avPid = mFilters[*it]->getTpid();
           DemuxFilterType type = mFilters[*it]->getFilterType();
           ALOGD("%s/%d avPid = %u", __FUNCTION__, __LINE__, avPid);
           if (type.subType.get<DemuxFilterSubType::Tag::tsFilterType>() == DemuxTsFilterType::VIDEO) {
               mVidPid = avPid;
           }

           if (type.subType.get<DemuxFilterSubType::Tag::tsFilterType>() == DemuxTsFilterType::AUDIO) {
               mAudPid = avPid;
           }
       }
    }
    if (mFilters[fid] != nullptr && mFilters[fid]->isMediaFilter() && !mPlaybackFilterIds.empty()) {
        uint16_t avPid = getFilterTpid(*mPlaybackFilterIds.begin());
        //DemuxFilterType type = mFilters[fid]->getFilterType();
        if (mMediaSync != nullptr) {
            if (mAvSyncHwId == -1) {
                 if (!mPcrFilterIds.empty()) {
                    mAvSyncHwId = *mPcrFilterIds.begin();
                    ALOGD("%s/%d mAvSyncHwId = %" PRIu64 "", __FUNCTION__, __LINE__, *mPcrFilterIds.begin());
                    mMediaSync->setParameter(MEDIASYNC_KEY_ISOMXTUNNELMODE, &mode);
                    mMediaSync->bindStaticAvSyncId(mAvSyncHwId);
                    uint16_t pcrPid = getFilterTpid(*mPcrFilterIds.begin());
                    mMediaSync->setPcrAndDmxId(mDemuxId, pcrPid);
                    mMediaSync->setSyncMode(MEDIA_SYNC_PCRMASTER);
                 } else {
                    mAvSyncHwId = mMediaSync->getAvSyncHwId(mDemuxId, -1);
                    mMediaSync->setParameter(MEDIASYNC_KEY_ISOMXTUNNELMODE, &mode);
                    mMediaSync->bindAvSyncId(mAvSyncHwId);
                }
            }
        }

        struct AmDemuxControlInfo info;
        info.demuxId = mDemuxId;
        info.mediasyncId = mAvSyncHwId;

        if (mDemuxHandle && mHwDemuxOps && mDvrPlayback) {
            ALOGD("%s/%d 0x%x 0x%x %u %" PRIu64 "", __FUNCTION__, __LINE__, mVidPid, mAudPid, mDemuxId, mAvSyncHwId);
            mHwDemuxOps->AmHwDemux_Init(mDemuxHandle, 0, &info);
            mWriteTsSize = 0;
        }

        ALOGD("[Demux] mAvFilterId:%" PRIu64 " avPid:0x%x avSyncHwId:%" PRIu64 "", *mPlaybackFilterIds.begin(), avPid, mAvSyncHwId);
        *_aidl_return = mAvSyncHwId;
        return ::ndk::ScopedAStatus::ok();
    } else if (mFilters[fid] != nullptr && mFilters[fid]->isPcrFilter() && !mPcrFilterIds.empty()) {
        // Return the lowest pcr filter id in the default implementation as the av sync id
        uint16_t pcrPid = getFilterTpid(*mPcrFilterIds.begin());
        if (mMediaSync != nullptr) {
            if (mAvSyncHwId == -1) {
                mAvSyncHwId = *mPcrFilterIds.begin();//mMediaSync->getAvSyncHwId(mDemuxId, pcrPid);
                mMediaSync->setParameter(MEDIASYNC_KEY_ISOMXTUNNELMODE, &mode);
                mMediaSync->bindStaticAvSyncId(mAvSyncHwId);
                uint16_t pcrPid = getFilterTpid(*mPcrFilterIds.begin());
                mMediaSync->setPcrAndDmxId(mDemuxId, pcrPid);
                mMediaSync->setSyncMode(MEDIA_SYNC_PCRMASTER);
            }
        }
        ALOGD("[Demux] mPcrFilterId:%" PRIu64 " pcrPid:0x%x avSyncHwId:%" PRIu64 "", *mPcrFilterIds.begin(), pcrPid, mAvSyncHwId);
         *_aidl_return = mAvSyncHwId;
        return ::ndk::ScopedAStatus::ok();
    } else {
        ALOGD("[Demux] No pcr or No media filter opened.");
        *_aidl_return = -1;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
            static_cast<int32_t>(Result::INVALID_STATE));
    }

    /*
    if (!mFilters[id]->isMediaFilter()) {
        ALOGE("[Demux] Given filter is not a media filter.");
        *_aidl_return = -1;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_STATE));
    }

    if (!mPcrFilterIds.empty()) {
        // Return the lowest pcr filter id in the default implementation as the av sync id
        *_aidl_return = *mPcrFilterIds.begin();
        return ::ndk::ScopedAStatus::ok();
    }

    ALOGE("[Demux] No PCR filter opened.");
    *_aidl_return = -1;
    */
    return ::ndk::ScopedAStatus::fromServiceSpecificError(
            static_cast<int32_t>(Result::INVALID_STATE));
}

::ndk::ScopedAStatus Demux::getAvSyncTime(int32_t in_avSyncHwId, int64_t* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    /*
    if (mPcrFilterIds.empty()) {
        *_aidl_return = -1;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_STATE));
    }
    if (in_avSyncHwId != *mPcrFilterIds.begin()) {
        *_aidl_return = -1;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }

    *_aidl_return = -1;
    */
    uint64_t avSyncTime = -1;

    if (mMediaSync != nullptr) {
        int64_t time = -1;
        time = mMediaSync->getAvSyncTime();
        avSyncTime = 0x1FFFFFFFF & ((9*time)/100);
    }
    *_aidl_return = avSyncTime;
    //ALOGD("%s/%d avSyncTime = %llu", __FUNCTION__, __LINE__, avSyncTime);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Demux::close() {
    ALOGD("[%s/%d] mDemuxId:%d", __FUNCTION__, __LINE__, mDemuxId);
    //std::lock_guard<std::mutex> lock(mFilterLock);
    stopFrontendInput();

    set<int64_t>::iterator it;
    if (mDvrPlayback != nullptr) {
        for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
            mDvrPlayback->removePlaybackFilter(*it);
        }
    }
    mPlaybackFilterIds.clear();
    mRecordFilterIds.clear();
    mFilters.clear();
    mPcrFilterIds.clear();
    mPesFilterIds.clear();
    if (!mScrambledCache.empty())
        mScrambledCache.clear();
    if (!mClearCache.empty())
         mClearCache.clear();

    mLastUsedFilterId = -1;

    mDvrPlayback = nullptr;
    mDvrRecord   = nullptr;
    destroyMediaSync();

    bDemuxUsePlayback = false;
    bDemuxUseRecord   = false;

    if (mHwDemuxOps != nullptr) {
        if (mDemuxHandle) {
            mHwDemuxOps->AmHwDemux_Destroy(mDemuxHandle);
            mDemuxHandle = NULL;
        }
        mHwDemuxOps = nullptr;
        mWriteTsSize = 0;
    }

    if (AmDmxDevice != NULL) {
        AmDmxDevice->AM_DMX_Close();
        AmDmxDevice = NULL;
    }
    //if (mAmCI != nullptr) {
    //    mAmCI = nullptr;
    //}
    if (mTuner != nullptr) {
        mTuner->removeDemux(mDemuxId);
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Demux::openDvr(DvrType in_type, int32_t in_bufferSize,
                                    const std::shared_ptr<IDvrCallback>& in_cb,
                                    std::shared_ptr<IDvr>* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    if (in_cb == nullptr) {
        ALOGW("[Demux] DVR callback can't be null");
        *_aidl_return = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }

    std::lock_guard<std::mutex> lock(mFilterLock);
    set<int64_t>::iterator it;
    switch (in_type) {
        case DvrType::PLAYBACK:
            ALOGD("%s/%d DvrType::PLAYBACK bufferSize:%d KB", __FUNCTION__, __LINE__,  in_bufferSize/1024);
            mDvrPlayback = ndk::SharedRefBase::make<Dvr>(in_type, in_bufferSize, in_cb,
                                                        this->ref<Demux>(), mTuner);
            //ALOGD("[Demux] dmx_dvr_open INPUT_LOCAL demuxId = %d", mDemuxId);
            //AmDmxDevice->dmx_dvr_open(INPUT_LOCAL);
            if (!mDvrPlayback->createDvrMQ()) {
                mDvrPlayback = nullptr;
                *_aidl_return = mDvrPlayback;
                return ::ndk::ScopedAStatus::fromServiceSpecificError(
                        static_cast<int32_t>(Result::UNKNOWN_ERROR));
            }

            for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
                if (!mDvrPlayback->addPlaybackFilter(*it, mFilters[*it])) {
                    ALOGE("[Demux] Can't get filter info for DVR playback");
                    mDvrPlayback = nullptr;
                    *_aidl_return = mDvrPlayback;
                    return ::ndk::ScopedAStatus::fromServiceSpecificError(
                            static_cast<int32_t>(Result::UNKNOWN_ERROR));
                }
            }

            mVidPid = 0x1FFF;
            mAudPid = 0x1FFF;
            bDemuxUsePlayback = true;
            *_aidl_return = mDvrPlayback;
            return ::ndk::ScopedAStatus::ok();
        case DvrType::RECORD:
            ALOGD("%s/%d DvrType::RECORD bufferSize:%d KB", __FUNCTION__, __LINE__,  in_bufferSize/1024);
            mDvrRecord = ndk::SharedRefBase::make<Dvr>(in_type, in_bufferSize, in_cb,
                                                       this->ref<Demux>(), mTuner);
            //ALOGD("[Demux] dmx_dvr_open INPUT_DEMOD");
             //mAmDvrDevice->AM_DVR_Open(INPUT_DEMOD);
            if (!mDvrRecord->createDvrMQ()) {
                mDvrRecord = nullptr;
                *_aidl_return = mDvrRecord;

                return ::ndk::ScopedAStatus::fromServiceSpecificError(
                        static_cast<int32_t>(Result::UNKNOWN_ERROR));
            }
            mDesc = getDescrambler();
            if (mDesc != nullptr) {
                std::vector<uint8_t> keyToken = mDesc->getBackUpKeyToken();
                if (!keyToken.empty()) {
                    ALOGD("start to set keyToken");
                    mDesc->closeDsmSession();
                    mDesc->setKeyToken(keyToken);
                }
            }
            bDemuxUseRecord = true;
            *_aidl_return = mDvrRecord;
            return ::ndk::ScopedAStatus::ok();
        default:
            *_aidl_return = nullptr;
            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }
}

::ndk::ScopedAStatus Demux::connectCiCam(int32_t in_ciCamId) {
    ALOGD("%s TS change to passthough %d %d", __FUNCTION__,in_ciCamId, mDemuxId);
    if (bCiInsert == false)
    {
        mCiCamId = in_ciCamId;
        bCiInsert = true;
        connectcicam_ref++;
        if (mCiCamId < 0x0F) {/*0~15 for PCMCIA type,16~31 USB type */
            if (connectcicam_ref == 1) {
                FileSystem_create();

                if (FileSystem_writeFile(TSO_SOURCE, "ts2") != 0) {
                    ALOGE("set tso_source erro %p\n",this);
                }
            }

            if (AmDmxDevice != NULL) {
                ALOGD("%s passthough %d %d", __FUNCTION__,in_ciCamId, mDemuxId);
                AmDmxDevice->AM_DMX_SetSource(mDemuxId, INPUT_DEMOD, FRONTEND_TS1);
            }
        } else {
            if (connectcicam_ref == 1)
                mAmCI->CIUsbOpen();

            mAmCI->setDvbSource(mDemuxId, INPUT_LOCAL, DMA_4);

        }
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Demux::disconnectCiCam() {
    ALOGD("%s TS change to bypass %d %d", __FUNCTION__, mCiCamId, mDemuxId);
    if (bCiInsert == true)
    {
        bCiInsert = false;
        connectcicam_ref--;
        if (mCiCamId < 0x0F) {/*0~15 for PCMCIA type,16~31 USB type */
            if (AmDmxDevice != NULL) {
                AmDmxDevice->AM_DMX_SetSource(mDemuxId, INPUT_DEMOD, FRONTEND_TS2);
            }
        } else {
            ALOGD("%s mTsInput = %d", __FUNCTION__, mTsInput);
            mAmCI->setDvbSource(mDemuxId, INPUT_DEMOD, mTsInput);

            if (connectcicam_ref == 0)
                mAmCI->CIUsbClose();
        }
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Demux::removeFilter(int64_t filterId) {
    ALOGD("%s/%d filterId = %" PRIu64 "", __FUNCTION__, __LINE__, filterId);
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (mFilters[filterId] != nullptr) {
        mFilters[filterId]->clear();
    }
    mFilters.erase(filterId);
    mPlaybackFilterIds.erase(filterId);
    mRecordFilterIds.erase(filterId);
    if (checkPesFilterId(filterId)) {
        if (bSupportSoftDemuxForSubtitle) {
            closePesRecordFilter();
        }
        ALOGD("remove PES filter mPesFid = %" PRIu64 "", filterId);
        mPesFilterIds.erase(filterId);
    }

    if (bSupportSoftDemuxForTemi && mTemiFid == filterId) {
        closeTemiRecordFilter();
    }
    mTemiFilterIds.erase(filterId);

    if (mDvrPlayback != nullptr) {
        mDvrPlayback->removePlaybackFilter(filterId);
    }

    if (mFilters.size() == 0) {
        destroyMediaSync();
        if (mDemuxHandle && mHwDemuxOps && mDvrPlayback) {
            ALOGD("%s/%d ", __FUNCTION__, __LINE__);
            mHwDemuxOps->AmHwDemux_ResetStatus(mDemuxHandle);
            mWriteTsSize = 0;
        }
    }
    return ::ndk::ScopedAStatus::ok();
}

void Demux::startBroadcastTsFilter(vector<int8_t> data) {
     bool isDscReady = false;

     if (DEBUG_DEMUX)
         ALOGD("write to dvr %d size:%zd", mDemuxId, data.size());
      {
          std::lock_guard<std::mutex> lock(mFilterLock);
          if (0) {
              for (auto descramblerIt = mDescramblers.begin(); \
                   descramblerIt != mDescramblers.end(); \
                   descramblerIt++) {
                  if (descramblerIt->second && descramblerIt->second->isDescramblerReady())
                      isDscReady = true;
              }

              if (!isDscReady && mDescramblers.size() > 0) {
                  for (int tsDataIdx = 0; tsDataIdx < data.size(); tsDataIdx += 188) {
                      if (data[tsDataIdx] != 0x47)
                          ALOGW("ts sync byte: 0x%x", data[tsDataIdx]);
                      uint16_t pid = ((data[tsDataIdx + 1] & 0x1f) << 8) | ((data[tsDataIdx + 2] & 0xff));
                      for (auto descramblerIt = mDescramblers.begin(); \
                           descramblerIt != mDescramblers.end(); \
                           descramblerIt++) {
                          if (descramblerIt->second && descramblerIt->second->isPidSupported(pid)) {
                              if (mScrambledCache.size() > MAX_SCRAMBLED_CACHE_SIZE) {
                                  ALOGW("reset scrambled cache! cache size:%zu", mScrambledCache.size());
                                  vector<uint8_t>().swap(mScrambledCache);
                              }
                              mScrambledCache.insert(mScrambledCache.end(), data.begin() + tsDataIdx, data.begin() + tsDataIdx + 188);
                              ALOGV("scrambled cache idx:%d pid:0x%x size:%zu", tsDataIdx, pid, mScrambledCache.size());
                          } else {
                              mClearCache.insert(mClearCache.end(), data.begin() + tsDataIdx, data.begin() + tsDataIdx + 188);
                              ALOGV("clear cache idx:%d pid:0x%x size:%zu", tsDataIdx, pid, mClearCache.size());
                              break;
                          }
                      }
                  }
                  if (!mClearCache.empty()) {
                      int writeRetry = 0;
                      ALOGD("write clear cache size:%zu", mClearCache.size());
                      while (dvr_playback_write(mPlaybackhandle, mClearCache.data(), mClearCache.size()) == -1 \
                             && writeRetry <= 100) {
                          usleep(100 * 1000);
                          writeRetry ++;
                          ALOGW("write clear cache retry: %d", writeRetry);
                      }
                      vector<uint8_t>().swap(mClearCache);
                  }
                  return;
              }
          }
     }

     vector<uint8_t> udata;
     udata.resize(data.size());
     memcpy(udata.data(), data.data(), data.size() * sizeof(uint8_t));
     //clear stream inject
     if (isValidTsPacket(udata)) {
         if (mDemuxHandle && mHwDemuxOps) {
             while (mHwDemuxOps->AmHwDemux_GetStreamControlStatus(mDemuxHandle, NULL, mWriteTsSize,
                 mVidPid, mAudPid) != AM_DEMUX_OK) {
                 usleep(10 * 1000);
                 if (mDvrPlayback) {
                     if (mDvrPlayback->stopInjectTs()) {
                        ALOGD("[dvr] exit Inject, break!");
                        break;
                     }
                 } else {
                     break;
                 }
             }
         }
     }
     #if 0
     if (isDscReady && !mScrambledCache.empty()) {
         if (isValidTsPacket(udata))
             mScrambledCache.insert(mScrambledCache.end(), udata.begin(), udata.end());
         int writeRetry = 0;
         ALOGD("write scrambled cache size:%d", mScrambledCache.size());
         while (AmDmxDevice[mDemuxId] != NULL && AmDmxDevice[mDemuxId]->AM_DMX_WriteTs(mScrambledCache.data(), mScrambledCache.size(), 300 * 1000) == -1 && writeRetry <= 100) {
             usleep(100 * 1000);
             writeRetry ++;
             ALOGW("write scrambled cache retry: %d", writeRetry);
         }
         vector<uint8_t>().swap(mScrambledCache);
         return;
     }
     #endif
     if (isValidTsPacket(udata)) {
         while (dvr_playback_write(mPlaybackhandle, udata.data(), udata.size()) == -1) {
             usleep(100 * 1000);
             if (mDvrPlayback && mDvrPlayback->stopInjectTs()) {
                 ALOGD("[demux] stop Inject TS, break!");
                 break;
             }
             ALOGD("[Demux] wait for 100ms to write dvr device demuxId = %d", mDemuxId);
         }
        if (property_get_int32(TUNERHAL_DUMP_TS_DATA, 0) && getDemuxId() == 2) {
            FILE *filedump = fopen("/data/local/tmp/demux_inject_demux2.ts", "ab+");
            if (filedump != NULL) {
                fwrite(data.data(), 1, data.size(), filedump);
                fflush(filedump);
                fclose(filedump);
                filedump = NULL;
            } else {
               ALOGE("Open demux_inject.ts failed!\n");
            }
        }
        if (property_get_int32(TUNERHAL_DUMP_TS_DATA, 0) && getDemuxId() == 1) {
            FILE *filedump = fopen("/data/local/tmp/demux_inject_demux1.ts", "ab+");
            if (filedump != NULL) {
                fwrite(data.data(), 1, data.size(), filedump);
                fflush(filedump);
                fclose(filedump);
                filedump = NULL;
            } else {
               ALOGE("Open demux_inject.ts failed!\n");
            }
        }

     } else {
         ALOGD("[Demux] data[0] = 0x%x", data[0]);
     }
    if (mWriteTsSize < UINT64_MAX) {
        mWriteTsSize += data.size();
    } else {
        mWriteTsSize = 0;
    }
}

void Demux::notifyDvrFlushed() {
    ALOGD("notifyDvrFlushed, demuxId: %d", mDemuxId);
    if (mDemuxHandle && mHwDemuxOps) {
        mHwDemuxOps->AmHwDemux_Flush(mDemuxHandle);
    }
}

void Demux::sendFrontendInputToRecord(vector<int8_t> data) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (mRecordFilterIds.size() == 0) {
        ALOGD("no record filter id");
        return;
    }
    set<int64_t>::iterator it = mRecordFilterIds.begin();
    if (DEBUG_DEMUX) {
        ALOGW("[Demux] update record filter output data size = %zu", data.size());
    }
    //mFilters[*it]->updateRecordOutput(data);
    for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
        if (mFilters[*it]->getRecordVideoPid() != -1) {
             break;
        }
    }

    if (it != mRecordFilterIds.end()) {
        mFilters[*it]->updateRecordOutput(data);
    } else {
        for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
            if (mFilters[*it]->getRecordAudioPid() != -1) {
                 //ALOGD("[demuxid = %d]find record audio pid = %d", mDemuxId, mFilters[*it]->getRecordAudioPid());
                 break;
            }
        }
        if (it != mRecordFilterIds.end()) {
            mFilters[*it]->updateRecordOutput(data);
        } else {
            it = mRecordFilterIds.begin();
            mFilters[*it]->updateRecordOutput(data);
        }
    }
}

void Demux::sendFrontendInputToRecord(vector<int8_t> data, uint16_t pid, uint64_t offset, uint64_t pts, int iFrameIndex,int pusiIndex) {
    sendFrontendInputToRecord(data);
    std::lock_guard<std::mutex> lock(mFilterLock);
    set<int64_t>::iterator it = mRecordFilterIds.begin();;
    for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
        if (mFilters[*it] != nullptr && pid == mFilters[*it]->getTpid()) {
            mFilters[*it]->updatePts(pts);
            mFilters[*it]->updateIndexType(iFrameIndex, pusiIndex);
            mFilters[*it]->updateCurrentOffset(offset);
        }
    }
}

bool Demux::startBroadcastFilterDispatcher() {
    std::lock_guard<std::mutex> lock(mFilterLock);
    set<int64_t>::iterator it;

    // Handle the output data per filter type
    for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
        if (!mFilters[*it]->startFilterHandler().isOk()) {
            return false;
        }
    }

    return true;
}

bool Demux::startRecordFilterDispatcher() {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (mRecordFilterIds.size() == 0) {
        ALOGD("no record filter id");
        return false;
    }
    set<int64_t>::iterator it = mRecordFilterIds.begin();

    //if (!mFilters[*it]->startRecordFilterHandler().isOk()) {
    //    return false;
    //}
    for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
        if (mFilters[*it]->getRecordVideoPid() != -1) {
            break;
        }
    }

    if (it != mRecordFilterIds.end()) {
        if (!mFilters[*it]->startRecordFilterHandler().isOk()) {
            return false;
        }
    } else {
        for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
            if (mFilters[*it]->getRecordAudioPid() != -1) {
                break;
            }
        }
        if (it != mRecordFilterIds.end()) {
            if (!mFilters[*it]->startRecordFilterHandler().isOk()) {
                return false;
            }
        } else {
            it = mRecordFilterIds.begin();
            if (!mFilters[*it]->startRecordFilterHandler().isOk()) {
                return false;
            }
        }
    }
    return true;
}

::ndk::ScopedAStatus Demux::startFilterHandler(int64_t filterId) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (DEBUG_DEMUX)
        ALOGD("%s/%d filterId:%" PRIu64 "", __FUNCTION__, __LINE__, filterId);
    //Create mFilterEvent with mFilterOutput
    if (mFilters[filterId] != nullptr) {
        mFilters[filterId]->startFilterHandler();
        return ::ndk::ScopedAStatus::ok();
    } else {
        ALOGW("%s/%d filterId = %" PRIu64 " may be removed", __FUNCTION__, __LINE__, filterId);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                static_cast<int32_t>(Result::UNKNOWN_ERROR));
    }
}

void Demux::updateFilterOutput(int64_t filterId, vector<int8_t> data) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (DEBUG_DEMUX)
        ALOGD("%s/%d filterId:%" PRIu64 "", __FUNCTION__, __LINE__, filterId);
    //Copy data to mFilterOutput
    if (mFilters[filterId] != nullptr) {
        mFilters[filterId]->updateFilterOutput(data);
    } else {
        ALOGW("%s/%d filterId = %" PRIu64 " may be removed", __FUNCTION__, __LINE__, filterId);
    }
}

void Demux::updateMediaFilterOutput(int64_t filterId, vector<int8_t> data, uint64_t pts) {
    updateFilterOutput(filterId, data);
    mFilters[filterId]->updatePts(pts);
}

uint16_t Demux::getFilterTpid(int64_t filterId) {
    if ( mFilters[filterId] != nullptr) {
        return mFilters[filterId]->getTpid();
    } else {
        return -1;
    }
}

#if PLATFORM_SDK_VERSION > 33
bool Demux::isInUse() {
    return mInUse;
}

void Demux::setInUse(bool inUse) {
    mInUse = inUse;
}

void Demux::getDemuxInfo(DemuxInfo* demuxInfo) {
    *demuxInfo = {.filterTypes = mFilterTypes};
}
#endif

void Demux::startFrontendInputLoop() {
    ALOGD("[Demux] start frontend on demux");
    // Stop current Frontend thread loop first, in case the user starts a new
    // tuning before stopping current tuning.
    stopFrontendInput();
    mFrontendInputThreadRunning = true;
    mFrontendInputThread = std::thread(&Demux::frontendInputThreadLoop, this);
}

void Demux::frontendInputThreadLoop() {
    prctl(PR_SET_NAME, "frontendInputThreadLoop");
    if (!mFrontendInputThreadRunning) {
        return;
    }

    if (!mDvrPlayback) {
        ALOGW("[Demux] No software Frontend input configured. Ending Frontend thread loop.");
        mFrontendInputThreadRunning = false;
        return;
    }

    while (mFrontendInputThreadRunning && mDvrPlayback && mDvrPlayback->getDvrEventFlag() != NULL) {
        uint32_t efState = 0;
        ::android::status_t status = mDvrPlayback->getDvrEventFlag()->wait(
                static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY), &efState, WAIT_TIMEOUT,
                true /* retry on spurious wake */);
        if (status != ::android::OK) {
            ALOGD("[Demux] wait for data ready on the playback FMQ");
            continue;
        }
        if (mDvrPlayback->getSettings().get<DvrSettings::Tag::playback>().dataFormat ==
            DataFormat::ES) {
            if (!mDvrPlayback->processEsDataOnPlayback(true /*isVirtualFrontend*/, mIsRecording)) {
                ALOGE("[Demux] playback es data failed to be filtered. Ending thread");
                break;
            }
            continue;
        }
        // Our current implementation filter the data and write it into the filter FMQ immediately
        // after the DATA_READY from the VTS/framework
        // This is for the non-ES data source, real playback use case handling.
        if (!mDvrPlayback->readPlaybackFMQ(true /*isVirtualFrontend*/, mIsRecording) ||
            !mDvrPlayback->startFilterDispatcher(true /*isVirtualFrontend*/, mIsRecording)) {
            ALOGE("[Demux] playback data failed to be filtered. Ending thread");
            break;
        }
    }

    mFrontendInputThreadRunning = false;
    ALOGW("[Demux] Frontend Input thread end.");
}

void Demux::stopFrontendInput() {
    ALOGD("[Demux] stop frontend on demux");
    mKeepFetchingDataFromFrontend = false;
    mFrontendInputThreadRunning = false;
    if (mFrontendInputThread.joinable()) {
        mFrontendInputThread.join();
    }
}

void Demux::setIsRecording(bool isRecording) {
    mIsRecording = isRecording;
}

bool Demux::isRecording() {
    return mIsRecording;
}

binder_status_t Demux::dump(int fd, const char** args, uint32_t numArgs) {
    dprintf(fd, " Demux %d:\n", mDemuxId);
    dprintf(fd, "  mIsRecording %d\n", mIsRecording);
    {
        dprintf(fd, "  Filters:\n");
        map<int64_t, std::shared_ptr<Filter>>::iterator it;
        for (it = mFilters.begin(); it != mFilters.end(); it++) {
            it->second->dump(fd, args, numArgs);
        }
    }
    {
        dprintf(fd, "  TimeFilter:\n");
        if (mTimeFilter != nullptr) {
            mTimeFilter->dump(fd, args, numArgs);
        }
    }
    {
        dprintf(fd, "  DvrPlayback:\n");
        if (mDvrPlayback != nullptr) {
            mDvrPlayback->dump(fd, args, numArgs);
        }
    }
    {
        dprintf(fd, "  DvrRecord:\n");
        if (mDvrRecord != nullptr) {
            mDvrRecord->dump(fd, args, numArgs);
        }
    }
    return STATUS_OK;
}

bool Demux::attachRecordFilter(int64_t filterId) {
    uint64_t dmxFilterId = findFilterIdByfakeFilterId(filterId);
    if (mFilters[dmxFilterId] == nullptr || mDvrRecord == nullptr ||
        !mFilters[dmxFilterId]->isRecordFilter()) {
        return false;
    }

    mRecordFilterIds.insert(dmxFilterId);
    mFilters[dmxFilterId]->attachFilterToRecord(mDvrRecord);

    return true;
}

bool Demux::detachRecordFilter(int64_t filterId) {
    uint64_t dmxFilterId = findFilterIdByfakeFilterId(filterId);
    if (mFilters[dmxFilterId] == nullptr || mDvrRecord == nullptr) {
        return false;
    }

    mRecordFilterIds.erase(dmxFilterId);
    mFilters[dmxFilterId]->detachFilterFromRecord();

    return true;
}

void Demux::attachDescrambler(int32_t descramblerId,
                              std::shared_ptr<Descrambler> descrambler) {
  std::lock_guard<std::mutex> lock(mFilterLock);
  ALOGD("%s/%d", __FUNCTION__, __LINE__);
  mDescramblers[descramblerId] = descrambler;
}

void Demux::detachDescrambler(int32_t descramblerId) {
  std::lock_guard<std::mutex> lock(mFilterLock);
  ALOGD("%s/%d", __FUNCTION__, __LINE__);
  mDescramblers.erase(descramblerId);
}

sp<AM_DMX_Device> Demux::getAmDmxDevice(void) {
    return AmDmxDevice;
}

//sp<AmDvr> Demux::getAmDvrDevice() {
//    return mAmDvrDevice[mDemuxId];
//}

sp<AmPesFilter> Demux::getAmPesFilter() {
    return mAmPesFilter;
}

int Demux::getPesFid() {
    return mPesFid;
}

bool Demux::checkPesFilterId(int64_t filterId) {
    set<int64_t>::iterator it;
    for (it = mPesFilterIds.begin(); it != mPesFilterIds.end(); it++) {
        if (*it == filterId) {
            return true;
         }
    }
    return false;
}

bool Demux::checkTemiFilterId(int64_t filterId) {
    set<int64_t>::iterator it;
    for (it = mTemiFilterIds.begin(); it != mTemiFilterIds.end(); it++) {
        if (*it == filterId) {
            return true;
         }
    }
    return false;

}

bool Demux::isRawData(int64_t filterId) {
    return mFilters[filterId]->isRawData();
}

bool Demux::checkSoftDemuxForSubtitle() {
    return bSupportSoftDemuxForSubtitle;
}

int Demux::recordTsPacketForPesData(int64_t         filterId) {
    mFilters[filterId]->stop();
    mPesFid = filterId;

    int pid = getFilterTpid(filterId);
    ALOGD("%s/%d pid = %d", __FUNCTION__, __LINE__, pid);

    DVR_RecordOpenParams_t openParams;
    memset(&openParams, 0, sizeof(DVR_RecordOpenParams_t));
    openParams.src = static_cast<DVB_DemuxSource_t>(DVB_DEMUX_SOURCE_DMA0 + mDemuxId);
    openParams.dmx_dev_id[0] = mDemuxId;
    openParams.non_sec_ringbuf_size = DVR_BUFFER_LEN;
    DVR_Result_t ret = dvr_record_open(&mSubtitleRecHandle, &openParams);
    if (ret != DVR_SUCCESS) {
        ALOGD("open dvr record failed!\n");
    }

    memset(&mSubReceiveParam, 0, sizeof(DVR_RecordReceiveParams_t));
    mSubReceiveParam.buf = (uint8_t *)malloc(DVR_MAX_PUSI_LEN);
    mSubReceiveParam.len = DVR_MAX_PUSI_LEN;
    mSubReceiveParam.mode = DVR_DIRECT_RECORD_MODE;
    mSubtitleRecordThreadRunning = true;
    mSubtitleRecordThread = std::thread(&Demux::subtitleRecordThreadLoop, this);
    ret = dvr_record_start(mSubtitleRecHandle);
    if (ret != DVR_SUCCESS) {
        ALOGD("start dvr record failed!\n");
    }

    DVR_RecordFilterParams_t filterParams;
    filterParams.pid = pid;//0x2000;
    mPesRecordFid = dvr_record_open_filter(mSubtitleRecHandle, &filterParams);

    ret = dvr_record_start_filter(mSubtitleRecHandle, mPesRecordFid);
    if (ret != DVR_SUCCESS) {
        ALOGD("dvr record start filter failed!\n");
    }
    ALOGD("stream(pid = %d) start recording, filter = %d", pid, mPesRecordFid);

    return 1;
}

int64_t Demux::findFilterIdByfakeFilterId(int64_t fakefilterId) {
    if (fakefilterId > DMX_FILTER_COUNT + START_FILTERID_FROM_ONE) {
         return (fakefilterId >> 26) & 0x3f;
    }
    return fakefilterId;
}

void Demux::destroyMediaSync() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    if (mMediaSync != nullptr) {
        ALOGD("[debuglevel]destroy mediasync");
        //mMediaSync->destroyMediaSync();
        mMediaSync = nullptr;
        mAvSyncHwId = -1;
    }
}

void Demux::closePesRecordFilter() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    DVR_Result_t ret = dvr_record_stop_filter(mSubtitleRecHandle, mPesRecordFid);
    if (ret != DVR_SUCCESS) {
        ALOGD("dvr record stop filter failed!\n");
    }
    mSubtitleRecordThreadRunning = false;
    if (mSubtitleRecordThread.joinable()) {
        mSubtitleRecordThread.join();
    }
    if (mSubReceiveParam.buf) {
        free(mSubReceiveParam.buf);
        mSubReceiveParam.buf = NULL;
    }

    ret = dvr_record_close_filter(mSubtitleRecHandle, mPesRecordFid);
    if (ret != DVR_SUCCESS) {
        ALOGD("dvr record close filter failed!\n");
    }

    ret = dvr_record_stop(mSubtitleRecHandle);
    if (ret != DVR_SUCCESS) {
        ALOGD("dvr record stop failed!\n");
    }

    ret = dvr_record_close(mSubtitleRecHandle);
    if (ret != DVR_SUCCESS) {
        ALOGD("dvr record close failed!\n");
    }

    //mAmDvrDevice[mDemuxId]->AM_DVR_SetCallback(NULL, this);
    if (mAmPesFilter != NULL) {
        mAmPesFilter->release();
        mAmPesFilter = NULL;
    }
    mPesFid = -1;
    mPesRecordFid = -1;

}

void Demux::subtitleRecordThreadLoop() {
    prctl(PR_SET_NAME, "subtitleRecordThread");
    while (mSubtitleRecordThreadRunning) {
        ssize_t len = 0;
        len = dvr_record_read(mSubtitleRecHandle, &mSubReceiveParam);
        //ALOGD("[Dvr] len = %d", len);
        if (len <= 0) {
          usleep(10*1000);
          //ALOGE("dvr no data\n");
          continue;
        }

        if (mAmPesFilter != NULL) {
            int pid = getFilterTpid(mPesFid);
            mAmPesFilter->extractPesDataFromTsPacket(pid, mSubReceiveParam.buf, len);
         }
    }
}

int32_t Demux::getDemuxId() {
    return mDemuxId;
}

int Demux::getRecordVideoPid() {
    std::lock_guard<std::mutex> lock(mFilterLock);
    set<int64_t>::iterator it;
    for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
        if (mFilters[*it]->getRecordVideoPid() != -1) {
            return mFilters[*it]->getRecordVideoPid();
        }
    }
    return -1;
}

int Demux::getRecordAudioPid() {
    std::lock_guard<std::mutex> lock(mFilterLock);
    set<int64_t>::iterator it;
    for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
        if (mFilters[*it]->getRecordAudioPid() != -1) {
            return mFilters[*it]->getRecordAudioPid();
        }
    }
    return -1;
}

void Demux::TemiRecordThreadLoop() {
    prctl(PR_SET_NAME, "TemiRecordThreadLoop");
    bool startToRead = false;
    while (mTemiRecordThreadRunning) {
        int temiFid = *mTemiFilterIds.begin();
        if (!startToRead && mFilters[temiFid]->getFilterStatus()) {
            recordTsPacketForTemiData(temiFid);
            startToRead = true;
        }
        if (startToRead) {
            ssize_t len = 0;
            len = dvr_record_read(mTemiRecHandle, &mTemiReceiveParam);
            //ALOGD("[Dvr] len = %d", len);
            if (len <= 0) {
              usleep(10*1000);
              //ALOGE("dvr no data\n");
              continue;
            }
            vector<int8_t> tmpData;
            tmpData.resize(len);
            memcpy(tmpData.data(), mTemiReceiveParam.buf, len *  sizeof(uint8_t));
            updateFilterOutput(temiFid, tmpData);
            startFilterHandler(temiFid);
        } else {
            usleep(10 * 1000);
        }
    }
}

int Demux::recordTsPacketForTemiData(int64_t filterId) {
    mFilters[filterId]->stop();
    mTemiFid = filterId;

    int pid = getFilterTpid(filterId);
    ALOGD("%s/%d TEMI pid = %d", __FUNCTION__, __LINE__, pid);

    DVR_RecordOpenParams_t openParams;
    memset(&openParams, 0, sizeof(DVR_RecordOpenParams_t));
    openParams.src = static_cast<DVB_DemuxSource_t>(DVB_DEMUX_SOURCE_TS0 + mDemuxId);
    openParams.dmx_dev_id[0] = mDemuxId;
    openParams.non_sec_ringbuf_size = DVR_BUFFER_LEN;
    DVR_Result_t ret = dvr_record_open(&mTemiRecHandle, &openParams);
    if (ret != DVR_SUCCESS) {
        ALOGD("open dvr record failed!\n");
    }
    memset(&mTemiReceiveParam, 0, sizeof(DVR_RecordReceiveParams_t));
    mTemiReceiveParam.buf = (uint8_t *)malloc(DVR_MAX_PUSI_LEN);
    mTemiReceiveParam.len = DVR_MAX_PUSI_LEN;
    mTemiReceiveParam.mode = DVR_DIRECT_RECORD_MODE;
    ret = dvr_record_start(mTemiRecHandle);
    if (ret != DVR_SUCCESS) {
        ALOGD("start dvr record failed!\n");
    }

    DVR_RecordFilterParams_t filterParams;
    filterParams.pid = pid;//0x2000;
    mTemiRecordFid = dvr_record_open_filter(mTemiRecHandle, &filterParams);
    if (ret != DVR_SUCCESS) {
        ALOGD("dvr record open filter failed!\n");
    }

    ret = dvr_record_start_filter(mTemiRecHandle, mTemiRecordFid);
    if (ret != DVR_SUCCESS) {
        ALOGD("dvr record start filter failed!\n");
    }
    ALOGD("stream(pid = %d) start recording, filter = %d", pid, mTemiRecordFid);

    return 1;
}

void Demux::closeTemiRecordFilter() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    DVR_Result_t ret = dvr_record_stop_filter(mTemiRecHandle, mTemiRecordFid);
    if (ret != DVR_SUCCESS) {
        ALOGD("dvr record stop filter failed!\n");
    }

    mTemiRecordThreadRunning = false;
    if (mTemiRecordThread.joinable()) {
        mTemiRecordThread.join();
    }

    if (mTemiReceiveParam.buf) {
        free(mTemiReceiveParam.buf);
        mTemiReceiveParam.buf = NULL;
    }

    ret = dvr_record_close_filter(mTemiRecHandle, mTemiRecordFid);
    if (ret != DVR_SUCCESS) {
        ALOGD("dvr record close filter failed!\n");
    }

    ret = dvr_record_stop(mTemiRecHandle);
    if (ret != DVR_SUCCESS) {
        ALOGD("dvr record stop failed!\n");
    }

    ret = dvr_record_close(mTemiRecHandle);
    if (ret != DVR_SUCCESS) {
        ALOGD("dvr record close failed!\n");
    }

    mTemiFid = -1;
    mTemiRecordFid = -1;
}

int Demux::getTemiFid() {
    return mTemiFid;
}

bool Demux::checkSoftDemuxForTemi() {
    return bSupportSoftDemuxForTemi;
}

int Demux::getTsInput() {
    return mTsInput;
}

std::shared_ptr<Descrambler> Demux::getDescrambler() {
    // one demux for one descrambler
    for (auto descramblerIt = mDescramblers.begin(); descramblerIt != mDescramblers.end(); descramblerIt++) {
         if (descramblerIt->second != nullptr) {
            ALOGD("get descrambler object");
            return descramblerIt->second;
         }
    }
    return nullptr;
}
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
