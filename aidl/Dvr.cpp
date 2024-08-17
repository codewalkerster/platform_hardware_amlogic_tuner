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

#define LOG_NDEBUG 0
#define LOG_TAG "tunerhal2.0-Dvr"

#include <aidl/android/hardware/tv/tuner/DemuxQueueNotifyBits.h>
#include <aidl/android/hardware/tv/tuner/Result.h>

#include <utils/Log.h>
#include <sys/prctl.h>
#include <cutils/properties.h>
#include "Dvr.h"

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

#define WAIT_TIMEOUT 3000000000
#define SUPPORT_AES128_DATA_INJECT "vendor.tunerhal.AES128.data.inject"
using namespace std;

Dvr::Dvr(DvrType type, uint32_t bufferSize, const std::shared_ptr<IDvrCallback>& cb,
         std::shared_ptr<Demux> demux, std::shared_ptr<Tuner> in_tuner) {
    mType = type;
    mBufferSize = bufferSize;
    mCallback = cb;
    mDemux = demux;
    mTuner = in_tuner;

    if (mType == DvrType::PLAYBACK) {
        mSupportAES128Data = property_get_bool(SUPPORT_AES128_DATA_INJECT, false);
        ALOGD("%s/%d dvr_playback_open dmxid = %d, mSupportAES128Data = %d", __FUNCTION__, __LINE__, mDemux->getDemuxId(), mSupportAES128Data);
        if (mSupportAES128Data) {
            mNeedCheckFirstPacket = true;
            mIsSecureBuffer = false;
        }
        mPlaybackParams.dmx_dev_id = mDemux->getDemuxId();
        DVR_Result_t ret = dvr_playback_open(&mPlaybackhandle, &mPlaybackParams);
        if (ret != DVR_SUCCESS) {
            ALOGD("open dvr playback failed!\n");
        }
        mDemux->setPlaybackHandle(mPlaybackhandle);
    } else if (mType == DvrType::RECORD) {
        ALOGD("%s/%d dvr_record_open dmxid = %d", __FUNCTION__, __LINE__, mDemux->getDemuxId());
        memset(&mOpenParams, 0, sizeof(DVR_RecordOpenParams_t));
        bool bPlayback = mDemux->checkDemuxPlayback();
        ALOGD("%s/%d  dmxid = %d, bPlayback = %d", __FUNCTION__, __LINE__, mDemux->getDemuxId(), bPlayback);
        if (bPlayback) {
            mOpenParams.src = static_cast<DVB_DemuxSource_t>(DVB_DEMUX_SOURCE_DMA0 + mDemux->getDemuxId());
        } else {
            if (mDemux->checkCiCamInsert()) {
                if (mDemux->getDemuxSource() < 0x0F) /*0~15 for PCMCIA type,16~31 USB type */
                    mOpenParams.src = DVB_DEMUX_SOURCE_TS1;
                else
                    mOpenParams.src = DVB_DEMUX_SOURCE_DMA4;
            }
            else {
                //do recording, no Feclient in Jtuner, get ts input from tuner;
                mOpenParams.src = getDemuxSourceByTsInput(mTuner->getTsInput());
            }
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
}

::ndk::ScopedAStatus Dvr::getQueueDesc(MQDescriptor<int8_t, SynchronizedReadWrite>* out_queue) {
    ALOGD("%s", __FUNCTION__);

    *out_queue = mDvrMQ->dupeDesc();

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Dvr::configure(const DvrSettings& in_settings) {
    ALOGD("%s", __FUNCTION__);

    mDvrSettings = in_settings;
    mDvrConfigured = true;

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Dvr::attachFilter(const std::shared_ptr<IFilter>& in_filter) {
    ALOGV("%s", __FUNCTION__);

    int64_t filterId;
    ::ndk::ScopedAStatus status = in_filter->getId64Bit(&filterId);
    if (!status.isOk()) {
        return status;
    }

    if (!mDemux->attachRecordFilter(filterId)) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Dvr::detachFilter(const std::shared_ptr<IFilter>& in_filter) {
    ALOGV("%s", __FUNCTION__);

    int64_t filterId;
    ::ndk::ScopedAStatus status = in_filter->getId64Bit(&filterId);
    if (!status.isOk()) {
        return status;
    }

    if (!mDemux->detachRecordFilter(filterId)) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Dvr::start() {
    ALOGD("%s/%d[demuxId = %d] mType = %hhu", __FUNCTION__, __LINE__, mDemux->getDemuxId(), mType);
    if (mDvrThreadRunning) {
        return ::ndk::ScopedAStatus::ok();
    }

    if (!mCallback) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::NOT_INITIALIZED));
    }

    if (!mDvrConfigured) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_STATE));
    }

    if (mType == DvrType::PLAYBACK) {
        mDvrThreadRunning = true;
        mDvrThread = std::thread(&Dvr::playbackThreadLoop, this);
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

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Dvr::stop() {
    ALOGD("%s/%d[demuxId = %d] mType = %hhu", __FUNCTION__, __LINE__, mDemux->getDemuxId(), mType);
    // thread should always be joinable if it is running,
    // so it should be safe to assume recording stopped.
    mDemux->setIsRecording(false);
    if (mType == DvrType::PLAYBACK) {
        DVR_Result_t ret = dvr_playback_stop(mPlaybackhandle);
        if (ret != DVR_SUCCESS) {
            ALOGD("stop dvr playback failed!\n");
        }
        mDvrThreadRunning = false;
        if (mDvrThread.joinable()) {
            mDvrEventFlag->wake(static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY));
            mDvrThread.join();
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
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Dvr::flush() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    int flushSize = 0;
    int left      = 0;
    char *buffer  = NULL;
    if (mType == DvrType::PLAYBACK) {
        flushSize = mDvrSettings.get<DvrSettings::Tag::playback>().packetSize * 100;//188 bytes
    } else if (mType == DvrType::RECORD) {
        flushSize = mDvrSettings.get<DvrSettings::Tag::record>().packetSize * 100;//188 bytes
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
                mDvrMQ->read((signed char *)&buffer[0], flushSize);
                left -= flushSize;
            } else {
                mDvrMQ->read((signed char *)&buffer[0], left);
                left = 0;
            }
            ALOGD("%s/%d flush left=%d", __FUNCTION__, __LINE__, left);
        }
        delete[] buffer;
      }
    }
    mRecordStatus = RecordStatus::DATA_READY;
    mNotifyFlushToDemux = true;
    mFlushing = false;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Dvr::close() {
    ALOGD("%s/%d  mType = %hhu", __FUNCTION__, __LINE__, mType);
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
        //avoid tunerhal crash, because app didn't call DvrPlayback stop interface to exit playback thread;
        if (mDvrThreadRunning) {
            mDvrThreadRunning = false;
            if (mDvrThread.joinable()) {
                mDvrEventFlag->wake(static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY));
                mDvrThread.join();
            }
        }
        mDemux->setPlaybackHandle(NULL);
    } else if (mType == DvrType::RECORD) {
        if (mOpenParams.dmx_dev_id[1] != 0 && mOpenParams.dmx_dev_id[2] != 0) {
            mTuner->removeDemuxResource(mOpenParams.dmx_dev_id[1]);
            mTuner->removeDemuxResource(mOpenParams.dmx_dev_id[2]);
            mOpenParams.dmx_dev_id[1] = 0;
            mOpenParams.dmx_dev_id[2] = 0;
        }
        DVR_Result_t ret = dvr_record_close(mRecordhandle);
        if (ret != DVR_SUCCESS) {
            ALOGD("close dvr record failed!\n");
        }
        //avoid tunerhal crash, because app didn't call DvrRecord stop interface to exit record thread;
        if (mDvrRecordThreadRunning) {
            mDvrRecordThreadRunning = false;
            if (mDvrRecordThread.joinable()) {
                mDvrRecordThread.join();
            }

            if (mReceiveParams.buf) {
                free(mReceiveParams.buf);
                mReceiveParams.buf = NULL;
            }
        }
        mDemux->setRecordHandle(NULL);
    }

    return ::ndk::ScopedAStatus::ok();
}

#if PLATFORM_SDK_VERSION > 33
::ndk::ScopedAStatus Dvr::setStatusCheckIntervalHint(int64_t /* in_milliseconds */) {
    ALOGV("%s", __FUNCTION__);

    // There is no active polling in this default implementation,
    // so directly return ok here.
    return ::ndk::ScopedAStatus::ok();
}
#endif

bool Dvr::createDvrMQ() {
    ALOGD("%s", __FUNCTION__);

    // Create a synchronized FMQ that supports blocking read/write
    unique_ptr<DvrMQ> tmpDvrMQ = unique_ptr<DvrMQ>(new (nothrow) DvrMQ(mBufferSize, true));
    if (!tmpDvrMQ->isValid()) {
        ALOGW("[Dvr] Failed to create FMQ of DVR");
        return false;
    }

    mDvrMQ = move(tmpDvrMQ);

    if (EventFlag::createEventFlag(mDvrMQ->getEventFlagWord(), &mDvrEventFlag) != ::android::OK) {
        return false;
    }

    return true;
}

void Dvr::initDvrRecordParams() {
    if (mDemux != NULL) {
        videoPid = mDemux->getRecordVideoPid();
        audioPid = mDemux->getRecordAudioPid();
        if (videoPid != -1 || audioPid != -1) {
            mReceiveParams.mode = DVR_PUSI_RECORD_MODE;
        } else {
            mReceiveParams.mode = DVR_DIRECT_RECORD_MODE;
        }
    }
}

void Dvr::DvrRecordThreadLoop() {
    prctl(PR_SET_NAME, "DvrRecordThread");
    while (mDvrRecordThreadRunning) {
        ssize_t len = 0;
        initDvrRecordParams();
        len = dvr_record_read(mRecordhandle, &mReceiveParams);
        //ALOGD("[Dvr] len = %d", len);
        if (len <= 0) {
            usleep(10*1000);
            //ALOGE("dvr no data\n");
            continue;
        }

        vector<int8_t> data;
        data.resize(len);
        memcpy(data.data(), mReceiveParams.buf, len * sizeof(uint8_t));

        if (mReceiveParams.mode == DVR_DIRECT_RECORD_MODE) {
            //ALOGD("[Dvr][demuxId = %d] read dvr data size = %d flag = %d, pts = %llu", mDemux->getDemuxId(), mReceiveParams.len, mReceiveParams.flags, mReceiveParams.pts);
            if (mDemux != NULL) {
                mDemux->sendFrontendInputToRecord(data);
                mDemux->startRecordFilterDispatcher();
            }
        } else if (mReceiveParams.mode == DVR_PUSI_RECORD_MODE) {
            mPusiIndex  = mReceiveParams.flags & DVR_INDEX_PUSI;
            mIframeIndex = mReceiveParams.flags & DVR_INDEX_IFRAME;
            mPts         = mReceiveParams.pts;
            if (videoPid != -1) {
                ALOGD("[Dvr][demuxId = %d] offset = %" PRId64 ", read video pid = %d, dvr data size = %zd, flag = %d, pts = %" PRId64 "", mDemux->getDemuxId(), mOffset, videoPid, len, mReceiveParams.flags, mReceiveParams.pts);
                if (mDemux->getDemuxId() == 3) {
                    if (recordFile == NULL) {
                        recordFile = fopen("/data/local/tmp/recordData.ts", "wb+");
                    }
                    if (recordFile != NULL ) {
                        fwrite(data.data(), 1, data.size(), recordFile);
                    }
                }
                if (mDemux != NULL) {
                    mDemux->sendFrontendInputToRecord(data, videoPid, mOffset, mPts, mIframeIndex, mPusiIndex);
                    mDemux->startRecordFilterDispatcher();
                }
            } else {
                if (audioPid != -1) {
                    ALOGD("[Dvr][demuxId = %d] read audio pid = %d dvr data size = %zd flag = %d, pts = %" PRId64 "",  mDemux->getDemuxId(), audioPid, len, mReceiveParams.flags, mReceiveParams.pts);
                    if (mDemux != NULL) {
                        mDemux->sendFrontendInputToRecord(data, audioPid, mOffset, mPts, mIframeIndex, mPusiIndex);
                        mDemux->startRecordFilterDispatcher();
                    }
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

binder_status_t Dvr::dump(int fd, const char** /* args */, uint32_t /* numArgs */) {
    dprintf(fd, "    Dvr:\n");
    dprintf(fd, "      mType: %hhd\n", mType);
    dprintf(fd, "      mDvrThreadRunning: %d\n", (bool)mDvrThreadRunning);
    return STATUS_OK;
}

void Dvr::playbackThreadLoop() {
    ALOGD("[Dvr] playback threadLoop start.");
    prctl(PR_SET_NAME, "playbackThread");

    while (mDvrThreadRunning) {
        uint32_t efState = 0;
        ::android::status_t status =
                mDvrEventFlag->wait(static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_READY),
                                    &efState, WAIT_TIMEOUT, true /* retry on spurious wake */);
        if (status != ::android::OK) {
            ALOGD("[Dvr] wait for data ready on the playback FMQ, demux id: %d", mDemux->getDemuxId());
            continue;
        }

        // If the both dvr playback and dvr record are created, the playback will be treated as
        // the source of the record. isVirtualFrontend set to true would direct the dvr playback
        // input to the demux record filters or live broadcast filters.
        bool isRecording = mDemux->isRecording();
        bool isVirtualFrontend = true;
        if (mDvrSettings.get<DvrSettings::Tag::playback>().dataFormat == DataFormat::ES) {
            if (!processEsDataOnPlayback(isVirtualFrontend, isRecording)) {
                ALOGE("[Dvr] playback es data failed to be filtered. Ending thread");
                break;
            }
            maySendPlaybackStatusCallback();
            continue;
        }

        if (mNotifyFlushToDemux) {
            if (isVirtualFrontend) {
                mDemux->notifyDvrFlushed();
            }

            mNotifyFlushToDemux = false;
        }

        // Our current implementation filter the data and write it into the filter FMQ immediately
        // after the DATA_READY from the VTS/framework
        // This is for the non-ES data source, real playback use case handling.
        if (!readPlaybackFMQ(isVirtualFrontend, isRecording) ||
            !startFilterDispatcher(isVirtualFrontend, isRecording)) {
            ALOGE("[Dvr] playback data failed to be filtered. Ending thread");
            break;
        }

        maySendPlaybackStatusCallback();
    }

    mDvrThreadRunning = false;
    ALOGD("[Dvr] playback thread ended.");
}

void Dvr::maySendPlaybackStatusCallback() {
    lock_guard<mutex> lock(mPlaybackStatusLock);
    int availableToRead = mDvrMQ->availableToRead();
    int availableToWrite = mDvrMQ->availableToWrite();

    PlaybackStatus newStatus =
            checkPlaybackStatusChange(availableToWrite, availableToRead,
                                      mDvrSettings.get<DvrSettings::Tag::playback>().highThreshold,
                                      mDvrSettings.get<DvrSettings::Tag::playback>().lowThreshold);
    if ((mPlaybackStatus != newStatus)
        || (mIsSecureBuffer
        && (newStatus == PlaybackStatus::SPACE_ALMOST_EMPTY || newStatus == PlaybackStatus::SPACE_EMPTY))) {
        mCallback->onPlaybackStatus(newStatus);
        mPlaybackStatus = newStatus;
    }
}

PlaybackStatus Dvr::checkPlaybackStatusChange(uint32_t availableToWrite, uint32_t availableToRead,
                                              int64_t highThreshold, int64_t lowThreshold) {
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

bool Dvr::checkIsSecureBuffer() {
    size_t size = mDvrMQ->availableToRead();
    ALOGD("%s fmq size: %u", __FUNCTION__, size);
    if (size == 0) {
        return true;
    }

    if (size > 188) {
        size = 188;
    }
    DvrMQ::MemTransaction tx;
    uint8_t *firstPacket = new uint8_t[size];
    memset(firstPacket, 0, size);
    if (mDvrMQ->beginRead(size, &tx)) {
        auto first = tx.getFirstRegion();
        auto data = first.getAddress();
        int64_t length = first.getLength();
        if (length < 10) {
            delete [] firstPacket;
            ALOGD("%s fmq first region too small, length: %" PRId64 "", __FUNCTION__, length);
            return false;
        }

        memcpy(firstPacket, (uint8_t *)data, size);

        if (firstPacket[0] == 0xFF && firstPacket[1] == 0xFF && firstPacket[2] == 0xFE
            && firstPacket[3] == 0xFE) {
            mIsSecureBuffer = true;
        } else {
            mIsSecureBuffer = false;
        }

        ALOGI("%s fmq header: [%x %x %x %x]", __FUNCTION__,
            firstPacket[0], firstPacket[1], firstPacket[2], firstPacket[3]);

        ALOGI("%s mIsSecureBuffer: %s", __FUNCTION__, mIsSecureBuffer ? "true" : "false");
        mDemux->setUseSecureBuffer(mIsSecureBuffer);

        delete [] firstPacket;
        mNeedCheckFirstPacket = false;

        if (mIsSecureBuffer) {
            if (!mDvrMQ->commitRead(size)) {
                ALOGD("%s fmq size: %u, commit read failed", __FUNCTION__, size);
                return false;
            } else {
                ALOGD("%s fmq read %u bytes", __FUNCTION__, size);
            }
        }

        return true;
    } else {
        delete [] firstPacket;
        return false;
    }
}

bool Dvr::injectSecureBuffer() {
    size_t size = mDvrMQ->availableToRead();
    ALOGD("%s isSecureBuffer fmq size: %u", __FUNCTION__, size);
    if (size == 0) {
        return true;
    }
    vector<int8_t> buffer;
    buffer.resize(size);
    DvrMQ::MemTransaction tx;
    if (!mDvrMQ->beginRead(size, &tx)) {
        ALOGE("%s can not read from fmq", __FUNCTION__);
        return false;
    }

    auto first = tx.getFirstRegion();
    auto data = first.getAddress();
    size_t length = first.getLength();
    size_t toRead = std::min(size, length);
    memcpy(buffer.data(), data, toRead);
    ALOGD("%s isSucureBuffer firstRegion length: %u, totalSize: %u", __FUNCTION__, length, size);

    if (toRead < size) {
        auto second = tx.getSecondRegion();
        auto secondData = second.getAddress();
        size_t secondLength = second.getLength();
        size_t secondRead = std::min(secondLength, size - toRead);
        memcpy(buffer.data() + toRead, secondData, secondRead);
        ALOGD("%s isSucureBuffer secondRegion length: %u, read: %u", __FUNCTION__, secondLength, secondRead);
    }

    if (!mDemux->broadcastSecureBuffer(buffer)) {
        ALOGE("%s failed to write secure buffer", __FUNCTION__);
        return false;
    }

    if (!mDvrMQ->commitRead(size)) {
        ALOGE("%s commit read secure buffer failed", __FUNCTION__);
        return false;
    }

    return true;
}

bool Dvr::readPlaybackFMQ(bool isVirtualFrontend, bool isRecording) {
    if (mDvrMQ.get() == NULL) {
        ALOGD("DvrMQ is null");
        return false;
    }

    // Read playback data from the input FMQ
    std::lock_guard<std::mutex> lock(mReadLock);
    if (mSupportAES128Data) {
        if (mNeedCheckFirstPacket && !mFlushing && mDvrThreadRunning) {
            bool success = checkIsSecureBuffer();
            if (!success) {
                return false;
            }
        }

        if (mIsSecureBuffer && !mFlushing && mDvrThreadRunning) {
            bool ret = injectSecureBuffer();
            return ret;
        }
    }

    size_t size = mDvrMQ->availableToRead();
    int64_t playbackPacketSize = mDvrSettings.get<DvrSettings::Tag::playback>().packetSize * 100; //188 bytes
    size_t tmpSize = 0;
    vector<int8_t> dataOutputBuffer;
    dataOutputBuffer.resize(playbackPacketSize);
    // Dispatch the packet to the PID matching filter output buffer
    for (int i = 0; !mFlushing && mDvrThreadRunning && i < size / playbackPacketSize; i++) {
        if (!mDvrMQ->read(dataOutputBuffer.data(), playbackPacketSize)) {
            return false;
        }

        ALOGD("%s isVirtualFrontend:%d isRecording:%d, demuxId = %d", __FUNCTION__, isVirtualFrontend, isRecording, mDemux->getDemuxId());
        if (isVirtualFrontend) {
            mDemux->startBroadcastTsFilter(dataOutputBuffer);
        } else {
            startTpidFilter(dataOutputBuffer);
        }
        tmpSize += playbackPacketSize;
    }
    size_t leftSize = size - tmpSize;
    if (leftSize > 0 && !mFlushing && mDvrThreadRunning) {
        ALOGD("[Dvr] inject data left size = %zd", leftSize);
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
    vector<int8_t> dataOutputBuffer;
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
        ALOGE("[Dvr] Invalid meta data, metaSize=%d, videoSize=%d, audioSize=%d, totalSize=%d",
              metaDataSize, videoEsDataSize, audioEsDataSize, size);
        return false;
    }

    // Read es raw data from the FMQ per meta data built previously
    vector<int8_t> frameData;
    map<int64_t, std::shared_ptr<IFilter>>::iterator it;
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
            //mDemux->sendFrontendInputToRecord(frameData, pid, static_cast<uint64_t>(esMeta[i].pts));
        }
        startFilterDispatcher(isVirtualFrontend, isRecording);
        frameData.clear();
    }

    return true;
}

void Dvr::getMetaDataValue(int& index, int8_t* dataOutputBuffer, int& value) {
    index += 2;  // Move the pointer across the ":" to the value
    while (dataOutputBuffer[index] != ',' && dataOutputBuffer[index] != '\n') {
        value = ((dataOutputBuffer[index++] - 48) + value * 10);
    }
}

void Dvr::startTpidFilter(vector<int8_t> data) {
    map<int64_t, std::shared_ptr<IFilter>>::iterator it;
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
    if (isVirtualFrontend) {
        return mDemux->startBroadcastFilterDispatcher();
    }

    map<int64_t, std::shared_ptr<IFilter>>::iterator it;
    // Handle the output data per filter type
    for (it = mFilters.begin(); it != mFilters.end(); it++) {
        if (!mDemux->startFilterHandler(it->first).isOk()) {
            return false;
        }
    }

    return true;
}

bool Dvr::writeRecordFMQ(const vector<int8_t>& data) {
    lock_guard<mutex> lock(mWriteLock);
    if (mRecordStatus == RecordStatus::OVERFLOW) {
        ALOGW("[Dvr] stops writing and wait for the client side flushing.");
        return true;
    }
    if (mDvrMQ.get() != NULL && mDvrMQ->write(data.data(), data.size())) {
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

        RecordStatus newStatus =
                checkRecordStatusChange(availableToWrite, availableToRead,
                                        mDvrSettings.get<DvrSettings::Tag::record>().highThreshold,
                                        mDvrSettings.get<DvrSettings::Tag::record>().lowThreshold);
        if (mRecordStatus != newStatus) {
            if (mCallback) {
                mCallback->onRecordStatus(newStatus);
                mRecordStatus = newStatus;
            }
        }
    }
}

RecordStatus Dvr::checkRecordStatusChange(uint32_t availableToWrite, uint32_t availableToRead,
                                          int64_t highThreshold, int64_t lowThreshold) {
    if (availableToWrite == 0) {
        return RecordStatus::OVERFLOW;
    } else if (availableToRead > highThreshold) {
        return RecordStatus::HIGH_WATER;
    } else if (availableToRead < lowThreshold) {
        return RecordStatus::LOW_WATER;
    }
    return mRecordStatus;
}

bool Dvr::addPlaybackFilter(int64_t filterId, std::shared_ptr<IFilter> filter) {
    ALOGD("%s", __FUNCTION__);
    mFilters[filterId] = filter;
    return true;
}

bool Dvr::removePlaybackFilter(int64_t filterId) {
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
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
