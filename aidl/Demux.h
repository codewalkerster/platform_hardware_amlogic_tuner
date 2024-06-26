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

#pragma once

#include <aidl/android/hardware/tv/tuner/BnDemux.h>

#include <fmq/AidlMessageQueue.h>
#include <math.h>
#include <atomic>
#include <set>
#include <thread>

#include "Descrambler.h"
#include "Dvr.h"
#include "Filter.h"
#include "Frontend.h"
#include "TimeFilter.h"
#include "Tuner.h"
#include "AmDmx.h"
#include "MediaSyncWrap.h"
#include "AmDvr.h"
#include "AmPesFilter.h"
#include "HwDemuxSCWrap.h"
//#include "AmTsIndexer.h"
#include "AmCI.h"
using namespace std;

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

using ::aidl::android::hardware::common::fmq::MQDescriptor;
using ::aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using ::android::AidlMessageQueue;
using ::android::hardware::EventFlag;

using FilterMQ = AidlMessageQueue<int8_t, SynchronizedReadWrite>;
#define DMX_COUNT (6)
#define DVR_BUFFER_LEN    (20*188*1024)
#define DVR_MAX_PUSI_LEN  (12*188*1024)

extern "C" {
#include "dvr_playback.h"
extern size_t dvr_playback_write(DVR_PlaybackHandle_t handle, uint8_t *data, size_t len);
}

class Dvr;
class Filter;
class Frontend;
class TimeFilter;
class Tuner;
class Descrambler;

class Demux : public BnDemux {
  public:
#if PLATFORM_SDK_VERSION > 33
    Demux(int32_t demuxId, uint32_t filterTypes);
#else
    Demux(int32_t demuxId, std::shared_ptr<Tuner> tuner);
#endif
    ~Demux();

    ::ndk::ScopedAStatus setFrontendDataSource(int32_t in_frontendId) override;
    ::ndk::ScopedAStatus openFilter(const DemuxFilterType& in_type, int32_t in_bufferSize,
                                    const std::shared_ptr<IFilterCallback>& in_cb,
                                    std::shared_ptr<IFilter>* _aidl_return) override;
    ::ndk::ScopedAStatus openTimeFilter(std::shared_ptr<ITimeFilter>* _aidl_return) override;
    ::ndk::ScopedAStatus getAvSyncHwId(const std::shared_ptr<IFilter>& in_filter,
                                       int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getAvSyncTime(int32_t in_avSyncHwId, int64_t* _aidl_return) override;
    ::ndk::ScopedAStatus close() override;
    ::ndk::ScopedAStatus openDvr(DvrType in_type, int32_t in_bufferSize,
                                 const std::shared_ptr<IDvrCallback>& in_cb,
                                 std::shared_ptr<IDvr>* _aidl_return) override;
    ::ndk::ScopedAStatus connectCiCam(int32_t in_ciCamId) override;
    ::ndk::ScopedAStatus disconnectCiCam() override;

    binder_status_t dump(int fd, const char** args, uint32_t numArgs) override;

    // Functions interacts with Tuner Service
    void stopFrontendInput();
    ::ndk::ScopedAStatus removeFilter(int64_t filterId);
    bool attachRecordFilter(int64_t filterId);
    bool detachRecordFilter(int64_t filterId);
    void attachDescrambler(int32_t descramblerId, std::shared_ptr<Descrambler> descrambler);
    void detachDescrambler(int32_t descramblerId);
    ::ndk::ScopedAStatus startFilterHandler(int64_t filterId);
    void updateFilterOutput(int64_t filterId, vector<int8_t> data);
    void updateMediaFilterOutput(int64_t filterId, vector<int8_t> data, uint64_t pts);
    uint16_t getFilterTpid(int64_t filterId);
    void setIsRecording(bool isRecording);
    bool isRecording();
    void startFrontendInputLoop();

    /**
     * A dispatcher to read and dispatch input data to all the started filters.
     * Each filter handler handles the data filtering/output writing/filterEvent updating.
     * Note that recording filters are not included.
     */
    bool startBroadcastFilterDispatcher();
    void startBroadcastTsFilter(vector<int8_t> data);
    void notifyDvrFlushed();

    void sendFrontendInputToRecord(vector<int8_t> data);
    void sendFrontendInputToRecord(vector<int8_t> data, uint16_t pid, uint64_t pts);
    void sendFrontendInputToRecord(vector<int8_t> data, uint16_t pid, uint64_t offset, uint64_t pts = 0, int iFrameIndex =
    0, int pusiIndex = 0);
    bool startRecordFilterDispatcher();
#if PLATFORM_SDK_VERSION > 33
    void getDemuxInfo(DemuxInfo* demuxInfo);
    bool isInUse();
    void setInUse(bool inUse);
    void setTunerService(std::shared_ptr<Tuner> tuner);
#endif

    static void postData(void* demux, int fid, bool esOutput, bool passthrough);
    static void pesDataCallback(void* demux, int fid, uint8_t *pes, int len);
    //static void TsIndexerCallback(TS_Indexer_t *ts_indexer, TS_Indexer_Event_t *event);
    sp<AM_DMX_Device> getAmDmxDevice();
    //sp<AmDvr> getAmDvrDevice();
    sp<AmPesFilter> getAmPesFilter();
    int getPesFid();
    void combinePesData(int64_t filterId);
    void getSectionData(int64_t filterId);
    void getPesRawData(int64_t filterId);
    bool checkPesFilterId(int64_t filterId);
    bool checkTemiFilterId(int64_t filterId);
    bool isRawData(int64_t filterId);
    bool checkSoftDemuxForSubtitle();
    void getTemiData(int64_t filterId);
    int recordTsPacketForPesData(int64_t filterId);
    int64_t findFilterIdByfakeFilterId(int64_t fakefilterId);
    void destroyMediaSync();
    void closePesRecordFilter();
    int32_t getDemuxId();
    uint64_t getVideoFid();
    int getRecordVideoPid();
    int getRecordAudioPid();
    int recordTsPacketForTemiData(int64_t filterId);
    void closeTemiRecordFilter();
    int getTemiFid();
    bool checkSoftDemuxForTemi();
    int getTsInput();

    void setRecordHandle(DVR_RecordHandle_t handle) { mRecordHandle = handle; }
    DVR_RecordHandle_t getRecordHandle() { return mRecordHandle; }
    void setPlaybackHandle (DVR_PlaybackHandle_t handle) { mPlaybackhandle = handle; }
    DVR_PlaybackHandle_t getPlaybackHandle() { return mPlaybackhandle; }
    bool checkDemuxPlayback() { return bDemuxUsePlayback; }
    bool checkDemuxRecord() { return bDemuxUseRecord; }
    bool checkCiCamInsert() { return bCiInsert; }
    std::shared_ptr<Descrambler> getDescrambler();

  private:
    // Tuner service
    std::shared_ptr<Tuner> mTuner;

    // Frontend source
    std::shared_ptr<Frontend> mFrontend;

    // A struct that passes the arguments to a newly created filter thread
    struct ThreadArgs {
        Demux* user;
        int64_t filterId;
    };

    static void* __threadLoopFrontend(void* user);
    void frontendInputThreadLoop();
    void subtitleRecordThreadLoop();
    void TemiRecordThreadLoop();

    /**
     * To create a FilterMQ with the next available Filter ID.
     * Creating Event Flag at the same time.
     * Add the successfully created/saved FilterMQ into the local list.
     *
     * Return false is any of the above processes fails.
     */
    void deleteEventFlag();
    bool readDataFromMQ();

    int32_t mDemuxId = -1;
    int32_t mCiCamId;
    set<int64_t> mPcrFilterIds;
    /**
     * Record the last used filter id. Initial value is -1.
     * Filter Id starts with 0.
     */
    int64_t mLastUsedFilterId = -1;
    set<int64_t> mTemiFilterIds;

    set<int64_t> mPesFilterIds;
    /**
     * Record all the used playback filter Ids.
     * Any removed filter id should be removed from this set.
     */
    set<int64_t> mPlaybackFilterIds;
    /**
     * Record all the attached record filter Ids.
     * Any removed filter id should be removed from this set.
     */
    set<int64_t> mRecordFilterIds;
    /**
     * A list of created Filter sp.
     * The array number is the filter ID.
     */
    std::map<int64_t, std::shared_ptr<Filter>> mFilters;

    /**
     * A list of created Descrambler sp.
     * The array number is the Descrambler ID.
     */
    std::map<int32_t, std::shared_ptr<Descrambler>> mDescramblers;
    vector<uint8_t> mScrambledCache;
    vector<uint8_t> mClearCache;

    /**
     * Local reference to the opened Timer Filter instance.
     */
    std::shared_ptr<TimeFilter> mTimeFilter;

    /**
     * Local reference to the opened DVR object.
     */
    std::shared_ptr<Dvr> mDvrPlayback;
    std::shared_ptr<Dvr> mDvrRecord;

    // Thread handlers
    std::thread mFrontendInputThread;

    /**
     * If a specific filter's writing loop is still running
     */
    std::atomic<bool> mFrontendInputThreadRunning;
    std::atomic<bool> mKeepFetchingDataFromFrontend;

    std::mutex mFilterLock;

    /**
     * If the dvr recording is running.
     */
    bool mIsRecording = false;
    /**
     * Lock to protect writes to the FMQs
     */
    std::mutex mWriteLock;

    // temp handle single PES filter
    // TODO handle mulptiple Pes filters
    int mPesSizeLeft = 0;
    vector<uint8_t> mPesOutput;

    const bool DEBUG_DEMUX = false;
#if PLATFORM_SDK_VERSION > 33
    int32_t mFilterTypes;
    bool mInUse = false;
#endif
    bool bSupportSoftDemuxForSubtitle = false;
    sp<AM_DMX_Device> AmDmxDevice[DMX_COUNT] = { NULL };
    //sp<AmDvr> mAmDvrDevice[DMX_COUNT]        = { NULL };
    sp<AmPesFilter> mAmPesFilter             = NULL;
    //sp<AmTsIndexer> mAmTsIndexer[DMX_COUNT]  = { NULL };
    sp<MediaSyncWrap> mMediaSync             = nullptr;
    int mPesFid = -1;
    int mPesRecordFid = -1;
    int64_t mAvSyncHwId = -1;

    // add stream speed control variable
    sp<HwDemuxOpsSCWrap> mHwDemuxOps[DMX_COUNT] = { NULL };
    void* mDemuxHandle[DMX_COUNT] = { NULL };
    uint64_t mWriteTsSize = 0;
    int mVidPid = 0x1FFF;
    int mAudPid = 0x1FFF;

    std::thread mTemiRecordThread;
    std::atomic<bool> mTemiRecordThreadRunning;
    int mTemiFid = -1;
    int mTemiRecordFid = -1;
    bool bSupportSoftDemuxForTemi = false;
    sp<AmCI> mAmCI[DMX_COUNT] = { NULL };
    DVR_RecordHandle_t mTemiRecHandle = NULL;
    DVR_RecordReceiveParams_t mTemiReceiveParam;

    DVR_RecordHandle_t mRecordHandle = NULL;
    DVR_PlaybackHandle_t mPlaybackhandle = NULL;
    DVR_RecordHandle_t mSubtitleRecHandle = NULL;
    std::atomic<bool> mSubtitleRecordThreadRunning;
    DVR_RecordReceiveParams_t mSubReceiveParam;
    std::thread mSubtitleRecordThread;

    bool bDemuxUsePlayback = false;
    bool bDemuxUseRecord   = false;
    bool bCiInsert   = false;
    std::shared_ptr<Descrambler> mDesc = nullptr;
};

}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
