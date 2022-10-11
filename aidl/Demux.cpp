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
#define LOG_TAG "android.hardware.tv.tuner-service.droidlogic-Demux"

#include <aidl/android/hardware/tv/tuner/DemuxQueueNotifyBits.h>
#include <aidl/android/hardware/tv/tuner/Result.h>

#include <utils/Log.h>
#include "Demux.h"
#include <cutils/properties.h>

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

namespace {
constexpr int kTsPacketSize = 188;

bool isValidTsPacket(const vector<uint8_t>& tsPacket) {
  return tsPacket.size() == kTsPacketSize && tsPacket[0] == 0x47;
}

}  // namespace

#define WAIT_TIMEOUT 3000000000
#define PSI_MAX_SIZE 4096
#define PES_RAW_DATA_SIZE 64 * 1024
#define PRIVATE_STREAM_1   0x1bd
#define PRIVATE_STREAM_2   0x1bf
#define SUPPORT_SOFTWARE_DEMUX_SUBTITLE "vendor.tunerhal.softwaredemux.subtitle"

static vector<int8_t> uint8DataToInt8Data(vector<uint8_t> uInt8Data) {
    vector<int8_t> int8Data;
    int len = uInt8Data.size();
    int8Data.resize(len);
    memcpy(int8Data.data(), uInt8Data.data(), len * sizeof(uint8_t));
    return int8Data;
}

Demux::Demux(int32_t demuxId, std::shared_ptr<Tuner> tuner) {
    mDemuxId = demuxId;
    mTuner = tuner;
    mCiCamId = 0;
    mFrontendInputThreadRunning = false;
    mKeepFetchingDataFromFrontend = false;
    bSupportSoftDemuxForSubtitle =  property_get_bool(SUPPORT_SOFTWARE_DEMUX_SUBTITLE, true);
    ALOGD("mDemuxId:%d, bSupportSoftDemuxForSubtitle = %d", mDemuxId, bSupportSoftDemuxForSubtitle);
    AmDmxDevice[mDemuxId] = new AM_DMX_Device(mDemuxId);
    AmDmxDevice[mDemuxId]->AM_DMX_Open();
    mAmDvrDevice[mDemuxId] = new AmDvr(mDemuxId);
    mAmDvrDevice[mDemuxId]->AM_DVR_Open(INPUT_DEMOD, mTuner->getTsInput(), true);
}

Demux::~Demux() {
    close();
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
    //ALOGD("dump bytes: %s", strData.c_str());

    Demux *dmxDev = (Demux*)demux;
    vector<int8_t> pesData;
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
        dmxDev->sendFrontendInputToRecord(uint8DataToInt8Data(dvrData));
        dmxDev->startRecordFilterDispatcher();
    }
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
                    result = AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, pesData.data() + 6 + 3 + pesHeaderLen +
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
    int readRet = AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, sectionData.data(), &sectionSize);
    if (readRet != 0) {
        ALOGE("AM_DMX_Read failed! readRet:0x%x", readRet);
        return;
    } else {
        ALOGD("fid =%lld section data size:%d", filterId, sectionSize);
        /* for debug
        uint16_t tableId = sectionData[0];
        if (tableId == 0x0) {
            ALOGD("received PAT table tableId = %d, fid = %d", tableId, filterId);
        }
        if (tableId == 0x2) {
            ALOGD("received PMT table tableId = %d, fid = %d", tableId, filterId);
        }*/
        updateFilterOutput(filterId, uint8DataToInt8Data(sectionData));
        startFilterHandler(filterId);
    }

}

void Demux::getPesRawData(int64_t filterId) {
    vector<uint8_t> pesRawData;
    int pesRawDataSize = PES_RAW_DATA_SIZE;
    pesRawData.resize(pesRawDataSize);
    int readRet = AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, pesRawData.data(), &pesRawDataSize);
    if (readRet != 0) {
        ALOGE("AM_DMX_Read failed! readRet:0x%x", readRet);
        return;
    } else {
        ALOGD("fid =%lld pes raw data size:%d", filterId, pesRawDataSize);
        pesRawData.resize(pesRawDataSize);
        updateFilterOutput(filterId, uint8DataToInt8Data(pesRawData));
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
    ALOGD("[Demux] postData fid =%d esOutput:%d dev_no:%d", fid, esOutput, dmxDev->getAmDmxDevice()->dev_no);

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
    ALOGV("%s", __FUNCTION__);

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

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Demux::openFilter(const DemuxFilterType& in_type, int32_t in_bufferSize,
                                       const std::shared_ptr<IFilterCallback>& in_cb,
                                       std::shared_ptr<IFilter>* _aidl_return) {
    ALOGD("%s", __FUNCTION__);

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
    AmDmxDevice[mDemuxId]->AM_DMX_AllocateFilter(&dmxFilterIdx);
    filterId = dmxFilterIdx;
    ALOGD("%s get filterid = %lld", __FUNCTION__, filterId);
    std::shared_ptr<Filter> filter = ndk::SharedRefBase::make<Filter>(
            in_type, filterId, in_bufferSize, in_cb, this->ref<Demux>());
    if (!filter->createFilterMQ()) {
        *_aidl_return = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNKNOWN_ERROR));
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

    if (hasTsFilterType && filter->isPcrFilter()) {
        mPcrFilterIds.insert(dmxFilterIdx);
        ALOGD("Insert pcr filter");
    }

    bool result = true;
    if (hasTsFilterType && tsFilterType != DemuxTsFilterType::RECORD && tsFilterType != DemuxTsFilterType::PCR) {
        // Only save non-record filters for now. Record filters are saved when the
        // IDvr.attacheFilter is called.
        mPlaybackFilterIds.insert(filterId);
        if (mDvrPlayback != nullptr) {
            ALOGD("[%s/%d] addPlaybackFilter filterIdx:%lld", __FUNCTION__, __LINE__, filterId);
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

    if (fid > DMX_FILTER_COUNT) {
        fid = findFilterIdByfakeFilterId(fid);
    }

    if (mMediaSync == nullptr) {
        ALOGD("[debuglevel] new mediasync");
        mMediaSync = new MediaSyncWrap();
    }

    ALOGD("%s/%d fid = %lld", __FUNCTION__, __LINE__, fid);
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (mFilters[fid] != nullptr && mFilters[fid]->isMediaFilter() && !mPlaybackFilterIds.empty()) {
        uint16_t avPid = getFilterTpid(*mPlaybackFilterIds.begin());
        if (mMediaSync != nullptr) {
            if (mAvSyncHwId == -1) {
                mAvSyncHwId = mMediaSync->getAvSyncHwId(mDemuxId, avPid);
                mMediaSync->setParameter(MEDIASYNC_KEY_ISOMXTUNNELMODE, &mode);
                mMediaSync->bindAvSyncId(mAvSyncHwId);
            }
        }
        ALOGD("[Demux] mAvFilterId:%lld avPid:0x%x avSyncHwId:%lld", *mPlaybackFilterIds.begin(), avPid, mAvSyncHwId);
        *_aidl_return = mAvSyncHwId;
        return ::ndk::ScopedAStatus::ok();
    } else if (mFilters[fid] != nullptr && mFilters[fid]->isPcrFilter() && !mPcrFilterIds.empty()) {
        // Return the lowest pcr filter id in the default implementation as the av sync id
        uint16_t pcrPid = getFilterTpid(*mPcrFilterIds.begin());
        if (mMediaSync != nullptr) {
            if (mAvSyncHwId == -1) {
                mAvSyncHwId = mMediaSync->getAvSyncHwId(mDemuxId, pcrPid);
                mMediaSync->setParameter(MEDIASYNC_KEY_ISOMXTUNNELMODE, &mode);
                mMediaSync->bindAvSyncId(mAvSyncHwId);
            }
        }
        ALOGD("[Demux] mPcrFilterId:%lld pcrPid:0x%x avSyncHwId:%lld", *mPcrFilterIds.begin(), pcrPid, mAvSyncHwId);
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
    ALOGV("%s", __FUNCTION__);
    std::lock_guard<std::mutex> lock(mFilterLock);
    stopFrontendInput();

    set<int64_t>::iterator it;
    for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
        mDvrPlayback->removePlaybackFilter(*it);
    }
    mPlaybackFilterIds.clear();
    mRecordFilterIds.clear();
    mFilters.clear();
    mPcrFilterIds.clear();
    mPesFilterIds.clear();
    mMapFilter.clear();
    mLastUsedFilterId = -1;

    mDvrPlayback = nullptr;
    destroyMediaSync();

    if (AmDmxDevice[mDemuxId] != NULL) {
        AmDmxDevice[mDemuxId]->AM_DMX_Close();
        AmDmxDevice[mDemuxId] = NULL;
    }

    if (mAmDvrDevice[mDemuxId] != NULL) {
        mAmDvrDevice[mDemuxId]->AM_DVR_Close();
        mAmDvrDevice[mDemuxId] = NULL;
    }
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
            ALOGD("DvrType::PLAYBACK");
            mDvrPlayback = ndk::SharedRefBase::make<Dvr>(in_type, in_bufferSize, in_cb,
                                                        this->ref<Demux>());
            if (bCheckVts) {
                ALOGD("[Demux] dmx_dvr_open INPUT_LOCAL");
                AmDmxDevice[mDemuxId]->dmx_dvr_open(INPUT_LOCAL);
            }
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

            *_aidl_return = mDvrPlayback;
            return ::ndk::ScopedAStatus::ok();
        case DvrType::RECORD:
            mDvrRecord = ndk::SharedRefBase::make<Dvr>(in_type, in_bufferSize, in_cb,
                                                       this->ref<Demux>());
            //ALOGD("[Demux] dmx_dvr_open INPUT_DEMOD");
             //mAmDvrDevice->AM_DVR_Open(INPUT_DEMOD);
            if (!mDvrRecord->createDvrMQ()) {
                mDvrRecord = nullptr;
                *_aidl_return = mDvrRecord;
                return ::ndk::ScopedAStatus::fromServiceSpecificError(
                        static_cast<int32_t>(Result::UNKNOWN_ERROR));
            }

            *_aidl_return = mDvrRecord;
            return ::ndk::ScopedAStatus::ok();
        default:
            *_aidl_return = nullptr;
            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }
}

::ndk::ScopedAStatus Demux::connectCiCam(int32_t in_ciCamId) {
    ALOGV("%s", __FUNCTION__);

    mCiCamId = in_ciCamId;

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Demux::disconnectCiCam() {
    ALOGV("%s", __FUNCTION__);

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Demux::removeFilter(int64_t filterId) {
    ALOGD("%s/%d filterId = %lld", __FUNCTION__, __LINE__, filterId);
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
        ALOGD("remove PES filter mPesFid = %lld", filterId);
        mPesFilterIds.erase(filterId);
    }

    if (mDvrPlayback != nullptr) {
        mDvrPlayback->removePlaybackFilter(filterId);
    }

    return ::ndk::ScopedAStatus::ok();
}

void Demux::startBroadcastTsFilter(vector<int8_t> data) {
     uint16_t pid = ((data[1] & 0x1f) << 8) | ((data[2] & 0xff));
     bool needWriteData = false;
     if (DEBUG_DEMUX)
         ALOGD("%s/%d write to dvr %d size:%d pid:0x%x", __FUNCTION__, __LINE__, mDemuxId, data.size(), pid);
     {
         std::lock_guard<std::mutex> lock(mFilterLock);
         /*
         for (auto descramblerIt = mDescramblers.begin(); descramblerIt != mDescramblers.end(); descramblerIt++) {
             if (descramblerIt->second && descramblerIt->second->isPidSupported(pid)) {
                 if (DEBUG_DEMUX)
                     ALOGD("[Demux] found descrambler for pid: 0x%x", pid);
                 if (!descramblerIt->second->isDescramblerReady())
                     ALOGV("[Demux] dsc isn't ready for pid: %d", pid);
                 continue;
             }
         }*/
         set<int64_t>::iterator it;
         for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
             if (mFilters[*it] != nullptr && pid == mFilters[*it]->getTpid()) {
                 needWriteData = true;
                 break;
             }
         }
    }
     if (needWriteData) {
         vector<uint8_t> udata;
         udata.resize(data.size());
         memcpy(udata.data(), data.data(), data.size() * sizeof(uint8_t));
         if (isValidTsPacket(udata)) {
             while (AmDmxDevice[mDemuxId]->AM_DMX_WriteTs(udata.data(), udata.size(), 300 * 1000) == -1) {
                 ALOGD("[Demux] wait for 100ms to write dvr device");
                 usleep(100 * 1000);
             }
         } else {
             ALOGD("[Demux] data[0] = 0x%x", data[0]);
         }
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
        ALOGW("[Demux] update record filter output data size = %d", data.size());
    }
    mFilters[*it]->updateRecordOutput(data);
    /*
    for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
        mFilters[*it]->updateRecordOutput(data);
    }*/

}

void Demux::sendFrontendInputToRecord(vector<int8_t> data, uint16_t pid, uint64_t pts) {
    sendFrontendInputToRecord(data);
    set<int64_t>::iterator it;
    for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
        if (pid == mFilters[*it]->getTpid()) {
            mFilters[*it]->updatePts(pts);
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

    if (!mFilters[*it]->startRecordFilterHandler().isOk()) {
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

::ndk::ScopedAStatus Demux::startFilterHandler(int64_t filterId) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (DEBUG_DEMUX)
        ALOGD("%s/%d filterId:%lld", __FUNCTION__, __LINE__, filterId);
    /*
    for (auto descramblerIt = mDescramblers.begin(); descramblerIt != mDescramblers.end(); descramblerIt++) {
        if (descramblerIt->second && !descramblerIt->second->isDescramblerReady())
            ALOGV("[Demux] dsc isn't ready.");
        continue;
    }*/
    //Create mFilterEvent with mFilterOutput
    if (mFilters[filterId] != nullptr) {
        mFilters[filterId]->startFilterHandler();
        return ::ndk::ScopedAStatus::ok();
    } else {
        ALOGW("%s/%d filterId = %lld may be removed", __FUNCTION__, __LINE__, filterId);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                static_cast<int32_t>(Result::UNKNOWN_ERROR));
    }
}

void Demux::updateFilterOutput(int64_t filterId, vector<int8_t> data) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (DEBUG_DEMUX)
        ALOGD("%s/%d filterId:%lld", __FUNCTION__, __LINE__, filterId);
    //Copy data to mFilterOutput
    if (mFilters[filterId] != nullptr) {
        mFilters[filterId]->updateFilterOutput(data);
    } else {
        ALOGW("%s/%d filterId = %lld may be removed", __FUNCTION__, __LINE__, filterId);
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

void Demux::startFrontendInputLoop() {
    ALOGD("[Demux] start frontend on demux");
    // Stop current Frontend thread loop first, in case the user starts a new
    // tuning before stopping current tuning.
    stopFrontendInput();
    mFrontendInputThreadRunning = true;
    mFrontendInputThread = std::thread(&Demux::frontendInputThreadLoop, this);
}

void Demux::frontendInputThreadLoop() {
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
    if (mFilters[filterId] == nullptr || mDvrRecord == nullptr ||
        !mFilters[filterId]->isRecordFilter()) {
        return false;
    }

    mRecordFilterIds.insert(filterId);
    mFilters[filterId]->attachFilterToRecord(mDvrRecord);

    return true;
}

bool Demux::detachRecordFilter(int64_t filterId) {
    if (mFilters[filterId] == nullptr || mDvrRecord == nullptr) {
        return false;
    }

    mRecordFilterIds.erase(filterId);
    mFilters[filterId]->detachFilterFromRecord();

    return true;
}

sp<AM_DMX_Device> Demux::getAmDmxDevice(void) {
    return AmDmxDevice[mDemuxId];
}

sp<AmDvr> Demux::getAmDvrDevice() {
    return mAmDvrDevice[mDemuxId];
}

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

bool Demux::isRawData(int64_t filterId) {
    return mFilters[filterId]->isRawData();
}

bool Demux::checkSoftDemuxForSubtitle() {
    return bSupportSoftDemuxForSubtitle;
}

int Demux::recordTsPacketForPesData(int64_t         filterId) {
    mFilters[filterId]->stop();
    mPesFid = filterId;

    mAmDvrDevice[mDemuxId]->AM_DVR_SetCallback(postDvrData, this);
    mAmDvrDevice[mDemuxId]->AM_DVR_Open(INPUT_LOCAL, mTuner->getTsInput(), false);

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

int64_t Demux::findFilterIdByfakeFilterId(int64_t fakefilterId) {
    std::map<int64_t, int64_t>::iterator it;
    it = mMapFilter.find(fakefilterId);
    if (it != mMapFilter.end()) {
        return it->second;
    }
    return -1;
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

void Demux::mapPassthroughMediaFilter(int64_t fakefilterId, int64_t filterId) {
    mMapFilter[fakefilterId] = filterId;
}

}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
