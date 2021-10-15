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

#define LOG_TAG "android.hardware.tv.tuner@1.0-Demux"

#include "Demux.h"
#include <utils/Log.h>
#include <cutils/properties.h>

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

#define WAIT_TIMEOUT 3000000000
#define PSI_MAX_SIZE 4096

#ifdef TUNERHAL_DBG
#define TF_DEBUG_DROP_TS_NUM "vendor.tf.drop.tsnum"
#define TF_DEBUG_DUMP_ES_DATA "vendor.tf.dump.es"

static uint32_t mFilterOutputTotalLen = 0;
static uint32_t mDropLen = 0;
static int mDropTsPktNum = 0;
static int mDumpEsData = 0;
#endif

Demux::Demux(uint32_t demuxId, sp<Tuner> tuner) {
    mDemuxId = demuxId;
    mTunerService = tuner;
#ifdef TUNERHAL_DBG
    mDropLen = 0;
    mFilterOutputTotalLen = 0;
    mDropTsPktNum = property_get_int32(TF_DEBUG_DROP_TS_NUM, 0);
    mDumpEsData = property_get_int32(TF_DEBUG_DUMP_ES_DATA, 0);
    ALOGD("mDropLen:%d mFilterOutputTotalLen:%d mDropTsPktNum:%d mDumpEsData:%d",
        mDropLen, mFilterOutputTotalLen, mDropTsPktNum, mDumpEsData);
#endif
    AmDmxDevice[mDemuxId] = new AM_DMX_Device(mDemuxId);
    ALOGD("mDemuxId:%d", mDemuxId);
    AmDmxDevice[mDemuxId]->AM_DMX_Open();
    mMediaSyncWrap = new MediaSyncWrap();
    mAmDvrDevice = new AmDvr(mDemuxId);
     //dump ts file
    //mfd = ::open("/data/local/tmp/media_demux.ts",  O_WRONLY|O_CREAT, 0666);
    //ALOGD("need dump ts file: ts file fd =%d %d", mfd, errno);

}

Demux::~Demux() {
    ALOGD("~Demux");
}

void Demux::postDvrData(void* demux) {
    Demux *dmxDev = (Demux*)demux;
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
    dmxDev->sendFrontendInputToRecord(dvrData);
    dmxDev->startRecordFilterDispatcher();
}

void Demux::combinePesData(uint32_t filterId) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);
    uint8_t tmpbuf[8] = {0};
    uint8_t tmpbuf1[8] = {0};
    int result = -1;
    int packetLen = 0;
    int64_t packetHeader = 0;
    vector<uint8_t> pesData;
    int size = 1;
    while (AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, tmpbuf, &size) == 0) {
        packetHeader = ((packetHeader<<8) & 0x000000ffffffff00) | tmpbuf[0];
        //ALOGD("[Demux] packetHeader = %llx", packetHeader);
        if ((packetHeader & 0xffffffff) == 0x000001bd) {
            ALOGD("## [Demux] combinePesData %x,%llx,-----------\n", tmpbuf[0], packetHeader & 0xffffffffff);
            size = 2;
            result = AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, tmpbuf1, &size);
            packetLen = (tmpbuf1[0] << 8) | tmpbuf1[1];
            ALOGD("[Demux] packetLen = %d", packetLen);
            pesData.resize(packetLen + 6);
            pesData[0] = 0x0;
            pesData[1] = 0x0;
            pesData[2] = 0x01;
            pesData[3] = 0xbd;
            pesData[4] = tmpbuf1[0];
            pesData[5] = tmpbuf1[1];
            int readLen = 0;
            int dataLen = 0;
            do {
                dataLen = packetLen - readLen;
                result = AmDmxDevice[mDemuxId]->AM_DMX_Read(filterId, pesData.data() + 6 + readLen, &dataLen);
                //ALOGD("[Demux] result = 0x%x", result);
                if (result == 0) {
                    readLen += dataLen;
                } else if (result == -1){
                    //ALOGD("[Demux] filter has not allocated");
                    return;
                }
            } while(readLen < packetLen);

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

void Demux::postData(void* demux, int fid, bool esOutput, bool passthrough) {
    vector<uint8_t> tmpData;
    vector<uint8_t> tmpSectionData;
    int sectionSize = PSI_MAX_SIZE;
    Demux *dmxDev = (Demux*)demux;
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
            dmxDev->updateFilterOutput(fid, tmpData);
            //Copy mFilterOutput to av ion buffer and create mFilterEvent
            dmxDev->startFilterHandler(fid);
#endif
            }
        }
    } else {
        bool isPesFilterId = dmxDev->checkPesFilterId(fid);
        if (isPesFilterId) {
            ALOGD("start pes data combine fid = %d", fid);
            dmxDev->combinePesData(fid);
        } else {
            tmpSectionData.resize(sectionSize);
            int readRet = dmxDev->getAmDmxDevice()
                          ->AM_DMX_Read(fid, tmpSectionData.data(), &sectionSize);
            if (readRet != 0) {
                ALOGE("AM_DMX_Read failed! readRet:0x%x", readRet);
                return;
            } else {
                ALOGV("fid =%d section data size:%d", fid, sectionSize);
                tmpSectionData.resize(sectionSize);
                dmxDev->updateFilterOutput(fid, tmpSectionData);
                dmxDev->startFilterHandler(fid);
            }
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
    int dmxFilterIdx;
    DemuxTsFilterType tsFilterType = type.subType.tsFilterType();
    std::lock_guard<std::mutex> lock(mFilterLock);

    if (tsFilterType == DemuxTsFilterType::UNDEFINED) {
        ALOGE("[Demux] Invalid filter type!");
        _hidl_cb(Result::INVALID_ARGUMENT, nullptr);
        return Void();
    }
    if (cb == nullptr) {
        ALOGE("[Demux] Filter callback is null!");
        _hidl_cb(Result::INVALID_ARGUMENT, nullptr);
        return Void();
    }

    AmDmxDevice[mDemuxId]->AM_DMX_AllocateFilter(&dmxFilterIdx);
    ALOGD("[%s/%d] Allocate filter subType:%d filterIdx:%d", __FUNCTION__, __LINE__, tsFilterType, dmxFilterIdx);

    //Use demux fd as the tuner hal filter id
    sp<Filter> filter = new Filter(type, dmxFilterIdx, bufferSize, cb, this);

    if (!filter->createFilterMQ()) {
        ALOGE("[Demux] filter %d createFilterMQ error!", dmxFilterIdx);
        AmDmxDevice[mDemuxId]->AM_DMX_FreeFilter(dmxFilterIdx);
        _hidl_cb(Result::UNKNOWN_ERROR, filter);
        return Void();
    }

    if (tsFilterType == DemuxTsFilterType::SECTION
        || tsFilterType == DemuxTsFilterType::VIDEO
        || tsFilterType == DemuxTsFilterType::AUDIO
        || tsFilterType == DemuxTsFilterType::PES) {
        AmDmxDevice[mDemuxId]->AM_DMX_SetCallback(dmxFilterIdx, this->postData, this);
    } else if (tsFilterType == DemuxTsFilterType::PCR) {
        AmDmxDevice[mDemuxId]->AM_DMX_SetCallback(dmxFilterIdx, NULL, NULL);
    } else if (tsFilterType == DemuxTsFilterType::RECORD) {
        mAmDvrDevice->AM_DVR_SetCallback(this->postDvrData, this);
    }

    mFilters[dmxFilterIdx] = filter;
    if (tsFilterType == DemuxTsFilterType::PES) {
        mPesFilterIds.insert(dmxFilterIdx);
        ALOGD("Insert PES filter");
    }
    if (tsFilterType == DemuxTsFilterType::PCR) {
        mPcrFilterIds.insert(dmxFilterIdx);
        ALOGD("Insert pcr filter");
    }
    bool result = true;

    if (tsFilterType != DemuxTsFilterType::RECORD && tsFilterType != DemuxTsFilterType::PCR) {
        // Only save non-record filters for now. Record filters are saved when the
        // IDvr.attacheFilter is called.
        mPlaybackFilterIds.insert(dmxFilterIdx);
        if (mDvrPlayback != nullptr) {
            ALOGD("[%s/%d] addPlaybackFilter filterIdx:%d", __FUNCTION__, __LINE__, dmxFilterIdx);
            result = mDvrPlayback->addPlaybackFilter(dmxFilterIdx, filter);
        }
    }

    _hidl_cb(result ? Result::SUCCESS : Result::INVALID_ARGUMENT, filter);
    return Void();
}

Return<void> Demux::openTimeFilter(openTimeFilter_cb _hidl_cb) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    mTimeFilter = new TimeFilter(this);

    _hidl_cb(Result::SUCCESS, mTimeFilter);
    return Void();
}

Return<void> Demux::getAvSyncHwId(const sp<IFilter>& filter, getAvSyncHwId_cb _hidl_cb) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    Result status;

    int fid;

    if (mAvSyncHwId != -1) {
        _hidl_cb(Result::SUCCESS, mAvSyncHwId);
        return Void();
    }

    if (filter == nullptr) {
        ALOGE("[Demux] filter is null!");
        _hidl_cb(Result::INVALID_STATE, mAvSyncHwId);
        return Void();
    }

    filter->getId([&](Result result, uint32_t filterId) {
        fid = filterId;
        status = result;
    });

    if (status != Result::SUCCESS) {
        ALOGE("[Demux] Can't get filter Id.");
        _hidl_cb(Result::INVALID_STATE, mAvSyncHwId);
        return Void();
    }

    if (fid > DMX_FILTER_COUNT) {
        fid = findFilterIdByfakeFilterId(fid);
    }

    ALOGD("%s/%d fid = %d", __FUNCTION__, __LINE__, fid);
    if (mFilters[fid]->isMediaFilter() && !mPlaybackFilterIds.empty()) {
        uint16_t avPid = getFilterTpid(*mPlaybackFilterIds.begin());
        mAvSyncHwId = mMediaSyncWrap->getAvSyncHwId(mDemuxId, avPid);
        mMediaSyncWrap->bindAvSyncId(mAvSyncHwId);
        ALOGD("[Demux] mAvFilterId:%d avPid:0x%x avSyncHwId:%d", *mPlaybackFilterIds.begin(), avPid, mAvSyncHwId);
        _hidl_cb(Result::SUCCESS, mAvSyncHwId);
        return Void();

    } else if (mFilters[fid]->isPcrFilter() && !mPcrFilterIds.empty()) {
        // Return the lowest pcr filter id in the default implementation as the av sync id
        uint16_t pcrPid = getFilterTpid(*mPcrFilterIds.begin());
        mAvSyncHwId = mMediaSyncWrap->getAvSyncHwId(mDemuxId, pcrPid);
        mMediaSyncWrap->bindAvSyncId(mAvSyncHwId);
        ALOGD("[Demux] mPcrFilterId:%d pcrPid:0x%x avSyncHwId:%d", *mPcrFilterIds.begin(), pcrPid, mAvSyncHwId);
        _hidl_cb(Result::SUCCESS, mAvSyncHwId);
        return Void();
    } else {
        ALOGD("[Demux] No pcr or No media filter opened.");
        _hidl_cb(Result::INVALID_STATE, mAvSyncHwId);
        return Void();
    }
}

Return<void> Demux::getAvSyncTime(AvSyncHwId avSyncHwId, getAvSyncTime_cb _hidl_cb) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);

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

    if (mMediaSyncWrap != NULL) {
        int64_t time = -1;
        time = mMediaSyncWrap->getAvSyncTime();
        avSyncTime = 0x1FFFFFFFF & ((9*time)/100);
    }
    //ALOGD("%s/%d avSyncTime = %llu", __FUNCTION__, __LINE__, avSyncTime);

    _hidl_cb(Result::SUCCESS, avSyncTime);
    return Void();
}

Return<Result> Demux::close() {
    ALOGD("[%s/%d] mDemuxId:%d", __FUNCTION__, __LINE__, mDemuxId);
    std::lock_guard<std::mutex> lock(mFilterLock);

    set<uint32_t>::iterator it;
    for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
        mDvrPlayback->removePlaybackFilter(*it);
    }
    mPlaybackFilterIds.clear();
    mRecordFilterIds.clear();
    mPcrFilterIds.clear();
    mPesFilterIds.clear();
    mFilters.clear();
    mMapFilter.clear();
    mLastUsedFilterId = -1;

    mDvrPlayback = nullptr;

    if (mMediaSyncWrap != NULL) {
        mMediaSyncWrap = NULL;
    }

    if (AmDmxDevice[mDemuxId] != NULL) {
        AmDmxDevice[mDemuxId]->AM_DMX_Close();
        AmDmxDevice[mDemuxId] = NULL;
    }
    if (mAmDvrDevice != NULL) {
        mAmDvrDevice->AM_DVR_Close();
        mAmDvrDevice = NULL;
    }

    return Result::SUCCESS;
}

Return<void> Demux::openDvr(DvrType type, uint32_t bufferSize, const sp<IDvrCallback>& cb,
                            openDvr_cb _hidl_cb) {
    if (cb == nullptr) {
        ALOGW("[Demux] DVR callback can't be null");
        _hidl_cb(Result::INVALID_ARGUMENT, new Dvr());
        return Void();
    }
    std::lock_guard<std::mutex> lock(mFilterLock);

    //bufferSize is used to create DvrMQ
    ALOGD("%s/%d bufferSize:%d KB", __FUNCTION__, __LINE__,  bufferSize/1024);
    set<uint32_t>::iterator it;
    switch (type) {
        case DvrType::PLAYBACK:
            ALOGD("DvrType::PLAYBACK");
            mDvrPlayback = new Dvr(type, bufferSize, cb, this);
            ALOGD("[Demux] dmx_dvr_open INPUT_LOCAL");
            AmDmxDevice[mDemuxId]->dmx_dvr_open(INPUT_LOCAL);
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

            _hidl_cb(Result::SUCCESS, mDvrPlayback);
            return Void();
        case DvrType::RECORD:
            ALOGD("DvrType::RECORD");
            mDvrRecord = new Dvr(type, bufferSize, cb, this);
            ALOGD("[Demux] dmx_dvr_open INPUT_DEMOD");
            mAmDvrDevice->AM_DVR_Open(INPUT_DEMOD);
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
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    mCiCamId = ciCamId;

    return Result::SUCCESS;
}

Return<Result> Demux::disconnectCiCam() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    return Result::SUCCESS;
}

Result Demux::removeFilter(uint32_t filterId) {

    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    std::lock_guard<std::mutex> lock(mFilterLock);

    if (mDvrPlayback != nullptr) {
        mDvrPlayback->removePlaybackFilter(filterId);
    }
    if (checkPesFilterId(filterId)) {
        mPesFilterIds.erase(filterId);
        ALOGD("%s/%d fid = %d", __FUNCTION__, __LINE__, filterId);
    }
    mPlaybackFilterIds.erase(filterId);
    mRecordFilterIds.erase(filterId);
    mFilters.erase(filterId);
    return Result::SUCCESS;
}

void Demux::startBroadcastTsFilter(vector<uint8_t> data) {
    std::lock_guard<std::mutex> lock(mFilterLock);

    uint16_t pid = ((data[1] & 0x1f) << 8) | ((data[2] & 0xff));
    if (DEBUG_DEMUX)
        ALOGD("%s/%d write to dvr %d size:%d pid:0x%x", __FUNCTION__, __LINE__, mDemuxId, data.size(), pid);

    for (auto descramblerIt = mDescramblers.begin();
         descramblerIt != mDescramblers.end(); descramblerIt++) {
      if (descramblerIt->second->isPidSupported(pid)) {
        if (DEBUG_DEMUX) {
          ALOGD("[Demux] found descrambler for pid: %d, data.size(): %d",
                pid,
                data.size());
        }
        if (!descramblerIt->second->isDescramblerReady())
            ALOGD("[Demux] dsc isn't ready for pid: %d", pid);
        continue;
      }
    }

    set<uint32_t>::iterator it;
    for (it = mPlaybackFilterIds.begin(); it != mPlaybackFilterIds.end(); it++) {
        if (pid == mFilters[*it]->getTpid()) {
            if (1) {
                AmDmxDevice[mDemuxId]->AM_DMX_WriteTs(data.data(), data.size(), 300 * 1000);
            } else {
                mFilters[*it]->updateFilterOutput(data);
            }
            break;
        }
    }
}

void Demux::sendFrontendInputToRecord(vector<uint8_t> data) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (mRecordFilterIds.size() == 0) {
        ALOGD("no record filter id");
        return;
    }
    set<uint32_t>::iterator it = mRecordFilterIds.begin();
    if (DEBUG_DEMUX) {
        ALOGW("[Demux] update record filter output data size = %d", data.size());
    }
    mFilters[*it]->updateRecordOutput(data);
    /*
    for (it = mRecordFilterIds.begin(); it != mRecordFilterIds.end(); it++) {
        mFilters[*it]->updateRecordOutput(data);
    }*/
}

bool Demux::startBroadcastFilterDispatcher() {
    std::lock_guard<std::mutex> lock(mFilterLock);
    set<uint32_t>::iterator it;

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
    set<uint32_t>::iterator it = mRecordFilterIds.begin();

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

Result Demux::startFilterHandler(uint32_t filterId) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (DEBUG_DEMUX)
        ALOGD("%s/%d filterId:%d", __FUNCTION__, __LINE__, filterId);
    //Create mFilterEvent with mFilterOutput
    return mFilters[filterId]->startFilterHandler();
}

void Demux::updateFilterOutput(uint16_t filterId, vector<uint8_t> data) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    if (DEBUG_DEMUX)
        ALOGD("%s/%d filterId:%d", __FUNCTION__, __LINE__, filterId);
    //Copy data to mFilterOutput
    mFilters[filterId]->updateFilterOutput(data);
}

uint16_t Demux::getFilterTpid(uint32_t filterId) {
    std::lock_guard<std::mutex> lock(mFilterLock);
    return mFilters[filterId]->getTpid();
}

void Demux::startFrontendInputLoop() {
    pthread_create(&mFrontendInputThread, NULL, __threadLoopFrontend, this);
    pthread_setname_np(mFrontendInputThread, "frontend_input_thread");
}

void* Demux::__threadLoopFrontend(void* user) {
    Demux* const self = static_cast<Demux*>(user);
    self->frontendInputThreadLoop();
    return 0;
}

void Demux::frontendInputThreadLoop() {
    std::lock_guard<std::mutex> lock(mFrontendInputThreadLock);
    mFrontendInputThreadRunning = true;
    ALOGI("%s/%d readPlaybackFMQ and startFilterDispatcher", __FUNCTION__, __LINE__);
    if (mDvrPlayback == nullptr) {
        ALOGE("DvrPlayback didn't open, no need to start frontend input thread");
        return;
    }

    while (mFrontendInputThreadRunning) {
        uint32_t efState = 0;
        status_t status = mDvrPlayback->getDvrEventFlag()->wait(
                static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY), &efState, WAIT_TIMEOUT,
                true /* retry on spurious wake */);
        if (status != OK) {
            ALOGD("[Demux] wait for data ready on the playback FMQ");
            continue;
        }
        // Our current implementation filter the data and write it into the filter FMQ immediately
        // after the DATA_READY from the VTS/framework
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
    pthread_join(mFrontendInputThread, NULL);
    std::lock_guard<std::mutex> lock(mFrontendInputThreadLock);
}

void Demux::setIsRecording(bool isRecording) {
    ALOGD("%s/%d isRecording:%d", __FUNCTION__, __LINE__, isRecording);

    mIsRecording = isRecording;
}

bool Demux::attachRecordFilter(int filterId) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    std::lock_guard<std::mutex> lock(mFilterLock);

    if (mFilters[filterId] == nullptr || mDvrRecord == nullptr ||
        !mFilters[filterId]->isRecordFilter()) {
        return false;
    }

    mRecordFilterIds.insert(filterId);
    mFilters[filterId]->attachFilterToRecord(mDvrRecord);

    return true;
}

bool Demux::detachRecordFilter(int filterId) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    std::lock_guard<std::mutex> lock(mFilterLock);

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
    return mAmDvrDevice;
}

bool Demux::checkPesFilterId(uint32_t filterId) {
    set<uint32_t>::iterator it;
    for (it = mPesFilterIds.begin(); it != mPesFilterIds.end(); it++) {
        if (*it == filterId)
            return true;
    }
    return false;
}

void Demux::mapPassthroughMediaFilter(uint32_t fakefilterId, uint32_t filterId) {
    mMapFilter[fakefilterId] = filterId;
}

uint32_t Demux::findFilterIdByfakeFilterId(uint32_t fakefilterId) {
    std::map<uint32_t, uint32_t>::iterator it;
    it = mMapFilter.find(fakefilterId);
    if (it != mMapFilter.end()) {
        return it->second;
    }
    return -1;
}

void Demux::eraseFakeFilterId(uint32_t fakefilterId) {
    mMapFilter.erase(fakefilterId);
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
  ALOGD("%s/%d", __FUNCTION__, __LINE__);
  mDescramblers[descramblerId] = descrambler;
}

void Demux::detachDescrambler(uint32_t descramblerId) {
  ALOGD("%s/%d", __FUNCTION__, __LINE__);
  mDescramblers.erase(descramblerId);
}
}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
