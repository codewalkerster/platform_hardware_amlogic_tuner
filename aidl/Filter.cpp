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
#define LOG_TAG "android.hardware.tv.tuner-service.droidlogic-Filter"

#include <BufferAllocator/BufferAllocator.h>
#include <aidl/android/hardware/tv/tuner/DemuxFilterMonitorEventType.h>
#include <aidl/android/hardware/tv/tuner/DemuxQueueNotifyBits.h>
#include <aidl/android/hardware/tv/tuner/Result.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <inttypes.h>
#include <utils/Log.h>

#include "Filter.h"

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

#define WAIT_TIMEOUT 3000000000

FilterCallbackScheduler::FilterCallbackScheduler(const std::shared_ptr<IFilterCallback>& cb)
    : mCallback(cb),
      mIsConditionMet(false),
      mDataLength(0),
      mTimeDelayInMs(0),
      mDataSizeDelayInBytes(0) {
      ALOGD("enter FilterCallbackScheduler");
    start();
}

FilterCallbackScheduler::~FilterCallbackScheduler() {
    stop();
    ALOGD("exit FilterCallbackScheduler");
}

void FilterCallbackScheduler::onFilterEvent(DemuxFilterEvent&& event) {
    std::unique_lock<std::mutex> lock(mLock);
    mCallbackBuffer.push_back(std::move(event));
    mDataLength += getDemuxFilterEventDataLength(event);

    if (isDataSizeDelayConditionMetLocked()) {
        mIsConditionMet = true;
        // unlock, so thread is not immediately blocked when it is notified.
        lock.unlock();
        mCv.notify_all();
    }
}

void FilterCallbackScheduler::onFilterStatus(const DemuxFilterStatus& status) {
    if (mCallback) {
        mCallback->onFilterStatus(status);
    }
}

void FilterCallbackScheduler::flushEvents() {
    std::unique_lock<std::mutex> lock(mLock);
    mCallbackBuffer.clear();
    mDataLength = 0;
}

void FilterCallbackScheduler::setTimeDelayHint(int timeDelay) {
    std::unique_lock<std::mutex> lock(mLock);
    mTimeDelayInMs = timeDelay;
    // always notify condition variable to update timeout
    mIsConditionMet = true;
    lock.unlock();
    mCv.notify_all();
}

void FilterCallbackScheduler::setDataSizeDelayHint(int dataSizeDelay) {
    std::unique_lock<std::mutex> lock(mLock);
    mDataSizeDelayInBytes = dataSizeDelay;
    if (isDataSizeDelayConditionMetLocked()) {
        mIsConditionMet = true;
        lock.unlock();
        mCv.notify_all();
    }
}

bool FilterCallbackScheduler::hasCallbackRegistered() const {
    return mCallback != nullptr;
}

void FilterCallbackScheduler::start() {
    mIsRunning = true;
    ALOGD("FilterCallbackScheduler start");
    mCallbackThread = std::thread(&FilterCallbackScheduler::threadLoop, this);
}

void FilterCallbackScheduler::stop() {
    mIsRunning = false;
    ALOGD("FilterCallbackScheduler stop");
    if (mCallbackThread.joinable()) {
        {
            std::lock_guard<std::mutex> lock(mLock);
            mIsConditionMet = true;
        }
        mCv.notify_all();
        mCallbackThread.join();
    }
}

void FilterCallbackScheduler::threadLoop() {
    while (mIsRunning) {
        threadLoopOnce();
    }
}

void FilterCallbackScheduler::threadLoopOnce() {
    std::unique_lock<std::mutex> lock(mLock);
    if (mTimeDelayInMs > 0) {
        // Note: predicate protects from lost and spurious wakeups
        mCv.wait_for(lock, std::chrono::milliseconds(mTimeDelayInMs),
                     [this] { return mIsConditionMet; });
    } else {
        // Note: predicate protects from lost and spurious wakeups
        mCv.wait(lock, [this] { return mIsConditionMet; });
    }
    mIsConditionMet = false;

    // condition_variable wait locks mutex on timeout / notify
    // Note: if stop() has been called in the meantime, do not send more filter
    // events.
    if (mIsRunning && !mCallbackBuffer.empty()) {
        if (mCallback) {
            mCallback->onFilterEvent(mCallbackBuffer);
        }
        mCallbackBuffer.clear();
        mDataLength = 0;
    }
}

// mLock needs to be held to call this function
bool FilterCallbackScheduler::isDataSizeDelayConditionMetLocked() {
    if (mDataSizeDelayInBytes == 0) {
        // Data size delay is disabled.
        if (mTimeDelayInMs == 0) {
            // Events should only be sent immediately if time delay is disabled
            // as well.
            return true;
        }
        return false;
    }

    // Data size delay is enabled.
    return mDataLength >= mDataSizeDelayInBytes;
}

int FilterCallbackScheduler::getDemuxFilterEventDataLength(const DemuxFilterEvent& event) {
    // there is a risk that dataLength could be a negative value, but it
    // *should* be safe to assume that it is always positive.
    switch (event.getTag()) {
        case DemuxFilterEvent::Tag::section:
            return event.get<DemuxFilterEvent::Tag::section>().dataLength;
        case DemuxFilterEvent::Tag::media:
            return event.get<DemuxFilterEvent::Tag::media>().dataLength;
        case DemuxFilterEvent::Tag::pes:
            return event.get<DemuxFilterEvent::Tag::pes>().dataLength;
        case DemuxFilterEvent::Tag::download:
            return event.get<DemuxFilterEvent::Tag::download>().dataLength;
        case DemuxFilterEvent::Tag::ipPayload:
            return event.get<DemuxFilterEvent::Tag::ipPayload>().dataLength;

        case DemuxFilterEvent::Tag::tsRecord:
        case DemuxFilterEvent::Tag::mmtpRecord:
        case DemuxFilterEvent::Tag::temi:
        case DemuxFilterEvent::Tag::monitorEvent:
        case DemuxFilterEvent::Tag::startId:
            // these events do not include a payload and should therefore return
            // 0.
            // do not add a default option, so this will not compile when new types
            // are added.
            return 0;
    }
}

#define NDS_EMM_DISABLE_TID 0x00U
#define NDS_EMM_ENABLE_TID 0x01U
#define NDS_EMM_ENABLE_TID_NDS 0x02U

static void dhexdump(vector<uint8_t> data, int data_size)
{
    char str_buf[64];
    char *buf_ptr = str_buf;
    enum BUF_LEN
    {
        BUF_LEN = 64
    };

    for (int idx = 0; idx < data_size; idx++) {
        if (idx % 16 == 0) {
            buf_ptr = str_buf;
            buf_ptr += sprintf(str_buf, "%04X: ", idx);
        }

        buf_ptr += sprintf(buf_ptr, "%02x ", data[idx]);

        if (idx % 16 == 15 || idx >= data_size - 1) {
            ALOGD("%s\n", str_buf);
        }
    }
}

static bool
GetNskEmmFltParam(const DemuxFilterSettings &settings, struct dmx_sct_filter_params &param) {
    const DemuxTsFilterSettings& tsConf = settings.get<DemuxFilterSettings::ts>();
    DemuxTsFilterSettings ts{
            .tpid = static_cast<uint16_t>(tsConf.tpid),
    };
    DemuxFilterSectionSettings section {
            .isCheckCrc = tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().isCheckCrc,
            .isRepeat = tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().isRepeat,
            .isRaw = tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().isRaw,
    };
    if (tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.getTag() ==
        DemuxFilterSectionSettingsCondition::sectionBits) {
        ALOGD("NSK EMM Filter Descriptors");
        ALOGD(
            "Filter[%d]",
            tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().filter.size());
            dhexdump(
            tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().filter,
            tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().filter.size());
            ALOGD(
            "Mask[%d]", tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mask.size());
            dhexdump(
            tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mask,
            tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mask.size());
            ALOGD(
            "mode[%d]", tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mode.size());
            dhexdump(
            tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mode,
            tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mode.size());
    }
    if (tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.getTag() ==
        DemuxFilterSectionSettingsCondition::sectionBits) {
        bool isCheckCrc = tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().isCheckCrc;
        if (isCheckCrc) {
            param.flags |= DMX_CHECK_CRC;
        }
        bool isRaw = tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().isRaw;
        if (isRaw) {
            param.flags |= DMX_OUTPUT_RAW_MODE;
        }
        /*bool isRepeat = tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().isRepeat;
        if (!isRepeat) {
            param.flags |= DMX_ONESHOT;
        }*/
        param.pid =
        (unsigned short)(tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mode[0]<< 8
        |
        tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mode[1]);
        // get 0x80 to 0x8f
        param.filter.filter[0] = 0x80;
        param.filter.mask[0] = 0xf0;
        param.filter.mode[0] = 0x00;
    } else {
        // error: not possible in NSK case.
          ALOGE("Emm requested without emm filter description.");
          return false;

    }
    return true;
}

static bool
GetSectionFltParam(const DemuxFilterSettings &settings, struct dmx_sct_filter_params &param) {
    const DemuxTsFilterSettings& tsConf = settings.get<DemuxFilterSettings::ts>();
    /*bool isRepeat = tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().isRepeat;
    if (!isRepeat) {
        param.flags |= DMX_ONESHOT;
    }*/
    bool isCheckCrc = tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().isCheckCrc;
    if (isCheckCrc) {
        param.flags |= DMX_CHECK_CRC;
    }

    bool isRaw = tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().isRaw;
    if (isRaw) {
        param.flags |= DMX_OUTPUT_RAW_MODE;
    }

    if (tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.getTag() ==
        DemuxFilterSectionSettingsCondition::sectionBits) {
        int size =
        tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().filter.size();
        if (size > 0 && size <= 16) {
            param.filter.filter[0] =
                tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().filter[0];
            for (int i = 1; i < size - 2; i++) {
                param.filter.filter[i] =
                    tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().filter[i + 2];
            }
            size = tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mask.size();
            param.filter.mask[0] =
                 tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mask[0];
            for (int i = 1; i < size - 2; i++) {
                param.filter.mask[i] =
                   tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mask[i + 2];
            }
            size = tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mode.size();
            param.filter.mode[0] =
                tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mode[0];
            for (int i = 1; i < size - 2; i++) {
                param.filter.mode[i] =
                    tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mode[i + 2];
            }
        }

    } else {
            param.filter.filter[0] =
                tsConf.filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::tableInfo>().tableId;
            param.filter.mask[0] = 0xff;
    }
    ALOGD("%s, isCheckCrc:%d, isRaw:%d, tableId:0x%x", __FUNCTION__, isCheckCrc, isRaw, param.filter.filter[0]);
    return true;
}

Filter::Filter(DemuxFilterType type, int64_t filterId, uint32_t bufferSize,
               const std::shared_ptr<IFilterCallback>& cb, std::shared_ptr<Demux> demux)
    : mDemux(demux),
      mCallbackScheduler(cb),
      mFilterId(filterId),
      mBufferSize(bufferSize),
      mType(type) {
      mTpid = 0;
    mFilterThreadRunning = false;
    bIsRaw = false;
    mFilterEventsFlag = NULL;
    mFilterStatus = DemuxFilterStatus(1);
    switch (mType.mainType) {
        case DemuxFilterMainType::TS:
            if (mType.subType.get<DemuxFilterSubType::Tag::tsFilterType>() ==
                        DemuxTsFilterType::AUDIO ||
                mType.subType.get<DemuxFilterSubType::Tag::tsFilterType>() ==
                        DemuxTsFilterType::VIDEO) {
                mIsMediaFilter = true;
            }
            if (mType.subType.get<DemuxFilterSubType::Tag::tsFilterType>() ==
                DemuxTsFilterType::PCR) {
                mIsPcrFilter = true;
            }
            if (mType.subType.get<DemuxFilterSubType::Tag::tsFilterType>() ==
                DemuxTsFilterType::RECORD) {
                mIsRecordFilter = true;
            }
            break;
        case DemuxFilterMainType::MMTP:
            if (mType.subType.get<DemuxFilterSubType::Tag::mmtpFilterType>() ==
                        DemuxMmtpFilterType::AUDIO ||
                mType.subType.get<DemuxFilterSubType::Tag::mmtpFilterType>() ==
                        DemuxMmtpFilterType::VIDEO) {
                mIsMediaFilter = true;
            }
            if (mType.subType.get<DemuxFilterSubType::Tag::mmtpFilterType>() ==
                DemuxMmtpFilterType::RECORD) {
                mIsRecordFilter = true;
            }
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
}

Filter::~Filter() {
}

::ndk::ScopedAStatus Filter::getId64Bit(int64_t* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    *_aidl_return = mFilterId;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::getId(int32_t* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    *_aidl_return = static_cast<int32_t>(mFilterId);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::setDataSource(const std::shared_ptr<IFilter>& in_filter) {
    ALOGV("%s", __FUNCTION__);

    mDataSource = in_filter;
    mIsDataSourceDemux = false;

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::setDelayHint(const FilterDelayHint& in_hint) {
    if (mIsMediaFilter) {
        // delay hint is not supported for media filters
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNAVAILABLE));
    }

    ALOGV("%s", __FUNCTION__);
    if (in_hint.hintValue < 0) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }

    switch (in_hint.hintType) {
        case FilterDelayHintType::TIME_DELAY_IN_MS:
            mCallbackScheduler.setTimeDelayHint(in_hint.hintValue);
            break;
        case FilterDelayHintType::DATA_SIZE_DELAY_IN_BYTES:
            mCallbackScheduler.setDataSizeDelayHint(in_hint.hintValue);
            break;
        default:
            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::getQueueDesc(MQDescriptor<int8_t, SynchronizedReadWrite>* out_queue) {
    ALOGV("%s", __FUNCTION__);

    mIsUsingFMQ = mIsRecordFilter ? false : true;

    *out_queue = mFilterMQ->dupeDesc();
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::configure(const DemuxFilterSettings& in_settings) {
    ALOGV("%s", __FUNCTION__);

    mFilterSettings = in_settings;
    switch (mType.mainType) {
        case DemuxFilterMainType::TS:
            mTpid = in_settings.get<DemuxFilterSettings::Tag::ts>().tpid;
            ALOGD("%s/%d mainType:TS mTpid:0x%x", __FUNCTION__, __LINE__, mTpid);
            switch (mType.subType.get<DemuxFilterSubType::Tag::tsFilterType>()) {
                case DemuxTsFilterType::SECTION: {
                    ALOGD("%s subType:SECTION", __FUNCTION__);
                    struct dmx_sct_filter_params param;
                    if (mDemux->getAmDmxDevice()->AM_DMX_SetBufferSize(mFilterId, mBufferSize) != 0) {
                        ALOGD("%s AM_DMX_SetBufferSize fail", __FUNCTION__);
                        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                    }
                    memset(&param, 0, sizeof(param));
                    // Special Value for NSK Emm Filtering.
                    if (mTpid == 0xfffe) {
                        bool result = GetNskEmmFltParam(in_settings, param);
                        if (!result) {
                            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                        }
                        mTpid = param.pid;
                        mIsNSKEmmFilter = true;
                    } else {
                        param.pid = mTpid;
                        bool result = GetSectionFltParam(in_settings, param);
                        if (!result) {
                            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                        }
                    }
                    if (mDemux->getAmDmxDevice()->AM_DMX_SetSecFilter(mFilterId, &param) != 0) {
                        ALOGE("Failed to set Section Filter");
                        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                    }
                    break;
                }
                case DemuxTsFilterType::AUDIO: {
                    ALOGD("%s subType:AUDIO", __FUNCTION__);
                    bool isPassthrough =
                    in_settings.get<DemuxFilterSettings::Tag::ts>().filterSettings.get<DemuxTsFilterSettingsFilterSettings::av>().isPassthrough;
                    if (isPassthrough) {
                        // for passthrough mode, will set pes filter in media hal
                        int64_t tempFilterId = mFilterId;
                        uint32_t dmxId = mDemux->getAmDmxDevice()->dev_no;
                        mFilterId = (dmxId << 16) | (uint32_t)(mTpid);
                        ALOGD("audio filter id = %lld", mFilterId);
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
                        if (mDemux->getAmDmxDevice()->AM_DMX_SetBufferSize(mFilterId, mBufferSize) != 0) {
                            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                        }
                        if (mDemux->getAmDmxDevice()->AM_DMX_SetPesFilter(mFilterId, &aparam) != 0) {
                            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                        }
                    }
                    break;
                }
                case DemuxTsFilterType::VIDEO: {
                    ALOGD("%s subType:VIDEO", __FUNCTION__);
                    bool isPassthrough =
                    in_settings.get<DemuxFilterSettings::Tag::ts>().filterSettings.get<DemuxTsFilterSettingsFilterSettings::av>().isPassthrough;
                    if (isPassthrough) {
                        // for passthrough mode, will set pes filter in media hal
                        int64_t tempFilterId = mFilterId;
                        uint32_t dmxId = mDemux->getAmDmxDevice()->dev_no;
                        mFilterId = (dmxId << 16) | (uint32_t)(mTpid);
                        ALOGD("video filter id = %lld", mFilterId);
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
                        ALOGD("%s AM_DMX_SetBufferSize:%d MB", __FUNCTION__, buffSize / 1024 / 1024);
                        if (mDemux->getAmDmxDevice()->AM_DMX_SetBufferSize(mFilterId, buffSize) != 0) {
                            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                        }
                        if (mDemux->getAmDmxDevice()->AM_DMX_SetPesFilter(mFilterId, &vparam) != 0) {
                            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
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
                    /*
                    if (mDemux->getAmDmxDevice()->AM_DMX_SetBufferSize(mFilterId, 10 * 1024 * 1024) != 0) {
                        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                    }*/
                    if (mDemux->getAmDmxDevice()->AM_DMX_SetPesFilter(mFilterId, &pparam) != 0) {
                        ALOGE("record AM_DMX_SetPesFilter");
                        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                    }
                    ALOGD("stream(pid = %d) start recording, filter = %lld", mTpid, mFilterId);
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
                    if (mDemux->getAmDmxDevice()->AM_DMX_SetBufferSize(mFilterId, mBufferSize) != 0) {
                        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                    }
                    if (mDemux->getAmDmxDevice()->AM_DMX_SetPesFilter(mFilterId, &pcrParam) != 0) {
                        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                    }
                    break;
                }
                case DemuxTsFilterType::PES: {
                    bIsRaw =
                    in_settings.get<DemuxFilterSettings::Tag::ts>().filterSettings.get<DemuxTsFilterSettingsFilterSettings::pesData>().isRaw;
                    ALOGD("%s subType:PES bIsRaw:%d", __FUNCTION__, bIsRaw);
                    struct dmx_pes_filter_params pesp;
                    memset(&pesp, 0, sizeof(pesp));
                    pesp.pid = mTpid;
                    pesp.output = DMX_OUT_TAP;
                    pesp.pes_type = DMX_PES_SUBTITLE;
                    pesp.input = DMX_IN_FRONTEND;
                    if (mDemux->getAmDmxDevice()->AM_DMX_SetBufferSize(mFilterId, mBufferSize) != 0) {
                        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
                    }
                    if (mDemux->getAmDmxDevice()->AM_DMX_SetPesFilter(mFilterId, &pesp) != 0) {
                        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
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

    mConfigured = true;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::start() {
    ALOGD("%s/%d mFilterId:%lld", __FUNCTION__, __LINE__, mFilterId);
    if (mDemux->getAmDmxDevice()
        ->AM_DMX_StartFilter(mFilterId) != 0) {
        bool isPassthrough =
        mFilterSettings.get<DemuxFilterSettings::Tag::ts>().filterSettings.get<DemuxTsFilterSettingsFilterSettings::av>().isPassthrough;
        if (mIsMediaFilter && isPassthrough) {
            ALOGD("av filter will start in mediahal");
        } else {
            ALOGE("Start filter %lld failed!", mFilterId);
        }
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                                    static_cast<int32_t>(Result::UNAVAILABLE));
    }
    mFilterThreadRunning = true;

    return startFilterLoop();
}

::ndk::ScopedAStatus Filter::stop() {
    ALOGD("%s/%d mFilterId:%lld", __FUNCTION__, __LINE__, mFilterId);
    if (mFilterId > DMX_FILTER_COUNT) {
        mFilterId = mDemux->findFilterIdByfakeFilterId(mFilterId);
    }
    mDemux->getAmDmxDevice()->AM_DMX_StopFilter(mFilterId);

    mFilterThreadRunning = false;
    if (mFilterThread.joinable()) {
        mFilterThread.join();
    }

    mCallbackScheduler.flushEvents();
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::flush() {
    ALOGV("%s", __FUNCTION__);

    // temp implementation to flush the FMQ
    if (mFilterMQ.get() != NULL) {
        int size = mFilterMQ->availableToRead();
        int8_t* buffer = new int8_t[size];
        mFilterMQ->read(buffer, size);
        delete[] buffer;
        mFilterStatus = DemuxFilterStatus::DATA_READY;
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::releaseAvHandle(const NativeHandle& in_avMemory, int64_t in_avDataId) {
    ALOGV("%s", __FUNCTION__);

    if ((mSharedAvMemHandle != nullptr) && (in_avMemory.fds.size() > 0) &&
        (sameFile(in_avMemory.fds[0].get(), mSharedAvMemHandle->data[0]))) {
        freeSharedAvHandle();
        return ::ndk::ScopedAStatus::ok();
    }

    if (mDataId2Avfd.find(in_avDataId) == mDataId2Avfd.end()) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }

    ::close(mDataId2Avfd[in_avDataId]);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::close() {
    ALOGD("%s/%d mFilterId = %lld", __FUNCTION__, __LINE__, mFilterId);
    mDemux->getAmDmxDevice()->AM_DMX_SetCallback(mFilterId, NULL, NULL);
    mDemux->getAmDmxDevice()->AM_DMX_FreeFilter(mFilterId);

    return mDemux->removeFilter(mFilterId);
}

void Filter::clear() {
    if (mFilterMQ.get() != NULL)
        mFilterMQ.reset();

    if (mFilterEventsFlag != nullptr) {
        EventFlag::deleteEventFlag(&mFilterEventsFlag);
        mFilterEventsFlag = nullptr;
    }

    //mCallbackScheduler.flushEvents();
}

::ndk::ScopedAStatus Filter::configureIpCid(int32_t in_ipCid) {
    ALOGV("%s", __FUNCTION__);

    if (mType.mainType != DemuxFilterMainType::IP) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_STATE));
    }

    mCid = in_ipCid;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::getAvSharedHandle(NativeHandle* out_avMemory, int64_t* _aidl_return) {
    ALOGV("%s", __FUNCTION__);

    if (!mIsMediaFilter) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_STATE));
    }

    if (mSharedAvMemHandle != nullptr) {
        *out_avMemory = ::android::dupToAidl(mSharedAvMemHandle);
        *_aidl_return = BUFFER_SIZE_16M;
        mUsingSharedAvMem = true;
        return ::ndk::ScopedAStatus::ok();
    }

    int av_fd = createAvIonFd(BUFFER_SIZE_16M);
    if (av_fd < 0) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::OUT_OF_MEMORY));
    }

    mSharedAvMemHandle = createNativeHandle(av_fd);
    if (mSharedAvMemHandle == nullptr) {
        ::close(av_fd);
        *_aidl_return = 0;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNKNOWN_ERROR));
    }
    ::close(av_fd);
    mUsingSharedAvMem = true;

    *out_avMemory = ::android::dupToAidl(mSharedAvMemHandle);
    *_aidl_return = BUFFER_SIZE_16M;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::configureAvStreamType(const AvStreamType& in_avStreamType) {
    ALOGV("%s", __FUNCTION__);

    if (!mIsMediaFilter) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNAVAILABLE));
    }

    switch (in_avStreamType.getTag()) {
        case AvStreamType::Tag::audio:
            mAudioStreamType =
                    static_cast<uint32_t>(in_avStreamType.get<AvStreamType::Tag::audio>());
            break;
        case AvStreamType::Tag::video:
            mVideoStreamType =
                    static_cast<uint32_t>(in_avStreamType.get<AvStreamType::Tag::video>());
            break;
        default:
            break;
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::configureMonitorEvent(int in_monitorEventTypes) {
    ALOGV("%s", __FUNCTION__);

    int32_t newScramblingStatus =
            in_monitorEventTypes &
            static_cast<int32_t>(DemuxFilterMonitorEventType::SCRAMBLING_STATUS);
    int32_t newIpCid =
            in_monitorEventTypes & static_cast<int32_t>(DemuxFilterMonitorEventType::IP_CID_CHANGE);

    // if scrambling status monitoring flipped, record the new state and send msg on enabling
    if (newScramblingStatus ^ mScramblingStatusMonitored) {
        mScramblingStatusMonitored = newScramblingStatus;
        if (mScramblingStatusMonitored) {
            if (mCallbackScheduler.hasCallbackRegistered()) {
                // Assuming current status is always NOT_SCRAMBLED
                auto monitorEvent = DemuxFilterMonitorEvent::make<
                        DemuxFilterMonitorEvent::Tag::scramblingStatus>(
                        ScramblingStatus::NOT_SCRAMBLED);
                auto event =
                        DemuxFilterEvent::make<DemuxFilterEvent::Tag::monitorEvent>(monitorEvent);
                mCallbackScheduler.onFilterEvent(std::move(event));
            } else {
                return ::ndk::ScopedAStatus::fromServiceSpecificError(
                        static_cast<int32_t>(Result::INVALID_STATE));
            }
        }
    }

    // if ip cid monitoring flipped, record the new state and send msg on enabling
    if (newIpCid ^ mIpCidMonitored) {
        mIpCidMonitored = newIpCid;
        if (mIpCidMonitored) {
            if (mCallbackScheduler.hasCallbackRegistered()) {
                // Return random cid
                auto monitorEvent =
                        DemuxFilterMonitorEvent::make<DemuxFilterMonitorEvent::Tag::cid>(1);
                auto event =
                        DemuxFilterEvent::make<DemuxFilterEvent::Tag::monitorEvent>(monitorEvent);
                mCallbackScheduler.onFilterEvent(std::move(event));
            } else {
                return ::ndk::ScopedAStatus::fromServiceSpecificError(
                        static_cast<int32_t>(Result::INVALID_STATE));
            }
        }
    }

    return ::ndk::ScopedAStatus::ok();
}

bool Filter::createFilterMQ() {
    ALOGV("%s", __FUNCTION__);

    // Create a synchronized FMQ that supports blocking read/write
    std::unique_ptr<FilterMQ> tmpFilterMQ =
            std::unique_ptr<FilterMQ>(new (std::nothrow) FilterMQ(mBufferSize, true));
    if (!tmpFilterMQ->isValid()) {
        ALOGW("[Filter] Failed to create FMQ of filter with id: %" PRIu64, mFilterId);
        return false;
    }

    mFilterMQ = std::move(tmpFilterMQ);

    if (EventFlag::createEventFlag(mFilterMQ->getEventFlagWord(), &mFilterEventsFlag) !=
        ::android::OK) {
        return false;
    }

    return true;
}

::ndk::ScopedAStatus Filter::startFilterLoop() {
    mFilterThread = std::thread(&Filter::filterThreadLoop, this);
    return ::ndk::ScopedAStatus::ok();
}

void Filter::filterThreadLoop() {
    if (!mFilterThreadRunning) {
        return;
    }

    ALOGD("[Filter] filter %" PRIu64 " threadLoop start.", mFilterId);

    // For the first time of filter output, implementation needs to send the filter
    // Event Callback without waiting for the DATA_CONSUMED to init the process.
    while (mFilterThreadRunning) {
        std::unique_lock<std::mutex> lock(mFilterEventsLock);
        if (mFilterEvents.size() == 0) {
            lock.unlock();
            if (DEBUG_FILTER) {
                ALOGD("[Filter] wait for filter data output.");
            }
            usleep(1000 * 1000);
            continue;
        }

        // After successfully write, send a callback and wait for the read to be done
        if (mCallbackScheduler.hasCallbackRegistered()) {
            if (mConfigured) {
                auto startEvent =
                        DemuxFilterEvent::make<DemuxFilterEvent::Tag::startId>(mStartId++);
                mCallbackScheduler.onFilterEvent(std::move(startEvent));
                mConfigured = false;
            }

            // lock is still being held
            for (auto&& event : mFilterEvents) {
                mCallbackScheduler.onFilterEvent(std::move(event));
            }
        } else {
            ALOGD("[Filter] filter callback is not configured yet.");
            mFilterThreadRunning = false;
            return;
        }

        mFilterEvents.clear();
        mFilterStatus = DemuxFilterStatus::DATA_READY;
        mCallbackScheduler.onFilterStatus(mFilterStatus);
        //break;
    }

    while (mFilterThreadRunning) {
        uint32_t efState = 0;
        // We do not wait for the last round of written data to be read to finish the thread
        // because the VTS can verify the reading itself.
        for (int i = 0; i < SECTION_WRITE_COUNT; i++) {
            if (!mFilterThreadRunning) {
                break;
            }
            while (mFilterThreadRunning && mIsUsingFMQ) {
                ::android::status_t status = mFilterEventsFlag->wait(
                        static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_CONSUMED), &efState,
                        WAIT_TIMEOUT, true /* retry on spurious wake */);
                if (status != ::android::OK) {
                    ALOGD("[Filter] wait for data consumed");
                    continue;
                }
                break;
            }

            maySendFilterStatusCallback();

            while (mFilterThreadRunning) {
                std::lock_guard<std::mutex> lock(mFilterEventsLock);
                if (mFilterEvents.size() == 0) {
                    continue;
                }
                // After successfully write, send a callback and wait for the read to be done
                for (auto&& event : mFilterEvents) {
                    mCallbackScheduler.onFilterEvent(std::move(event));
                }
                mFilterEvents.clear();
                break;
            }
            // We do not wait for the last read to be done
            // VTS can verify the read result itself.
            if (i == SECTION_WRITE_COUNT - 1) {
                ALOGD("[Filter] filter %" PRIu64 " writing done. Ending thread", mFilterId);
                break;
            }
        }
        break;
    }
    ALOGD("[Filter] filter thread ended.");
}

void Filter::freeSharedAvHandle() {
    if (!mIsMediaFilter) {
        return;
    }
    native_handle_close(mSharedAvMemHandle);
    native_handle_delete(mSharedAvMemHandle);
    mSharedAvMemHandle = nullptr;
}

binder_status_t Filter::dump(int fd, const char** /* args */, uint32_t /* numArgs */) {
    dprintf(fd, "    Filter %" PRIu64 ":\n", mFilterId);
    dprintf(fd, "      Main type: %d\n", mType.mainType);
    dprintf(fd, "      mIsMediaFilter: %d\n", mIsMediaFilter);
    dprintf(fd, "      mIsPcrFilter: %d\n", mIsPcrFilter);
    dprintf(fd, "      mIsRecordFilter: %d\n", mIsRecordFilter);
    dprintf(fd, "      mIsUsingFMQ: %d\n", mIsUsingFMQ);
    dprintf(fd, "      mFilterThreadRunning: %d\n", (bool)mFilterThreadRunning);
    return STATUS_OK;
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
        mCallbackScheduler.onFilterStatus(newStatus);
        mFilterStatus = newStatus;
    }
}

DemuxFilterStatus Filter::checkFilterStatusChange(uint32_t availableToWrite,
                                                  uint32_t availableToRead, uint32_t highThreshold,
                                                  uint32_t lowThreshold) {
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
    return mTpid;
}

bool Filter::postFilteredEmmSection(vector<uint8_t> data)
{
    int tIdFlagCount = 0;
    int tIdMatchedIndex = 0xFF;
    bool addBuf = false;
    uint8_t tableIdFlag = 0xFF;

    // mode [0..1] emm pid
    // mode [2..17] table IDs
    // mode [18..33] table ID Flags
   if (mFilterSettings.get<DemuxFilterSettings::ts>().filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.getTag() ==
        DemuxFilterSectionSettingsCondition::sectionBits) {
        for (int flagIndex = 18; flagIndex < 18 + 16;
             flagIndex++) { // mode 0..1 is reserved for emm pid
            if (mFilterSettings.get<DemuxFilterSettings::ts>().filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mode
                    [flagIndex]
                != 0) { // count flags
                tIdFlagCount++;
            }
        }

        for (int tidIndex = 2; tidIndex < 2 + 16; tidIndex++) {
            if (mFilterSettings.get<DemuxFilterSettings::ts>().filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mode[tidIndex]
                == data[0]) {
                tIdMatchedIndex = tidIndex;
                tableIdFlag = mFilterSettings.get<DemuxFilterSettings::ts>().filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>()
                                  .mode[tidIndex + 16];
            }
        }

        if (tIdMatchedIndex == 0xFF) {
            if (tIdFlagCount == 0) { // ignore table Id map
                ALOGD("Table ID array would be ignored\n");
                addBuf = true;
            } else {
                ALOGD("No allocation information for table ID 0x%02X\n", data[0]);
                addBuf = false;
            }
        } else { // Find
            ALOGD(
                "Found matched table ID index = %d, FlagIndex = %d, ID = 0x%02X", tIdMatchedIndex,
                tIdMatchedIndex + 16,
                mFilterSettings.get<DemuxFilterSettings::ts>().filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mode
                    [tIdMatchedIndex]);

            vector<uint8_t> filterAddressMap =
                mFilterSettings.get<DemuxFilterSettings::ts>().filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().filter;
            vector<uint8_t> filterAddressMask =
                mFilterSettings.get<DemuxFilterSettings::ts>().filterSettings.get<DemuxTsFilterSettingsFilterSettings::section>().condition.get<DemuxFilterSectionSettingsCondition::sectionBits>().mask;

            ALOGD("Table ID = 0x%02X, flag = 0x%02X", data[0], tableIdFlag);
            switch (tableIdFlag) {
            case NDS_EMM_DISABLE_TID: //  ignore this section
                // do nothing
                ALOGI("NDS_EMM_DISABLE_TID, Discard the section");
                addBuf = false;
                break;

            case NDS_EMM_ENABLE_TID: //  table ID filtering
                // post this section to emm buf
                ALOGI("NDS_EMM_ENABLE_TID, Post the section");
                addBuf = true;
                break;

            case NDS_EMM_ENABLE_TID_NDS: //  NDS proprietary filtering
            {
                uint8_t idx_filter_def, addr_idx;
                uint8_t label_type = (data[3] & 0xC0) >> 6;
                uint8_t num_of_packet = ((data[3] & 0x30) >> 4) + 1;

                ALOGD("label_type: 0x%X\n", label_type);
                ALOGD("num of address: %d\n", num_of_packet);

                /* General Addressing: Accept EMM. */
                if (label_type == 0) {
                    ALOGI("label: General Addressing, accept EMM, post the section");
                    addBuf = true;
                    break;
                }

                // for (i = 0; i < 8; i++) {     //from NSK2 Harmonizer, it will
                // look for the same label first. For a new android
                // DemuxfilterSetting with special Magic value, it is assumed to
                // use fixed label in order.
                idx_filter_def = label_type - 1;
                {
                    ALOGD("filter def [%d]", idx_filter_def + 1);
                    /*
                                            NSK2HDX_EMM_FILTER_DEF *filter_def =
                                                &(emmDevInfo->filter_allocation.emmfl.filter_def[i]);

                                            if (filter_def->filter_type != label_type) {
                                                continue;
                                            }
                    */
                    for (addr_idx = 0; addr_idx < num_of_packet; addr_idx++) {
                        uint8_t size_address = 4;
                        uint8_t addressing_mode = (data[3] & 0x08) >> 3;

                        /* ID addressing */
                        if (addressing_mode == 0) {
                            int idx;
                            uint8_t address_ca[4], address_requested[4];

                            for (idx = 0; idx < 4; idx++) {
                                address_ca[idx] = filterAddressMask[idx_filter_def * 4 + idx]
                                    & data[4 + idx + addr_idx * size_address];
                                address_requested[idx] = filterAddressMask[idx_filter_def * 4 + idx]
                                    & filterAddressMap[idx_filter_def * 4 + idx];
                            }

                            if (memcmp(address_ca, address_requested, sizeof(uint8_t) * 4) == 0) {
                                ALOGI("Address mode: 0x%X\n", addressing_mode);
                                ALOGD(
                                    "ca address: %02x %02x %02x %02x\n", address_ca[0],
                                    address_ca[1], address_ca[2], address_ca[3]);
                                ALOGD(
                                    "Reg Data: %02x %02x %02x %02x\n", address_requested[0],
                                    address_requested[1], address_requested[2],
                                    address_requested[3]);

                                ALOGI("ID addressing matched, post the section");

                                addBuf = true;
                                break;
                            }
                        }
                        /* Bitmap addressing */
                        else if (addressing_mode == 1) {
                            int idx;
                            uint8_t address_ca[4];

                            for (idx = 0; idx < 4; idx++) {
                                address_ca[idx] = filterAddressMap[idx_filter_def * 4 + idx]
                                    & data[4 + idx + addr_idx * size_address];
                            }

                            if (*(uint32_t *)address_ca != 0) {
                                ALOGI("Address mode: 0x%X\n", addressing_mode);
                                ALOGD(
                                    "ca address: %02x %02x %02x %02x\n", address_ca[0],
                                    address_ca[1], address_ca[2], address_ca[3]);
                                ALOGI("Bitmap addressing Matched: post the section");
                                addBuf = true;
                                break;
                            }
                        }
                    }
                }
                if (!addBuf) {
                    ALOGI("Not found matched address map, ignored");
                }
                break;
            }
            }
        }
    }

    return addBuf;
}

void Filter::updateFilterOutput(vector<int8_t>& data) {
    std::lock_guard<std::mutex> lock(mFilterOutputLock);
    if (DEBUG_FILTER) {
        ALOGD(
            "%s/%d mFilterId:%lld data size:%d output size:%dKB", __FUNCTION__, __LINE__, mFilterId,
            data.size(), mFilterOutput.size() / 1024);
    }

    if (mIsNSKEmmFilter) {
        char str_buf[64];
        char *buf_ptr = str_buf;
        enum BUF_LEN
        {
            BUF_LEN = 64
        };

        ALOGD("Filtered EMM %d bytes", data.size());

        for (int idx = 0; idx < data.size(); idx++) {
            if (idx % 16 == 0) {
                buf_ptr = str_buf;
                buf_ptr += sprintf(str_buf, "%04X: ", idx);
            }

            buf_ptr += sprintf(buf_ptr, "%02x ", data[idx]);

            if (idx % 16 == 15 || idx >= data.size() - 1) {
                ALOGD("%s\n", str_buf);
            }
        }
        vector<uint8_t> udata;
        udata.resize(data.size());
        memcpy(udata.data(), data.data(), data.size() * sizeof(uint8_t));
        if (!postFilteredEmmSection(udata)) {
            return;
        }
    }

    mFilterOutput.insert(mFilterOutput.end(), data.begin(), data.end());
}

void Filter::updatePts(uint64_t pts) {
    std::lock_guard<std::mutex> lock(mFilterOutputLock);
    mPts = pts;
}

void Filter::updateRecordOutput(vector<int8_t>& data) {
    std::lock_guard<std::mutex> lock(mRecordFilterOutputLock);
    mRecordFilterOutput.insert(mRecordFilterOutput.end(), data.begin(), data.end());
}

::ndk::ScopedAStatus Filter::startFilterHandler() {
    std::lock_guard<std::mutex> lock(mFilterOutputLock);
    switch (mType.mainType) {
        case DemuxFilterMainType::TS:
            switch (mType.subType.get<DemuxFilterSubType::Tag::tsFilterType>()) {
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
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::startSectionFilterHandler() {
    if (mFilterOutput.empty()) {
        return ::ndk::ScopedAStatus::ok();
    }
    if (!writeSectionsAndCreateEvent(mFilterOutput)) {
        ALOGD("[Filter] filter %" PRIu64 " fails to write into FMQ. Ending thread", mFilterId);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNKNOWN_ERROR));
    }

    mFilterOutput.clear();

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::startPesFilterHandler() {
    if (mFilterOutput.empty()) {
        return ::ndk::ScopedAStatus::ok();
    }
    /*
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
        vector<int8_t>::const_iterator first = mFilterOutput.begin() + i + 4;
        vector<int8_t>::const_iterator last = mFilterOutput.begin() + i + 4 + endPoint;
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
            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Result::INVALID_ARGUMENT));
        }
        maySendFilterStatusCallback();
        DemuxFilterPesEvent pesEvent;
        pesEvent = {
                // temp dump meta data
                .streamId = static_cast<int32_t>(mPesOutput[3]),
                .dataLength = static_cast<int32_t>(mPesOutput.size()),
        };
        if (DEBUG_FILTER) {
            ALOGD("[Filter] assembled pes data length %d", pesEvent.dataLength);
        }

        {
            std::lock_guard<std::mutex> lock(mFilterEventsLock);
            mFilterEvents.push_back(DemuxFilterEvent::make<DemuxFilterEvent::Tag::pes>(pesEvent));
        }

        mPesOutput.clear();
    }*/
    // size match then create event
    if (!writeDataToFilterMQ(mFilterOutput)) {
        ALOGD("[Filter] pes data write failed");
        mFilterOutput.clear();
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_ARGUMENT));
    }
    maySendFilterStatusCallback();
    DemuxFilterPesEvent pesEvent;
    pesEvent = {
            // temp dump meta data
            .streamId = static_cast<int32_t>(mFilterOutput[3]),
            .dataLength = static_cast<int32_t>(mFilterOutput.size()),
    };
    if (DEBUG_FILTER) {
        ALOGD("[Filter] assembled pes data length %d", pesEvent.dataLength);
    }

    {
        std::lock_guard<std::mutex> lock(mFilterEventsLock);
        mFilterEvents.push_back(DemuxFilterEvent::make<DemuxFilterEvent::Tag::pes>(pesEvent));
    }

    mFilterOutput.clear();

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::startTsFilterHandler() {
    // TODO handle starting TS filter
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::startMediaFilterHandler() {
    if (mFilterOutput.empty()) {
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus result;
    if (mPts) {
        result = createMediaFilterEventWithIon(mFilterOutput);
        if (result.isOk()) {
            mFilterOutput.clear();
        }
        return result;
    }

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
        vector<int8_t>::const_iterator first = mFilterOutput.begin() + i + 4;
        vector<int8_t>::const_iterator last = mFilterOutput.begin() + i + 4 + endPoint;
        mPesOutput.insert(mPesOutput.end(), first, last);
        // size does not match then continue
        mPesSizeLeft -= endPoint;
        if (DEBUG_FILTER) {
            ALOGD("[Filter] pes data left %d", mPesSizeLeft);
        }
        if (mPesSizeLeft > 0 || mAvBufferCopyCount++ < 10) {
            continue;
        }

        result = createMediaFilterEventWithIon(mPesOutput);
        if (result.isOk()) {
            return result;
        }
    }

    mFilterOutput.clear();

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::createMediaFilterEventWithIon(vector<int8_t>& output) {
    if (mUsingSharedAvMem) {
        if (mSharedAvMemHandle == nullptr) {
            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Result::UNKNOWN_ERROR));
        }
        return createShareMemMediaEvents(output);
    }

    return createIndependentMediaEvents(output);
}

::ndk::ScopedAStatus Filter::startRecordFilterHandler() {
    std::lock_guard<std::mutex> lock(mRecordFilterOutputLock);
    if (mRecordFilterOutput.empty()) {
        return ::ndk::ScopedAStatus::ok();
    }

    if (mDvr == nullptr || !mDvr->writeRecordFMQ(mRecordFilterOutput)) {
        ALOGD("[Filter] dvr fails to write into record FMQ.");
        mRecordFilterOutput.clear();
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNKNOWN_ERROR));
    }

    DemuxFilterTsRecordEvent recordEvent;
    DemuxPid demuxPid;
    demuxPid.set<DemuxPid::tPid>(static_cast<int32_t>(mTpid));
    recordEvent = {
            .pid = demuxPid,
            .byteNumber = static_cast<int64_t>(mRecordFilterOutput.size()),
            .pts = (mPts == 0) ? static_cast<int64_t>(time(NULL)) * 900000 : mPts,
            .firstMbInSlice = 0,  // random address
    };

    {
        std::lock_guard<std::mutex> lock(mFilterEventsLock);
        mFilterEvents.push_back(
                DemuxFilterEvent::make<DemuxFilterEvent::Tag::tsRecord>(recordEvent));
    }

    mRecordFilterOutput.clear();
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::startPcrFilterHandler() {
    // TODO handle starting PCR filter
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::startTemiFilterHandler() {
    // TODO handle starting TEMI filter
    return ::ndk::ScopedAStatus::ok();
}

bool Filter::writeSectionsAndCreateEvent(vector<int8_t>& data) {
    // TODO check how many sections has been read
    ALOGD("[Filter] section handler");
    if (!writeDataToFilterMQ(data)) {
        return false;
    }
    DemuxFilterSectionEvent secEvent;
    secEvent = {
            // temp dump meta data
            .tableId = data[0],
            .version = static_cast<uint16_t>((data[5] >> 1) & 0x1f),
            .sectionNum = data[6],
            .dataLength = static_cast<int32_t>(data.size()),
    };

    {
        std::lock_guard<std::mutex> lock(mFilterEventsLock);
        mFilterEvents.push_back(DemuxFilterEvent::make<DemuxFilterEvent::Tag::section>(secEvent));
    }

    return true;
}

bool Filter::writeDataToFilterMQ(const std::vector<int8_t>& data) {
    std::lock_guard<std::mutex> lock(mWriteLock);
    if (mFilterMQ.get() != NULL && mFilterMQ->write(data.data(), data.size())) {
        return true;
    }
    return false;
}

void Filter::attachFilterToRecord(const std::shared_ptr<Dvr> dvr) {
    mDvr = dvr;
}

void Filter::detachFilterFromRecord() {
    mDvr = nullptr;
}

int Filter::createAvIonFd(int size) {
    // Create a DMA-BUF fd and allocate an av fd mapped to a buffer to it.
    auto buffer_allocator = std::make_unique<BufferAllocator>();
    if (!buffer_allocator) {
        ALOGE("[Filter] Unable to create BufferAllocator object");
        return -1;
    }
    int av_fd = -1;
    av_fd = buffer_allocator->Alloc("system-uncached", size);
    if (av_fd < 0) {
        ALOGE("[Filter] Failed to create av fd %d", errno);
        return -1;
    }
    return av_fd;
}

uint8_t* Filter::getIonBuffer(int fd, int size) {
    uint8_t* avBuf = static_cast<uint8_t*>(
            mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 /*offset*/));
    if (avBuf == MAP_FAILED) {
        ALOGE("[Filter] fail to allocate buffer %d", errno);
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
    return nativeHandle;
}

::ndk::ScopedAStatus Filter::createIndependentMediaEvents(vector<int8_t>& output) {
    int av_fd = createAvIonFd(output.size());
    if (av_fd == -1) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNKNOWN_ERROR));
    }
    // copy the filtered data to the buffer
    uint8_t* avBuffer = getIonBuffer(av_fd, output.size());
    if (avBuffer == NULL) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNKNOWN_ERROR));
    }
    memcpy(avBuffer, output.data(), output.size() * sizeof(uint8_t));

    native_handle_t* nativeHandle = createNativeHandle(av_fd);
    if (nativeHandle == NULL) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNKNOWN_ERROR));
    }

    // Create a dataId and add a <dataId, av_fd> pair into the dataId2Avfd map
    uint64_t dataId = mLastUsedDataId++ /*createdUID*/;
    mDataId2Avfd[dataId] = dup(av_fd);

    // Create mediaEvent and send callback
    auto event = DemuxFilterEvent::make<DemuxFilterEvent::Tag::media>();
    auto& mediaEvent = event.get<DemuxFilterEvent::Tag::media>();
    mediaEvent.avMemory = ::android::dupToAidl(nativeHandle);
    mediaEvent.dataLength = static_cast<int64_t>(output.size());
    mediaEvent.avDataId = static_cast<int64_t>(dataId);
    if (mPts) {
        mediaEvent.pts = mPts;
        mPts = 0;
    }

    {
        std::lock_guard<std::mutex> lock(mFilterEventsLock);
        mFilterEvents.push_back(std::move(event));
    }

    // Clear and log
    native_handle_close(nativeHandle);
    native_handle_delete(nativeHandle);
    output.clear();
    mAvBufferCopyCount = 0;
    if (DEBUG_FILTER) {
        ALOGD("[Filter] av data length %d", static_cast<int32_t>(output.size()));
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Filter::createShareMemMediaEvents(vector<int8_t>& output) {
    // copy the filtered data to the shared buffer
    uint8_t* sharedAvBuffer =
            getIonBuffer(mSharedAvMemHandle->data[0], output.size() + mSharedAvMemOffset);
    if (sharedAvBuffer == NULL) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNKNOWN_ERROR));
    }
    memcpy(sharedAvBuffer + mSharedAvMemOffset, output.data(), output.size() * sizeof(uint8_t));

    // Create a memory handle with numFds == 0
    native_handle_t* nativeHandle = createNativeHandle(-1);
    if (nativeHandle == NULL) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::UNKNOWN_ERROR));
    }

    // Create mediaEvent and send callback
    auto event = DemuxFilterEvent::make<DemuxFilterEvent::Tag::media>();
    auto& mediaEvent = event.get<DemuxFilterEvent::Tag::media>();
    mediaEvent.avMemory = ::android::dupToAidl(nativeHandle);
    mediaEvent.offset = mSharedAvMemOffset;
    mediaEvent.dataLength = static_cast<int64_t>(output.size());
    if (mPts) {
        mediaEvent.pts = mPts;
        mPts = 0;
    }

    {
        std::lock_guard<std::mutex> lock(mFilterEventsLock);
        mFilterEvents.push_back(std::move(event));
    }

    mSharedAvMemOffset += output.size();

    // Clear and log
    native_handle_close(nativeHandle);
    native_handle_delete(nativeHandle);
    output.clear();
    if (DEBUG_FILTER) {
        ALOGD("[Filter] shared av data length %d", static_cast<int32_t>(output.size()));
    }
    return ::ndk::ScopedAStatus::ok();
}

bool Filter::sameFile(int fd1, int fd2) {
    struct stat stat1, stat2;
    if (fstat(fd1, &stat1) < 0 || fstat(fd2, &stat2) < 0) {
        return false;
    }
    return (stat1.st_dev == stat2.st_dev) && (stat1.st_ino == stat2.st_ino);
}

void Filter::createMediaEvent(vector<DemuxFilterEvent>& events) {
    AudioExtraMetaData audio;
    audio.adFade = 1;
    audio.adPan = 2;
    audio.versionTextTag = 3;
    audio.adGainCenter = 4;
    audio.adGainFront = 5;
    audio.adGainSurround = 6;

    DemuxFilterMediaEvent mediaEvent;
    mediaEvent.streamId = 1;
    mediaEvent.isPtsPresent = true;
    mediaEvent.isDtsPresent = false;
    mediaEvent.dataLength = 3;
    mediaEvent.offset = 4;
    mediaEvent.isSecureMemory = true;
    mediaEvent.mpuSequenceNumber = 6;
    mediaEvent.isPesPrivateData = true;
    mediaEvent.extraMetaData.set<DemuxFilterMediaEventExtraMetaData::Tag::audio>(audio);

    int av_fd = createAvIonFd(BUFFER_SIZE_16M);
    if (av_fd == -1) {
        return;
    }

    native_handle_t* nativeHandle = createNativeHandle(av_fd);
    if (nativeHandle == nullptr) {
        ::close(av_fd);
        ALOGE("[Filter] Failed to create native_handle %d", errno);
        return;
    }

    // Create a dataId and add a <dataId, av_fd> pair into the dataId2Avfd map
    uint64_t dataId = mLastUsedDataId++ /*createdUID*/;
    mDataId2Avfd[dataId] = dup(av_fd);

    mediaEvent.avDataId = static_cast<int64_t>(dataId);
    mediaEvent.avMemory = ::android::dupToAidl(nativeHandle);

    events.push_back(DemuxFilterEvent::make<DemuxFilterEvent::Tag::media>(std::move(mediaEvent)));

    native_handle_close(nativeHandle);
    native_handle_delete(nativeHandle);
}

void Filter::createTsRecordEvent(vector<DemuxFilterEvent>& events) {
    DemuxPid pid;
    DemuxFilterScIndexMask mask;
    DemuxFilterTsRecordEvent tsRecord1;
    pid.set<DemuxPid::Tag::tPid>(1);
    mask.set<DemuxFilterScIndexMask::Tag::scIndex>(1);
    tsRecord1.pid = pid;
    tsRecord1.tsIndexMask = 1;
    tsRecord1.scIndexMask = mask;
    tsRecord1.byteNumber = 2;

    DemuxFilterTsRecordEvent tsRecord2;
    tsRecord2.pts = 1;
    tsRecord2.firstMbInSlice = 2;  // random address

    events.push_back(DemuxFilterEvent::make<DemuxFilterEvent::Tag::tsRecord>(std::move(tsRecord1)));
    events.push_back(DemuxFilterEvent::make<DemuxFilterEvent::Tag::tsRecord>(std::move(tsRecord2)));
}

void Filter::createMmtpRecordEvent(vector<DemuxFilterEvent>& events) {
    DemuxFilterMmtpRecordEvent mmtpRecord1;
    mmtpRecord1.scHevcIndexMask = 1;
    mmtpRecord1.byteNumber = 2;

    DemuxFilterMmtpRecordEvent mmtpRecord2;
    mmtpRecord2.pts = 1;
    mmtpRecord2.mpuSequenceNumber = 2;
    mmtpRecord2.firstMbInSlice = 3;
    mmtpRecord2.tsIndexMask = 4;

    events.push_back(
            DemuxFilterEvent::make<DemuxFilterEvent::Tag::mmtpRecord>(std::move(mmtpRecord1)));
    events.push_back(
            DemuxFilterEvent::make<DemuxFilterEvent::Tag::mmtpRecord>(std::move(mmtpRecord2)));
}

void Filter::createSectionEvent(vector<DemuxFilterEvent>& events) {
    DemuxFilterSectionEvent section;
    section.tableId = 1;
    section.version = 2;
    section.sectionNum = 3;
    section.dataLength = 0;

    events.push_back(DemuxFilterEvent::make<DemuxFilterEvent::Tag::section>(std::move(section)));
}

void Filter::createPesEvent(vector<DemuxFilterEvent>& events) {
    DemuxFilterPesEvent pes;
    pes.streamId = 1;
    pes.dataLength = 1;
    pes.mpuSequenceNumber = 2;

    events.push_back(DemuxFilterEvent::make<DemuxFilterEvent::Tag::pes>(std::move(pes)));
}

void Filter::createDownloadEvent(vector<DemuxFilterEvent>& events) {
    DemuxFilterDownloadEvent download;
    download.itemId = 1;
    download.downloadId = 1;
    download.mpuSequenceNumber = 2;
    download.itemFragmentIndex = 3;
    download.lastItemFragmentIndex = 4;
    download.dataLength = 0;

    events.push_back(DemuxFilterEvent::make<DemuxFilterEvent::Tag::download>(std::move(download)));
}

void Filter::createIpPayloadEvent(vector<DemuxFilterEvent>& events) {
    DemuxFilterIpPayloadEvent ipPayload;
    ipPayload.dataLength = 0;

    events.push_back(
            DemuxFilterEvent::make<DemuxFilterEvent::Tag::ipPayload>(std::move(ipPayload)));
}

void Filter::createTemiEvent(vector<DemuxFilterEvent>& events) {
    DemuxFilterTemiEvent temi;
    temi.pts = 1;
    temi.descrTag = 2;
    temi.descrData = {3};

    events.push_back(DemuxFilterEvent::make<DemuxFilterEvent::Tag::temi>(std::move(temi)));
}

void Filter::createMonitorEvent(vector<DemuxFilterEvent>& events) {
    DemuxFilterMonitorEvent monitor;
    monitor.set<DemuxFilterMonitorEvent::Tag::scramblingStatus>(ScramblingStatus::SCRAMBLED);

    events.push_back(
            DemuxFilterEvent::make<DemuxFilterEvent::Tag::monitorEvent>(std::move(monitor)));
}

void Filter::createRestartEvent(vector<DemuxFilterEvent>& events) {
    events.push_back(DemuxFilterEvent::make<DemuxFilterEvent::Tag::startId>(1));
}

bool Filter::isRawData() {
    return bIsRaw;
}

DemuxFilterType Filter::getFilterType() {
    return mType;
}

}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
