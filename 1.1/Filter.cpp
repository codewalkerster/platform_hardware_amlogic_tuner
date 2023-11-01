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

#define LOG_TAG "android.hardware.tv.tuner@1.1-Filter"



#include <BufferAllocator/BufferAllocator.h>
#include <utils/Log.h>
#include <cutils/properties.h>
#include "Filter.h"
#include "Dmabufwrapper.h"

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

#define WAIT_TIMEOUT 3000000000
#define NDS_EMM_DISABLE_TID 0x00U
#define NDS_EMM_ENABLE_TID 0x01U
#define NDS_EMM_ENABLE_TID_NDS 0x02U

#define DEFAULT_PAGE_SIZE     4096

static int gFilterToken = 0;

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

static Return<Result>
GetNskEmmFltParam(const DemuxFilterSettings &settings, struct dmx_sct_filter_params &param)
{
    /*
    filter =
    [filter1_4b],[filter2_4b],[filter3_4b],[padding_00_22b]
    mask = [mask1_4b],[mask2_4b],[mask3_4b],[padding_00_22b]
    mode = [emm_pid_2b],[table_id_16b],[table_id_flag_16b]
    */
    if (settings.ts().filterSettings.section().condition.getDiscriminator()
        == DemuxFilterSectionSettings::Condition::hidl_discriminator::sectionBits) {
        ALOGD("NSK EMM Filter Descriptors");
        ALOGD(
            "Filter[%d]",
            settings.ts().filterSettings.section().condition.sectionBits().filter.size());
        dhexdump(
            settings.ts().filterSettings.section().condition.sectionBits().filter,
            settings.ts().filterSettings.section().condition.sectionBits().filter.size());
        ALOGD(
            "Mask[%d]", settings.ts().filterSettings.section().condition.sectionBits().mask.size());
        dhexdump(
            settings.ts().filterSettings.section().condition.sectionBits().mask,
            settings.ts().filterSettings.section().condition.sectionBits().mask.size());
        ALOGD(
            "mode[%d]", settings.ts().filterSettings.section().condition.sectionBits().mode.size());
        dhexdump(
            settings.ts().filterSettings.section().condition.sectionBits().mode,
            settings.ts().filterSettings.section().condition.sectionBits().filter.size());
    }

    // NSK Specific EMM Filter request
    if (settings.ts().filterSettings.section().condition.getDiscriminator()
        == DemuxFilterSectionSettings::Condition::hidl_discriminator::sectionBits) {
        param.pid = (unsigned short)((settings.ts().filterSettings.section().condition.sectionBits()
            .mode[0] << 8) | (settings.ts().filterSettings.section().condition.sectionBits().mode[1]));

        int size = settings.ts().filterSettings.section().condition.sectionBits().filter.size();
        ALOGI("NSK EMM filtering descriptor size = %d", size);
        ALOGI("Filtering request for PID 0x%04x", param.pid);

        bool isCheckCrc = settings.ts().filterSettings.section().isCheckCrc;
        ALOGD("%s isCheckCrc:%d", __FUNCTION__, isCheckCrc);
        if (isCheckCrc) {
            param.flags |= DMX_CHECK_CRC;
        }

        bool isRaw = settings.ts().filterSettings.section().isRaw;
        ALOGD("%s isRaw:%d", __FUNCTION__, isRaw);
        if (isRaw) {
            param.flags |= DMX_OUTPUT_RAW_MODE;
        }

        // get 0x80 to 0x8f

        param.filter.filter[0] = 0x80;
        param.filter.mask[0] = 0xf0;
        param.filter.mode[0] = 0x00;
    } else {
        // error: not possible in NSK case.
        ALOGE("Emm requested without emm filter description.");
        return Result::UNAVAILABLE;
    }

    return Result::SUCCESS;
}

static Return<Result>
GetSectionFltParam(const DemuxFilterSettings &settings, struct dmx_sct_filter_params &param)
{
   /* bool isRepeat = settings.ts().filterSettings.section().isRepeat;
    if (!isRepeat) {
        param.flags |= DMX_ONESHOT;
    }*/
    bool isCheckCrc = settings.ts().filterSettings.section().isCheckCrc;
    if (isCheckCrc) {
        param.flags |= DMX_CHECK_CRC;
    }
    bool isRaw = settings.ts().filterSettings.section().isRaw;
    if (isRaw) {
        param.flags |= DMX_OUTPUT_RAW_MODE;
    }

    if (settings.ts().filterSettings.section().condition.getDiscriminator()
        == DemuxFilterSectionSettings::Condition::hidl_discriminator::sectionBits) {
        int size = settings.ts().filterSettings.section().condition.sectionBits().filter.size();
        ALOGD("%s size:%d", __FUNCTION__, size);
        if (size > 0 && size <= 16) {
            param.filter.filter[0] =
                settings.ts().filterSettings.section().condition.sectionBits().filter[0];
            for (int i = 1; i < size - 2; i++) {
                param.filter.filter[i] =
                    settings.ts().filterSettings.section().condition.sectionBits().filter[i + 2];
            }

            size = settings.ts().filterSettings.section().condition.sectionBits().mask.size();
            param.filter.mask[0] =
                settings.ts().filterSettings.section().condition.sectionBits().mask[0];
            for (int i = 1; i < size - 2; i++) {
                param.filter.mask[i] =
                    settings.ts().filterSettings.section().condition.sectionBits().mask[i + 2];
            }

            size = settings.ts().filterSettings.section().condition.sectionBits().mode.size();
            param.filter.mode[0] =
                settings.ts().filterSettings.section().condition.sectionBits().mode[0];
            for (int i = 1; i < size - 2; i++) {
                param.filter.mode[i] =
                    settings.ts().filterSettings.section().condition.sectionBits().mode[i + 2];
            }
        }
    } else {
        param.filter.filter[0] =
            settings.ts().filterSettings.section().condition.tableInfo().tableId;
        param.filter.mask[0] = 0xff;
    }
    ALOGD("%s, isCheckCrc:%d, isRaw:%d, tableId:0x%x", __FUNCTION__, isCheckCrc, isRaw, param.filter.filter[0]);
    return Result::SUCCESS;
}

Filter::Filter() {
    mExtendId = 0;
    mFilterId = 0;
    mBufferSize = 0;
    mTpid = 0;
    mFilterEventFlag = NULL;
    mFilterThread = 0;
    mFilterStatus = DemuxFilterStatus(1);
    mFilterThreadRunning = false;
    mKeepFetchingDataFromFrontend = false;
    bIsRaw = false;
    mEnableDmaBuf = dmabuf_manager_support();
    mFilterFd = -1;
    mFilterToken = 0;
}

Filter::Filter(DemuxFilterType type, uint64_t filterId, uint32_t bufferSize,
               const sp<IFilterCallback>& cb, sp<Demux> demux) {
    mType = type;
    mFilterId = static_cast<int32_t>(filterId);
    uint32_t dmxId = demux->getDemuxId();
    /*
    6bit  fid         111111
    5bit  stream type 11111
    1bit  sec         1
    4bit  dmx id      1111
    16bit pid         1111 1111 1111 1111
    */
    mExtendId = ((mFilterId & 0x003f) << 26) | ((dmxId & 0x000f) << 16);
    mBufferSize = bufferSize;
    mDemux = demux;
    mTpid = 0;
    mFilterThreadRunning = false;
    mKeepFetchingDataFromFrontend = false;
    bIsRaw = false;
    mFilterEventFlag = NULL;
    mFilterThread = 0;
    mFilterStatus = DemuxFilterStatus(1);
    mEnableDmaBuf = dmabuf_manager_support();
    mFilterFd = -1;
    mFilterToken = 0;

    switch (mType.mainType) {
        case DemuxFilterMainType::TS:
            ALOGD("%s DemuxFilterMainType::TS mCallback:%p", __FUNCTION__, mCallback.get());
            if (mType.subType.tsFilterType() == DemuxTsFilterType::AUDIO ||
                mType.subType.tsFilterType() == DemuxTsFilterType::VIDEO) {
                mIsMediaFilter = true;
            }
            if (mType.subType.tsFilterType() == DemuxTsFilterType::PCR) {
                mIsPcrFilter = true;
            }
            if (mType.subType.tsFilterType() == DemuxTsFilterType::RECORD) {
                mIsRecordFilter = true;
            }
            if (mType.subType.tsFilterType() == DemuxTsFilterType::PES) {
                ALOGD("%s tsFilter subType is PES", __FUNCTION__);
                mIsPesFilter = true;
            }
            break;
        case DemuxFilterMainType::MMTP:
            if (mType.subType.mmtpFilterType() == DemuxMmtpFilterType::AUDIO ||
                mType.subType.mmtpFilterType() == DemuxMmtpFilterType::VIDEO) {
                mIsMediaFilter = true;
            }
            if (mType.subType.mmtpFilterType() == DemuxMmtpFilterType::RECORD) {
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

    sp<V1_1::IFilterCallback> filterCallback_v1_1 = V1_1::IFilterCallback::castFrom(cb);
    if (filterCallback_v1_1 != NULL) {
        mCallback_1_1 = filterCallback_v1_1;
    }
    mCallback = cb;
}

Filter::~Filter() {}

Return<void> Filter::getId64Bit(getId64Bit_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    _hidl_cb(Result::SUCCESS, mExtendId);
    return Void();
}

Return<void> Filter::getId(getId_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    _hidl_cb(Result::SUCCESS, static_cast<uint32_t>(mExtendId));
    return Void();
}

Return<Result> Filter::setDataSource(const sp<V1_0::IFilter>& filter) {
    ALOGV("%s", __FUNCTION__);

    mDataSource = filter;
    mIsDataSourceDemux = false;

    return Result::SUCCESS;
}

Return<void> Filter::getQueueDesc(getQueueDesc_cb _hidl_cb) {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    mIsUsingFMQ = mIsRecordFilter ? false : true;

    _hidl_cb(Result::SUCCESS, *mFilterMQ->getDesc());
    return Void();
}

Return<Result> Filter::configure(const DemuxFilterSettings& settings) {
    mFilterSettings = settings;
    switch (mType.mainType) {
        case DemuxFilterMainType::TS:
            mTpid = settings.ts().tpid;
            ALOGD("%s/%d mainType:TS mTpid:0x%x", __FUNCTION__, __LINE__, mTpid);

            switch (mType.subType.tsFilterType()) {
            case DemuxTsFilterType::SECTION: {
                ALOGD("%s subType:SECTION", __FUNCTION__);
                struct dmx_sct_filter_params param;
                if (mDemux->getAmDmxDevice()->AM_DMX_SetBufferSize(mFilterId, mBufferSize) != 0) {
                    ALOGD("%s AM_DMX_SetBufferSize fail", __FUNCTION__);
                    return Result::UNAVAILABLE;
                }
                memset(&param, 0, sizeof(param));
                // Special Value for NSK Emm Filtering.
                if (mTpid == 0xfffe) {
                    Return<Result> result = GetNskEmmFltParam(settings, param);
                    if (result != Result::SUCCESS) {
                        return result;
                    }

                    mTpid = param.pid;

                    mIsNSKEmmFilter = true;
                } else {
                    param.pid = mTpid;
                    Return<Result> result = GetSectionFltParam(settings, param);
                    if (result != Result::SUCCESS) {
                        return result;
                    }
                }
                //mTableId = param.filter.filter[0];
                ALOGD("mFilterId = (%llu:%u), mTableId = %d, dmxId = %d", mExtendId, mFilterId, param.filter.filter[0], mDemux->getDemuxId());

                if (mDemux->getAmDmxDevice()->AM_DMX_SetSecFilter(mFilterId, &param) != 0) {
                    ALOGE("Failed to set Section Filter");
                    return Result::UNAVAILABLE;
                }
                break;
            }
            case DemuxTsFilterType::AUDIO: {
                ALOGD("%s subType:AUDIO", __FUNCTION__);
                if (settings.ts().filterSettings.av().isPassthrough) {
                    // aparam.flags |= DMX_OUTPUT_RAW_MODE;
                    // for passthrough mode, will set pes filter in media hal
                    mExtendId |= (mTpid & 0xffff);
                    /*
                        fmt 5bits, sec 1bit,    dmxId 4bit,     pid 16bits
                        111111     11111      1       1111   1111 1111 1111 1111
                        unused      fmt      sec      dmxid       pid
                    */
                    ALOGD("audio filter id = (%llu:%u)", mExtendId, mFilterId);
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
                        return Result::UNAVAILABLE;
                    }
                    if (mDemux->getAmDmxDevice()->AM_DMX_SetPesFilter(mFilterId, &aparam) != 0) {
                        return Result::UNAVAILABLE;
                    }
                }
                break;
            }
            case DemuxTsFilterType::VIDEO: {
                ALOGD("%s subType:VIDEO", __FUNCTION__);
                if (settings.ts().filterSettings.av().isPassthrough) {
                    // vparam.flags |= DMX_OUTPUT_RAW_MODE;
                    // for passthrough mode, will set pes filter in media hal
                    mExtendId |= (mTpid & 0xffff);
                    /*
                        fmt 5bits, sec 1bit,    dmxId 4bit,     pid 16bits
                        111111     11111      1       1111   1111 1111 1111 1111
                        unused      fmt      sec      dmxid       pid
                    */
                    ALOGD("video filter id = (%llu:%u)", mExtendId, mFilterId);
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
                    if (mEnableDmaBuf) {
                        vparam.flags |= DMX_OUTPUT_RAW_MODE;
                    }
                    //if (settings.ts().filterSettings.av().isSecureMemory) {
                    //    vparam.flags |= DMX_MEM_SEC_LEVEL2;
                    //}
#ifdef TUNERHAL_DBG
                    buffSize = mVideoFilterSize;
#else
                    buffSize = mBufferSize;
#endif
                    ALOGD("%s AM_DMX_SetBufferSize:%d MB", __FUNCTION__, buffSize / 1024 / 1024);
                    if (mDemux->getAmDmxDevice()->AM_DMX_SetBufferSize(mFilterId, buffSize) != 0) {
                        return Result::UNAVAILABLE;
                    }
                    if (mDemux->getAmDmxDevice()->AM_DMX_SetPesFilter(mFilterId, &vparam) != 0) {
                        return Result::UNAVAILABLE;
                    }
                }
                break;
            }
            case DemuxTsFilterType::RECORD: {
                ALOGD("%s subType:RECORD", __FUNCTION__);
                mTsIndex     = settings.ts().filterSettings.record().tsIndexMask;
                mScIndexType = settings.ts().filterSettings.record().scIndexType;
                if (settings.ts().filterSettings.record().scIndexMask.getDiscriminator()
                    == DemuxFilterRecordSettings::ScIndexMask::hidl_discriminator::sc) {
                    mScIndex = settings.ts().filterSettings.record().scIndexMask.sc();
                } else if (settings.ts().filterSettings.record().scIndexMask.getDiscriminator()
                    == DemuxFilterRecordSettings::ScIndexMask::hidl_discriminator::scHevc) {
                    mScIndex =  settings.ts().filterSettings.record().scIndexMask.scHevc();
                }
                struct dmx_pes_filter_params pparam;
                memset(&pparam, 0, sizeof(pparam));
                pparam.pid = mTpid;
                pparam.input = DMX_IN_FRONTEND;
                pparam.output = DMX_OUT_TS_TAP;
                pparam.pes_type = DMX_PES_OTHER;
                /*
                if (mDemux->getAmDmxDevice()->AM_DMX_SetBufferSize(mFilterId, 10 * 1024 * 1024) != 0) {
                    return Result::UNAVAILABLE;
                }*/
                if (mDemux->getAmDmxDevice()->AM_DMX_SetPesFilter(mFilterId, &pparam) != 0) {
                    ALOGE("record AM_DMX_SetPesFilter");
                    return Result::UNAVAILABLE;
                }
                ALOGD("stream(pid = %d) start recording, filter = (%llu:%u)", mTpid, mExtendId, mFilterId);
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
                    return Result::UNAVAILABLE;
                }
                if (mDemux->getAmDmxDevice()->AM_DMX_SetPesFilter(mFilterId, &pcrParam) != 0) {
                    return Result::UNAVAILABLE;
                }
                break;
            }
            case DemuxTsFilterType::PES: {
                bIsRaw = settings.ts().filterSettings.pesData().isRaw;
                ALOGD("%s subType:PES bIsRaw:%d", __FUNCTION__, bIsRaw);
                struct dmx_pes_filter_params pesp;
                memset(&pesp, 0, sizeof(pesp));
                pesp.pid = mTpid;
                pesp.output = DMX_OUT_TAP;
                pesp.pes_type = DMX_PES_SUBTITLE;
                pesp.input = DMX_IN_FRONTEND;
                if (mDemux->getAmDmxDevice()->AM_DMX_SetBufferSize(mFilterId, mBufferSize) != 0) {
                    return Result::UNAVAILABLE;
                }
                if (mDemux->getAmDmxDevice()->AM_DMX_SetPesFilter(mFilterId, &pesp) != 0) {
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
        case DemuxFilterMainType::IP: {
                ALOGD("%s subType:IP", __FUNCTION__);
                DemuxIpAddress ipAddr = settings.ip().ipAddr;
            }
            break;
        case DemuxFilterMainType::TLV:
            break;
        case DemuxFilterMainType::ALP:
            break;
        default:
            break;
    }

    mConfigured = true;
    return Result::SUCCESS;
}

Return<Result> Filter::start() {
    ALOGD("%s/%d mFilterId:(%llu:%u)", __FUNCTION__, __LINE__, mExtendId, mFilterId);
    gettimeofday(&mTable_start, NULL);
    /*if (mTableId == 0x0) {
        ALOGD("start PAT filter in demuxId = %d", mDemux->getDemuxId());
    } else if (mTableId == 0x02) {
        ALOGD("start PMT filter in demuxId = %d", mDemux->getDemuxId());
    }*/

    if (mType.mainType == DemuxFilterMainType::IP) {
        ALOGD("start Ip Filter");
        return Result::SUCCESS;
    }

    if (mEnableDmaBuf && mType.mainType == DemuxFilterMainType::TS &&
        mType.subType.tsFilterType() == DemuxTsFilterType::VIDEO) {
        mDemux->getAmDmxDevice()->AM_DMX_GetFilterFd(mFilterId, &mFilterFd);
        mFilterToken = ++gFilterToken;
        dmabuf_wrapper_setfilterinfo(mFilterToken, mFilterFd, 0);
    }

    if (mDemux->getAmDmxDevice()
        ->AM_DMX_StartFilter(mFilterId) != 0) {
        if (mIsMediaFilter && mFilterSettings.ts().filterSettings.av().isPassthrough) {
            ALOGD("av filter will start in mediahal");
        } else {
            ALOGE("Start filter (%llu:%u) failed!", mExtendId, mFilterId);
        }
        return Result::UNAVAILABLE;
    }
    return Result::SUCCESS;
    /*
    mFilterThreadRunning = true;
    // All the filter event callbacks in start are for testing purpose.
    switch (mType.mainType) {
        case DemuxFilterMainType::TS:
            mCallback->onFilterEvent(createMediaEvent());
            mCallback->onFilterEvent(createTsRecordEvent());
            mCallback->onFilterEvent(createTemiEvent());
            // clients could still pass 1.0 callback
            if (mCallback_1_1 != NULL) {
                mCallback_1_1->onFilterEvent_1_1(createTsRecordEvent(), createTsRecordEventExt());
            }
            break;
        case DemuxFilterMainType::MMTP:
            mCallback->onFilterEvent(createDownloadEvent());
            mCallback->onFilterEvent(createMmtpRecordEvent());
            if (mCallback_1_1 != NULL) {
                mCallback_1_1->onFilterEvent_1_1(createMmtpRecordEvent(),
                                                 createMmtpRecordEventExt());
            }
            break;
        case DemuxFilterMainType::IP:
            mCallback->onFilterEvent(createSectionEvent());
            mCallback->onFilterEvent(createIpPayloadEvent());
            break;
        case DemuxFilterMainType::TLV: {
            if (mCallback_1_1 != NULL) {
                DemuxFilterEvent emptyFilterEvent;
                mCallback_1_1->onFilterEvent_1_1(emptyFilterEvent, createMonitorEvent());
            }
            break;
        }
        case DemuxFilterMainType::ALP: {
            if (mCallback_1_1 != NULL) {
                DemuxFilterEvent emptyFilterEvent;
                mCallback_1_1->onFilterEvent_1_1(emptyFilterEvent, createRestartEvent());
            }
            break;
        }
        default:
            break;
    }
    return startFilterLoop();*/
}

Return<Result> Filter::stop() {
    ALOGD("%s/%d mFilterId:(%llu:%u)", __FUNCTION__, __LINE__, mExtendId, mFilterId);
    if (mEnableDmaBuf && mType.mainType == DemuxFilterMainType::TS &&
        mType.subType.tsFilterType() == DemuxTsFilterType::VIDEO) {
        dmabuf_wrapper_setfilterinfo(mFilterToken, mFilterFd, 1);
        mFilterToken = 0;
    }
    mDemux->getAmDmxDevice()->AM_DMX_StopFilter(mFilterId);
    mFilterThreadRunning = false;

    std::lock_guard<std::mutex> lock(mFilterThreadLock);
    return Result::SUCCESS;
}

Return<Result> Filter::flush() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);
    // temp implementation to flush the FMQ
    if (mFilterMQ.get() != NULL) {
        size_t size = mFilterMQ->availableToRead();
        if (size > 0) {
            char* buffer = new char[size];
            mFilterMQ->read((unsigned char*)&buffer[0], size);
            delete[] buffer;
        }
    }
    mFilterStatus = DemuxFilterStatus::DATA_READY;
    return Result::SUCCESS;
}

Return<Result> Filter::releaseAvHandle(const hidl_handle& avMemory, uint64_t avDataId) {
    ALOGV("%s", __FUNCTION__);

    if (mSharedAvMemHandle != NULL && avMemory != NULL &&
        (mSharedAvMemHandle.getNativeHandle()->numFds > 0) &&
        (avMemory.getNativeHandle()->numFds > 0) &&
        (sameFile(avMemory.getNativeHandle()->data[0],
                  mSharedAvMemHandle.getNativeHandle()->data[0]))) {
        freeSharedAvHandle();
        return Result::SUCCESS;
    }

    if (mDataId2Avfd.find(avDataId) == mDataId2Avfd.end()) {
        return Result::INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(mFilterEventLock);
    ::close(mDataId2Avfd[avDataId]);
    mDataId2Avfd.erase(avDataId);
    return Result::SUCCESS;
}

Return<Result> Filter::close() {
    ALOGD("%s/%d mFilterId = (%llu:%u)", __FUNCTION__, __LINE__, mExtendId, mFilterId);

    Return<Result> res = Result::SUCCESS;
    if (mDemux->getAmDmxDevice() != NULL) {
        mDemux->getAmDmxDevice()->AM_DMX_SetCallback(mFilterId, NULL, NULL);
        mDemux->getAmDmxDevice()->AM_DMX_FreeFilter(mFilterId);
    }
    res = mDemux->removeFilter(mFilterId);

    std::lock_guard<std::mutex> lock(mFilterEventLock);
    auto it = mDataId2Avfd.begin();
    while (it != mDataId2Avfd.end()) {
        ::close(it->second);
        it++;
    }
    mDataId2Avfd.clear();
    mFilterFd = -1;
    return res;
}

void Filter::clear() {
    if (mFilterMQ.get() != NULL)
        mFilterMQ.reset();

    if (mFilterEventFlag != nullptr) {
        EventFlag::deleteEventFlag(&mFilterEventFlag);
        mFilterEventFlag = nullptr;
    }

    mCallback = nullptr;
}

Return<Result> Filter::configureIpCid(uint32_t ipCid) {
    ALOGV("%s  ipCid =%d", __FUNCTION__, ipCid);

    if (mType.mainType != DemuxFilterMainType::IP) {
        return Result::INVALID_STATE;
    }

    mCid = ipCid;
    return Result::SUCCESS;
}

Return<void> Filter::getAvSharedHandle(getAvSharedHandle_cb _hidl_cb) {
    ALOGV("%s", __FUNCTION__);

    if (!mIsMediaFilter) {
        _hidl_cb(Result::INVALID_STATE, NULL, BUFFER_SIZE_16M);
        return Void();
    }

    if (mSharedAvMemHandle.getNativeHandle() != nullptr) {
        _hidl_cb(Result::SUCCESS, mSharedAvMemHandle, BUFFER_SIZE_16M);
        mUsingSharedAvMem = true;
        return Void();
    }

    int av_fd = createAvIonFd(BUFFER_SIZE_16M);
    if (av_fd == -1) {
        _hidl_cb(Result::UNKNOWN_ERROR, NULL, 0);
        return Void();
    }

    native_handle_t* nativeHandle = createNativeHandle(av_fd);
    if (nativeHandle == NULL) {
        ::close(av_fd);
        _hidl_cb(Result::UNKNOWN_ERROR, NULL, 0);
        return Void();
    }
    mSharedAvMemHandle.setTo(nativeHandle, /*shouldOwn=*/true);
    ::close(av_fd);

    _hidl_cb(Result::SUCCESS, mSharedAvMemHandle, BUFFER_SIZE_16M);
    mUsingSharedAvMem = true;
    return Void();
}

Return<Result> Filter::configureAvStreamType(const V1_1::AvStreamType& avStreamType) {
    ALOGV("%s", __FUNCTION__);

    if (!mIsMediaFilter) {
        return Result::UNAVAILABLE;
    }

    switch (avStreamType.getDiscriminator()) {
        case V1_1::AvStreamType::hidl_discriminator::audio:
            mAudioStreamType = static_cast<uint32_t>(avStreamType.audio());
            if (mFilterSettings.ts().filterSettings.av().isPassthrough) {
                mExtendId |= ((mAudioStreamType & 0x1f) << 21);// 5bit
                ALOGD("%s/%d  audio mFilterId = (%llu:%u), mAudioStreamType = %d ", __FUNCTION__, __LINE__, mExtendId, mFilterId, mAudioStreamType);
            }
            break;
        case V1_1::AvStreamType::hidl_discriminator::video:
            mVideoStreamType = static_cast<uint32_t>(avStreamType.video());
            if (mFilterSettings.ts().filterSettings.av().isPassthrough) {
                mExtendId |= ((mVideoStreamType & 0x1f) << 21);// 5bit
                ALOGD("%s/%d  video mFilterId = (%llu:%u), mVideoStreamType = %d", __FUNCTION__, __LINE__, mExtendId, mFilterId, mVideoStreamType);
            }
            break;
        default:
            break;
    }

    return Result::SUCCESS;
}

Return<Result> Filter::configureMonitorEvent(uint32_t monitorEventTypes) {
    ALOGV("%s", __FUNCTION__);

    DemuxFilterEvent emptyFilterEvent;
    V1_1::DemuxFilterMonitorEvent monitorEvent;
    V1_1::DemuxFilterEventExt eventExt;

    uint32_t newScramblingStatus =
            monitorEventTypes & V1_1::DemuxFilterMonitorEventType::SCRAMBLING_STATUS;
    uint32_t newIpCid = monitorEventTypes & V1_1::DemuxFilterMonitorEventType::IP_CID_CHANGE;

    // if scrambling status monitoring flipped, record the new state and send msg on enabling
    if (newScramblingStatus ^ mScramblingStatusMonitored) {
        mScramblingStatusMonitored = newScramblingStatus;
        if (mScramblingStatusMonitored) {
            if (mCallback_1_1 != nullptr) {
                // Assuming current status is always NOT_SCRAMBLED
                monitorEvent.scramblingStatus(V1_1::ScramblingStatus::NOT_SCRAMBLED);
                eventExt.events.resize(1);
                eventExt.events[0].monitorEvent(monitorEvent);
                mCallback_1_1->onFilterEvent_1_1(emptyFilterEvent, eventExt);
            } else {
                return Result::INVALID_STATE;
            }
        }
    }

    // if ip cid monitoring flipped, record the new state and send msg on enabling
    if (newIpCid ^ mIpCidMonitored) {
        mIpCidMonitored = newIpCid;
        if (mIpCidMonitored) {
            if (mCallback_1_1 != nullptr) {
                // Return random cid
                monitorEvent.cid(1);
                eventExt.events.resize(1);
                eventExt.events[0].monitorEvent(monitorEvent);
                mCallback_1_1->onFilterEvent_1_1(emptyFilterEvent, eventExt);
            } else {
                return Result::INVALID_STATE;
            }
        }
    }

    return Result::SUCCESS;
}

bool Filter::createFilterMQ() {
    ALOGD("%s/%d", __FUNCTION__, __LINE__);

    // Create a synchronized FMQ that supports blocking read/write
    std::unique_ptr<FilterMQ> tmpFilterMQ =
            std::unique_ptr<FilterMQ>(new (std::nothrow) FilterMQ(mBufferSize, true));
    if (!tmpFilterMQ->isValid()) {
        ALOGW("[Filter] Failed to create FMQ of filter with id: (%llu:%u)", mExtendId, mFilterId);
        return false;
    }

    mFilterMQ = std::move(tmpFilterMQ);

    if (EventFlag::createEventFlag(mFilterMQ->getEventFlagWord(), &mFilterEventFlag) != OK) {
        return false;
    }

    return true;
}

Result Filter::startFilterLoop() {
    if (pthread_create(&mFilterThread, NULL, __threadLoopFilter, this)) {
        ALOGD("[Filter] can't create startFilterLoop thread");
    }
    pthread_setname_np(mFilterThread, "filter_waiting_loop");

    return Result::SUCCESS;
}

void* Filter::__threadLoopFilter(void* user) {
    Filter* const self = static_cast<Filter*>(user);
    self->filterThreadLoop();
    return 0;
}

void Filter::filterThreadLoop() {
    if (!mFilterThreadRunning) {
        return;
    }
    std::lock_guard<std::mutex> lock(mFilterThreadLock);
    ALOGD("[Filter] filter (%llu:%u) threadLoop start.", mExtendId, mFilterId);

    // For the first time of filter output, implementation needs to send the filter
    // Event Callback without waiting for the DATA_CONSUMED to init the process.
    while (mFilterThreadRunning) {
        if (mFilterEvent.events.size() == 0 && mFilterEventExt.events.size() == 0) {
            if (DEBUG_FILTER) {
                ALOGD("[Filter] wait for filter data output.");
            }
            usleep(1000 * 1000);
            continue;
        }

        // After successfully write, send a callback and wait for the read to be done
        if (mCallback_1_1 != nullptr) {
            if (mConfigured) {
                DemuxFilterEvent emptyEvent;
                V1_1::DemuxFilterEventExt startEvent;
                startEvent.events.resize(1);
                startEvent.events[0].startId(mStartId++);
                mCallback_1_1->onFilterEvent_1_1(emptyEvent, startEvent);
                mConfigured = false;
            }
            mCallback_1_1->onFilterEvent_1_1(mFilterEvent, mFilterEventExt);
            mFilterEventExt.events.resize(0);
        } else if (mCallback != nullptr) {
            mCallback->onFilterEvent(mFilterEvent);
        } else {
            ALOGD("[Filter] filter callback is not configured yet.");
            mFilterThreadRunning = false;
            return;
        }
        mFilterEvent.events.resize(0);

        freeAvHandle();
        mFilterStatus = DemuxFilterStatus::DATA_READY;
        if (mCallback != nullptr) {
            mCallback->onFilterStatus(mFilterStatus);
        } else if (mCallback_1_1 != nullptr) {
            mCallback_1_1->onFilterStatus(mFilterStatus);
        }
        break;
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
                status_t status = mFilterEventFlag->wait(
                        static_cast<uint32_t>(DemuxQueueNotifyBits::DATA_CONSUMED), &efState,
                        WAIT_TIMEOUT, true /* retry on spurious wake */);
                if (status != OK) {
                    ALOGD("[Filter] wait for data consumed");
                    continue;
                }
                break;
            }

            maySendFilterStatusCallback();

            while (mFilterThreadRunning) {
                std::lock_guard<std::mutex> lock(mFilterEventLock);
                if (mFilterEvent.events.size() == 0 && mFilterEventExt.events.size() == 0) {
                    continue;
                }
                // After successfully write, send a callback and wait for the read to be done
                if (mCallback_1_1 != nullptr) {
                    mCallback_1_1->onFilterEvent_1_1(mFilterEvent, mFilterEventExt);
                    mFilterEventExt.events.resize(0);
                } else if (mCallback != nullptr) {
                    mCallback->onFilterEvent(mFilterEvent);
                }
                mFilterEvent.events.resize(0);
                break;
            }
            // We do not wait for the last read to be done
            // VTS can verify the read result itself.
            if (i == SECTION_WRITE_COUNT - 1) {
                ALOGD("[Filter] filter (%llu:%u) writing done. Ending thread", mExtendId, mFilterId);
                break;
            }
        }
        break;
    }
    ALOGD("[Filter] filter thread ended.");
}

bool Filter::fillDataToDecoder() {
    ALOGV("%s/%d mFilterId:(%llu:%u)", __FUNCTION__, __LINE__, mExtendId, mFilterId);

    // For the first time of filter output, implementation needs to send the filter
    // Event Callback without waiting for the DATA_CONSUMED to init the process.
    if (mFilterEvent.events.size() == 0 && mFilterEventExt.events.size() == 0) {
        ALOGV("[Filter (%llu:%u] wait for new mFilterEvent", mExtendId, mFilterId);
        return false;
    }
    // After successfully write, send a callback and wait for the read to be done
    if (mCallback_1_1 != nullptr) {
        auto ret = mCallback_1_1->onFilterEvent_1_1(mFilterEvent, mFilterEventExt);
        if (!ret.isOk()) {
            ALOGD("[Filter] return mCallback_1_1 onFilterEvent_1_1fail");
            return false;
        }
    } else if (mCallback != nullptr){
        auto ret = mCallback->onFilterEvent(mFilterEvent);
        if (!ret.isOk()) {
            ALOGD("[Filter] return mCallback onFilterEvent fail");
            return false;
        }
    } else {
        ALOGD("[Filter] filter callback is not configured yet.");
        return false;
    }
    freeAvHandle();
    mFilterEvent.events.resize(0);
    mFilterEventExt.events.resize(0);
    mFilterStatus = DemuxFilterStatus::DATA_READY;
    if (mCallback != nullptr) {
        auto ret = mCallback->onFilterStatus(mFilterStatus);
        if (!ret.isOk()) {
            ALOGD("[Filter] return mCallback onFilterStatus fail");
            return false;
        }
    } else if (mCallback_1_1 != nullptr) {
        auto ret = mCallback_1_1->onFilterStatus(mFilterStatus);
        if (!ret.isOk()) {
            ALOGD("[Filter] return mCallback_1_1  onFilterStatus fail");
            return false;
        }
    }
    return true;
}

void Filter::freeAvHandle() {
    if (!mIsMediaFilter) {
        return;
    }
    for (int i = 0; i < mFilterEvent.events.size(); i++) {
        native_handle_close(mFilterEvent.events[i].media().avMemory.getNativeHandle());
    }
}

void Filter::freeSharedAvHandle() {
    if (!mIsMediaFilter) {
        return;
    }
    ::close(mSharedAvMemHandle.getNativeHandle()->data[0]);
    native_handle_delete(const_cast<native_handle_t*>(mSharedAvMemHandle.getNativeHandle()));
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
        if (mCallback != nullptr) {
            mCallback->onFilterStatus(newStatus);
        } else if (mCallback_1_1 != nullptr) {
            mCallback_1_1->onFilterStatus(newStatus);
        }
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
    if (mFilterSettings.ts().filterSettings.section().condition.getDiscriminator()
        == DemuxFilterSectionSettings::Condition::hidl_discriminator::sectionBits) {
        for (int flagIndex = 18; flagIndex < 18 + 16;
             flagIndex++) { // mode 0..1 is reserved for emm pid
            if (mFilterSettings.ts().filterSettings.section().condition.sectionBits().mode
                    [flagIndex]
                != 0) { // count flags
                tIdFlagCount++;
            }
        }

        for (int tidIndex = 2; tidIndex < 2 + 16; tidIndex++) {
            if (mFilterSettings.ts().filterSettings.section().condition.sectionBits().mode[tidIndex]
                == data[0]) {
                tIdMatchedIndex = tidIndex;
                tableIdFlag = mFilterSettings.ts()
                                  .filterSettings.section()
                                  .condition.sectionBits()
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
                mFilterSettings.ts().filterSettings.section().condition.sectionBits().mode
                    [tIdMatchedIndex]);

            vector<uint8_t> filterAddressMap =
                mFilterSettings.ts().filterSettings.section().condition.sectionBits().filter;
            vector<uint8_t> filterAddressMask =
                mFilterSettings.ts().filterSettings.section().condition.sectionBits().mask;

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

void Filter::updateFilterOutput(vector<uint8_t> data, void *priv) {
    std::lock_guard<std::mutex> lock(mFilterOutputLock);

    if (DEBUG_FILTER) {
        ALOGD(
            "%s/%d mFilterId:(%llu:%u) data size:%d output size:%dKB", __FUNCTION__, __LINE__, mExtendId, mFilterId,
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

        if (!postFilteredEmmSection(data)) {
            return;
        }
    }
    mFilterOutput.insert(mFilterOutput.end(), data.begin(), data.end());
    if (priv)
        mEsPrivateHeader = priv;
}

void Filter::updatePts(uint64_t pts) {
    std::lock_guard<std::mutex> lock(mFilterOutputLock);
    mPts = pts;
}

void Filter::updateRecordOutput(vector<uint8_t> data) {
    std::lock_guard<std::mutex> lock(mRecordFilterOutputLock);
    if (DEBUG_FILTER)
        ALOGD("%s/%d mFilterId:(%llu:%u) data size:%d", __FUNCTION__, __LINE__, mExtendId, mFilterId, data.size());

    mRecordFilterOutput.insert(mRecordFilterOutput.end(), data.begin(), data.end());
}

Result Filter::startFilterHandler() {
    std::lock_guard<std::mutex> lock(mFilterOutputLock);
    if (DEBUG_FILTER)
        ALOGD("%s/%d Filter id:(%llu:%u)", __FUNCTION__, __LINE__, mExtendId, mFilterId);
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
        ALOGD("%s/%d mFilterId:(%llu:%u)", __FUNCTION__, __LINE__, mExtendId, mFilterId);

    if (mFilterOutput.empty()) {
        return Result::SUCCESS;
    }
    if (!writeSectionsAndCreateEvent(mFilterOutput)) {
        ALOGD("[Filter] filter (%llu:%u) fails to write into FMQ. Ending thread", mExtendId, mFilterId);
        mFilterOutput.clear();
        return Result::UNKNOWN_ERROR;
    }
    fillDataToDecoder();

    mFilterOutput.clear();

    return Result::SUCCESS;
}

Result Filter::startPesFilterHandler() {
    std::lock_guard<std::mutex> lock(mFilterEventLock);
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
    int left =  mFilterOutput.size();
    if ( mSequenceNumber == UINT32_MAX) {
        mSequenceNumber = 0;
    } else {
        ++mSequenceNumber;
    }
    for (int i = 0; i < left; i++) {
        if (left > UINT16_MAX) {
            mPesOutput.insert(mPesOutput.end(), mFilterOutput.begin() + i * UINT16_MAX, mFilterOutput.begin() + (i + 1) * UINT16_MAX);
            left -= UINT16_MAX;
            if (!writeDataToFilterMQ(mPesOutput)) {
                ALOGD("[Filter] pes data write failed");
                mPesOutput.clear();
                mFilterOutput.clear();
                return Result::INVALID_STATE;
            }
            maySendFilterStatusCallback();
            DemuxFilterPesEvent pesEvent;
            pesEvent = {
                    // temp dump meta data
                    .streamId = mFilterOutput[3],
                    .dataLength = static_cast<uint16_t>(mPesOutput.size()),
                    .mpuSequenceNumber = mSequenceNumber,
            };
            if (DEBUG_FILTER) {
                ALOGD("[Filter] assembled pes data length %d", pesEvent.dataLength);
            }
            int size = mFilterEvent.events.size();
            mFilterEvent.events.resize(size + 1);
            mFilterEvent.events[size].pes(pesEvent);
            fillDataToDecoder();
            mPesOutput.clear();
        } else {
            mPesOutput.insert(mPesOutput.end(), mFilterOutput.begin() + i * UINT16_MAX, mFilterOutput.end());
            if (!writeDataToFilterMQ(mPesOutput)) {
                ALOGD("[Filter] pes data write failed");
                mPesOutput.clear();
                mFilterOutput.clear();
                return Result::INVALID_STATE;
            }
            maySendFilterStatusCallback();
            DemuxFilterPesEvent pesEvent;
            pesEvent = {
                    // temp dump meta data
                    .streamId = mFilterOutput[3],
                    .dataLength = static_cast<uint16_t>(mPesOutput.size()),
                    .mpuSequenceNumber = mSequenceNumber,
            };
            if (DEBUG_FILTER) {
                ALOGD("[Filter] assembled pes data length %d", pesEvent.dataLength);
            }
            int size = mFilterEvent.events.size();
            mFilterEvent.events.resize(size + 1);
            mFilterEvent.events[size].pes(pesEvent);
            fillDataToDecoder();
            mPesOutput.clear();
            left = 0;
        }
    }
    mFilterOutput.clear();

    return Result::SUCCESS;
}

Result Filter::startTsFilterHandler() {
    // TODO handle starting TS filter
    return Result::SUCCESS;
}

Result Filter::startMediaFilterHandler() {
    std::lock_guard<std::mutex> lock(mFilterEventLock);

    if (mFilterOutput.size() > 0)
        ALOGV("%s/%d mFilterId:(%llu:%u) output size:%d KB", __FUNCTION__, __LINE__, mExtendId, mFilterId, mFilterOutput.size()/1024);

#ifdef TUNERHAL_DBG
    if (mFilterOutput.empty() || mFilterOutput.size() < mFilterEventSize) {
#else
    if (mFilterOutput.empty() /*|| mFilterOutput.size() < 1024 * 1024 * 10*/) {//10MB
#endif
        return Result::SUCCESS;
    }

    if (mConfigured) {
        mFilterEventExt.events.resize(1);
        mFilterEventExt.events[0].startId(mStartId++);
        fillDataToDecoder();
        mConfigured = false;
     }

    if (mEnableDmaBuf && mType.mainType == DemuxFilterMainType::TS &&
        mType.subType.tsFilterType() == DemuxTsFilterType::VIDEO) {
        int esNum = mFilterOutput.size() / sizeof(struct dmx_sec_es_data);
        native_handle_t* nativeHandle = NULL;
        hidl_handle handle;
        struct dmx_sec_es_data es;
        int offset = 0;
        uint32_t dataoffset = 0;
        int av_fd = -1;
        int es_size = 0;
        uint64_t dataId = 0;
        int eventsize = 0;
        uint64_t pts = 0;
        bool isPtsPresent = false;
        bool isSecureMemory = false;
        if (esNum > 0) {
            for (int i = 0; i < esNum; i++) {
                pts = 0;
                isPtsPresent = false;
                memcpy(&es, mFilterOutput.data() + offset, sizeof(es));
                if (es.data_end >= es.data_start)
                    es_size = es.data_end - es.data_start;
                else
                    es_size = es.buf_end - es.data_start + es.data_end - es.buf_start;
                dataoffset = (es.data_start % DEFAULT_PAGE_SIZE);
                es.data_start -= dataoffset;
                if (es.pts_dts_flag & 0x2) {
                    pts = es.pts;
                    isPtsPresent = true;
                }
                av_fd = dmabuf_wrapper_export((void *)&es, mLastUsedDataId, mFilterToken);
                if (av_fd < 0) {
                    ALOGE("%s/%d Export dma buf failed (%llu:%u)", __FUNCTION__, __LINE__, mExtendId, mFilterId);
                    return Result::UNKNOWN_ERROR;
                }
                nativeHandle = createNativeHandle(av_fd);
                if (nativeHandle == NULL) {
                  ::close(av_fd);
                  return Result::UNKNOWN_ERROR;
               }
               handle.setTo(nativeHandle, true);
               dataId = mLastUsedDataId++;
               mDataId2Avfd[dataId] = dup(av_fd);
               DemuxFilterMediaEvent mediaEvent;
               mediaEvent = {
                   .avMemory = std::move(handle),
                   .dataLength = static_cast<uint32_t>(es_size),
                   .avDataId = dataId,
                   .offset = dataoffset,
                   .isPtsPresent = isPtsPresent,
                   .pts = pts,
                   .isSecureMemory = isSecureMemory,
               };
               eventsize = mFilterEvent.events.size();
               mFilterEvent.events.resize(eventsize + 1);
               mFilterEvent.events[eventsize].media(mediaEvent);
               offset += sizeof(es);
               ::close(av_fd);
            }
            fillDataToDecoder();
            mFilterOutput.clear();
        }
    } else {
        uint64_t pts = 0;
        bool isPtsPresent = false;
        dmx_non_sec_es_header* esHeader = NULL;
        int av_fd = createAvIonFd(mFilterOutput.size());
        if (av_fd == -1) {
           return Result::UNKNOWN_ERROR;
        }
        // copy the filtered data to the buffer
        uint8_t* avBuffer = getIonBuffer(av_fd, mFilterOutput.size());
        if (avBuffer == NULL) {
           return Result::UNKNOWN_ERROR;
        }
        memcpy(avBuffer, mFilterOutput.data(), mFilterOutput.size() * sizeof(uint8_t));
        if (mEsPrivateHeader) {
            esHeader = (dmx_non_sec_es_header*)mEsPrivateHeader;
            if (esHeader->pts_dts_flag & 0x2) {
                pts = esHeader->pts;
                isPtsPresent = true;
            }
            mEsPrivateHeader = NULL;
        }
        ALOGV("%s/%d mFilterId:(%llu:%u) mFilterEvent size:%d isPtsPresent is %d pts is %lld av_fd is %d", __FUNCTION__, __LINE__,
            mExtendId, mFilterId, mFilterOutput.size(), isPtsPresent, pts, av_fd);
        native_handle_t* nativeHandle = createNativeHandle(av_fd);
        if (nativeHandle == NULL) {
           releaseIonBuffer(avBuffer, mFilterOutput.size());
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
               .offset = 0,
               .isPtsPresent = isPtsPresent,
               .pts = pts,
               .isSecureMemory = false,
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
    }
    return Result::SUCCESS;
}

Result Filter::createMediaFilterEventWithIon(vector<uint8_t> output) {
    if (mUsingSharedAvMem) {
        if (mSharedAvMemHandle.getNativeHandle() == nullptr) {
            return Result::UNKNOWN_ERROR;
        }
        return createShareMemMediaEvents(output);
    }

    return createIndependentMediaEvents(output);
}

Result Filter::startRecordFilterHandler() {
    std::lock_guard<std::mutex> lock(mRecordFilterOutputLock);
    if (mRecordFilterOutput.empty()) {
        return Result::SUCCESS;
    }

    if (mDvr == nullptr || !mDvr->writeRecordFMQ(mRecordFilterOutput)) {
        ALOGD("[Filter] dvr fails to write into record FMQ.");
        return Result::UNKNOWN_ERROR;
    }

    V1_0::DemuxFilterTsRecordEvent recordEvent;
    DemuxPid demuxPid;
    demuxPid.tPid(static_cast<DemuxTpid>(mTpid));
    DemuxFilterTsRecordEvent::ScIndexMask mask;
    mask.sc(mScIndex);
    recordEvent = {
            .pid         = demuxPid,
            .tsIndexMask = mTsIndex,
            .scIndexMask = mask,
            .byteNumber  = static_cast<uint64_t>(mRecordFilterOutput.size()),
    };
    V1_1::DemuxFilterTsRecordEventExt recordEventExt;
    recordEventExt = {
            .pts = (mPts == 0) ? (time(NULL) * 900000) & 0x1ffffffffull : mPts,
            .firstMbInSlice = 0,     // random address
    };

    int size;
    size = mFilterEventExt.events.size();
    mFilterEventExt.events.resize(size + 1);
    mFilterEventExt.events[size].tsRecord(recordEventExt);

    size = mFilterEvent.events.size();
    mFilterEvent.events.resize(size + 1);
    mFilterEvent.events[size].tsRecord(recordEvent);
    fillDataToDecoder();
    mRecordFilterOutput.clear();
    return Result::SUCCESS;
}

Result Filter::startPcrFilterHandler() {
    // TODO handle starting PCR filter
    return Result::SUCCESS;
}

Result Filter::startTemiFilterHandler() {
    // TODO handle starting TEMI filter
    return Result::SUCCESS;
}

bool Filter::writeSectionsAndCreateEvent(vector<uint8_t> data) {
    // TODO check how many sections has been read
    std::lock_guard<std::mutex> lock(mFilterEventLock);
    if (!writeDataToFilterMQ(data)) {
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
    if (mFilterMQ.get() != NULL && mFilterMQ->write(data.data(), data.size())) {
        return true;
    }
    return false;
}

void Filter::attachFilterToRecord(const sp<Dvr> dvr) {
    ALOGD("%s/%d mFilterId:(%llu:%u)", __FUNCTION__, __LINE__, mExtendId, mFilterId);
    mDvr = dvr;
}

void Filter::detachFilterFromRecord() {
    ALOGD("%s/%d mFilterId:(%llu:%u)", __FUNCTION__, __LINE__, mExtendId, mFilterId);
    mDvr = nullptr;
}

int Filter::createAvIonFd(int size) {
    // Create an DMA-BUF fd and allocate an av fd mapped to a buffer to it.
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

void Filter::releaseIonBuffer(uint8_t* avBuf, int size) {
    if (avBuf != NULL) {
        munmap(avBuf, size);
    }
}

Result Filter::createIndependentMediaEvents(vector<uint8_t> output) {
    int av_fd = createAvIonFd(output.size());
    if (av_fd == -1) {
        return Result::UNKNOWN_ERROR;
    }
    // copy the filtered data to the buffer
    uint8_t* avBuffer = getIonBuffer(av_fd, output.size());
    if (avBuffer == NULL) {
        return Result::UNKNOWN_ERROR;
    }
    memcpy(avBuffer, output.data(), output.size() * sizeof(uint8_t));

    native_handle_t* nativeHandle = createNativeHandle(av_fd);
    if (nativeHandle == NULL) {
        releaseIonBuffer(avBuffer, output.size());
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
            .dataLength = static_cast<uint32_t>(output.size()),
            .avDataId = dataId,
    };
    if (mPts) {
        mediaEvent.pts = mPts;
        mPts = 0;
    }
    int size = mFilterEvent.events.size();
    mFilterEvent.events.resize(size + 1);
    mFilterEvent.events[size].media(mediaEvent);

    // Clear and log
    releaseIonBuffer(avBuffer, output.size());
    output.clear();
    mAvBufferCopyCount = 0;
    ::close(av_fd);
    if (DEBUG_FILTER) {
        ALOGD("[Filter] av data length %d", mediaEvent.dataLength);
    }
    return Result::SUCCESS;
}

Result Filter::createShareMemMediaEvents(vector<uint8_t> output) {
    // copy the filtered data to the shared buffer
    uint8_t* sharedAvBuffer = getIonBuffer(mSharedAvMemHandle.getNativeHandle()->data[0],
                                           output.size() + mSharedAvMemOffset);
    if (sharedAvBuffer == NULL) {
        releaseIonBuffer(sharedAvBuffer, output.size() + mSharedAvMemOffset);
        return Result::UNKNOWN_ERROR;
    }
    memcpy(sharedAvBuffer + mSharedAvMemOffset, output.data(), output.size() * sizeof(uint8_t));

    // Create a memory handle with numFds == 0
    /*
     * The logic is like this, ignoring.
     */
    /* coverity[negative_returns:SUPPRESS] */
    native_handle_t* nativeHandle = createNativeHandle(-1);
    if (nativeHandle == NULL) {
        return Result::UNKNOWN_ERROR;
    }
    hidl_handle handle;
    handle.setTo(nativeHandle, /*shouldOwn=*/true);

    // Create mediaEvent and send callback
    DemuxFilterMediaEvent mediaEvent;
    mediaEvent = {
            .offset = static_cast<uint32_t>(mSharedAvMemOffset),
            .dataLength = static_cast<uint32_t>(output.size()),
            .avMemory = handle,
    };
    mSharedAvMemOffset += output.size();
    if (mPts) {
        mediaEvent.pts = mPts;
        mPts = 0;
    }
    int size = mFilterEvent.events.size();
    mFilterEvent.events.resize(size + 1);
    mFilterEvent.events[size].media(mediaEvent);

    // Clear and log
    releaseIonBuffer(sharedAvBuffer, output.size() + mSharedAvMemOffset);
    output.clear();
    if (DEBUG_FILTER) {
        ALOGD("[Filter] shared av data length %d", mediaEvent.dataLength);
    }
    return Result::SUCCESS;
}

bool Filter::sameFile(int fd1, int fd2) {
    struct stat stat1, stat2;
    if (fstat(fd1, &stat1) < 0 || fstat(fd2, &stat2) < 0) {
        return false;
    }
    return (stat1.st_dev == stat2.st_dev) && (stat1.st_ino == stat2.st_ino);
}

DemuxFilterEvent Filter::createMediaEvent() {
    DemuxFilterEvent event;
    event.events.resize(1);

    event.events[0].media({
            .streamId = 1,
            .isPtsPresent = true,
            .pts = 2,
            .dataLength = 3,
            .offset = 4,
            .isSecureMemory = true,
            .mpuSequenceNumber = 6,
            .isPesPrivateData = true,
    });

    event.events[0].media().extraMetaData.audio({
            .adFade = 1,
            .adPan = 2,
            .versionTextTag = 3,
            .adGainCenter = 4,
            .adGainFront = 5,
            .adGainSurround = 6,
    });

    int av_fd = createAvIonFd(BUFFER_SIZE_16M);
    if (av_fd == -1) {
        return event;
    }

    native_handle_t* nativeHandle = createNativeHandle(av_fd);
    if (nativeHandle == NULL) {
        ::close(av_fd);
        ALOGE("[Filter] Failed to create native_handle %d", errno);
        return event;
    }

    // Create a dataId and add a <dataId, av_fd> pair into the dataId2Avfd map
    uint64_t dataId = mLastUsedDataId++ /*createdUID*/;
    mDataId2Avfd[dataId] = dup(av_fd);
    event.events[0].media().avDataId = dataId;

    hidl_handle handle;
    handle.setTo(nativeHandle, /*shouldOwn=*/true);
    event.events[0].media().avMemory = std::move(handle);
    ::close(av_fd);

    return event;
}


DemuxFilterEvent Filter::createTsRecordEvent() {
    DemuxFilterEvent event;
    event.events.resize(1);

    DemuxPid pid;
    pid.tPid(1);
    DemuxFilterTsRecordEvent::ScIndexMask mask;
    mask.sc(1);
    event.events[0].tsRecord({
            .pid = pid,
            .tsIndexMask = 1,
            .scIndexMask = mask,
            .byteNumber = 2,
    });
    return event;
}

V1_1::DemuxFilterEventExt Filter::createTsRecordEventExt() {
    V1_1::DemuxFilterEventExt event;
    event.events.resize(1);

    event.events[0].tsRecord({
            .pts = 1,
            .firstMbInSlice = 2,  // random address
    });
    return event;
}

DemuxFilterEvent Filter::createMmtpRecordEvent() {
    DemuxFilterEvent event;
    event.events.resize(1);

    event.events[0].mmtpRecord({
            .scHevcIndexMask = 1,
            .byteNumber = 2,
    });
    return event;
}

V1_1::DemuxFilterEventExt Filter::createMmtpRecordEventExt() {
    V1_1::DemuxFilterEventExt event;
    event.events.resize(1);

    event.events[0].mmtpRecord({
            .pts = 1,
            .mpuSequenceNumber = 2,
            .firstMbInSlice = 3,
            .tsIndexMask = 4,
    });
    return event;
}

DemuxFilterEvent Filter::createSectionEvent() {
    DemuxFilterEvent event;
    event.events.resize(1);

    event.events[0].section({
            .tableId = 1,
            .version = 2,
            .sectionNum = 3,
            .dataLength = 0,
    });
    return event;
}

DemuxFilterEvent Filter::createPesEvent() {
    DemuxFilterEvent event;
    event.events.resize(1);

    event.events[0].pes({
            .streamId = static_cast<DemuxStreamId>(1),
            .dataLength = 1,
            .mpuSequenceNumber = 2,
    });
    return event;
}

DemuxFilterEvent Filter::createDownloadEvent() {
    DemuxFilterEvent event;
    event.events.resize(1);

    event.events[0].download({
            .itemId = 1,
            .mpuSequenceNumber = 2,
            .itemFragmentIndex = 3,
            .lastItemFragmentIndex = 4,
            .dataLength = 0,
    });
    return event;
}

DemuxFilterEvent Filter::createIpPayloadEvent() {
    DemuxFilterEvent event;
    event.events.resize(1);

    event.events[0].ipPayload({
            .dataLength = 0,
    });
    return event;
}

DemuxFilterEvent Filter::createTemiEvent() {
    DemuxFilterEvent event;
    event.events.resize(1);

    event.events[0].temi({.pts = 1, .descrTag = 2, .descrData = {3}});
    return event;
}

V1_1::DemuxFilterEventExt Filter::createMonitorEvent() {
    V1_1::DemuxFilterEventExt event;
    event.events.resize(1);

    V1_1::DemuxFilterMonitorEvent monitor;
    monitor.scramblingStatus(V1_1::ScramblingStatus::SCRAMBLED);
    event.events[0].monitorEvent(monitor);
    return event;
}

V1_1::DemuxFilterEventExt Filter::createRestartEvent() {
    V1_1::DemuxFilterEventExt event;
    event.events.resize(1);

    event.events[0].startId(1);
    return event;
}

DemuxFilterType Filter::getFilterType() {
    return mType;
}

bool Filter::isRawData() {
    return bIsRaw;
}

timeval Filter::getTableStartTime() {
    return mTable_start;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
