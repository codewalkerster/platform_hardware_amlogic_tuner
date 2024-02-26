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
#define LOG_TAG "tunerhal2.0-Descrambler"

#include <aidl/android/hardware/tv/tuner/IFrontendCallback.h>
#include <aidl/android/hardware/tv/tuner/Result.h>
#include <utils/Log.h>
#include <cutils/properties.h>

#include "Descrambler.h"
#include "FileSystemIo.h"

// for NSK_DESCRAMBLER
// dsm: Change Maximum property number [1/1]
// #define DSM_PROP_CUSTOM_1 (DSM_PROP_ENC_SLOT_READY + 1)
#define DSM_PROP_IS_NSK_CM_KEYSLOT DSM_PROP_CUSTOM_1

// for synamedia NSK descrambler requirements
#define EXTRACT_CM_NSK_ALGO_TYPE(x) ((uint32_t)((x) >> (30)))
#define EXTRACT_CM_SLOT_ID(x) ((uint32_t)(~(3 << 30)) & (x))
#define ADD_IV_FLAG_PREFIX(x) ((uint32_t)((1) << 31) | (x))

enum ca_algo_type {
  CA_ALGO_TYPE_LDE = 0,
  CA_ALGO_TYPE_ESA,
  CA_ALGO_TYPE_LEN,
  CA_ALGO_TYPE_MAX
};

#define ADD_DSC_TYPE_FLAG_WITH_PID(t, pid) ((uint16_t((t) << 14)) | (pid))

namespace aidl {
namespace android {
namespace hardware {
namespace tv {
namespace tuner {

Descrambler::Descrambler(int32_t in_dscId,     std::shared_ptr<Tuner> in_tuner) {
  mDescramblerId = in_dscId;
  mTuner = in_tuner;
  if (mTuner != nullptr)
      mDscType = mTuner->getDscMode();
  mSourceDemuxId = 0;
  mKeyToken = 0;

  FileSystem_create();
  getTsnSourceStatus(&mIsLocalMode);

#ifdef SUPPORT_TSD
  if (mDscType == CA_DSC_TSD_TYPE) {
    mDscType = FileSystem_getPropertyInt(TUNERHAL_DSC_TYPE_PROP, CA_DSC_COMMON_TYPE);
    TUNER_DSC_DBG(mDescramblerId, "getProperty mDscType:%d", mDscType);
  }
#endif
}

Descrambler::~Descrambler() {
    TUNER_DSC_TRACE(mDescramblerId);
}

::ndk::ScopedAStatus Descrambler::setDemuxSource(int32_t in_demuxId) {
    TUNER_DSC_TRACE(mDescramblerId);
    std::lock_guard<std::mutex> lock(mDescrambleLock);
    if (mDemuxSet) {
      TUNER_DSC_WRAN(mDescramblerId,
          "[   WARN   ] Descrambler has already been set with a demux id %" PRIu32, mSourceDemuxId);
      return ::ndk::ScopedAStatus::fromServiceSpecificError(
          static_cast<int32_t>(Result::INVALID_STATE));
    }
    mDemuxSet = true;
    mSourceDemuxId = in_demuxId;
    if (mTuner != nullptr)
        mTuner->attachDescramblerToDemux(mDescramblerId, in_demuxId);

    TUNER_DSC_INFO(mDescramblerId, "source dmx id:%d dsc id:%d", mSourceDemuxId, mDescramblerId);
    mDemux = mTuner->getDemuxById(in_demuxId);
    mType = checkPlayType();
    TUNER_DSC_INFO(mDescramblerId, "type = %d", mType);
    if (mType == DEMOD_LIVE) {
        TUNER_DSC_INFO(mDescramblerId, "playback type is demod live");
    } else {
        TUNER_DSC_INFO(mDescramblerId, "type is playback or record");
        ::ndk::ScopedAStatus::ok();
    }
    mDsmFd = DSM_OpenSession(0);
    TUNER_DSC_DBG(mDescramblerId, "mIsLocalMode:%d mDscType:%d mDsmFd:%d", \
      mIsLocalMode, mDscType, mDsmFd);

    if (ca_open(in_demuxId) != CA_DSC_OK) {
        TUNER_DSC_ERR(mDescramblerId, "ca_open ca%d failed!", in_demuxId);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
            static_cast<int32_t>(Result::INVALID_STATE));
    }

  return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Descrambler::setKeyToken(const std::vector<uint8_t>&  in_keyToken) {
  std::lock_guard<std::mutex> lock(mDescrambleLock);
  TUNER_DSC_TRACE(mDescramblerId);

  uint32_t tempToken = 0;
  uint32_t tokenSize = in_keyToken.size();

  if (tokenSize == 0) {
      TUNER_DSC_ERR(mDescramblerId, "tokenSize is 0!");
          return ::ndk::ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Result::INVALID_ARGUMENT));
  }

  for (int tokenIdx = sizeof(mKeyToken) - 1; tokenIdx >= 0; --tokenIdx) {
      tempToken = (tempToken << 8) | in_keyToken[tokenIdx];
  }

  if (tempToken == mKeyToken) {
      TUNER_DSC_DBG(mDescramblerId, "The same keyToken:0x%x", tempToken);
      return ::ndk::ScopedAStatus::ok();
  } else {
      mKeyToken = tempToken;
      if (mIsReady && mDsmFd >= 0 && mType == DEMOD_LIVE) {
          TUNER_DSC_DBG(mDescramblerId, "Reset keyToken");
          DSM_CloseSession(mDsmFd);
          if (mIsNskDsc) {
              clearNskDscChannels();
          } else {
              clearDscChannels();
          }
          mIsReady = false;
          mDsmFd = DSM_OpenSession(0);
      }
  }
  TUNER_DSC_DBG(mDescramblerId, "mKeyToken:0x%x", mKeyToken);
    if (mType == DEMOD_LIVE) {
        TUNER_DSC_INFO(mDescramblerId, "playback type is demod live");
    } else {
        set<uint16_t>::iterator it;
        for (it = mAddedPid.begin(); it != mAddedPid.end(); it++) {
            uint16_t mPid = *it;
            if (mPid != -1 && mKeyToken != 0) {
                DVR_Result_t ret = setKeyToken(mType, mPid, mKeyToken);
                if (ret != DVR_SUCCESS) {
                    TUNER_DSC_ERR(mDescramblerId, "set key token fail");
                }
            }
        }
        return ::ndk::ScopedAStatus::ok();
    }

  int ret = DSM_BindToken(mDsmFd, mKeyToken);
  if (ret)
      TUNER_DSC_WRAN(mDescramblerId, "DSM_BindToken failed! %s", strerror(errno));

#ifdef SUPPORT_TSD
  uint32_t dsmDscType = DSM_PROP_SC2_DSC_TYPE_INVALID;
  if (mDscType == CA_DSC_COMMON_TYPE)
    dsmDscType = DSM_PROP_SC2_DSC_TYPE_TSN;
  else if (mDscType == CA_DSC_TSD_TYPE)
    dsmDscType = DSM_PROP_SC2_DSC_TYPE_TSD;
  else if (mDscType == CA_DSC_TSE_TYPE)
    dsmDscType = DSM_PROP_SC2_DSC_TYPE_TSE;
  TUNER_DSC_DBG(mDescramblerId, "set dsmDscType:%d", dsmDscType);
  if (!ret && DSM_SetProperty(mDsmFd, DSM_PROP_SC2_DSC_TYPE, dsmDscType)) {
    TUNER_DSC_ERR(mDescramblerId, "DSM_SetProperty failed! %s", strerror(errno));
      return ::ndk::ScopedAStatus::fromServiceSpecificError(
          static_cast<int32_t>(Result::INVALID_STATE));
  }
#endif

  return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Descrambler::addPid(
        const DemuxPid& in_pid,
        const std::shared_ptr<IFilter>& in_optionalSourceFilter) {
  std::lock_guard<std::mutex> lock(mDescrambleLock);
  (void)in_optionalSourceFilter;

  uint16_t mPid = in_pid.get<DemuxPid::Tag::tPid>() & 0x1FFF;
  TUNER_DSC_INFO(mDescramblerId, "mPid: 0x%x", mPid);
  // Assume transport stream pid only.
  mAddedPid.insert(mPid);

  if (mType == DEMOD_LIVE) {
      TUNER_DSC_INFO(mDescramblerId, "playback type is demod live");
  } else {
      if (mPid != -1 && mKeyToken != 0) {
          DVR_Result_t ret = setKeyToken(mType, mPid, mKeyToken);
          if (ret != DVR_SUCCESS) {
              TUNER_DSC_ERR(mDescramblerId, "set key token fail");
              return ::ndk::ScopedAStatus::fromServiceSpecificError(
                  static_cast<int32_t>(Result::INVALID_STATE));
          }
          return ::ndk::ScopedAStatus::ok();
      } else {
          TUNER_DSC_ERR(mDescramblerId, "pid = %d or mCasSessionToken = %d is not ready", mPid, mKeyToken);
          return ::ndk::ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Result::INVALID_STATE));
      }
  }

  if (mIsNskDsc) {
    bool isChannelFound = false;

    if (mPidToDscChannel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
            CA_DSC_COMMON_TYPE, mPid)) != mPidToDscChannel.end()) {
      isChannelFound = true;
    }
    if (mPidToDscChannel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
            CA_DSC_TSD_TYPE, mPid)) != mPidToDscChannel.end()) {
      isChannelFound = true;
    }
    if (mPidToDscChannel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
            CA_DSC_TSE_TYPE, mPid)) != mPidToDscChannel.end()) {
      isChannelFound = true;
    }

    if (!isChannelFound && mIsReady) {
      int ca_esa_index = -1;
      int ca_lde_index = -1;
      int ca_len_index = -1;

      for (int i = 0; i < mKeyslotList.count; i++) {
        int algo_type = EXTRACT_CM_NSK_ALGO_TYPE(mKeyslotList.keyslots[i].id);
        int key_parity_type = -1;
        int algorithm = mKeyslotList.keyslots[i].algo;
        uint32_t kte_id = EXTRACT_CM_SLOT_ID(mKeyslotList.keyslots[i].id);

        uint32_t kt_parity = mKeyslotList.keyslots[i].parity;
        uint32_t kt_is_iv = mKeyslotList.keyslots[i].is_iv;

        if (kt_parity == DSM_PARITY_NONE) {
          key_parity_type = kt_is_iv ? CA_KEY_00_IV_TYPE : CA_KEY_00_TYPE;
        } else if (kt_parity == DSM_PARITY_EVEN) {
          key_parity_type = kt_is_iv ? CA_KEY_EVEN_IV_TYPE : CA_KEY_EVEN_TYPE;
        } else if (kt_parity == DSM_PARITY_ODD) {
          key_parity_type = kt_is_iv ? CA_KEY_ODD_IV_TYPE : CA_KEY_ODD_TYPE;
        } else {
          return ::ndk::ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Result::INVALID_ARGUMENT));
        }

        if (mKeyslotList.keyslots[i].is_iv) {
          kte_id = ADD_IV_FLAG_PREFIX(kte_id);
        }

        TUNER_DSC_DBG(mDescramblerId,
            "ALGO TYPE %d, keyslot id %x, algorithm = %d, key_parity_type = %d",
            algo_type, kte_id, algorithm, key_parity_type);

        if (algo_type == CA_ALGO_TYPE_ESA) {
          if (ca_esa_index == -1) {
            TUNER_DSC_DBG(mDescramblerId,
                "Allocate new esa(tsn common) ca channel for pid 0x%04x "
                "algorithm %d", mPid, algorithm);
            ca_esa_index = ca_alloc_chan(mSourceDemuxId, mPid, algorithm,
                                         CA_DSC_COMMON_TYPE);

            if (ca_esa_index < 0) {
              TUNER_DSC_ERR(mDescramblerId, "FAILED to allocate esa channel for pid %x", mPid);
              return ::ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Result::INVALID_STATE));
            } else {
              mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
                  CA_DSC_COMMON_TYPE, mPid)] = ca_esa_index;
            }
          }
          TUNER_DSC_DBG(mDescramblerId,  "set esa key type %d to index %d kte %x ",
              key_parity_type, ca_esa_index, kte_id);
          if (ca_set_key(mSourceDemuxId, ca_esa_index, key_parity_type,
                         kte_id)) {
            TUNER_DSC_ERR(mDescramblerId, "ca_set_key(%d, %d, %d) failed",
                ca_esa_index, key_parity_type, kte_id);
          return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_STATE));
          }
        } else if (algo_type == CA_ALGO_TYPE_LDE) {
          if (ca_lde_index == -1) {
            TUNER_DSC_DBG(mDescramblerId,
                "Allocate new lde(tsd) ca channel for pid 0x%04x algorithm %d",
                mPid, algorithm);
            ca_lde_index =
                ca_alloc_chan(mSourceDemuxId, mPid, algorithm, CA_DSC_TSD_TYPE);

            if (ca_lde_index < 0) {
              TUNER_DSC_ERR(mDescramblerId, "FAILED to allocate lde channel for pid %x", mPid);
              return ::ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Result::INVALID_STATE));
            } else {
              mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
                  CA_DSC_TSD_TYPE, mPid)] = ca_lde_index;
            }
          }
          TUNER_DSC_DBG(mDescramblerId, "set lde key type %d to index %d kte %x ",
              key_parity_type, ca_lde_index, kte_id);
          if (ca_set_key(mSourceDemuxId, ca_lde_index, key_parity_type,
                         kte_id)) {
            TUNER_DSC_ERR(mDescramblerId,
                "ca_set_key(%d, %d, %d) failed", ca_lde_index, key_parity_type, kte_id);
            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                  static_cast<int32_t>(Result::INVALID_STATE));
          }
        } else if (algo_type == CA_ALGO_TYPE_LEN) {
          if (ca_len_index == -1) {
            TUNER_DSC_DBG(mDescramblerId,
                "Allocate new len(tse) ca channel for pid 0x%04x algorithm %d",
                mPid, algorithm);
            ca_len_index =
                ca_alloc_chan(mSourceDemuxId, mPid, algorithm, CA_DSC_TSE_TYPE);

            if (ca_len_index < 0) {
              TUNER_DSC_ERR(mDescramblerId, "FAILED to allocate lde channel for pid %x", mPid);
              return ::ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Result::INVALID_STATE));
            } else {
              mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
                  CA_DSC_TSE_TYPE, mPid)] = ca_len_index;
            }
          }

          TUNER_DSC_DBG(mDescramblerId, "set len key type %d to index %d kte %x ",
              key_parity_type, ca_len_index, kte_id);
          if (ca_set_key(mSourceDemuxId, ca_len_index, key_parity_type, kte_id)) {
            TUNER_DSC_ERR(mDescramblerId, "ca_set_key(%d, %d, %d) failed",
                ca_len_index, key_parity_type, kte_id);
            return ::ndk::ScopedAStatus::fromServiceSpecificError(
                  static_cast<int32_t>(Result::INVALID_STATE));
          }
        } else {
          TUNER_DSC_ERR(mDescramblerId, "Invalid CA algorithm type to set: %d\n", algo_type);
          ca_close(mSourceDemuxId);
          return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_STATE));
        }
      }
    }
  } else {
    if (mPidToDscChannel.find(mPid) == mPidToDscChannel.end() && mIsReady) {
      int handle = ca_alloc_chan(mSourceDemuxId, mPid, mDscAlgo, mDscType);
      if (handle < 0) {
        TUNER_DSC_ERR(mDescramblerId, "ca_alloc_chan failed!");
        return ::ndk::ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Result::INVALID_STATE));
      } else {
        if (!bindDscChannelToKeyTable(mSourceDemuxId, handle)) {
          return ::ndk::ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::INVALID_STATE));
        } else {
          mPidToDscChannel[mPid] = handle;
        }
      }
      TUNER_DSC_DBG(mDescramblerId, "ca_alloc_chan(0x%x 0x%x) ok.",
          mPid, mPidToDscChannel[mPid]);
    }
  }

  return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Descrambler::removePid(
        const DemuxPid& in_pid,
        const std::shared_ptr<IFilter>& in_optionalSourceFilter) {
  std::lock_guard<std::mutex> lock(mDescrambleLock);
  (void)in_optionalSourceFilter;

  uint16_t mPid = in_pid.get<DemuxPid::Tag::tPid>() & 0x1FFF;

  TUNER_DSC_INFO(mDescramblerId, "mPid:0x%x", mPid);

   if (mType == DEMOD_LIVE) {
        TUNER_DSC_INFO(mDescramblerId, "playback type is demod live");
   } else {
       if (mType == DVR_PLAYBACK) {
            if (mPlaybackhandle) {
                DVR_Result_t result = DVR_FAILURE;
                result = dvr_playback_set_key_token(mPlaybackhandle, mPid, -1);
                if (result != DVR_SUCCESS) {
                    TUNER_DSC_ERR(mDescramblerId, "unset playback key token fail");
                }
            } else {
                TUNER_DSC_ERR(mDescramblerId, "mPlaybackhandle is null");
            }
       } else if (mType == DVR_RECORD) {
           TUNER_DSC_INFO(mDescramblerId, "to do unset record key token");
       }
       return ::ndk::ScopedAStatus::ok();
   }
  if (mIsNskDsc) {
    if (mPidToDscChannel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
          CA_DSC_COMMON_TYPE, mPid)) != mPidToDscChannel.end()) {
    ca_free_chan(mSourceDemuxId,
                 mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
                     CA_DSC_COMMON_TYPE, mPid)]);
    mPidToDscChannel.erase(
        ADD_DSC_TYPE_FLAG_WITH_PID(CA_DSC_COMMON_TYPE, mPid));
    }
    if (mPidToDscChannel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
          CA_DSC_TSD_TYPE, mPid)) != mPidToDscChannel.end()) {
    ca_free_chan(mSourceDemuxId,
                 mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
                     CA_DSC_TSD_TYPE, mPid)]);
    mPidToDscChannel.erase(
        ADD_DSC_TYPE_FLAG_WITH_PID(CA_DSC_TSD_TYPE, mPid));
    }
    if (mPidToDscChannel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
          CA_DSC_TSE_TYPE, mPid)) != mPidToDscChannel.end()) {
    ca_free_chan(mSourceDemuxId,
                 mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
                     CA_DSC_TSE_TYPE, mPid)]);
    mPidToDscChannel.erase(
        ADD_DSC_TYPE_FLAG_WITH_PID(CA_DSC_TSE_TYPE, mPid));
    }
  } else {
    if (mPidToDscChannel.find(mPid) != mPidToDscChannel.end()) {
      ca_free_chan(mSourceDemuxId, mPidToDscChannel[mPid]);
      mPidToDscChannel.erase(mPid);
    }
  }

  mAddedPid.erase(mPid);

  return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus Descrambler::close() {
  TUNER_DSC_TRACE(mDescramblerId);

  if (mTuner != nullptr) {
    mTuner->detachDescramblerFromDemux(mDescramblerId, mSourceDemuxId);
    mTuner->removeDescrambler(mDescramblerId);
  }

  {
    std::lock_guard<std::mutex> lock(mDescrambleLock);
      TUNER_DSC_INFO(mDescramblerId, "type = %d", mType);
      if (mType == DEMOD_LIVE) {
          TUNER_DSC_INFO(mDescramblerId, "playback type is demod live");
      } else {
          if (mType == DVR_PLAYBACK) {
              if (mPlaybackhandle) {
                  DVR_Result_t result = DVR_FAILURE;
                  set<uint16_t>::iterator it;
                  for (it = mAddedPid.begin(); it != mAddedPid.end(); it++) {
                      result = dvr_playback_set_key_token(mPlaybackhandle, *it, -1);
                      if (result != DVR_SUCCESS) {
                          TUNER_DSC_ERR(mDescramblerId, "unset playback pid = %d key token fail", *it);
                      }
                  }
              } else {
                  TUNER_DSC_ERR(mDescramblerId, "mPlaybackhandle is null");
              }
          } else if (mType == DVR_RECORD) {
              TUNER_DSC_ERR(mDescramblerId, "to do unset record key token");
          }
        mAddedPid.clear();
        return ::ndk::ScopedAStatus::ok();
      }

    if (mIsNskDsc) {
      clearNskDscChannels();
      mIsNskDsc = 0;
    } else {
      clearDscChannels();
    }

    if (mDsmFd >= 0) {
      DSM_CloseSession(mDsmFd);
      mDsmFd = -1;
    }

    if (mDemuxSet) {
      ca_close(mSourceDemuxId);
      mDemuxSet = false;
    }
  }

  return ::ndk::ScopedAStatus::ok();
}

bool Descrambler::isPidSupported(uint16_t in_pid) {
  std::lock_guard<std::mutex> lock(mDescrambleLock);

  TUNER_DSC_VERB(mDescramblerId, "pid: 0x%x", in_pid);

  return mAddedPid.find(in_pid) != mAddedPid.end();
}

bool Descrambler::bindDscChannelToKeyTable(uint32_t in_dscDevId, uint32_t in_dscHandle) {
  for (int i = 0; i < mKeyslotList.count; i++) {
    uint32_t kt_parity = mKeyslotList.keyslots[i].parity;
    uint32_t kt_is_iv = mKeyslotList.keyslots[i].is_iv;
    uint32_t kt_type = 0, kt_id = 0;
    if (kt_parity == DSM_PARITY_NONE)
      kt_type = kt_is_iv ? CA_KEY_00_IV_TYPE : CA_KEY_00_TYPE;
    else if (kt_parity == DSM_PARITY_EVEN)
      kt_type = kt_is_iv ? CA_KEY_EVEN_IV_TYPE : CA_KEY_EVEN_TYPE;
    else if (kt_parity == DSM_PARITY_ODD)
      kt_type = kt_is_iv ? CA_KEY_ODD_IV_TYPE : CA_KEY_ODD_TYPE;
    else
      return false;
    kt_id = mKeyslotList.keyslots[i].id;

    if (ca_set_key(in_dscDevId, in_dscHandle, kt_type, kt_id)) {
      TUNER_DSC_ERR(mDescramblerId, "ca_set_key(%d %d %d) failed!", in_dscHandle, kt_type, kt_id);
      return false;
    }
  }

  return true;
}

bool Descrambler::clearDscChannels() {
  TUNER_DSC_TRACE(mDescramblerId);

  set<uint16_t>::iterator it;
  for (it = mAddedPid.begin(); it != mAddedPid.end(); it++) {
    uint16_t mPid = *it;
    uint32_t dsc_chan;
    if (mPidToDscChannel.find(mPid) != mPidToDscChannel.end()) {
      dsc_chan = mPidToDscChannel[mPid];
      ca_free_chan(mSourceDemuxId, dsc_chan);
      mPidToDscChannel.erase(mPid);
    } else {
      continue;
    }
    TUNER_DSC_DBG(mDescramblerId, "ca_free_chan(0x%x 0x%x) ok.", mPid, dsc_chan);
  }

  return true;
}

bool Descrambler::allocDscChannels() {
  TUNER_DSC_TRACE(mDescramblerId);

  set<uint16_t>::iterator it;
  for (it = mAddedPid.begin(); it != mAddedPid.end(); it++) {
    uint16_t mPid = *it;
    int handle = ca_alloc_chan(mSourceDemuxId, mPid, mDscAlgo, mDscType);
    if (handle < 0) {
      TUNER_DSC_ERR(mDescramblerId, "ca_alloc_chan failed!");
      return false;
    } else {
      if (!bindDscChannelToKeyTable(mSourceDemuxId, handle)) {
        return false;
      } else {
        mPidToDscChannel[mPid] = handle;
      }
    }
    TUNER_DSC_DBG(mDescramblerId, "ca_alloc_chan(0x%x 0x%x) ok.", mPid, mPidToDscChannel[mPid]);
  }

  return true;
}

bool Descrambler::clearNskDscChannels() {
  // std::lock_guard<std::mutex> lock(mDescrambleLock);

  TUNER_DSC_TRACE(mDescramblerId);

  set<uint16_t>::iterator it;
  for (it = mAddedPid.begin(); it != mAddedPid.end(); it++) {
    uint16_t mPid = *it;
    uint32_t dsc_chan;
    if (mPidToDscChannel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
            CA_DSC_COMMON_TYPE, mPid)) != mPidToDscChannel.end()) {
      dsc_chan = mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
          CA_DSC_COMMON_TYPE, mPid)];
      ca_free_chan(mSourceDemuxId, dsc_chan);
      mPidToDscChannel.erase(
          ADD_DSC_TYPE_FLAG_WITH_PID(CA_DSC_COMMON_TYPE, mPid));

      TUNER_DSC_DBG(mDescramblerId, "ca_free_chan(0x%x 0x%x) ok.", mPid,
                    dsc_chan);
    }
    if (mPidToDscChannel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
            CA_DSC_TSD_TYPE, mPid)) != mPidToDscChannel.end()) {
      dsc_chan = mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
          CA_DSC_TSD_TYPE, mPid)];
      ca_free_chan(mSourceDemuxId, dsc_chan);
      mPidToDscChannel.erase(
          ADD_DSC_TYPE_FLAG_WITH_PID(CA_DSC_TSD_TYPE, mPid));

      TUNER_DSC_DBG(mDescramblerId, "ca_free_chan(0x%x 0x%x) ok.", mPid,
                    dsc_chan);
    }
    if (mPidToDscChannel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
            CA_DSC_TSE_TYPE, mPid)) != mPidToDscChannel.end()) {
      dsc_chan = mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
          CA_DSC_TSE_TYPE, mPid)];
      ca_free_chan(mSourceDemuxId, dsc_chan);
      mPidToDscChannel.erase(
          ADD_DSC_TYPE_FLAG_WITH_PID(CA_DSC_TSE_TYPE, mPid));

      TUNER_DSC_DBG(mDescramblerId, "ca_free_chan(0x%x 0x%x) ok.", mPid,
                    dsc_chan);
    }
  }

  return true;
}

bool Descrambler::allocNskDscChannels() {
  TUNER_DSC_TRACE(mDescramblerId);

  set<uint16_t>::iterator it;
  for (it = mAddedPid.begin(); it != mAddedPid.end(); it++) {
    uint16_t mPid = *it;

    int ca_esa_index = -1;
    int ca_lde_index = -1;
    int ca_len_index = -1;

    for (int i = 0; i < mKeyslotList.count; i++) {
      int algo_type = EXTRACT_CM_NSK_ALGO_TYPE(mKeyslotList.keyslots[i].id);
      int key_parity_type = -1;
      int algorithm = mKeyslotList.keyslots[i].algo;
      uint32_t kte_id = EXTRACT_CM_SLOT_ID(mKeyslotList.keyslots[i].id);

      uint32_t kt_parity = mKeyslotList.keyslots[i].parity;
      uint32_t kt_is_iv = mKeyslotList.keyslots[i].is_iv;

      if (kt_parity == DSM_PARITY_NONE)
        key_parity_type = kt_is_iv ? CA_KEY_00_IV_TYPE : CA_KEY_00_TYPE;
      else if (kt_parity == DSM_PARITY_EVEN)
        key_parity_type = kt_is_iv ? CA_KEY_EVEN_IV_TYPE : CA_KEY_EVEN_TYPE;
      else if (kt_parity == DSM_PARITY_ODD)
        key_parity_type = kt_is_iv ? CA_KEY_ODD_IV_TYPE : CA_KEY_ODD_TYPE;
      else
        return false;

      if (mKeyslotList.keyslots[i].is_iv) {
        kte_id = ADD_IV_FLAG_PREFIX(kte_id);
      }

      TUNER_DSC_DBG(mDescramblerId,
          "ALGO TYPE %d, keyslot id %x, algorithm = %d, key_parity_type = %d",
           algo_type, kte_id, algorithm, key_parity_type);

      if (algo_type == CA_ALGO_TYPE_ESA) {
        if (ca_esa_index == -1) {
          TUNER_DSC_DBG(mDescramblerId,
              "Allocate new esa(tsn common) ca channel for pid 0x%04x "
              "algorithm %d", mPid, algorithm);
          ca_esa_index = ca_alloc_chan(mSourceDemuxId, mPid, algorithm,
                                       CA_DSC_COMMON_TYPE);

          if (ca_esa_index < 0) {
            TUNER_DSC_ERR(mDescramblerId,
                "FAILED to allocate esa channel for pid %x", mPid);
            return false;
          } else {
            mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
                CA_DSC_COMMON_TYPE, mPid)] = ca_esa_index;
          }
        }
        TUNER_DSC_DBG(mDescramblerId, "set esa key type %d to index %d kte %x ",
            key_parity_type, ca_esa_index, kte_id);
        if (ca_set_key(mSourceDemuxId, ca_esa_index, key_parity_type, kte_id)) {
          TUNER_DSC_ERR(mDescramblerId, "ca_set_key(%d, %d, %d) failed",
              ca_esa_index, key_parity_type, kte_id);
          return false;
        }
      } else if (algo_type == CA_ALGO_TYPE_LDE) {
        if (ca_lde_index == -1) {
          TUNER_DSC_DBG(mDescramblerId,
              "Allocate new lde(tsd) ca channel for pid 0x%04x algorithm %d",
              mPid, algorithm);
          ca_lde_index =
              ca_alloc_chan(mSourceDemuxId, mPid, algorithm, CA_DSC_TSD_TYPE);

          if (ca_lde_index < 0) {
            TUNER_DSC_ERR(mDescramblerId,
                "FAILED to allocate lde channel for pid %x", mPid);
            return false;
          } else {
            mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
                CA_DSC_TSD_TYPE, mPid)] = ca_lde_index;
          }
        }
        TUNER_DSC_DBG(mDescramblerId, "set lde key type %d to index %d kte %x ",
            key_parity_type, ca_lde_index, kte_id);
        if (ca_set_key(mSourceDemuxId, ca_lde_index, key_parity_type, kte_id)) {
          TUNER_DSC_ERR(mDescramblerId,
              "ca_set_key(%d, %d, %d) failed", ca_lde_index, key_parity_type, kte_id);
          return false;
        }
      } else if (algo_type == CA_ALGO_TYPE_LEN) {
        if (ca_len_index == -1) {
          TUNER_DSC_DBG(mDescramblerId,
              "Allocate new len(tse) ca channel for pid 0x%04x algorithm %d",
              mPid, algorithm);
          ca_len_index =
              ca_alloc_chan(mSourceDemuxId, mPid, algorithm, CA_DSC_TSE_TYPE);

          if (ca_len_index < 0) {
            TUNER_DSC_ERR(mDescramblerId,
                "FAILED to allocate lde channel for pid %x", mPid);
            return false;
          } else {
            mPidToDscChannel[ADD_DSC_TYPE_FLAG_WITH_PID(
                CA_DSC_TSE_TYPE, mPid)] = ca_len_index;
          }
        }

        TUNER_DSC_DBG(mDescramblerId,
            "set len key type %d to index %d kte %x ",
            key_parity_type, ca_len_index, kte_id);
        if (ca_set_key(mSourceDemuxId, ca_len_index, key_parity_type, kte_id)) {
          TUNER_DSC_ERR(mDescramblerId,
              "ca_set_key(%d, %d, %d) failed", ca_len_index, key_parity_type, kte_id);
          return false;
        }
      } else {
        TUNER_DSC_ERR(mDescramblerId,
            "Invalid CA algorithm type to set: %d\n", algo_type);
        ca_close(mSourceDemuxId);
        return false;
      }
    }
  }
  TUNER_DSC_DBG(mDescramblerId, "keytable allocation has been finished successfully");
  return true;
}

bool Descrambler::isDescramblerReady() {
  std::lock_guard<std::mutex> lock(mDescrambleLock);

  if (!mIsReady && mDsmFd >= 0) {
    int dsmRet = -1;
    uint32_t mIsKtReady = DSM_PROP_SLOT_NOT_READY;
    dsmRet = DSM_GetProperty(mDsmFd, DSM_PROP_DEC_SLOT_READY, &mIsKtReady);
    if (dsmRet != 0 || mIsKtReady != DSM_PROP_SLOT_IS_READY) {
      dsmRet = DSM_GetProperty(mDsmFd, DSM_PROP_ENC_SLOT_READY, &mIsKtReady);
      if (dsmRet != 0 || mIsKtReady != DSM_PROP_SLOT_IS_READY)
        return mIsReady;
      else
        mIsEnc = true;
    } else {
      mIsEnc = false;
    }

    if (DSM_GetProperty(mDsmFd, DSM_PROP_IS_NSK_CM_KEYSLOT, &mIsNskDsc) == 0) {
      TUNER_DSC_DBG(mDescramblerId, "DSM_PROP_IS_NSK_CM_KEYSLOT is set value = %d", mIsNskDsc);
    }
    memset(&mKeyslotList, 0, sizeof(dsm_keyslot_list));
    if (DSM_GetKeySlots(mDsmFd, &mKeyslotList)) {
      TUNER_DSC_ERR(mDescramblerId, "DSM_GetKeySlots failed! %s", strerror(errno));
      return mIsReady;
    }
    TUNER_DSC_DBG(mDescramblerId, "mKeyslotList count:%d mIsEnc:%d", mKeyslotList.count, mIsEnc);
    for (int i = 0; i < mKeyslotList.count; i++) {
      if (!mIsEnc && mKeyslotList.keyslots[i].is_enc == 0) {
        mDscAlgo = mKeyslotList.keyslots[i].algo;
        break;
      } else if (mIsEnc && mKeyslotList.keyslots[i].is_enc != 0) {
        mDscAlgo = mKeyslotList.keyslots[i].algo;
        break;
      }
    }
    if (mDscAlgo == CA_ALGO_UNKNOWN)
      return mIsReady;
    uint32_t dsm_dsc_type = DSM_PROP_SC2_DSC_TYPE_TSN;
    if (DSM_GetProperty(mDsmFd, DSM_PROP_SC2_DSC_TYPE, &dsm_dsc_type))
      TUNER_DSC_WRAN(mDescramblerId, "Get dsm_dsc_type failed! %s", strerror(errno));
    else
      TUNER_DSC_DBG(mDescramblerId, "Get dsm_dsc_type:%d from cas", dsm_dsc_type);
    if (dsm_dsc_type == DSM_PROP_SC2_DSC_TYPE_TSN)
      mDscType = CA_DSC_COMMON_TYPE;
    else if (dsm_dsc_type == DSM_PROP_SC2_DSC_TYPE_TSD)
      mDscType = CA_DSC_TSD_TYPE;
    else if (dsm_dsc_type == DSM_PROP_SC2_DSC_TYPE_TSE)
      mDscType = CA_DSC_TSE_TYPE;

    if (mIsNskDsc) {
      if (!allocNskDscChannels()) {
        clearNskDscChannels();
        return mIsReady;
      }
    } else {
      if (!allocDscChannels()) {
        clearDscChannels();
        return mIsReady;
      }
    }
    mIsReady = true;
  }

  return mIsReady;
}

bool Descrambler::getTsnSourceStatus(bool *out_isLocalMode) {
  TUNER_DSC_TRACE(mDescramblerId);
  char tsnSource[32] = {0};

  if (!out_isLocalMode)
    return false;

  if (!FileSystem_readFile(TSN_SOURCE, tsnSource, sizeof(tsnSource))) {
      if (strstr(tsnSource, TSN_LOCAL)) {
          *out_isLocalMode = true;
      } else if (strstr(tsnSource, TSN_DEMOD)) {
          *out_isLocalMode = false;
      } else {
          TUNER_DSC_WRAN(mDescramblerId, "unknown tsn source %s!", tsnSource);
          return false;
      }
  } else {
      TUNER_DSC_WRAN(mDescramblerId, "read tsn source failed!");
      return false;
  }

  return true;
}

DVR_Result_t Descrambler::setKeyToken(PlayType type, int pid, int token) {
    DVR_Result_t result = DVR_FAILURE;
    TUNER_DSC_ERR(mDescramblerId, "start to set pid = %d key token", pid);

    if (type == DVR_PLAYBACK) {
        mPlaybackhandle = mDemux->getPlaybackHandle();
        if (mPlaybackhandle) {
            result = dvr_playback_set_key_token(mPlaybackhandle, pid, token);
            if (result != DVR_SUCCESS) {
                TUNER_DSC_ERR(mDescramblerId, "set playback key token fail");
            }
        } else {
            TUNER_DSC_ERR(mDescramblerId, "mPlaybackhandle is null");
        }
    } else if (type == DVR_RECORD) {
        mRecordHandle = mDemux->getRecordHandle();
        if (mRecordHandle) {
            result = dvr_record_set_key_token(mRecordHandle, pid, token);
            if (result != DVR_SUCCESS) {
                TUNER_DSC_ERR(mDescramblerId, "set record key token fail");
            }
        } else {
            TUNER_DSC_ERR(mDescramblerId, "mPlaybackhandle is null");
        }
    }
    return result;
}

PlayType Descrambler::checkPlayType() {
    bool bRecord   = mDemux->checkDemuxRecord();
    bool bPlayback = mDemux->checkDemuxPlayback();
    TUNER_DSC_DBG(mDescramblerId, "[demuxId = %d]demux use for record = %d and playback = %d", mSourceDemuxId, bRecord, bPlayback);
    //in the same demux, both record and playback, set key token by record
    if (bRecord) {
        return DVR_RECORD;
    } else if (bPlayback) {
        return DVR_PLAYBACK;
    } else {
        TUNER_DSC_DBG(mDescramblerId, "Live, data from demod");
        return DEMOD_LIVE;
    }
}

}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
}  // namespace aidl
