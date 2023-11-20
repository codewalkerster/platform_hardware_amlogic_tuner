/*
 * Copyright 2020 The Android Open Source Project
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

#define LOG_TAG "android.hardware.tv.tuner@1.1-Demux"

#include <utils/Log.h>
#include <cutils/properties.h>
#include "Demux.h"
#include "FileSystemIo.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

namespace {
constexpr int kTsPacketSize = 188 * 100;

bool isValidTsPacket(const vector<uint8_t>& tsPacket) {
  return kTsPacketSize == tsPacket.size() && tsPacket[0] == 0x47;
}

}  // namespace

#define WAIT_TIMEOUT 3000000000
#define PSI_MAX_SIZE 4096
#define PES_RAW_DATA_SIZE 64 * 1024
#define PRIVATE_STREAM_1   0x1bd
#define PRIVATE_STREAM_2   0x1bf
#define SUPPORT_SOFTWARE_DEMUX_SUBTITLE "vendor.tunerhal.softwaredemux.subtitle"
#define VIDEO_BUFFER_SIZE  "/sys/module/amlogic_dvb_demux/parameters/video_buf_size"
#define AUDIO_BUFFER_SIZE  "/sys/module/amlogic_dvb_demux/parameters/audio_buf_size"
#define TUNERHAL_DUMP_TS_DATA "vendor.tf.dump.ts"

Demux::Demux(uint32_t demuxId, sp<Tuner> tuner) {

    FileSystem_create();

    if (FileSystem_writeFile(VIDEO_BUFFER_SIZE,"15728640") != 0) {
        ALOGE("set video_buf_size erro %p\n",this);
    }
    if (FileSystem_writeFile(AUDIO_BUFFER_SIZE,"3145728") != 0) {
        ALOGE("set audio_buf_size erro %p\n",this);
    }

    mDemuxId = demuxId;
    mTunerService = tuner;
    mCiCamId = 0;
    mFrontendInputThreadRunning = false;
    mKeepFetchingDataFromFrontend = false;
    mFrontendInputThread = 0;
    bSupportSoftDemuxForSubtitle =  property_get_bool(SUPPORT_SOFTWARE_DEMUX_SUBTITLE, true);
    AmDmxDevice[mDemuxId] = new AM_DMX_Device(mDemuxId);
    ALOGD("mDemuxId:%d, bSupportSoftDemuxForSubtitle = %d", mDemuxId, bSupportSoftDemuxForSubtitle);
    AmDmxDevice[mDemuxId]->AM_DMX_Open();
    mAmDvrDevice[mDemuxId] = new AmDvr(mDemuxId);
    mAmDvrDevice[mDemuxId]->AM_DVR_Open(INPUT_DEMOD, mTunerService->getTsInput(), true);

    mHwDemuxOps[mDemuxId] = new HwDemuxOpsSCWrap();
    if (mHwDemuxOps[mDemuxId] != nullptr) {
        mDemuxHandle[mDemuxId] = mHwDemuxOps[mDemuxId]->AmHwDemux_Create(0, NULL);
    }
}

Demux::~Demux() {
    ALOGD("~Demux");
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
    vector<uint8_t> pesData;
    pesData.resize(len);
    memcpy(pesData.data(), pes, len * sizeof(uint8_t));
    dmxDev->updateFilterOutput(fid, pesData);
    dmxDev->startFilterHandler(fid);
}

void Demux::postDvrData(void* demux) {
    Demux *dmxDev = (Demux*)demux;
    if (dmxDev == NULL) {
        ALOGD("get demux device is NULL in dvr thread");
        return;
    }
    int ret = -1;
    int cnt = -1;
    int size = 10 * 188;
    vector<uint8_t> dvrData;
    dvrData.resize(size);
    cnt = size;

    ret = dmxDev->getAmDvrDevice()->AM_DVR_Read(dvrData.data(), &cnt);
    if (ret != 0) {
        ALOGE("No data available from DVR");
        //usleep(200 * 1000);
        return;
    }

    if (cnt < size) {
        //ALOGD("read dvr read size = %d", cnt);
    }

    /*
    ALOGD("dmxDev->mfd = %d", dmxDev->mfd);
    if (dmxDev->mfd > 0) { //data/local/tmp/media_demux.ts
        write(dmxDev->mfd, dvrData.data(), cnt);
    }*/
    dvrData.resize(cnt);
    int pesFid = dmxDev->getPesFid();
    if (pesFid != -1) {
        int pid = dmxDev->getFilterTpid(pesFid);
        ALOGD("%s/%d pid = %d, pesFid = %d", __FUNCTION__, __LINE__, pid, pesFid);
        if (pid != -1 && dmxDev->getAmPesFilter() != NULL) {
            dmxDev->getAmPesFilter()->extractPesDataFromTsPacket(pid, dvrData.data(), cnt);
        }
    } else {
       if (property_get_int32(TUNERHAL_DUMP_TS_DATA, 0)) {
            FILE *filedump = fopen("/data/local/tmp/dump_dvr.ts", "ab+");
            if (filedump != NULL) {
                fwrite(dvrData.data(), 1, dvrData.size(), filedump);
                fflush(filedump);
                fclose(filedump);
                filedump = NULL;
            } else {
               ALOGE("Open dump_dvr.ts failed!\n");
            }
        }

        //uint16_t pid = ((dvrData[1] & 0x1f) << 8) | ((dvrData[2] & 0xff));
        //ALOGD("%s/%d dvr pid:0x%x", __FUNCTION__, __LINE__, pid);
        dmxDev->sendFrontendInputToRecord(dvrData);
        dmxDev->startRecordFilterDispatcher();
    }
}

void Demux::combinePesData(uint64_t filterId) {
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
    while (AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, tmpbuf, &size) == 0) {
        packetHeader = ((packetHeader<<8) & 0x000000ffffffff00) | tmpbuf[0];
        //ALOGD("[Demux] packetHeader = %llx", packetHeader);
        stream_id = packetHeader & 0xffffffff;
        if (stream_id == PRIVATE_STREAM_1 || stream_id == PRIVATE_STREAM_2) {
            ALOGD("## [Demux] combinePesData %x,%llx,-----------\n", tmpbuf[0], packetHeader & 0xffffffffff);
            size = 2;
            result = AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, tmpbuf1, &size);
            packetLen = (tmpbuf1[0] << 8) | tmpbuf1[1];
            ALOGD("[Demux] packetLen = %d", packetLen);
            if (packetLen >= 3) {
                /*
                 * The logic is like this, ignoring.
                 */
                /* coverity[tainted_data:SUPPRESS] */
                pesData.resize(packetLen + 6);
                pesData[0] = 0x0;
                pesData[1] = 0x0;
                pesData[2] = 0x01;
                pesData[3] = tmpbuf[0];
                pesData[4] = tmpbuf1[0];
                pesData[5] = tmpbuf1[1];
                size = 3;
                result =  AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, pesData.data() + 6, &size);
                packetLen -= 3;
                pesHeaderLen = pesData[8];
                ALOGD("[Demux] pesHeaderLen = %d", pesHeaderLen);
                if (packetLen >= pesHeaderLen) {
                    if ((pesData[7] & 0xc0) == 0x80) {
                        result = AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, pesData.data() + 6 + 3, &pesHeaderLen);
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
                        result = AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, pesData.data() + 6 + 3, &pesHeaderLen);
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
                    result = AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, pesData.data() + 6 + 3 + pesHeaderLen + readLen, &dataLen);
                    //ALOGD("[Demux] result = 0x%x", result);
                    if (result == AM_SUCCESS) {
                        readLen += dataLen;
                    } else if (result == AM_FAILURE) {
                        ALOGD("[Demux] pes data read fail");
                        return;
                    }
                } while(readLen < packetLen);
            }

            updateFilterOutput(filterId, pesData);
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
                if (packetHeader == 0xff00 || packetHeader == 0xff0000 || packetHeader == 0xffffffff00 || packetHeader == 0xffffff0000) {
                    continue;
                }
            } else if (tmpbuf[0] == 1 && (packetHeader == 0xffff000001 || packetHeader == 0xff000001)) {
                continue;
            }
        }
    }
}

void Demux::getSectionData(uint64_t filterId) {
    vector<uint8_t> sectionData;
    int sectionSize = PSI_MAX_SIZE;

    sectionData.resize(sectionSize);
    int readRet = AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, sectionData.data(), &sectionSize);
    if (readRet != 0) {
        ALOGE("AM_DMX_Read failed! readRet:0x%x", readRet);
        return;
    } else {
        ALOGV("fid =%llu section data size:%d", filterId, sectionSize);
        sectionData.resize(sectionSize);
        /*gettimeofday(&mTable_end, NULL);
        timeval table_start;
        if (mFilters[filterId] != nullptr) {
            table_start = mFilters[filterId]->getTableStartTime();
            timersub(&mTable_end, &table_start, &mTable_elapsed);
        }
        uint16_t tableId = sectionData[0];
        if (tableId == 0x0) {
            //ALOGD("received PAT table tableId = %d, fid = %llu, dmxid = %d", tableId, filterId, mDemuxId);
            table_time_trace_log(&mStbTrace_info, "received PAT table", tableId, filterId, mDemuxId, mTable_elapsed);
            int i;
            string strData;
            char ch[3];
            for (i = 0; i < sectionData.size(); i++)
            {
                snprintf(ch, 3, "%02x", sectionData[i]);
                strData += ch;
            }
            ALOGV("dump bytes: %s", strData.c_str());
        }
        if (tableId == 0x2) {
            //ALOGD("received PMT table tableId = %d, fid = %llu", tableId, filterId);
            table_time_trace_log(&mStbTrace_info, "received PMT table", tableId, filterId, mDemuxId, mTable_elapsed);
        }*/
        updateFilterOutput(filterId, sectionData);
        startFilterHandler(filterId);
    }

}

void Demux::getPesRawData(uint64_t filterId) {
    vector<uint8_t> pesRawData;
    int pesRawDataSize = PES_RAW_DATA_SIZE;
    pesRawData.resize(pesRawDataSize);
    int readRet = AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, pesRawData.data(), &pesRawDataSize);
    if (readRet != 0) {
        ALOGE("AM_DMX_Read failed! readRet:0x%x", readRet);
        return;
    } else {
        ALOGD("fid =%llu pes raw data size:%d", filterId, pesRawDataSize);
        pesRawData.resize(pesRawDataSize);
        updateFilterOutput(filterId, pesRawData);
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
                tmpData.resize(size);
                dmxDev->updateFilterOutput(fid, tmpData);
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
            dmx_non_sec_es_header esPriv;
            memcpy(&esPriv, esHeader, sizeof(dmx_non_sec_es_header));
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
                    dmxDev->updateFilterOutput(fid, tmpData);
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
            dmxDev->updateFilterOutput(fid, tmpData, &esPriv);
            //Copy mFilterOutput to av ion buffer and create mFilterEvent
            dmxDev->startFilterHandler(fid);
#endif
            }
        }
    } else {
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

Return<Result> Demux::setFrontendDataSource(uint32_t frontendId) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    if (mTunerService == nullptr) {
        return Result::NOT_INITIALIZED;
    }

    mFrontend = mTunerService->getFrontendById(frontendId);

    if (mFrontend == nullptr) {
        return Result::INVALID_STATE;
    }

    mTunerService->setFrontendAsDemuxSource(frontendId, mDemuxId);

    return Result::SUCCESS;
}

Return<void> Demux::openFilter(const DemuxFilterType& type, uint32_t bufferSize,
                               const sp<IFilterCallback>& cb, openFilter_cb _hidl_cb) {
    uint64_t filterId;
    int32_t dmxFilterIdx;
    //DemuxTsFilterType tsFilterType;
    DemuxTsFilterType tsFilterType = DemuxTsFilterType::UNDEFINED;
    bool hasTsFilterType = (DemuxFilterType::DemuxFilterSubType::hidl_discriminator::tsFilterType
                            == type.subType.getDiscriminator());

    if (hasTsFilterType) {
        tsFilterType = type.subType.tsFilterType();
    }

    /* for vts
    if (hasTsFilterType && tsFilterType == DemuxTsFilterType::UNDEFINED) {
        ALOGE("[Demux] Invalid filter type!");
         _hidl_cb(Result::INVALID_ARGUMENT, nullptr);
         return Void();
     }*/

    std::lock_guard<std::mutex> lock(mFilterLock);
    if (AmDmxDevice[mDemuxId]->AM_DMX_AllocateFilter(&dmxFilterIdx) != 0) {
        ALOGE("Allocate filterid fail");
        _hidl_cb(Result::INVALID_ARGUMENT, nullptr);
        return Void();
    }
    filterId = dmxFilterIdx;

    //Use demux fd as the tuner hal filter id
    sp<Filter> filter = new Filter(type, filterId, bufferSize, cb, this);
    ALOGD("[%s/%d] Allocate filter subType:%d filterIdx:%llu, bufferSize:%d KB", __FUNCTION__, __LINE__, tsFilterType, filterId, bufferSize/1024);

    if (!filter->createFilterMQ()) {
        ALOGE("[Demux] filter %d createFilterMQ error!", dmxFilterIdx);
        AmDmxDevice[mDemuxId]->AM_DMX_FreeFilter(dmxFilterIdx);
        _hidl_cb(Result::UNKNOWN_ERROR, filter);
        return Void();
    }

    if (hasTsFilterType) {
        if (tsFilterType == DemuxTsFilterType::SECTION
            || tsFilterType == DemuxTsFilterType::VIDEO
            || tsFilterType == DemuxTsFilterType::AUDIO
            || tsFilterType == DemuxTsFilterType::PES) {
            //AmDmxDevice[mDemuxId]->AM_DMX_SetCallback(dmxFilterIdx, this->postData, this);
            bCheckVts = true;
            AmDmxDevice[mDemuxId]->AM_DMX_SetCallback(dmxFilterIdx, postData, this);
        } else if (tsFilterType == DemuxTsFilterType::PCR) {
            AmDmxDevice[mDemuxId]->AM_DMX_SetCallback(dmxFilterIdx, NULL, NULL);
        } else if (tsFilterType == DemuxTsFilterType::RECORD) {
            //mAmDvrDevice->AM_DVR_SetCallback(this->postDvrData, this);
            bCheckVts = false;
            mAmDvrDevice[mDemuxId]->AM_DVR_SetCallback(postDvrData, this);
        }
    }

    if (hasTsFilterType && tsFilterType == DemuxTsFilterType::PES) {
        mAmPesFilter = new AmPesFilter(dmxFilterIdx, pesDataCallback, this);
        mPesFilterIds.insert(dmxFilterIdx);
        ALOGD("Insert PES filter mPesFid = %d", dmxFilterIdx);
    }
    if (hasTsFilterType && tsFilterType == DemuxTsFilterType::PCR) {
        mPcrFilterIds.insert(dmxFilterIdx);
        ALOGD("Insert pcr filter");
    }
    bool result = true;

    if (hasTsFilterType && tsFilterType != DemuxTsFilterType::RECORD && tsFilterType != DemuxTsFilterType::PCR) {
        // Only save non-record filters for now. Record filters are saved when the
        // IDvr.attacheFilter is called.
        mPlaybackFilterIds.insert(dmxFilterIdx);
        if (mDvrPlayback != nullptr) {
            ALOGD("[%s/%d] addPlaybackFilter filterIdx:%d", __FUNCTION__, __LINE__, dmxFilterIdx);
            result = mDvrPlayback->addPlaybackFilter(dmxFilterIdx, filter);
        }
    }

    mFilters[dmxFilterIdx] = filter;
    _hidl_cb(result ? Result::SUCCESS : Result::INVALID_ARGUMENT, filter);

    return Void();
}

Return<void> Demux::openTimeFilter(openTimeFilter_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    mTimeFilter = new TimeFilter(this);

    _hidl_cb(Result::SUCCESS, mTimeFilter);
    return Void();
}

Return<void> Demux::getAvSyncHwId(const sp<IFilter>& filter, getAvSyncHwId_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    uint32_t avSyncHwId = -1;
    uint64_t fid;
    int mode = 0;
    Result status;
    if (filter == nullptr) {
        ALOGE("[Demux] filter is null!");
        _hidl_cb(Result::INVALID_STATE, mAvSyncHwId);
        return Void();
    }

    sp<V1_1::IFilter> filter_v1_1 = V1_1::IFilter::castFrom(filter);
    if (filter_v1_1 != NULL) {
        filter_v1_1->getId64Bit([&](Result result, uint64_t filterId) {
            fid = filterId;
            status = result;
        });
    } else {
        filter->getId([&](Result result, uint32_t filterId) {
            fid = filterId;
            status = result;
        });
    }

    if (status != Result::SUCCESS) {
        ALOGE("[Demux] Can't get filter Id.");
        _hidl_cb(Result::INVALID_STATE, avSyncHwId);
        return Void();
    }

    if (fid > DMX_FILTER_COUNT) {
        fid = findFilterIdByfakeFilterId(fid);
    }

    if (mMediaSync == nullptr) {
        ALOGD("[debuglevel] new mediasync");
        mMediaSync = new MediaSyncWrap();
    }

    ALOGD("%s/%d fid = %llu", __FUNCTION__, __LINE__, fid);
    std::lock_guard<std::mutex> lock(mFilterLock);
    set<uint64_t>::iterator it;
    if (mDvrPlayback != nullptr) {
       uint16_t avPid;
       for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
           avPid = mFilters[*it]->getTpid();
           DemuxFilterType type = mFilters[*it]->getFilterType();
           ALOGD("%s/%d avPid = %u", __FUNCTION__, __LINE__, avPid);
           if (type.subType.tsFilterType() == DemuxTsFilterType::VIDEO) {
               mVidPid = avPid;
           }

           if (type.subType.tsFilterType() == DemuxTsFilterType::AUDIO) {
               mAudPid = avPid;
           }
       }
    }
    if (mFilters[fid] != nullptr && mFilters[fid]->isMediaFilter() && !mPlaybackFilterIds.empty()) {
        uint16_t avPid = getFilterTpid(*mPlaybackFilterIds.begin());
        DemuxFilterType type = mFilters[fid]->getFilterType();
        if (mMediaSync != nullptr) {
            if (mAvSyncHwId == -1) {
                 if (!mPcrFilterIds.empty()) {
                    mAvSyncHwId = *mPcrFilterIds.begin();
                    ALOGD("%s/%d mAvSyncHwId = %llu", __FUNCTION__, __LINE__, *mPcrFilterIds.begin());
                    mMediaSync->setParameter(MEDIASYNC_KEY_ISOMXTUNNELMODE, &mode);
                    mMediaSync->bindStaticAvSyncId(mAvSyncHwId);
                 } else {
                    mAvSyncHwId = mMediaSync->getAvSyncHwId(mDemuxId, avPid);
                    mMediaSync->setParameter(MEDIASYNC_KEY_ISOMXTUNNELMODE, &mode);
                    mMediaSync->bindAvSyncId(mAvSyncHwId);
                }
            }
        }
        struct AmDemuxControlInfo info;
        info.demuxId = mDemuxId;
        info.mediasyncId = mAvSyncHwId;

        if (mDemuxHandle[mDemuxId] && mHwDemuxOps[mDemuxId] && mDvrPlayback) {
            ALOGD("%s/%d 0x%x 0x%x %u %d", __FUNCTION__, __LINE__, mVidPid, mAudPid, mDemuxId, mAvSyncHwId);
            mHwDemuxOps[mDemuxId]->AmHwDemux_Init(mDemuxHandle[mDemuxId], 0, &info);
            mWriteTsSize = 0;
        }

        ALOGD("[Demux] mAvFilterId:%llu avPid:0x%x avSyncHwId:%d", *mPlaybackFilterIds.begin(), avPid, mAvSyncHwId);
        _hidl_cb(Result::SUCCESS, mAvSyncHwId);
        return Void();
    } else if (mFilters[fid] != nullptr && mFilters[fid]->isPcrFilter() && !mPcrFilterIds.empty()) {
        // Return the lowest pcr filter id in the default implementation as the av sync id
        uint16_t pcrPid = getFilterTpid(*mPcrFilterIds.begin());
        if (mMediaSync != nullptr) {
            if (mAvSyncHwId == -1) {
                mAvSyncHwId = *mPcrFilterIds.begin();//mMediaSync->getAvSyncHwId(mDemuxId, pcrPid);
                mMediaSync->setParameter(MEDIASYNC_KEY_ISOMXTUNNELMODE, &mode);
                mMediaSync->bindStaticAvSyncId(mAvSyncHwId);
            }
        }
        ALOGD("[Demux] mPcrFilterId:%llu pcrPid:0x%x avSyncHwId:%d", *mPcrFilterIds.begin(), pcrPid, mAvSyncHwId);
        _hidl_cb(Result::SUCCESS, mAvSyncHwId);
        return Void();
    } else {
        ALOGD("[Demux] No pcr or No media filter opened.");
        _hidl_cb(Result::INVALID_STATE, mAvSyncHwId);
        return Void();
    }
}

Return<void> Demux::getAvSyncTime(AvSyncHwId avSyncHwId, getAvSyncTime_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    uint64_t avSyncTime = -1;
    /*
    if (mPcrFilterIds.empty()) {
        _hidl_cb(Result::INVALID_STATE, avSyncTime);
        return Void();
    }
    if (avSyncHwId != *mPcrFilterIds.begin()) {
        _hidl_cb(Result::INVALID_ARGUMENT, avSyncTime);
        return Void();
    }*/

    if (mMediaSync != nullptr) {
        int64_t time = -1;
        time = mMediaSync->getAvSyncTime();
        avSyncTime = 0x1FFFFFFFF & ((9*time)/100);
    }
    //ALOGD("%s/%d avSyncTime = %llu", __FUNCTION__, __LINE__, avSyncTime);

    _hidl_cb(Result::SUCCESS, avSyncTime);
    return Void();
}

Return<Result> Demux::close() {
    ALOGD("[%s/%d] mDemuxId:%d", __FUNCTION__, __LINE__, mDemuxId);
    std::lock_guard<std::mutex> lock(mFilterLock);

    set<uint64_t>::iterator it;
    if (mDvrPlayback != nullptr) {
        for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
            mDvrPlayback->removePlaybackFilter(*it);
        }
    }
    mPlaybackFilterIds.clear();
    mRecordFilterIds.clear();
    mPcrFilterIds.clear();
    mPesFilterIds.clear();
    mFilters.clear();
    mLastUsedFilterId = -1;
    if (!mScrambledCache.empty())
        mScrambledCache.clear();
    if (!mClearCache.empty())
        mClearCache.clear();

    mDvrPlayback = nullptr;
    mDvrRecord   = nullptr;
    destroyMediaSync();

    if (mHwDemuxOps[mDemuxId] != nullptr) {
        if (mDemuxHandle[mDemuxId]) {
            mHwDemuxOps[mDemuxId]->AmHwDemux_Destroy(mDemuxHandle[mDemuxId]);
            mDemuxHandle[mDemuxId] = NULL;
        }
        mHwDemuxOps[mDemuxId] = nullptr;
        mWriteTsSize = 0;
    }

    if (AmDmxDevice[mDemuxId] != NULL) {
        AmDmxDevice[mDemuxId]->AM_DMX_Close();
        AmDmxDevice[mDemuxId] = NULL;
    }

    if (mAmDvrDevice[mDemuxId] != NULL) {
        mAmDvrDevice[mDemuxId]->AM_DVR_Close();
        mAmDvrDevice[mDemuxId] = NULL;
    }
    mTunerService->removeDemux(mDemuxId);
    return Result::SUCCESS;
}

Return<void> Demux::openDvr(DvrType type, uint32_t bufferSize, const sp<IDvrCallback>& cb,
                            openDvr_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    if (cb == nullptr) {
        ALOGW("[Demux] DVR callback can't be null");
        _hidl_cb(Result::INVALID_ARGUMENT, new Dvr());
        return Void();
    }
    std::lock_guard<std::mutex> lock(mFilterLock);

    set<uint64_t>::iterator it;
    switch (type) {
        case DvrType::PLAYBACK:
            ALOGD("%s/%d DvrType::PLAYBACK bufferSize:%d KB", __FUNCTION__, __LINE__,  bufferSize/1024);
            mDvrPlayback = new Dvr(type, bufferSize, cb, this);
            if (bCheckVts) {
                ALOGD("[Demux] dmx_dvr_open INPUT_LOCAL");
                AmDmxDevice[mDemuxId]->dmx_dvr_open(INPUT_LOCAL);
            }
            if (!mDvrPlayback->createDvrMQ()) {
                _hidl_cb(Result::UNKNOWN_ERROR, mDvrPlayback);
                return Void();
            }

            for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
                if (!mDvrPlayback->addPlaybackFilter(*it, mFilters[*it])) {
                    ALOGE("[Demux] Can't get filter info for DVR playback");
                    _hidl_cb(Result::UNKNOWN_ERROR, mDvrPlayback);
                    return Void();
                }
            }

            mVidPid = 0x1FFF;
            mAudPid = 0x1FFF;
            _hidl_cb(Result::SUCCESS, mDvrPlayback);
            return Void();
        case DvrType::RECORD:
            ALOGD("%s/%d DvrType::RECORD bufferSize:%d KB", __FUNCTION__, __LINE__,  bufferSize/1024);
            mDvrRecord = new Dvr(type, bufferSize, cb, this);
            //ALOGD("[Demux] dmx_dvr_open INPUT_DEMOD");
            //mAmDvrDevice->AM_DVR_Open(INPUT_DEMOD);
            if (!mDvrRecord->createDvrMQ()) {
                _hidl_cb(Result::UNKNOWN_ERROR, mDvrRecord);
                return Void();
            }

            _hidl_cb(Result::SUCCESS, mDvrRecord);
            return Void();
        default:
            _hidl_cb(Result::INVALID_ARGUMENT, nullptr);
            return Void();
    }
}

Return<Result> Demux::connectCiCam(uint32_t ciCamId) {
    ALOGV("%s", __FUNCTION__);

    mCiCamId = ciCamId;

    return Result::SUCCESS;
}

Return<Result> Demux::disconnectCiCam() {
    ALOGV("%s", __FUNCTION__);

    return Result::SUCCESS;
}

Result Demux::removeFilter(uint64_t filterId) {
    ALOGD("%s/%d filterId = %llu", __FUNCTION__, __LINE__, filterId);
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
        ALOGD("remove PES filter mPesFid = %llu", filterId);
        mPesFilterIds.erase(filterId);
    }

    if (mDvrPlayback != nullptr) {
        mDvrPlayback->removePlaybackFilter(filterId);
    }

    if (mFilters.size() == 0) {
        destroyMediaSync();
        if (mDemuxHandle[mDemuxId] && mHwDemuxOps[mDemuxId] && mDvrPlayback) {
            ALOGD("%s/%d ", __FUNCTION__, __LINE__);
            mHwDemuxOps[mDemuxId]->AmHwDemux_ResetStatus(mDemuxHandle[mDemuxId]);
            mWriteTsSize = 0;
        }
    }

    return Result::SUCCESS;
}

void Demux::startBroadcastTsFilter(vector<uint8_t> data) {
        bool isDscReady = false;
        if (DEBUG_DEMUX)
            ALOGD("write to dvr %d size:%d", mDemuxId, data.size());

        {
            std::lock_guard<std::mutex> lock(mFilterLock);
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
                                ALOGW("reset scrambled cache! cache size:%d", mScrambledCache.size());
                                vector<uint8_t>().swap(mScrambledCache);
                            }
                            mScrambledCache.insert(mScrambledCache.end(), data.begin() + tsDataIdx, data.begin() + tsDataIdx + 188);
                            ALOGV("scrambled cache idx:%d pid:0x%x size:%d", tsDataIdx, pid, mScrambledCache.size());
                        } else {
                            mClearCache.insert(mClearCache.end(), data.begin() + tsDataIdx, data.begin() + tsDataIdx + 188);
                            ALOGV("clear cache idx:%d pid:0x%x size:%d", tsDataIdx, pid, mClearCache.size());
                            break;
                        }
                    }
                }
                if (!mClearCache.empty()) {
                    int writeRetry = 0;
                    ALOGD("write clear cache size:%d", mClearCache.size());
                    while (AmDmxDevice[mDemuxId]->AM_DMX_WriteTs(mClearCache.data(), mClearCache.size(), 300 * 1000) == -1 \
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
        if (isValidTsPacket(data)) {
            if (mDemuxHandle[mDemuxId] && mHwDemuxOps[mDemuxId]) {
                while (mHwDemuxOps[mDemuxId]->AmHwDemux_GetStreamControlStatus(mDemuxHandle[mDemuxId], NULL, mWriteTsSize,
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

        if (isDscReady && !mScrambledCache.empty()) {
            int writeRetry = 0;
            ALOGD("write scrambled cache size:%d", mScrambledCache.size());
            while (AmDmxDevice[mDemuxId]->AM_DMX_WriteTs(mScrambledCache.data(), mScrambledCache.size(), 300 * 1000) == -1 \
                   && writeRetry <= 100) {
                usleep(100 * 1000);
                writeRetry ++;
                ALOGW("write scrambled cache retry: %d", writeRetry);
            }
            vector<uint8_t>().swap(mScrambledCache);
        }
        if (isValidTsPacket(data)) {
            while (AmDmxDevice[mDemuxId] != NULL && AmDmxDevice[mDemuxId]->AM_DMX_WriteTs(data.data(), data.size(), 300 * 1000) == -1) {
                usleep(100 * 1000);
                if (mDvrPlayback && mDvrPlayback->stopInjectTs()) {
                    ALOGD("[demux] stop Inject TS, break!");
                    break;
                }
                ALOGD("[Demux] wait for 100ms to write dvr device");
            }
            if (property_get_int32(TUNERHAL_DUMP_TS_DATA, 0)) {
                FILE *filedump = fopen("/data/local/tmp/demux_inject.ts", "ab+");
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

void Demux::sendFrontendInputToRecord(vector<uint8_t> data) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (mRecordFilterIds.size() == 0) {
        ALOGD("no record filter id");
        return;
    }
    set<uint64_t>::iterator it = mRecordFilterIds.begin();
    if (DEBUG_DEMUX) {
        ALOGW("[Demux] update record filter output data size = %d", data.size());
    }
    mFilters[*it]->updateRecordOutput(data);
    /*
    for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
        mFilters[*it]->updateRecordOutput(data);
    }*/
}

void Demux::sendFrontendInputToRecord(vector<uint8_t> data, uint16_t pid, uint64_t pts) {
    sendFrontendInputToRecord(data);
    set<uint64_t>::iterator it;
    for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
        if (mFilters[*it] != nullptr && pid == mFilters[*it]->getTpid()) {
            mFilters[*it]->updatePts(pts);
        }
    }
}

bool Demux::startBroadcastFilterDispatcher() {
    std::lock_guard<std::mutex> lock(mFilterLock);
    set<uint64_t>::iterator it;

    // Handle the output data per filter type
    for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
        if (mFilters[*it]->startFilterHandler() != Result::SUCCESS) {
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
    set<uint64_t>::iterator it = mRecordFilterIds.begin();

    if (mFilters[*it]->startRecordFilterHandler() != Result::SUCCESS) {
        return false;
    }
    /*
    for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
        if (mFilters[*it]->startRecordFilterHandler() != Result::SUCCESS) {
            return false;
        }
    }*/

    return true;
}

Result Demux::startFilterHandler(uint64_t filterId) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (DEBUG_DEMUX)
        ALOGD("%s/%d filterId:%llu", __FUNCTION__, __LINE__, filterId);
    for (auto descramblerIt = mDescramblers.begin(); descramblerIt != mDescramblers.end(); descramblerIt++) {
        if (descramblerIt->second && !descramblerIt->second->isDescramblerReady())
            ALOGV("[Demux] dsc isn't ready.");
        continue;
    }
    //Create mFilterEvent with mFilterOutput
    if (mFilters[filterId] != nullptr) {
        mFilters[filterId]->startFilterHandler();
    } else {
        ALOGW("%s/%d filterId = %llu may be removed", __FUNCTION__, __LINE__, filterId);
    }
    return Result::SUCCESS;
}

void Demux::updateFilterOutput(uint64_t filterId, vector<uint8_t> data, void * priv) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (DEBUG_DEMUX)
        ALOGD("%s/%d filterId:%llu", __FUNCTION__, __LINE__, filterId);
    //Copy data to mFilterOutput
    if (mFilters[filterId] != nullptr) {
        mFilters[filterId]->updateFilterOutput(data, priv);
    } else {
        ALOGW("%s/%d filterId = %llu may be removed", __FUNCTION__, __LINE__, filterId);
    }
}

void Demux::updateMediaFilterOutput(uint64_t filterId, vector<uint8_t> data, uint64_t pts) {
    updateFilterOutput(filterId, data);
    mFilters[filterId]->updatePts(pts);
}

uint16_t Demux::getFilterTpid(uint64_t filterId) {
    if ( mFilters[filterId] != nullptr) {
        return mFilters[filterId]->getTpid();
    } else {
        return -1;
    }
}

void Demux::startFrontendInputLoop() {
    mFrontendInputThreadRunning = true;
    if (pthread_create(&mFrontendInputThread, NULL, __threadLoopFrontend, this)) {
        ALOGD("[Filter] can't create mFrontendInputThread thread");
    }
    pthread_setname_np(mFrontendInputThread, "frontend_input_thread");
}

void* Demux::__threadLoopFrontend(void* user) {
    Demux* const self = static_cast<Demux*>(user);
    self->frontendInputThreadLoop();
    return 0;
}

void Demux::frontendInputThreadLoop() {
    if (!mFrontendInputThreadRunning) {
        return;
    }

    std::lock_guard<std::mutex> lock(mFrontendInputThreadLock);
    if (!mDvrPlayback) {
        ALOGW("[Demux] No software Frontend input configured. Ending Frontend thread loop.");
        mFrontendInputThreadRunning = false;
        return;
    }

    while (mFrontendInputThreadRunning && mDvrPlayback && mDvrPlayback->getDvrEventFlag() != nullptr) {
        uint32_t efState = 0;
        status_t status = mDvrPlayback->getDvrEventFlag()->wait(
                static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY), &efState, WAIT_TIMEOUT,
                true /* retry on spurious wake */);
        if (status != OK) {
            ALOGD("[Demux] wait for data ready on the playback FMQ");
            continue;
        }
        if (mDvrPlayback->getSettings().playback().dataFormat == DataFormat::ES) {
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
    std::lock_guard<std::mutex> lock(mFrontendInputThreadLock);
}

void Demux::setIsRecording(bool isRecording) {
    ALOGD("%s/%d isRecording:%d", __FUNCTION__, __LINE__, isRecording);

    mIsRecording = isRecording;
}

bool Demux::isRecording() {
    return mIsRecording;
}

bool Demux::attachRecordFilter(uint64_t filterId) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    uint64_t dmxFilterId = findFilterIdByfakeFilterId(filterId);
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (mFilters[dmxFilterId] == nullptr || mDvrRecord == nullptr ||
        !mFilters[dmxFilterId]->isRecordFilter()) {
        return false;
    }

    mRecordFilterIds.insert(dmxFilterId);
    mFilters[dmxFilterId]->attachFilterToRecord(mDvrRecord);

    return true;
}

bool Demux::detachRecordFilter(uint64_t filterId) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    uint64_t dmxFilterId = findFilterIdByfakeFilterId(filterId);
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (mFilters[dmxFilterId] == nullptr || mDvrRecord == nullptr) {
        return false;
    }

    mRecordFilterIds.erase(dmxFilterId);
    mFilters[dmxFilterId]->detachFilterFromRecord();

    return true;
}

sp<AM_DMX_Device> Demux::getAmDmxDevice(void) {
    return AmDmxDevice[mDemuxId];
}

sp<AmDvr> Demux::getAmDvrDevice() {
    return mAmDvrDevice[mDemuxId];
}

bool Demux::checkPesFilterId(uint64_t filterId) {
    set<uint64_t>::iterator it;
    for (it = mPesFilterIds.begin(); it != mPesFilterIds.end(); it++) {
        if (*it == filterId) {
            return true;
         }
    }
    return false;
}

uint32_t Demux::findFilterIdByfakeFilterId(uint64_t fakefilterId) {
    if (fakefilterId > DMX_FILTER_COUNT) {
         return (fakefilterId >> 26) & 0x3f;
    }
    return fakefilterId;
}

bool Demux::setStbSource(const char *path, const char *value)
{
    int ret = 0, fd = -1, len = 0;

    if (path == NULL || value == NULL)
        goto ERROR_EXIT;

    fd = open(path, O_WRONLY);
    if (fd < 0)
        goto ERROR_EXIT;

    len = strlen(value);
    ret = write(fd, value, len);
    if (ret != len)
        goto ERROR_EXIT;
    ALOGD("[Demux] [%s/%d] Write %s ok. value:%s",
        __FUNCTION__, __LINE__, path, value);
    ::close(fd);

    return true;
ERROR_EXIT:
    if (fd >= 0)
        ::close(fd);
    ALOGE("[Demux] [%s/%d] error ret:%d! %s path:%s value:%s",
        __FUNCTION__, __LINE__, ret, strerror(errno), path, value);
    return false;
}

void Demux::attachDescrambler(uint32_t descramblerId,
                              sp<Descrambler> descrambler) {
  std::lock_guard<std::mutex> lock(mFilterLock);
  ALOGD("%s/%d", __FUNCTION__, __LINE__);
  mDescramblers[descramblerId] = descrambler;
}

void Demux::detachDescrambler(uint32_t descramblerId) {
  std::lock_guard<std::mutex> lock(mFilterLock);
  ALOGD("%s/%d", __FUNCTION__, __LINE__);
  mDescramblers.erase(descramblerId);
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

bool Demux::isRawData(uint64_t filterId) {
    return mFilters[filterId]->isRawData();
}

uint32_t Demux::getDemuxId() {
    return mDemuxId;
}

sp<AmPesFilter> Demux::getAmPesFilter() {
    return mAmPesFilter;
}

int Demux::getPesFid() {
    return mPesFid;
}

int Demux::recordTsPacketForPesData(uint64_t filterId) {
    mFilters[filterId]->stop();
    mPesFid = filterId;

    mAmDvrDevice[mDemuxId]->AM_DVR_SetCallback(postDvrData, this);
    mAmDvrDevice[mDemuxId]->AM_DVR_Open(INPUT_LOCAL, mTunerService->getTsInput(), false);

    int pid = getFilterTpid(filterId);
    ALOGD("%s/%d pid = %d", __FUNCTION__, __LINE__, pid);

    struct dmx_pes_filter_params pparam;
    memset(&pparam, 0, sizeof(pparam));
    pparam.pid = pid;
    pparam.input = DMX_IN_FRONTEND;
    pparam.output = DMX_OUT_TS_TAP;
    pparam.pes_type = DMX_PES_OTHER;
    AmDmxDevice[mDemuxId]->AM_DMX_AllocateFilter(&mPesRecordFid);

    if (AmDmxDevice[mDemuxId]->AM_DMX_SetBufferSize(mPesRecordFid, 10 * 1024 * 1024) != 0) {
        ALOGE("record AM_DMX_SetBufferSize");
        return -1;
    }
    if (AmDmxDevice[mDemuxId]->AM_DMX_SetPesFilter(mPesRecordFid, &pparam) != 0) {
        ALOGE("record AM_DMX_SetPesFilter");
        return -1;
    }
    if (AmDmxDevice[mDemuxId]->AM_DMX_StartFilter(mPesRecordFid) != 0) {
        ALOGE("Start filter %d failed!", mPesRecordFid);
        return -1;
    }
    ALOGD("stream(pid = %d) start recording, filter = %d", pid, mPesRecordFid);

    return 1;
}

void Demux::closePesRecordFilter() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    if (AmDmxDevice[mDemuxId] != NULL) {
        AmDmxDevice[mDemuxId]->AM_DMX_StopFilter(mPesRecordFid);
        AmDmxDevice[mDemuxId]->AM_DMX_FreeFilter(mPesRecordFid);
    }
    //mAmDvrDevice[mDemuxId]->AM_DVR_SetCallback(NULL, this);
    if (mAmPesFilter != NULL) {
        mAmPesFilter->release();
        mAmPesFilter = NULL;
    }
    mPesFid = -1;
    mPesRecordFid = -1;
}

bool Demux::checkSoftDemuxForSubtitle() {
    return bSupportSoftDemuxForSubtitle;
}


}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
