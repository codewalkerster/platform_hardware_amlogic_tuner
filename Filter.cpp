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

#define LOG_TAG "android.hardware.tv.tuner@1.0-Filter"

#include "Filter.h"
#include <utils/Log.h>
#include <cutils/properties.h>

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

#define WAIT_TIMEOUT 3000000000

#ifdef TUNERHAL_DBG
#define TF_FILTER_PROP_EVENTSIZE "vendor.tf.event.size"
#define TF_FILTER_PROP_VBUFSIZE "vendor.tf.vfilter.bufsize"
#endif

Filter::Filter() {}

Filter::Filter(DemuxFilterType type, uint32_t filterId, uint32_t bufferSize,
               const sp<IFilterCallback>& cb, sp<Demux> demux) {
    mType = type;
    mFilterId = filterId;
    mBufferSize = bufferSize;
    mCallback = cb;
    mDemux = demux;
#ifdef TUNERHAL_DBG
    mFilterEventSize = property_get_int32(TF_FILTER_PROP_EVENTSIZE, 10);
    mFilterEventSize *= 1024 * 1024;
    mVideoFilterSize = property_get_int32(TF_FILTER_PROP_VBUFSIZE, 10);
    mVideoFilterSize *= 1024 * 1024;
    ALOGD("%s mFilterEventSize:%dMB mVideoFilterSize:%dMB", __FUNCTION__, mFilterEventSize/1024/1024, mVideoFilterSize/1024/1024);
#endif

    switch (mType.mainType) {
        case DemuxFilterMainType::TS:
            ALOGD("%s DemuxFilterMainType::TS mCallback:%p", __FUNCTION__, mCallback.get());
            if (mType.subType.tsFilterType() == DemuxTsFilterType::AUDIO ||
                mType.subType.tsFilterType() == DemuxTsFilterType::VIDEO) {
                ALOGD("%s tsFilter subType is AUDIO or VIDEO", __FUNCTION__);
                mIsMediaFilter = true;
            }
            if (mType.subType.tsFilterType() == DemuxTsFilterType::PCR) {
                mIsPcrFilter = true;
            }
            if (mType.subType.tsFilterType() == DemuxTsFilterType::RECORD) {
                mIsRecordFilter = true;
            }
            break;
        case DemuxFilterMainType::MMTP:
            ALOGD("%s DemuxFilterMainType::MMTP:", __FUNCTION__);
            if (mType.subType.mmtpFilterType() == DemuxMmtpFilterType::AUDIO ||
                mType.subType.mmtpFilterType() == DemuxMmtpFilterType::VIDEO) {
                mIsMediaFilter = true;
            }
            if (mType.subType.mmtpFilterType() == DemuxMmtpFilterType::RECORD) {
                mIsRecordFilter = true;
            }
            break;
        case DemuxFilterMainType::IP:
            ALOGD("%s DemuxFilterMainType::IP:", __FUNCTION__);
            break;
        case DemuxFilterMainType::TLV:
            ALOGD("%s DemuxFilterMainType::TLV:", __FUNCTION__);
            break;
        case DemuxFilterMainType::ALP:
            ALOGD("%s DemuxFilterMainType::ALP:", __FUNCTION__);
            break;
        default:
            break;
    }
}

Filter::~Filter() {}

Return<void> Filter::getId(getId_cb _hidl_cb) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);
    _hidl_cb(Result::SUCCESS, mFilterId);
    return Void();
}

Return<Result> Filter::setDataSource(const sp<IFilter>& filter) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    mDataSource = filter;
    mIsDataSourceDemux = false;

    return Result::SUCCESS;
}

Return<void> Filter::getQueueDesc(getQueueDesc_cb _hidl_cb) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    mIsUsingFMQ = true;

    _hidl_cb(Result::SUCCESS, *mFilterMQ->getDesc());
    return Void();
}

Return<Result> Filter::configure(const DemuxFilterSettings& settings) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    mFilterSettings = settings;
    switch (mType.mainType) {
        case DemuxFilterMainType::TS:
            mTpid = settings.ts().tpid;
            ALOGD("%s mainType:TS mTpid:0x%x", __FUNCTION__, mTpid);
            switch (mType.subType.tsFilterType()) {
                case DemuxTsFilterType::SECTION: {
                    ALOGD("%s subType:SECTION", __FUNCTION__);
                    struct dmx_sct_filter_params param;
                    if (mDemux->getAmDmxDevice()
                        ->AM_DMX_SetBufferSize(mFilterId, mBufferSize) != 0 ) {
                        ALOGD("%s AM_DMX_SetBufferSize fail", __FUNCTION__);
                        return Result::UNAVAILABLE;
                    }
                    memset(&param, 0, sizeof(param));
                    param.pid = mTpid;
                    //param.filter.filter[0] = settings.ts().filterSettings
                     //                        .section().condition.tableInfo().tableId;
                    //param.filter.mask[0] = 0xff;
                    bool isRepeat = settings.ts().filterSettings.section().isRepeat;
                    ALOGD("%s isRepeat:%d", __FUNCTION__, isRepeat);
                    if (!isRepeat) {
                        param.flags |= DMX_ONESHOT;
                    }
                    bool isCheckCrc = settings.ts().filterSettings.section().isCheckCrc;
                    ALOGD("%s isCheckCrc:%d", __FUNCTION__, isCheckCrc);
                    if (isCheckCrc) {
                        param.flags |= DMX_CHECK_CRC;
                    }

                    bool isRaw     = settings.ts().filterSettings.section().isRaw;
                    ALOGD("%s isRaw:%d", __FUNCTION__, isRaw);
                    if (isRaw) {
                        param.flags |= DMX_OUTPUT_RAW_MODE;
                    }
                    int size = settings.ts().filterSettings.section().condition.sectionBits().filter.size();
                    ALOGD("%s size:%d", __FUNCTION__, size);
                    if (size <= 0 || size > 16) {
                        return Result::UNAVAILABLE;
                    }
                    param.filter.filter[0] = settings.ts().filterSettings.section().condition.sectionBits().filter[0];
                    ALOGD("%s param.filter.filter[0] = %d", __FUNCTION__, param.filter.filter[0]);
                    for (int i = 1; i < size - 2; i++) {
                        param.filter.filter[i] = settings.ts().filterSettings.section().condition.sectionBits().filter[i+2];
                    }

                    size = settings.ts().filterSettings.section().condition.sectionBits().mask.size();
                    param.filter.mask[0] = settings.ts().filterSettings.section().condition.sectionBits().mask[0];
                    for (int i = 1; i < size - 2; i++) {
                        param.filter.mask[i] = settings.ts().filterSettings.section().condition.sectionBits().mask[i+2];
                    }

                    size = settings.ts().filterSettings.section().condition.sectionBits().mode.size();
                    param.filter.mode[0] = settings.ts().filterSettings.section().condition.sectionBits().mode[0];
                    for (int i = 1; i < size - 2; i++) {
                        param.filter.mode[i] = settings.ts().filterSettings.section().condition.sectionBits().mode[i+2];
                    }
                    ALOGD("%s tableId:0x%x", __FUNCTION__, param.filter.filter[0]);
                    if (mDemux->getAmDmxDevice()
                        ->AM_DMX_SetSecFilter(mFilterId, &param) != 0 ) {
                        return Result::UNAVAILABLE;
                    }
                    break;
                }
                case DemuxTsFilterType::AUDIO: {
                    ALOGD("%s subType:AUDIO", __FUNCTION__);
                    if (settings.ts().filterSettings.av().isPassthrough) {
                        //aparam.flags |= DMX_OUTPUT_RAW_MODE;
                        // for passthrough mode, will set pes filter in media hal
                        uint32_t tempFilterId = mFilterId;
                        uint32_t dmxId = mDemux->getAmDmxDevice()->dev_no;
                        mFilterId      = (dmxId << 16) | (uint32_t)(mTpid);
                        ALOGD("audio filter id = %d", mFilterId);
                        mDemux->mapPassthroughMediaFilter(mFilterId, tempFilterId);
                    } else {
                        struct dmx_pes_filter_params aparam;
                        memset(&aparam, 0, sizeof(aparam));
                        aparam.pid = mTpid;
                        aparam.pes_type = DMX_PES_AUDIO0;
                        aparam.input = DMX_IN_FRONTEND;
                        aparam.output = DMX_OUT_TAP;
                        aparam.flags = 0;
                        aparam.flags |= DMX_ES_OUTPUT;
                        if (mDemux->getAmDmxDevice()
                            ->AM_DMX_SetBufferSize(mFilterId, mBufferSize) != 0 ) {
                            return Result::UNAVAILABLE;
                        }
                        if (mDemux->getAmDmxDevice()
                            ->AM_DMX_SetPesFilter(mFilterId, &aparam) != 0 ) {
                            return Result::UNAVAILABLE;
                        }
                    }
                    break;
                }
                case DemuxTsFilterType::VIDEO: {
                    ALOGD("%s subType:VIDEO", __FUNCTION__);
                    if (settings.ts().filterSettings.av().isPassthrough) {
                        //vparam.flags |= DMX_OUTPUT_RAW_MODE;
                        // for passthrough mode, will set pes filter in media hal
                        uint32_t tempFilterId = mFilterId;
                        uint32_t dmxId = mDemux->getAmDmxDevice()->dev_no;
                        mFilterId      = (dmxId << 16) | (uint32_t)(mTpid);
                        ALOGD("video filter id = %d", mFilterId);
                        mDemux->mapPassthroughMediaFilter(mFilterId, tempFilterId);
                    } else {
                        int buffSize = 0;
                        struct dmx_pes_filter_params vparam;
                        memset(&vparam, 0, sizeof(vparam));
                        vparam.pid = mTpid;
                        vparam.pes_type = DMX_PES_VIDEO0;
                        vparam.input = DMX_IN_FRONTEND;
                        vparam.output = DMX_OUT_TAP;
                        vparam.flags = 0;
                        vparam.flags |= DMX_ES_OUTPUT;
#ifdef TUNERHAL_DBG
                        buffSize = mVideoFilterSize;
#else
                        buffSize = mBufferSize;
#endif
                        ALOGD("%s AM_DMX_SetBufferSize:%d MB", __FUNCTION__, buffSize/1024/1024);
                        if (mDemux->getAmDmxDevice()
                            ->AM_DMX_SetBufferSize(mFilterId, buffSize) != 0 ) {
                            return Result::UNAVAILABLE;
                        }
                        if (mDemux->getAmDmxDevice()
                            ->AM_DMX_SetPesFilter(mFilterId, &vparam) != 0 ) {
                            return Result::UNAVAILABLE;
                        }
                    }
                    break;
                }
                case DemuxTsFilterType::RECORD: {
                    ALOGD("%s subType:RECORD", __FUNCTION__);
                    struct dmx_pes_filter_params pparam;
                    memset(&pparam, 0, sizeof(pparam));
                    pparam.pid = mTpid;
                    pparam.input = DMX_IN_FRONTEND;
                    pparam.output = DMX_OUT_TS_TAP;
                    pparam.pes_type = DMX_PES_OTHER;
                    if (mDemux->getAmDmxDevice()
                        ->AM_DMX_SetBufferSize(mFilterId, 10 * 1024 * 1024) != 0 ) {
                        return Result::UNAVAILABLE;
                    }
                    if (mDemux->getAmDmxDevice()
                        ->AM_DMX_SetPesFilter(mFilterId, &pparam) != 0 ) {
                        ALOGE("record AM_DMX_SetPesFilter");
                        return Result::UNAVAILABLE;
                    }
                    ALOGD("stream(pid = %d) start recording, filter = %d", mTpid, mFilterId);
                    break;
                }
                case DemuxTsFilterType::PCR: {
                    ALOGD("%s subType:PCR", __FUNCTION__);
                    struct dmx_pes_filter_params pcrParam;
                    memset(&pcrParam, 0, sizeof(pcrParam));
                    pcrParam.pid = mTpid;
                    pcrParam.pes_type = DMX_PES_PCR0;
                    pcrParam.input = DMX_IN_FRONTEND;
                    pcrParam.output = DMX_OUT_TAP;
                    pcrParam.flags = 0;
                    pcrParam.flags |= DMX_ES_OUTPUT;
                    if (mDemux->getAmDmxDevice()
                        ->AM_DMX_SetBufferSize(mFilterId, mBufferSize) != 0 ) {
                        return Result::UNAVAILABLE;
                    }
                    if (mDemux->getAmDmxDevice()
                        ->AM_DMX_SetPesFilter(mFilterId, &pcrParam) != 0 ) {
                        return Result::UNAVAILABLE;
                    }
                    break;
                }
                case DemuxTsFilterType::PES: {
                    ALOGD("%s subType:PES", __FUNCTION__);
                    struct dmx_pes_filter_params pesp;
                    memset(&pesp, 0, sizeof(pesp));
                    pesp.pid = mTpid;
                    pesp.output = DMX_OUT_TAP;
                    pesp.pes_type = DMX_PES_SUBTITLE;
                    pesp.input = DMX_IN_FRONTEND;
                    if (mDemux->getAmDmxDevice()
                        ->AM_DMX_SetBufferSize(mFilterId, mBufferSize) != 0 ) {
                        return Result::UNAVAILABLE;
                    }
                    if (mDemux->getAmDmxDevice()
                        ->AM_DMX_SetPesFilter(mFilterId, &pesp) != 0 ) {
                        return Result::UNAVAILABLE;
                    }
                    break;
                }
                default:
                    break;
            }
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

    return Result::SUCCESS;
}

Return<Result> Filter::start() {
    ALOGD("%s/%d mFilterId:%d", __FUNCTION__, __LINE__, mFilterId);
    if (mDemux->getAmDmxDevice()
        ->AM_DMX_StartFilter(mFilterId) != 0) {
        ALOGE("Start filter %d failed!", mFilterId);
        return Result::UNAVAILABLE;
    }

    //return startFilterLoop();
    return Result::SUCCESS;
}

Return<Result> Filter::stop() {
    ALOGD("%s/%d mFilterId:%d", __FUNCTION__, __LINE__, mFilterId);
    if (mFilterId > DMX_FILTER_COUNT) {
        mFilterId = mDemux->findFilterIdByfakeFilterId(mFilterId);
    }
    mDemux->getAmDmxDevice()->AM_DMX_SetCallback(mFilterId, NULL, NULL);
    mDemux->getAmDmxDevice()->AM_DMX_StopFilter(mFilterId);
    mFilterThreadRunning = false;

    std::lock_guard<std::mutex> lock(mFilterThreadLock);
    return Result::SUCCESS;
}

Return<Result> Filter::flush() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    // temp implementation to flush the FMQ
    int size = mFilterMQ->availableToRead();
    char* buffer = new char[size];
    mFilterMQ->read((unsigned char*)&buffer[0], size);
    delete[] buffer;
    mFilterStatus = DemuxFilterStatus::DATA_READY;

    return Result::SUCCESS;
}

Return<Result> Filter::releaseAvHandle(const hidl_handle& /*avMemory*/, uint64_t avDataId) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);
    if (mDataId2Avfd.find(avDataId) == mDataId2Avfd.end()) {
        return Result::INVALID_ARGUMENT;
    }

    ::close(mDataId2Avfd[avDataId]);
    return Result::SUCCESS;
}

Return<Result> Filter::close() {
    ALOGD("%s/%d mFilterId = %d", __FUNCTION__, __LINE__, mFilterId);
    if (mFilterId > DMX_FILTER_COUNT) {
        uint32_t tmpFilterId = mFilterId;
        mFilterId = mDemux->findFilterIdByfakeFilterId(mFilterId);
        mDemux->eraseFakeFilterId(tmpFilterId);
    }
    mDemux->getAmDmxDevice()->AM_DMX_FreeFilter(mFilterId);
    if (mFilterMQ.get() != NULL)
        mFilterMQ.reset();
    return mDemux->removeFilter(mFilterId);
}

bool Filter::createFilterMQ() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    // Create a synchronized FMQ that supports blocking read/write
    std::unique_ptr<FilterMQ> tmpFilterMQ =
            std::unique_ptr<FilterMQ>(new (std::nothrow) FilterMQ(mBufferSize, true));
    if (!tmpFilterMQ->isValid()) {
        ALOGW("[Filter] Failed to create FMQ for filter %d", mFilterId);
        return false;
    }

    mFilterMQ = std::move(tmpFilterMQ);

    if (EventFlag::createEventFlag(mFilterMQ->getEventFlagWord(), &mFilterEventFlag) != OK) {
        return false;
    }

    return true;
}

Result Filter::startFilterLoop() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    pthread_create(&mFilterThread, NULL, __threadLoopFilter, this);
    pthread_setname_np(mFilterThread, "filter_waiting_loop");

    return Result::SUCCESS;
}

void* Filter::__threadLoopFilter(void* user) {
    Filter* const self = static_cast<Filter*>(user);
    self->filterThreadLoop();
    return 0;
}

void Filter::filterThreadLoop() {
    std::lock_guard<std::mutex> lock(mFilterThreadLock);
    mFilterThreadRunning = true;
    ALOGD("[Filter] Filter %d threadLoop start.", mFilterId);

    // For the first time of filter output, implementation needs to send the filter
    // Event Callback without waiting for the DATA_CONSUMED to init the process.
    while (mFilterThreadRunning) {
        if (mFilterEvent.events.size() == 0) {
            if (DEBUG_FILTER) {
                ALOGD("[Filter] wait for filter data output.");
            }
            usleep(1000 * 1000);
            continue;
        }
        if (mCallback == nullptr) {
            ALOGW("[Filter] Filter %d does not hava callback. Ending thread", mFilterId);
            break;
        }
        ALOGD("onFilterEvent mFilterId:%d", mFilterId);
        // After successfully write, send a callback and wait for the read to be done
        mCallback->onFilterEvent(mFilterEvent);
        freeAvHandle();
        mFilterEvent.events.resize(0);
        mFilterStatus = DemuxFilterStatus::DATA_READY;
        mCallback->onFilterStatus(mFilterStatus);
        //break;
    }

    while (mFilterThreadRunning) {
        //uint32_t efState = 0;
        // We do not wait for the last round of written data to be read to finish the thread
        // because the VTS can verify the reading itself.
        for (int i = 0; i < SECTION_WRITE_COUNT; i++) {
            #if 0
            while (mFilterThreadRunning && mIsUsingFMQ) {
                status_t status = mFilterEventFlag->wait(
                        static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_CONSUMED), &efState,
                        WAIT_TIMEOUT, true /* retry on spurious wake */);
                if (status != OK) {
                    ALOGD("[Filter] wait for data consumed");
                    continue;
                }
                break;
            }
            #endif
            maySendFilterStatusCallback();

            while (mFilterThreadRunning) {
                std::lock_guard<std::mutex> lock(mFilterEventLock);
                if (mFilterEvent.events.size() == 0) {
                    continue;
                }
                // After successfully write, send a callback and wait for the read to be done
                mCallback->onFilterEvent(mFilterEvent);
                mFilterEvent.events.resize(0);
                //break;
            }
            // We do not wait for the last read to be done
            // VTS can verify the read result itself.
            if (i == SECTION_WRITE_COUNT - 1) {
                ALOGD("[Filter] Filter %d writing done. Ending thread", mFilterId);
                //break;
            }
        }
        mFilterThreadRunning = false;
    }

    ALOGD("[Filter] Filter %d thread ended.", mFilterId);
}

void Filter::fillDataToDecoder() {
    ALOGV("%s/%d mFilterId:%d", __FUNCTION__, __LINE__, mFilterId);

    // For the first time of filter output, implementation needs to send the filter
    // Event Callback without waiting for the DATA_CONSUMED to init the process.
    if (mFilterEvent.events.size() == 0) {
        ALOGV("[Filter %d] wait for new mFilterEvent", mFilterId);
        return;
    }
    if (mCallback == nullptr) {
        ALOGW("[Filter] mFilterId:%d does not hava callback.", mFilterId);
        return;
    }
    // After successfully write, send a callback and wait for the read to be done
    mCallback->onFilterEvent(mFilterEvent);
    freeAvHandle();
    mFilterEvent.events.resize(0);
    mFilterStatus = DemuxFilterStatus::DATA_READY;
    mCallback->onFilterStatus(mFilterStatus);
}

void Filter::freeAvHandle() {
    ALOGV("%s/%d mFilterId:%d", __FUNCTION__, __LINE__, mFilterId);
    if (!mIsMediaFilter) {
        return;
    }
    for (int i = 0; i < mFilterEvent.events.size(); i++) {
        ::close(mFilterEvent.events[i].media().avMemory.getNativeHandle()->data[0]);
        native_handle_close(mFilterEvent.events[i].media().avMemory.getNativeHandle());
    }
}

void Filter::maySendFilterStatusCallback() {
    if (!mIsUsingFMQ) {
        return;
    }
    std::lock_guard<std::mutex> lock(mFilterStatusLock);
    int availableToRead = mFilterMQ->availableToRead();
    int availableToWrite = mFilterMQ->availableToWrite();
    int fmqSize = mFilterMQ->getQuantumCount();

    DemuxFilterStatus newStatus = checkFilterStatusChange(
            availableToWrite, availableToRead, ceil(fmqSize * 0.75), ceil(fmqSize * 0.25));
    if (mFilterStatus != newStatus) {
        ALOGD("%s/%d mFilterId:%d aw:%d ar:%d fmqSize:%d", __FUNCTION__, __LINE__,
        mFilterId, availableToWrite, availableToRead, fmqSize);
        mCallback->onFilterStatus(newStatus);
        mFilterStatus = newStatus;
    }
}

DemuxFilterStatus Filter::checkFilterStatusChange(uint32_t availableToWrite,
                                                  uint32_t availableToRead, uint32_t highThreshold,
                                                  uint32_t lowThreshold) {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);
    if (availableToWrite == 0) {
        return DemuxFilterStatus::OVERFLOW;
    } else if (availableToRead > highThreshold) {
        return DemuxFilterStatus::HIGH_WATER;
    } else if (availableToRead < lowThreshold) {
        return DemuxFilterStatus::LOW_WATER;
    }
    return mFilterStatus;
}

uint16_t Filter::getTpid() {
    ALOGV("%s/%d", __FUNCTION__, __LINE__);
    return mTpid;
}

void Filter::updateFilterOutput(vector<uint8_t> data) {
    std::lock_guard<std::mutex> lock(mFilterOutputLock);
    if (DEBUG_FILTER)
        ALOGD("%s/%d mFilterId:%d data size:%d output size:%dKB", __FUNCTION__, __LINE__,
        mFilterId, data.size(), mFilterOutput.size()/1024);

    mFilterOutput.insert(mFilterOutput.end(), data.begin(), data.end());
}

void Filter::updateRecordOutput(vector<uint8_t> data) {
    std::lock_guard<std::mutex> lock(mRecordFilterOutputLock);
    if (DEBUG_FILTER)
        ALOGD("%s/%d mFilterId:%d data size:%d", __FUNCTION__, __LINE__, mFilterId, data.size());

    mRecordFilterOutput.insert(mRecordFilterOutput.end(), data.begin(), data.end());
}

Result Filter::startFilterHandler() {
    std::lock_guard<std::mutex> lock(mFilterOutputLock);
    if (DEBUG_FILTER)
        ALOGD("%s/%d Filter id:%d", __FUNCTION__, __LINE__, mFilterId);
    switch (mType.mainType) {
        case DemuxFilterMainType::TS:
            switch (mType.subType.tsFilterType()) {
                case DemuxTsFilterType::UNDEFINED:
                    break;
                case DemuxTsFilterType::SECTION:
                    startSectionFilterHandler();
                    break;
                case DemuxTsFilterType::PES:
                    startPesFilterHandler();
                    break;
                case DemuxTsFilterType::TS:
                    startTsFilterHandler();
                    break;
                case DemuxTsFilterType::AUDIO:
                case DemuxTsFilterType::VIDEO:
                    startMediaFilterHandler();
                    break;
                case DemuxTsFilterType::PCR:
                    startPcrFilterHandler();
                    break;
                case DemuxTsFilterType::TEMI:
                    startTemiFilterHandler();
                    break;
                default:
                    break;
            }
            break;
        case DemuxFilterMainType::MMTP:
            /*mmtpSettings*/
            break;
        case DemuxFilterMainType::IP:
            /*ipSettings*/
            break;
        case DemuxFilterMainType::TLV:
            /*tlvSettings*/
            break;
        case DemuxFilterMainType::ALP:
            /*alpSettings*/
            break;
        default:
            break;
    }
    return Result::SUCCESS;
}

Result Filter::startSectionFilterHandler() {
    if (DEBUG_FILTER)
        ALOGD("%s/%d mFilterId:%d", __FUNCTION__, __LINE__, mFilterId);

    if (mFilterOutput.empty()) {
        ALOGV("%s/%d mFilterOutput is empty!", __FUNCTION__, __LINE__);
        return Result::SUCCESS;
    }
    if (!writeSectionsAndCreateEvent(mFilterOutput)) {
        ALOGE("[Filter] filter %d fails to write into FMQ. Ending thread", mFilterId);
        mFilterOutput.clear();
        return Result::UNKNOWN_ERROR;
    }
    fillDataToDecoder();

    mFilterOutput.clear();

    return Result::SUCCESS;
}

Result Filter::startPesFilterHandler() {
    std::lock_guard<std::mutex> lock(mFilterEventLock);
    if (DEBUG_FILTER)
        ALOGD("%s/%d mFilterId:%d", __FUNCTION__, __LINE__, mFilterId);
    if (mFilterOutput.empty()) {
        return Result::SUCCESS;
    }


    #if 0
    for (int i = 0; i < mFilterOutput.size(); i += 188) {
        if (mPesSizeLeft == 0) {
            uint32_t prefix = (mFilterOutput[i + 4] << 16) | (mFilterOutput[i + 5] << 8) |
                              mFilterOutput[i + 6];
            if (DEBUG_FILTER) {
                ALOGD("[Filter] prefix %d", prefix);
            }
            if (prefix == 0x000001) {
                // TODO handle mulptiple Pes filters
                mPesSizeLeft = (mFilterOutput[i + 8] << 8) | mFilterOutput[i + 9];
                mPesSizeLeft += 6;
                if (DEBUG_FILTER) {
                    ALOGD("[Filter] pes data length %d", mPesSizeLeft);
                }
            } else {
                continue;
            }
        }

        int endPoint = min(184, mPesSizeLeft);
        // append data and check size
        vector<uint8_t>::const_iterator first = mFilterOutput.begin() + i + 4;
        vector<uint8_t>::const_iterator last = mFilterOutput.begin() + i + 4 + endPoint;
        mPesOutput.insert(mPesOutput.end(), first, last);
        // size does not match then continue
        mPesSizeLeft -= endPoint;
        if (DEBUG_FILTER) {
            ALOGD("[Filter] pes data left %d", mPesSizeLeft);
        }
        if (mPesSizeLeft > 0) {
            continue;
        }
        // size match then create event
        if (!writeDataToFilterMQ(mPesOutput)) {
            ALOGD("[Filter] pes data write failed");
            mFilterOutput.clear();
            return Result::INVALID_STATE;
        }
        maySendFilterStatusCallback();
        DemuxFilterPesEvent pesEvent;
        pesEvent = {
                // temp dump meta data
                .streamId = mPesOutput[3],
                .dataLength = static_cast<uint16_t>(mPesOutput.size()),
        };
        if (DEBUG_FILTER) {
            ALOGD("[Filter] assembled pes data length %d", pesEvent.dataLength);
        }

        int size = mFilterEvent.events.size();
        mFilterEvent.events.resize(size + 1);
        mFilterEvent.events[size].pes(pesEvent);
        fillDataToDecoder();
        mPesOutput.clear();
    }
    #endif
            // size match then create event
    if (!writeDataToFilterMQ(mFilterOutput)) {
        ALOGD("[Filter] pes data write failed");
        mFilterOutput.clear();
        return Result::INVALID_STATE;
    }
    maySendFilterStatusCallback();
    DemuxFilterPesEvent pesEvent;
    pesEvent = {
            // temp dump meta data
            .streamId = mFilterOutput[3],
            .dataLength = static_cast<uint16_t>(mFilterOutput.size()),
    };
    if (DEBUG_FILTER) {
        ALOGD("[Filter] assembled pes data length %d", pesEvent.dataLength);
    }

    int size = mFilterEvent.events.size();
    mFilterEvent.events.resize(size + 1);
    mFilterEvent.events[size].pes(pesEvent);
    fillDataToDecoder();
    mFilterOutput.clear();

    return Result::SUCCESS;
}

Result Filter::startTsFilterHandler() {
    if (DEBUG_FILTER)
        ALOGD("%s/%d mFilterId:%d TODO", __FUNCTION__, __LINE__, mFilterId);

    // TODO handle starting TS filter
    return Result::SUCCESS;
}

Result Filter::startMediaFilterHandler() {
    std::lock_guard<std::mutex> lock(mFilterEventLock);

    if (mFilterOutput.size() > 0)
        ALOGV("%s/%d mFilterId:%d output size:%d KB", __FUNCTION__, __LINE__, mFilterId, mFilterOutput.size()/1024);

#ifdef TUNERHAL_DBG
    if (mFilterOutput.empty() || mFilterOutput.size() < mFilterEventSize) {
#else
    if (mFilterOutput.empty() || mFilterOutput.size() < 1024 * 1024 * 10) {//10MB
#endif
        return Result::SUCCESS;
    }

#if 1
    int av_fd = createAvIonFd(mFilterOutput.size());
    if (av_fd == -1) {
       return Result::UNKNOWN_ERROR;
    }
    // copy the filtered data to the buffer
    uint8_t* avBuffer = getIonBuffer(av_fd, mFilterOutput.size());
    if (avBuffer == NULL) {
       return Result::UNKNOWN_ERROR;
    }
    ALOGD("%s/%d mFilterId:%d mFilterEvent size:%d", __FUNCTION__, __LINE__,
    mFilterId, mFilterOutput.size());
    memcpy(avBuffer, mFilterOutput.data(), mFilterOutput.size() * sizeof(uint8_t));

    native_handle_t* nativeHandle = createNativeHandle(av_fd);
    if (nativeHandle == NULL) {
       return Result::UNKNOWN_ERROR;
    }
    hidl_handle handle;
    handle.setTo(nativeHandle, /*shouldOwn=*/true);

    // Create a dataId and add a <dataId, av_fd> pair into the dataId2Avfd map
    uint64_t dataId = mLastUsedDataId++ /*createdUID*/;
    mDataId2Avfd[dataId] = dup(av_fd);

    // Create mediaEvent and send callback
    DemuxFilterMediaEvent mediaEvent;
    mediaEvent = {
           .avMemory = std::move(handle),
           .dataLength = static_cast<uint32_t>(mFilterOutput.size()),
           .avDataId = dataId,
    };
    if (mType.subType.tsFilterType() == DemuxTsFilterType::AUDIO) {
        AudioExtraMetaData audio;
        mediaEvent.extraMetaData.audio(audio);
    }
    int size = mFilterEvent.events.size();
    mFilterEvent.events.resize(size + 1);
    mFilterEvent.events[size].media(mediaEvent);
    fillDataToDecoder();

    // Clear and log
    releaseIonBuffer(avBuffer, mFilterOutput.size());
    mFilterOutput.clear();
    mAvBufferCopyCount = 0;
    ::close(av_fd);
    if (DEBUG_FILTER) {
       ALOGD("[Filter] assembled av data length %d", mediaEvent.dataLength);
    }


#else
    for (int i = 0; i < mFilterOutput.size(); i += 188) {
        if (mPesSizeLeft == 0) {
            uint32_t prefix = (mFilterOutput[i + 4] << 16) | (mFilterOutput[i + 5] << 8) |
                              mFilterOutput[i + 6];
            if (DEBUG_FILTER) {
                ALOGD("[Filter] prefix %d", prefix);
            }
            if (prefix == 0x000001) {
                // TODO handle mulptiple Pes filters
                mPesSizeLeft = (mFilterOutput[i + 8] << 8) | mFilterOutput[i + 9];
                mPesSizeLeft += 6;
                if (DEBUG_FILTER) {
                    ALOGD("[Filter] pes data length %d", mPesSizeLeft);
                }
            } else {
                continue;
            }
        }

        int endPoint = min(184, mPesSizeLeft);
        // append data and check size
        vector<uint8_t>::const_iterator first = mFilterOutput.begin() + i + 4;
        vector<uint8_t>::const_iterator last = mFilterOutput.begin() + i + 4 + endPoint;
        mPesOutput.insert(mPesOutput.end(), first, last);
        // size does not match then continue
        mPesSizeLeft -= endPoint;
        if (DEBUG_FILTER) {
            ALOGD("[Filter] pes data left %d", mPesSizeLeft);
        }
        if (mPesSizeLeft > 0 || mAvBufferCopyCount++ < 10) {
            continue;
        }

        int av_fd = createAvIonFd(mPesOutput.size());
        if (av_fd == -1) {
            return Result::UNKNOWN_ERROR;
        }
        // copy the filtered data to the buffer
        uint8_t* avBuffer = getIonBuffer(av_fd, mPesOutput.size());
        if (avBuffer == NULL) {
            return Result::UNKNOWN_ERROR;
        }
        memcpy(avBuffer, mPesOutput.data(), mPesOutput.size() * sizeof(uint8_t));

        native_handle_t* nativeHandle = createNativeHandle(av_fd);
        if (nativeHandle == NULL) {
            return Result::UNKNOWN_ERROR;
        }
        hidl_handle handle;
        handle.setTo(nativeHandle, /*shouldOwn=*/true);

        // Create a dataId and add a <dataId, av_fd> pair into the dataId2Avfd map
        uint64_t dataId = mLastUsedDataId++ /*createdUID*/;
        mDataId2Avfd[dataId] = dup(av_fd);

        // Create mediaEvent and send callback
        DemuxFilterMediaEvent mediaEvent;
        mediaEvent = {
                .avMemory = std::move(handle),
                .dataLength = static_cast<uint32_t>(mPesOutput.size()),
                .avDataId = dataId,
        };
        int size = mFilterEvent.events.size();
        mFilterEvent.events.resize(size + 1);
        mFilterEvent.events[size].media(mediaEvent);

        // Clear and log
        mPesOutput.clear();
        mAvBufferCopyCount = 0;
        ::close(av_fd);
        if (DEBUG_FILTER) {
            ALOGD("[Filter] assembled av data length %d", mediaEvent.dataLength);
        }
    }

    mFilterOutput.clear();
#endif
    return Result::SUCCESS;
}

Result Filter::startRecordFilterHandler() {
    std::lock_guard<std::mutex> lock(mRecordFilterOutputLock);
    if (DEBUG_FILTER)
        ALOGD("%s/%d mFilterId:%d", __FUNCTION__, __LINE__, mFilterId);

    if (mRecordFilterOutput.empty()) {
        return Result::SUCCESS;
    }

    if (mDvr == nullptr || !mDvr->writeRecordFMQ(mRecordFilterOutput)) {
        ALOGD("[Filter] dvr fails to write into record FMQ!");
        return Result::UNKNOWN_ERROR;
    }

    // Create tsRecEvent and send callback
    if (DEBUG_FILTER) {
        ALOGD("[Filter] create tsRecEvent mTpid = %d and send callback.", mTpid);
    }
    DemuxFilterTsRecordEvent tsRecEvent;
    DemuxPid demuxPid;
    demuxPid.tPid(static_cast<DemuxTpid>(mTpid));
    tsRecEvent = {
        .pid       = demuxPid,
        .byteNumber = static_cast<uint64_t>(mRecordFilterOutput.size()),
    };
    int size = mFilterEvent.events.size();
    mFilterEvent.events.resize(size + 1);
    mFilterEvent.events[size].tsRecord(tsRecEvent);
    fillDataToDecoder();
    mRecordFilterOutput.clear();
    return Result::SUCCESS;
}

Result Filter::startPcrFilterHandler() {
    if (DEBUG_FILTER)
        ALOGI("%s/%d mFilterId:%d", __FUNCTION__, __LINE__, mFilterId);

    // TODO handle starting PCR filter
    return Result::SUCCESS;
}

Result Filter::startTemiFilterHandler() {
    if (DEBUG_FILTER)
        ALOGD("%s/%d mFilterId:%d", __FUNCTION__, __LINE__, mFilterId);
    // TODO handle starting TEMI filter
    return Result::SUCCESS;
}

bool Filter::writeSectionsAndCreateEvent(vector<uint8_t> data) {
    // TODO check how many sections has been read
    std::lock_guard<std::mutex> lock(mFilterEventLock);
    ALOGV("%s/%d mFilterId:%d Create mFilterEvent size:%d", __FUNCTION__, __LINE__, mFilterId, data.size());

    if (!writeDataToFilterMQ(data)) {
        ALOGE("%s/%d mFilterId:%d writeDataToFilterMQ failed!", __FUNCTION__, __LINE__, mFilterId);
        return false;
    }
    int size = mFilterEvent.events.size();
    mFilterEvent.events.resize(size + 1);
    DemuxFilterSectionEvent secEvent;
    secEvent = {
            // temp dump meta data
            .tableId = data[0],
            .version = static_cast<uint16_t>((data[5] >> 1) & 0x1f),
            .sectionNum = data[6],
            .dataLength = static_cast<uint16_t>(data.size()),
    };
    mFilterEvent.events[size].section(secEvent);
    return true;
}

bool Filter::writeDataToFilterMQ(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mWriteLock);
    if (mFilterMQ->write(data.data(), data.size())) {
        return true;
    }
    return false;
}

void Filter::attachFilterToRecord(const sp<Dvr> dvr) {
    ALOGD("%s/%d mFilterId:%d", __FUNCTION__, __LINE__, mFilterId);
    mDvr = dvr;
}

void Filter::detachFilterFromRecord() {
    ALOGD("%s/%d mFilterId:%d", __FUNCTION__, __LINE__, mFilterId);
    mDvr = nullptr;
}

int Filter::createAvIonFd(int size) {
    // Create an ion fd and allocate an av fd mapped to a buffer to it.
    mIonFd = ion_open();
    if (mIonFd == -1) {
        ALOGE("[Filter] Failed to open ion fd errno:%d!", errno);
        return -1;
    }
    int av_fd = -1;
    ion_alloc_fd(dup(mIonFd), size, 0 /*align*/, ION_HEAP_SYSTEM_MASK, 0 /*flags*/, &av_fd);
    if (av_fd == -1) {
        ALOGE("[Filter] Failed to create av fd errno:%d!", errno);
        return -1;
    }
    ALOGD("%s mFilterId:%d ion_alloc_fd:%d size:%d", __FUNCTION__, mFilterId, av_fd, size);
    return av_fd;
}

uint8_t* Filter::getIonBuffer(int fd, int size) {
    ALOGD("%s/%d mFilterId:%d fd:%d size:%d", __FUNCTION__, __LINE__, mFilterId, fd, size);
    uint8_t* avBuf = static_cast<uint8_t*>(
            mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 /*offset*/));
    if (avBuf == MAP_FAILED) {
        ALOGE("[Filter] Failed to allocate buffer %d!", errno);
        return NULL;
    }
    return avBuf;
}

native_handle_t* Filter::createNativeHandle(int fd) {
    native_handle_t* nativeHandle;
    if (fd < 0) {
        nativeHandle = native_handle_create(/*numFd*/ 0, 0);
    } else {
        // Create a native handle to pass the av fd via the callback event.
        nativeHandle = native_handle_create(/*numFd*/ 1, 0);
    }
    if (nativeHandle == NULL) {
        ALOGE("[Filter] Failed to create native_handle %d", errno);
        return NULL;
    }
    if (nativeHandle->numFds > 0) {
        nativeHandle->data[0] = dup(fd);
    }
    ALOGD("%s/%d mFilterId:%d fd:%d nativeHandle fd:%d", __FUNCTION__, __LINE__, mFilterId, fd, nativeHandle->data[0]);

    return nativeHandle;
}

void Filter::releaseIonBuffer(uint8_t* avBuf, int size) {
    if (avBuf != NULL) {
        munmap(avBuf, size);
    }

    if (mIonFd >= 0) {
        ion_close(mIonFd);
        mIonFd = -1;
    }
}
}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
