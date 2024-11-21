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

#define LOG_TAG "android.hardware.tv.tuner@1.1-Dvr"

#include "Dvr.h"
#include <utils/Log.h>
#include <sys/prctl.h>

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

#define WAIT_TIMEOUT 3000000000

Dvr::Dvr() {
    mType = DvrType(0);
    mBufferSize = 0;
    mDvrEventFlag = NULL;
    mDvrThread = 0;
    mPlaybackStatus = PlaybackStatus(1u);
    mRecordStatus = DemuxFilterStatus(0);
    mKeepFetchingDataFromFrontend = false;
}

Dvr::Dvr(DvrType type, uint32_t bufferSize, const sp<IDvrCallback>& cb, sp<Demux> demux, sp<Tuner> tuner) {
    mType = type;
    mBufferSize = bufferSize;
    mCallback = cb;
    mDemux = demux;
    mKeepFetchingDataFromFrontend = false;
    mDvrEventFlag = NULL;
    mDvrThread = 0;
    mTuner = tuner;
    mPlaybackStatus = PlaybackStatus(1u);
    mRecordStatus = DemuxFilterStatus(0);
    ALOGD("%s/%d type:%d bufsize:%d MB", __FUNCTION__, __LINE__, (int)type, bufferSize/1024/1024);
    if (mType == DvrType::PLAYBACK) {
        ALOGD("%s/%d dvr_playback_open dmxid = %d", __FUNCTION__, __LINE__, mDemux->getDemuxId());
        mPlaybackParams.dmx_dev_id = mDemux->getDemuxId();
        DVR_Result_t ret = dvr_playback_open(&mPlaybackhandle, &mPlaybackParams);
        if (ret != DVR_SUCCESS) {
            ALOGD("open dvr playback failed!\n");
        }
        mDemux->setPlaybackHandle(mPlaybackhandle);
        mStartDvrThread = true;
        if (pthread_create(&mDvrThread, NULL, __threadLoopPlayback, this)) {
            ALOGD("[Filter] can't create mDvrThread thread!");
        }
        pthread_setname_np(mDvrThread, "playback_waiting_loop");
    } else if (mType == DvrType::RECORD) {
        ALOGD("%s/%d dvr_record_open dmxid = %d", __FUNCTION__, __LINE__, mDemux->getDemuxId());
        memset(&mOpenParams, 0, sizeof(DVR_RecordOpenParams_t));
        bool bPlayback = mDemux->checkDemuxPlayback();
        ALOGD("%s/%d  dmxid = %d, bPlayback = %d", __FUNCTION__, __LINE__, mDemux->getDemuxId(), bPlayback);
        if (bPlayback) {
            mOpenParams.src = static_cast<DVB_DemuxSource_t>(DVB_DEMUX_SOURCE_DMA0 + mDemux->getDemuxId());
        } else {
            mOpenParams.src = getDemuxSourceByTsInput(mDemux->getTsInput());
        }
        mOpenParams.dmx_dev_id[0] = mDemux->getDemuxId();
        mOpenParams.dmx_dev_id[1] = mTuner->allocateDemuxResource(); //keep demux4 is idle(unused)
        mOpenParams.dmx_dev_id[2] = mTuner->allocateDemuxResource(); //keep demux5 is idle(unused)
        mOpenParams.non_sec_ringbuf_size = DVR_BUFFER_LEN;
        mOpenParams.sec_buf_size         = DVR_BUFFER_LEN;
        mOpenParams.encrypt_pvr          = mTuner->getEncryptPvrSetting();
        DVR_Result_t ret = dvr_record_open(&mRecordhandle, &mOpenParams);
        if (ret != DVR_SUCCESS) {
            ALOGD("open dvr record failed!\n");
        }
        mDemux->setRecordHandle(mRecordhandle);
    }
}

Dvr::~Dvr() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
}

Return<void> Dvr::getQueueDesc(getQueueDesc_cb _hidl_cb) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    _hidl_cb(Result::SUCCESS, *mDvrMQ->getDesc());
    return Void();
}

Return<Result> Dvr::configure(const DvrSettings& settings) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    mDvrSettings = settings;
    mDvrConfigured = true;

    return Result::SUCCESS;
}

Return<Result> Dvr::attachFilter(const sp<V1_0::IFilter>& filter) {
     ALOGD("%s/%d", __FUNCTION__, __LINE__);

    uint64_t filterId;
    Result status;

    sp<V1_1::IFilter> filter_v1_1 = V1_1::IFilter::castFrom(filter);
    if (filter_v1_1 != NULL) {
        filter_v1_1->getId64Bit([&](Result result, uint64_t id) {
            filterId = id;
            status = result;
        });
    } else {
        filter->getId([&](Result result, uint32_t id) {
            filterId = id;
            status = result;
        });
    }

    if (status != Result::SUCCESS) {
        return status;
    }

    if (!mDemux->attachRecordFilter(filterId)) {
        return Result::INVALID_ARGUMENT;
    }

    return Result::SUCCESS;
}

Return<Result> Dvr::detachFilter(const sp<V1_0::IFilter>& filter) {
     ALOGD("%s/%d", __FUNCTION__, __LINE__);

    uint64_t filterId;
    Result status;

    sp<V1_1::IFilter> filter_v1_1 = V1_1::IFilter::castFrom(filter);
    if (filter_v1_1 != NULL) {
        filter_v1_1->getId64Bit([&](Result result, uint64_t id) {
            filterId = id;
            status = result;
        });
    } else {
        filter->getId([&](Result result, uint32_t id) {
            filterId = id;
            status = result;
        });
    }

    if (status != Result::SUCCESS) {
        return status;
    }

    if (!mDemux->detachRecordFilter(filterId)) {
        return Result::INVALID_ARGUMENT;
    }

    return Result::SUCCESS;
}

Return<Result> Dvr::start() {
    ALOGD("%s/%d[demuxId = %d] mType = %hhu", __FUNCTION__, __LINE__, mDemux->getDemuxId(), mType);

    if (!mCallback) {
        return Result::NOT_INITIALIZED;
    }

    if (!mDvrConfigured) {
        return Result::INVALID_STATE;
    }
    if (mType == DvrType::PLAYBACK) {
        mDvrThreadRunning = true;
        DVR_Result_t ret = dvr_playback_start(mPlaybackhandle);
        if (ret != DVR_SUCCESS) {
            ALOGD("start dvr playback failed!\n");
        }
    } else if (mType == DvrType::RECORD) {
        memset(&mReceiveParams, 0, sizeof(DVR_RecordReceiveParams_t));
        mReceiveParams.buf = (uint8_t *)malloc(DVR_MAX_PUSI_LEN);
        mReceiveParams.len = DVR_MAX_PUSI_LEN;
        mReceiveParams.mode = DVR_DIRECT_RECORD_MODE;
        mDvrRecordThreadRunning = true;
        mDvrRecordThread = std::thread(&Dvr::DvrRecordThreadLoop, this);
        DVR_Result_t ret = dvr_record_start(mRecordhandle);
        if (ret != DVR_SUCCESS) {
            ALOGD("start dvr record failed!\n");
        }
        mRecordStatus = RecordStatus::DATA_READY;
        mDemux->setIsRecording(mType == DvrType::RECORD);
    }
    // TODO start another thread to send filter status callback to the framework

    return Result::SUCCESS;
}

Return<Result> Dvr::stop() {
    ALOGD("%s/%d[demuxId = %d] mType = %hhu", __FUNCTION__, __LINE__, mDemux->getDemuxId(), mType);
    mDvrThreadRunning = false;
    //std::lock_guard<std::mutex> lock(mDvrThreadLock);
    mIsRecordStarted = false;
    mDemux->setIsRecording(false);

    if (mType == DvrType::PLAYBACK) {
        DVR_Result_t ret = dvr_playback_stop(mPlaybackhandle);
        if (ret != DVR_SUCCESS) {
            ALOGD("stop dvr playback failed!\n");
        }
    } else if (mType == DvrType::RECORD) {
        DVR_Result_t ret = dvr_record_stop(mRecordhandle);
        if (ret != DVR_SUCCESS) {
            ALOGD("stop dvr record failed!\n");
        }
        mDvrRecordThreadRunning = false;
        if (mDvrRecordThread.joinable()) {
            mDvrRecordThread.join();
        }
        if (mReceiveParams.buf) {
            free(mReceiveParams.buf);
            mReceiveParams.buf = NULL;
        }
        if (recordFile != NULL) {
            fflush(recordFile);
            fclose(recordFile);
            recordFile = NULL;
        }
    }

    return Result::SUCCESS;
}

Return<Result> Dvr::flush() {
    int flushSize = 0;
    int left      = 0;
    char *buffer  = NULL;
    if (mType == DvrType::PLAYBACK) {
        flushSize = mDvrSettings.playback().packetSize * 100;//188 bytes
    } else if (mType == DvrType::RECORD) {
        flushSize = mDvrSettings.record().packetSize * 100;//188 bytes
    }

    mFlushing = true;
    std::lock_guard<std::mutex> lock(mReadLock);
    if (mDvrMQ.get() != NULL) {
      left = mDvrMQ->availableToRead();
      ALOGD("%s/%d mType=%hhu size=%d", __FUNCTION__, __LINE__, mType, left);
      if (left > 0) {
        buffer = new char[flushSize];
        for (int i = 0;  left > 0; i++) {
            if (left > flushSize) {
                mDvrMQ->read((unsigned char *)&buffer[0], flushSize);
                left -= flushSize;
            } else {
                mDvrMQ->read((unsigned char *)&buffer[0], left);
                left = 0;
            }
            ALOGD("%s/%d flush left=%d", __FUNCTION__, __LINE__, left);
        }
        delete[] buffer;
      }
    }
    mRecordStatus = RecordStatus::DATA_READY;
    mFlushing = false;
    return Result::SUCCESS;
}

Return<Result> Dvr::close() {
    ALOGD("%s/%d[demuxId = %d]  mType = %hhu", __FUNCTION__, __LINE__, mDemux->getDemuxId(), mType);
    if (mType == DvrType::PLAYBACK) {
        mStartDvrThread = false;
        mDvrEventFlag->wake(static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY));
        pthread_join(mDvrThread, NULL);
    }
    if (mDvrMQ.get() != NULL)
       mDvrMQ.reset();

    if (mDvrEventFlag != nullptr) {
        EventFlag::deleteEventFlag(&mDvrEventFlag);
        mDvrEventFlag = nullptr;
    }

    if (mType == DvrType::PLAYBACK) {
        DVR_Result_t ret = dvr_playback_close(mPlaybackhandle);
        if (ret != DVR_SUCCESS) {
            ALOGD("close dvr playback failed!\n");
        }
        mDemux->setPlaybackHandle(NULL);
    } else if (mType == DvrType::RECORD) {
        DVR_Result_t ret = dvr_record_close(mRecordhandle);
        if (ret != DVR_SUCCESS) {
            ALOGD("close dvr record failed!\n");
        }
        mDemux->setRecordHandle(NULL);
    }
    return Result::SUCCESS;
}

bool Dvr::createDvrMQ() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    // Create a synchronized FMQ that supports blocking read/write
    unique_ptr<DvrMQ> tmpDvrMQ = unique_ptr<DvrMQ>(new (nothrow) DvrMQ(mBufferSize, true));
    if (!tmpDvrMQ->isValid()) {
        ALOGW("[Dvr] Failed to create FMQ of DVR");
        return false;
    }

    mDvrMQ = move(tmpDvrMQ);

    if (EventFlag::createEventFlag(mDvrMQ->getEventFlagWord(), &mDvrEventFlag) != OK) {
        return false;
    }

    return true;
}

void Dvr::DvrRecordThreadLoop() {
    prctl(PR_SET_NAME, "DvrRecordThread");
    while (mDvrRecordThreadRunning) {
        ssize_t len = 0;
        if (mDemux != NULL && mDemux->getRecordVideoPid() != -1) {
            videoPid = mDemux->getRecordVideoPid();
            mReceiveParams.mode = DVR_PUSI_RECORD_MODE;
        } else {
            if (mDemux != NULL && mDemux->getRecordAudioPid() != -1) {
                audioPid = mDemux->getRecordAudioPid();
                mReceiveParams.mode = DVR_PUSI_RECORD_MODE;
            } else {
                mReceiveParams.mode = DVR_DIRECT_RECORD_MODE;
            }
        }
        len = dvr_record_read(mRecordhandle, &mReceiveParams);
        //ALOGD("[Dvr] len = %d", len);
        if (len <= 0) {
            usleep(100*1000);
            ALOGE("dvr no data\n");
            continue;
        }

        vector<uint8_t> data;
        data.resize(len);
        memcpy(data.data(), mReceiveParams.buf, len * sizeof(uint8_t));

        if (mReceiveParams.mode == DVR_DIRECT_RECORD_MODE) {
            //ALOGD("[Dvr][demuxId = %d] read dvr data size = %d flag = %d, pts = %llu", mDemux->getDemuxId(), mReceiveParams.len, mReceiveParams.flags, mReceiveParams.pts);
            mDemux->sendFrontendInputToRecord(data);
            mDemux->startRecordFilterDispatcher();
        } else if (mReceiveParams.mode == DVR_PUSI_RECORD_MODE) {
            mPusiIndex  = mReceiveParams.flags & DVR_INDEX_PUSI;
            mIframeIndex = mReceiveParams.flags & DVR_INDEX_IFRAME;
            mPts         = mReceiveParams.pts;
            if (videoPid != -1) {
                ALOGD("[Dvr][demuxId = %d] offset = %" PRIu64 ", read pid = %d, dvr data size = %zd, flag = %d, pts = %" PRIu64 "",  mDemux->getDemuxId(), mOffset, videoPid, len, mReceiveParams.flags, mReceiveParams.pts);
                if (mDemux->getDemuxId() == 3) {
                    if (recordFile == NULL) {
                        recordFile = fopen("/data/local/tmp/recordData.ts", "wb+");
                    }
                    if (recordFile != NULL ) {
                        fwrite(data.data(), 1, data.size(), recordFile);
                    }
                }
                mDemux->sendFrontendInputToRecord(data, videoPid, mOffset, mPts, mIframeIndex, mPusiIndex);
                mDemux->startRecordFilterDispatcher();
            } else {
                if (audioPid != -1) {
                    ALOGD("[Dvr][demuxId = %d] read pid = %d dvr data size = %zd flag = %d, pts = %" PRIu64 "",  mDemux->getDemuxId(), audioPid, len, mReceiveParams.flags, mReceiveParams.pts);
                    mDemux->sendFrontendInputToRecord(data, audioPid, mOffset, mPts, mIframeIndex, mPusiIndex);
                    mDemux->startRecordFilterDispatcher();
                }
            }
            mOffset += len;
        }
    }
    ALOGD("[Dvr] Dvr Record thread ended.");
}

EventFlag* Dvr::getDvrEventFlag() {
    return mDvrEventFlag;
}

void* Dvr::__threadLoopPlayback(void* user) {
    Dvr* const self = static_cast<Dvr*>(user);
    self->playbackThreadLoop();
    return 0;
}

void Dvr::playbackThreadLoop() {
    ALOGI("[Dvr] playback threadLoop start.");
    std::lock_guard<std::mutex> lock(mDvrThreadLock);
    //mDvrThreadRunning = true;
    prctl(PR_SET_NAME, "playbackThread");

    while (mStartDvrThread) {
        if (mDvrThreadRunning) {
            uint32_t efState = 0;
            status_t status =
                    mDvrEventFlag->wait(static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY),
                                        &efState, WAIT_TIMEOUT, true /* retry on spurious wake */);
            if (status != OK) {
                ALOGD("[Dvr] wait for data ready on the playback FMQ");
                continue;
            }

            // If the both dvr playback and dvr record are created, the playback will be treated as
            // the source of the record. isVirtualFrontend set to true would direct the dvr playback
            // input to the demux record filters or live broadcast filters.
            bool isRecording = mDemux->isRecording();
            bool isVirtualFrontend = true;

            if (mDvrSettings.playback().dataFormat == DataFormat::ES) {
                if (!processEsDataOnPlayback(isVirtualFrontend, isRecording)) {
                    ALOGE("[Dvr] playback es data failed to be filtered. Ending thread");
                    break;
                }
                maySendPlaybackStatusCallback();
                continue;
            }

            // Our current implementation filter the data and write it into the filter FMQ immediately
            // after the DATA_READY from the VTS/framework
            // This is for the non-ES data source, real playback use case handling.
            if (!readPlaybackFMQ(isVirtualFrontend, isRecording) ||
                !startFilterDispatcher(isVirtualFrontend, isRecording)) {
                ALOGE("[Dvr] playback data failed to be filtered. Ending thread");
                continue;
            }

            maySendPlaybackStatusCallback();
        }
        usleep(10 * 1000);
    }

    mDvrThreadRunning = false;
    ALOGD("[Dvr] playback thread ended.");
}

void Dvr::maySendPlaybackStatusCallback() {
    lock_guard<mutex> lock(mPlaybackStatusLock);
    int availableToRead = mDvrMQ->availableToRead();
    int availableToWrite = mDvrMQ->availableToWrite();

    PlaybackStatus newStatus = checkPlaybackStatusChange(availableToWrite, availableToRead,
                                                         mDvrSettings.playback().highThreshold,
                                                         mDvrSettings.playback().lowThreshold);
    if (mPlaybackStatus != newStatus) {
        ALOGD("[Dvr] Playback status %d->%d [ar:%d aw:%d dvrmq size:%zd highThreshold:%d lowThreshold:%d]",
        mPlaybackStatus, newStatus, availableToRead/188, availableToWrite/188, mDvrMQ->getQuantumCount(),
        mDvrSettings.playback().highThreshold/188, mDvrSettings.playback().lowThreshold/188);
        mCallback->onPlaybackStatus(newStatus);
        mPlaybackStatus = newStatus;
    }
}

PlaybackStatus Dvr::checkPlaybackStatusChange(uint32_t availableToWrite, uint32_t availableToRead,
                                              uint32_t highThreshold, uint32_t lowThreshold) {
    if (availableToWrite == 0) {
        return PlaybackStatus::SPACE_FULL;
    } else if (availableToRead > highThreshold) {
        return PlaybackStatus::SPACE_ALMOST_FULL;
    } else if (availableToRead < lowThreshold) {
        return PlaybackStatus::SPACE_ALMOST_EMPTY;
    } else if (availableToRead == 0) {
        return PlaybackStatus::SPACE_EMPTY;
    }
    return mPlaybackStatus;
}

bool Dvr::readPlaybackFMQ(bool isVirtualFrontend, bool isRecording) {
    if (mDvrMQ.get() == NULL) {
        ALOGD("DvrMQ is null");
        return false;
    }

    std::lock_guard<std::mutex> lock(mReadLock);
    // Read playback data from the input FMQ
    int size = mDvrMQ->availableToRead();
    int playbackPacketSize = mDvrSettings.playback().packetSize * 100;//188 bytes
    size_t tmpSize = 0;
    vector<uint8_t> dataOutputBuffer;
    dataOutputBuffer.resize(playbackPacketSize);
    // Dispatch the packet to the PID matching filter output buffer
    for (int i = 0;  !mFlushing && mDvrThreadRunning && i < size / playbackPacketSize; i++) {
    //if (!mFlushing && mDvrThreadRunning && size > 0) {
        if (!mDvrMQ->read(dataOutputBuffer.data(), playbackPacketSize)) {
            ALOGE("%s read ts from mDvrMQ failed!", __FUNCTION__);
            return false;
        }
        if (1)
            ALOGD("%s [dmxId = %d]isVirtualFrontend:%d isRecording:%d", __FUNCTION__, mDemux->getDemuxId(), isVirtualFrontend, isRecording);
        if (isVirtualFrontend) {
            mDemux->startBroadcastTsFilter(dataOutputBuffer);
        } else {
            startTpidFilter(dataOutputBuffer);
        }
        tmpSize += playbackPacketSize;
    }

    size_t leftSize = size - tmpSize;
    if (leftSize > 0 && !mFlushing && mDvrThreadRunning) {
        ALOGD("[Dvr] inject data left size = %zu", leftSize);
        dataOutputBuffer.resize(leftSize);
        if (!mDvrMQ->read(dataOutputBuffer.data(), leftSize)) {
            ALOGD("%s/%d read data fail", __FUNCTION__, __LINE__);
            return false;
        }
        mDemux->startBroadcastTsFilter(dataOutputBuffer);
    }
    return true;
}

bool Dvr::processEsDataOnPlayback(bool isVirtualFrontend, bool isRecording) {
    // Read ES from the DVR FMQ
    // Note that currently we only provides ES with metaData in a specific format to be parsed.
    // The ES size should be smaller than the Playback FMQ size to avoid reading truncated data.
    int size = mDvrMQ->availableToRead();
    vector<uint8_t> dataOutputBuffer;
    dataOutputBuffer.resize(size);
    if (!mDvrMQ->read(dataOutputBuffer.data(), size)) {
        return false;
    }

    int metaDataSize = size;
    int totalFrames = 0;
    int videoEsDataSize = 0;
    int audioEsDataSize = 0;
    int audioPid = 0;
    int videoPid = 0;

    vector<MediaEsMetaData> esMeta;
    int videoReadPointer = 0;
    int audioReadPointer = 0;
    int frameCount = 0;
    // Get meta data from the es
    for (int i = 0; i < metaDataSize; i++) {
        switch (dataOutputBuffer[i]) {
            case 'm':
                metaDataSize = 0;
                getMetaDataValue(i, dataOutputBuffer.data(), metaDataSize);
                videoReadPointer = metaDataSize;
                continue;
            case 'l':
                getMetaDataValue(i, dataOutputBuffer.data(), totalFrames);
                esMeta.resize(totalFrames);
                continue;
            case 'V':
                getMetaDataValue(i, dataOutputBuffer.data(), videoEsDataSize);
                audioReadPointer = metaDataSize + videoEsDataSize;
                continue;
            case 'A':
                getMetaDataValue(i, dataOutputBuffer.data(), audioEsDataSize);
                continue;
            case 'p':
                if (dataOutputBuffer[++i] == 'a') {
                    getMetaDataValue(i, dataOutputBuffer.data(), audioPid);
                } else if (dataOutputBuffer[i] == 'v') {
                    getMetaDataValue(i, dataOutputBuffer.data(), videoPid);
                }
                continue;
            case 'v':
            case 'a':
                if (dataOutputBuffer[i + 1] != ',') {
                    ALOGE("[Dvr] Invalid format meta data.");
                    return false;
                }
                esMeta[frameCount] = {
                        .isAudio = dataOutputBuffer[i] == 'a' ? true : false,
                };
                i += 5;  // Move to Len
                getMetaDataValue(i, dataOutputBuffer.data(), esMeta[frameCount].len);
                if (esMeta[frameCount].isAudio) {
                    esMeta[frameCount].startIndex = audioReadPointer;
                    audioReadPointer += esMeta[frameCount].len;
                } else {
                    esMeta[frameCount].startIndex = videoReadPointer;
                    videoReadPointer += esMeta[frameCount].len;
                }
                i += 4;  // move to PTS
                getMetaDataValue(i, dataOutputBuffer.data(), esMeta[frameCount].pts);
                frameCount++;
                continue;
            default:
                continue;
        }
    }

    if (frameCount != totalFrames) {
        ALOGE("[Dvr] Invalid meta data, frameCount=%d, totalFrames reported=%d", frameCount,
              totalFrames);
        return false;
    }

    if (metaDataSize + audioEsDataSize + videoEsDataSize != size) {
        ALOGE("[Dvr] Invalid meta data, metaSize=%d, videoSize=%d, audioSize=%d, totolSize=%d",
              metaDataSize, videoEsDataSize, audioEsDataSize, size);
        return false;
    }

    // Read es raw data from the FMQ per meta data built previously
    vector<uint8_t> frameData;
    map<uint64_t, sp<IFilter>>::iterator it;
    int pid = 0;
    for (int i = 0; i < totalFrames; i++) {
        frameData.resize(esMeta[i].len);
        pid = esMeta[i].isAudio ? audioPid : videoPid;
        memcpy(frameData.data(), dataOutputBuffer.data() + esMeta[i].startIndex, esMeta[i].len);
        // Send to the media filters or record filters
        if (!isRecording) {
            for (it = mFilters.begin(); it != mFilters.end(); it++) {
                if (pid == mDemux->getFilterTpid(it->first)) {
                    mDemux->updateMediaFilterOutput(it->first, frameData,
                                                    static_cast<uint64_t>(esMeta[i].pts));
                }
            }
        } else {
            mDemux->sendFrontendInputToRecord(frameData, pid, static_cast<uint64_t>(esMeta[i].pts));
        }
        startFilterDispatcher(isVirtualFrontend, isRecording);
        frameData.clear();
    }

    return true;
}

void Dvr::getMetaDataValue(int& index, uint8_t* dataOutputBuffer, int& value) {
    index += 2;  // Move the pointer across the ":" to the value
    while (dataOutputBuffer[index] != ',' && dataOutputBuffer[index] != '\n') {
        value = ((dataOutputBuffer[index++] - 48) + value * 10);
    }
}

void Dvr::startTpidFilter(vector<uint8_t> data) {
    map<uint64_t, sp<IFilter>>::iterator it;
    for (it = mFilters.begin(); it != mFilters.end(); it++) {
        uint16_t pid = ((data[1] & 0x1f) << 8) | ((data[2] & 0xff));
        if (DEBUG_DVR) {
            ALOGW("[Dvr] start ts filter pid: %d", pid);
        }
        if (pid == mDemux->getFilterTpid(it->first)) {
            mDemux->updateFilterOutput(it->first, data);
        }
    }
}

bool Dvr::startFilterDispatcher(bool isVirtualFrontend, bool isRecording) {
    if (DEBUG_DVR)
        ALOGD("%s/%d isVirtualFrontend:%d isRecording:%d", __FUNCTION__, __LINE__, isVirtualFrontend, isRecording);

    if (isVirtualFrontend) {
        return mDemux->startBroadcastFilterDispatcher();
    }

    map<uint64_t, sp<IFilter>>::iterator it;
    // Handle the output data per filter type
    for (it = mFilters.begin(); it != mFilters.end(); it++) {
        if (mDemux->startFilterHandler(it->first) != Result::SUCCESS) {
            return false;
        }
    }

    return true;
}

bool Dvr::writeRecordFMQ(const vector<uint8_t>& data) {
    lock_guard<mutex> lock(mWriteLock);
    if (mRecordStatus == RecordStatus::OVERFLOW) {
        ALOGW("[Dvr] stops writing and wait for the client side flushing.");
        return true;
    }
    if (mDvrMQ.get() != NULL &&mDvrMQ->write(data.data(), data.size())) {
        mDvrEventFlag->wake(static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY));
        maySendRecordStatusCallback();
        return true;
    }

    maySendRecordStatusCallback();
    return false;
}

void Dvr::maySendRecordStatusCallback() {
    lock_guard<mutex> lock(mRecordStatusLock);
    if (mDvrMQ.get() != NULL) {
        int availableToRead = mDvrMQ->availableToRead();
        int availableToWrite = mDvrMQ->availableToWrite();

        RecordStatus newStatus = checkRecordStatusChange(availableToWrite, availableToRead,
                                                         mDvrSettings.record().highThreshold,
                                                         mDvrSettings.record().lowThreshold);
        if (mRecordStatus != newStatus) {
            if (mCallback != nullptr) {
                mCallback->onRecordStatus(newStatus);
                mRecordStatus = newStatus;
            }
        }
    }
}

RecordStatus Dvr::checkRecordStatusChange(uint32_t availableToWrite, uint32_t availableToRead,
                                          uint32_t highThreshold, uint32_t lowThreshold) {
    if (availableToWrite == 0) {
        return DemuxFilterStatus::OVERFLOW;
    } else if (availableToRead > highThreshold) {
        return DemuxFilterStatus::HIGH_WATER;
    } else if (availableToRead < lowThreshold) {
        return DemuxFilterStatus::LOW_WATER;
    }
    return mRecordStatus;
}

bool Dvr::addPlaybackFilter(uint64_t filterId, sp<IFilter> filter) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    mFilters[filterId] = filter;
    return true;
}

bool Dvr::removePlaybackFilter(uint64_t filterId) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    mFilters.erase(filterId);
    return true;
}

DVB_DemuxSource_t Dvr::getDemuxSourceByTsInput(int tsInput) {
    ALOGD("%s/%d tsInput = %d", __FUNCTION__, __LINE__, tsInput);
    switch (tsInput) {
        case FRONTEND_TS0:
            return DVB_DEMUX_SOURCE_TS0;
        case FRONTEND_TS1:
            return DVB_DEMUX_SOURCE_TS1;
        case FRONTEND_TS2:
            return DVB_DEMUX_SOURCE_TS2;
        case FRONTEND_TS3:
            return DVB_DEMUX_SOURCE_TS3;
        case FRONTEND_TS4:
            return DVB_DEMUX_SOURCE_TS4;
        case FRONTEND_TS5:
            return DVB_DEMUX_SOURCE_TS5;
        default:
            ALOGD("%s/%d tsInput = %d", __FUNCTION__, __LINE__, tsInput);
    }
    return DVB_DEMUX_SOURCE_TS0;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
