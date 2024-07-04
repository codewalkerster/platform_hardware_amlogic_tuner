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

#define LOG_TAG "android.hardware.tv.tuner@1.1-Descrambler"

#include <android/hardware/tv/tuner/1.0/IFrontendCallback.h>
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

namespace android {
namespace hardware {
namespace tv {
namespace tuner {
namespace V1_0 {
namespace implementation {

Descrambler::Descrambler(uint32_t descramblerId, sp<Tuner> tuner) {
    mDescramblerId = descramblerId;
    mTunerService = tuner;
    if (mTunerService != nullptr)
      mDscType = mTunerService->getDscMode();
    mSourceDemuxId = 0;
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

Return<Result> Descrambler::setDemuxSource(uint32_t demuxId) {
    std::lock_guard<std::mutex> lock(mDescrambleLock);
    TUNER_DSC_TRACE(mDescramblerId);

    if (mDemuxSet) {
      TUNER_DSC_WRAN(mDescramblerId, "Descrambler has already been set with a demux id %d", mSourceDemuxId);
      return Result::INVALID_STATE;
    }

    mDemuxSet = true;
    mSourceDemuxId = demuxId;
    if (mTunerService != nullptr)
      mTunerService->attachDescramblerToDemux(mDescramblerId, demuxId);

    TUNER_DSC_INFO(mDescramblerId, "mSourceDemuxId:%d mDescramblerId:%d,dsc type:%d", mSourceDemuxId, mDescramblerId, mDscType);
    mDemux = mTunerService->getDemuxById(demuxId);

    return Result::SUCCESS;
}

void* Descrambler::caSetKey(void *arg) {
    Descrambler *dsc = (Descrambler*)arg;
    bool caReady = false;
    int num = 0;

    while (dsc && !caReady) {
        {
            std::lock_guard<std::mutex> lock(dsc->mDescrambleLock);
            if (num > 0 && num % 100 == 0)
                TUNER_DSC_DBG(dsc->mDescramblerId, "Waiting for CA ready %ds", num/100);
            if (dsc->mDsmFd == -1) {
                TUNER_DSC_DBG(dsc->mDescramblerId, "Exit ca set key thread!");
                break;
            }
        }

        caReady = dsc->isDescramblerReady();
        if (caReady) {
            TUNER_DSC_DBG(dsc->mDescramblerId, "CA is ready! num:%d", num);
            break;
        }
        usleep(10 * 1000);
        num ++;
    }

    return NULL;
}

Return<Result> Descrambler::setKeyToken(const hidl_vec<uint8_t>& keyToken) {
    std::lock_guard<std::mutex> lock(mDescrambleLock);
    TUNER_DSC_TRACE(mDescramblerId);

    uint32_t tempToken = 0;
    uint32_t token_size = keyToken.size();
    if (token_size == 0) {
        TUNER_DSC_ERR(mDescramblerId, "Invalid keyToken!");
        return Result::INVALID_ARGUMENT;
    }
    mBackUpKeyToken.resize(token_size);
    memcpy(mBackUpKeyToken.data(), keyToken.data(), token_size * sizeof(uint8_t));

    for (int token_idx = sizeof(tempToken) - 1; token_idx >= 0; --token_idx) {
        tempToken = (tempToken << 8) | keyToken[token_idx];
    }
    if (mCasSessionUsage == DSM_PROP_SESSION_USAGE_LIVE) {
      if (tempToken == mKeyToken) {
          TUNER_DSC_DBG(mDescramblerId, "The same keyToken:0x%x", tempToken);
          return Result::SUCCESS;
      } else {
          mKeyToken = tempToken;
          if (mIsReady && mDsmFd >= 0) {
              TUNER_DSC_DBG(mDescramblerId, "Reset keyToken");
              DSM_CloseSession(mDsmFd);
              if (mIsNskDsc) {
                  clearNskDscChannels();
              } else {
                  clearDscChannels();
              }
              mIsReady = false;
          }
      }
    }

    mDsmFd = DSM_OpenSession(0);
    TUNER_DSC_DBG(mDescramblerId, "mKeyToken:0x%x mDsmFd:%d", mKeyToken, mDsmFd);

    if (mDsmFd > 0) {
        int dsmRet = DSM_BindToken(mDsmFd, mKeyToken);
        if (dsmRet)
          TUNER_DSC_WRAN(mDescramblerId, "DSM_BindToken failed! %s", strerror(errno));

        if (DSM_GetProperty(mDsmFd, DSM_PROP_CAS_SESSION_USAGE, &mCasSessionUsage) == 0) {
          TUNER_DSC_DBG(mDescramblerId, "LIVE:0, PLAYBACK:1, RECORD:2, TIMESHIFT:3, mCasSessionUsage = %d", mCasSessionUsage);
        }
        if (mCasSessionUsage == DSM_PROP_SESSION_USAGE_LIVE && mDemux->checkDemuxPlayback()) {
          mCasSessionUsage = DSM_PROP_SESSION_USAGE_PLAYBACK;
          TUNER_DSC_DBG(mDescramblerId, "Reset mCasSessionUsage to %d", mCasSessionUsage);
        }
        if (mCasSessionUsage != DSM_PROP_SESSION_USAGE_LIVE) {
            DSM_CloseSession(mDsmFd);
            mDsmFd = -1;
            set<uint16_t>::iterator it;
            for (it = mAddedPid.begin(); it != mAddedPid.end(); it++) {
                uint16_t mPid = *it;
                if (mPid != -1 && mKeyToken != 0) {
                    DVR_Result_t ret = setKeyToken(mPid, mKeyToken);
                    if (ret != DVR_SUCCESS) {
                        TUNER_DSC_ERR(mDescramblerId, "set key token fail");
                    }
                }
            }
            return Result::SUCCESS;
        } else {
            if (ca_open(mSourceDemuxId) != CA_DSC_OK) {
                TUNER_DSC_ERR(mDescramblerId, "ca_open ca%d failed!", mSourceDemuxId);
                return Result::INVALID_STATE;
            }
        }
    }

#ifdef SUPPORT_TSD
    uint32_t dsm_dsc_type = DSM_PROP_SC2_DSC_TYPE_INVALID;
    if (mDscType == CA_DSC_COMMON_TYPE)
        dsm_dsc_type = DSM_PROP_SC2_DSC_TYPE_TSN;
    else if (mDscType == CA_DSC_TSD_TYPE)
        dsm_dsc_type = DSM_PROP_SC2_DSC_TYPE_TSD;
    else if (mDscType == CA_DSC_TSE_TYPE)
        dsm_dsc_type = DSM_PROP_SC2_DSC_TYPE_TSE;
    TUNER_DSC_DBG(mDescramblerId, "set dsm_dsc_type:%d", dsm_dsc_type);

    if (DSM_SetProperty(mDsmFd, DSM_PROP_SC2_DSC_TYPE, dsm_dsc_type) != 0) {
        TUNER_DSC_ERR(mDescramblerId, "DSM_SetProperty failed! %s", strerror(errno));
        return Result::INVALID_STATE;
    }
#endif
    if (mCaSetKeyThread == 0) {
        int ret = pthread_create(&mCaSetKeyThread, NULL, caSetKey, this);
        if (ret < 0)
            TUNER_DSC_ERR(mDescramblerId, "Create ca set key thread failed! ret=%d", ret);
    }

    return Result::SUCCESS;
}

Return<Result> Descrambler::addPid(const DemuxPid& pid,
                                   const sp<IFilter>& filter/* optionalSourceFilter */) {
    std::lock_guard<std::mutex> lock(mDescrambleLock);

    (void)filter;
    uint16_t mPid = pid.tPid();
    TUNER_DSC_INFO(mDescramblerId, "mPid:0x%x", mPid);
    // Assume transport stream pid only.
    mAddedPid.insert(mPid);

    if (mCasSessionUsage == DSM_PROP_SESSION_USAGE_LIVE) {
        TUNER_DSC_INFO(mDescramblerId, "playback type is demod live");
    } else {
        if (mPid != -1 && mCasSessionToken != 0) {
            DVR_Result_t ret = setKeyToken(mPid, mCasSessionToken);
            if (ret != DVR_SUCCESS) {
                TUNER_DSC_ERR(mDescramblerId, "set key token fail");
                return Result::INVALID_STATE;
            }
            return Result::SUCCESS;
        } else {
            TUNER_DSC_ERR(mDescramblerId, "pid = %d or mCasSessionToken = %d is not ready", mPid, mCasSessionToken);
            return Result::INVALID_STATE;
        }
    }

    if (mIsNskDsc) {
      bool isChannelFound = false;

      if (es_pid_to_dsc_channel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
              CA_DSC_COMMON_TYPE, mPid)) != es_pid_to_dsc_channel.end()) {
        isChannelFound = true;
      }
      if (es_pid_to_dsc_channel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
              CA_DSC_TSD_TYPE, mPid)) != es_pid_to_dsc_channel.end()) {
        isChannelFound = true;
      }
      if (es_pid_to_dsc_channel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
              CA_DSC_TSE_TYPE, mPid)) != es_pid_to_dsc_channel.end()) {
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
            return Result::INVALID_ARGUMENT;
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
                return Result::INVALID_STATE;
              } else {
                es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
                    CA_DSC_COMMON_TYPE, mPid)] = ca_esa_index;
              }
            }
            TUNER_DSC_DBG(mDescramblerId, "set esa key type %d to index %d kte %x ", key_parity_type,
                  ca_esa_index, kte_id);
            if (ca_set_key(mSourceDemuxId, ca_esa_index, key_parity_type,
                           kte_id)) {
              TUNER_DSC_ERR(mDescramblerId, "ca_set_key(%d, %d, %d) failed", ca_esa_index,
                    key_parity_type, kte_id);
              return Result::INVALID_STATE;
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
                return Result::INVALID_STATE;
              } else {
                es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
                    CA_DSC_TSD_TYPE, mPid)] = ca_lde_index;
              }
            }
          TUNER_DSC_DBG(mDescramblerId, "set lde key type %d to index %d kte %x ",
              key_parity_type, ca_lde_index, kte_id);
            if (ca_set_key(mSourceDemuxId, ca_lde_index, key_parity_type,
                           kte_id)) {
            TUNER_DSC_ERR(mDescramblerId,
                "ca_set_key(%d, %d, %d) failed", ca_lde_index, key_parity_type, kte_id);
              return Result::INVALID_STATE;
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
                return Result::INVALID_STATE;
              } else {
                es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
                    CA_DSC_TSE_TYPE, mPid)] = ca_len_index;
              }
            }

          TUNER_DSC_DBG(mDescramblerId, "set len key type %d to index %d kte %x ",
              key_parity_type, ca_len_index, kte_id);
            if (ca_set_key(mSourceDemuxId, ca_len_index, key_parity_type,
                           kte_id)) {
            TUNER_DSC_ERR(mDescramblerId, "ca_set_key(%d, %d, %d) failed",
                ca_len_index, key_parity_type, kte_id);
              return Result::INVALID_STATE;
            }
          } else {
          TUNER_DSC_ERR(mDescramblerId, "Invalid CA algorithm type to set: %d\n", algo_type);
            ca_close(mSourceDemuxId);
            return Result::INVALID_STATE;
          }
        }
      }
    } else {
      if (es_pid_to_dsc_channel.find(mPid) == es_pid_to_dsc_channel.end() && mIsReady) {
        int handle = ca_alloc_chan(mSourceDemuxId, mPid, mDscAlgo, mDscType);
        if (handle < 0) {
          TUNER_DSC_ERR(mDescramblerId, "ca_alloc_chan failed!");
          return Result::INVALID_STATE;
        } else {
          if (!bindDscChannelToKeyTable(mSourceDemuxId, handle)) {
            return Result::INVALID_STATE;
          } else {
            es_pid_to_dsc_channel[mPid] = handle;
          }
        }
        TUNER_DSC_DBG(mDescramblerId, "ca_alloc_chan(0x%x 0x%x) ok.", mPid, es_pid_to_dsc_channel[mPid]);
      }
    }
    return Result::SUCCESS;
}

Return<Result> Descrambler::removePid(const DemuxPid& pid,
                                      const sp<IFilter>& filter/* optionalSourceFilter */) {
   std::lock_guard<std::mutex> lock(mDescrambleLock);

   (void)filter;

   uint16_t mPid = pid.tPid();
   TUNER_DSC_INFO(mDescramblerId, "mPid:0x%x", mPid);

   if (mCasSessionUsage == DSM_PROP_SESSION_USAGE_LIVE) {
        TUNER_DSC_INFO(mDescramblerId, "playback type is demod live");
   } else {
       if (mCasSessionUsage == DSM_PROP_SESSION_USAGE_PLAYBACK) {
            if (mPlaybackhandle) {
                DVR_Result_t result = DVR_FAILURE;
                result = dvr_playback_set_key_token(mPlaybackhandle, mPid, -1);
                if (result != DVR_SUCCESS) {
                    TUNER_DSC_ERR(mDescramblerId, "unset playback key token fail");
                }
            } else {
                TUNER_DSC_ERR(mDescramblerId, "mPlaybackhandle is null");
            }
       } else if (mCasSessionUsage == DSM_PROP_SESSION_USAGE_RECORD ||
           mCasSessionUsage == DSM_PROP_SESSION_USAGE_TIMESHIFT) {
           TUNER_DSC_INFO(mDescramblerId, "to do unset record key token");
       }
       return Result::SUCCESS;
   }
   if (mIsNskDsc) {
     if (es_pid_to_dsc_channel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
             CA_DSC_COMMON_TYPE, mPid)) != es_pid_to_dsc_channel.end()) {
       ca_free_chan(mSourceDemuxId,
                    es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
                        CA_DSC_COMMON_TYPE, mPid)]);
       es_pid_to_dsc_channel.erase(
           ADD_DSC_TYPE_FLAG_WITH_PID(CA_DSC_COMMON_TYPE, mPid));
     }
     if (es_pid_to_dsc_channel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
             CA_DSC_TSD_TYPE, mPid)) != es_pid_to_dsc_channel.end()) {
       ca_free_chan(mSourceDemuxId,
                    es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
                        CA_DSC_TSD_TYPE, mPid)]);
       es_pid_to_dsc_channel.erase(
           ADD_DSC_TYPE_FLAG_WITH_PID(CA_DSC_TSD_TYPE, mPid));
     }
     if (es_pid_to_dsc_channel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
             CA_DSC_TSE_TYPE, mPid)) != es_pid_to_dsc_channel.end()) {
       ca_free_chan(mSourceDemuxId,
                    es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
                        CA_DSC_TSE_TYPE, mPid)]);
       es_pid_to_dsc_channel.erase(
           ADD_DSC_TYPE_FLAG_WITH_PID(CA_DSC_TSE_TYPE, mPid));
     }
   } else {
     if (es_pid_to_dsc_channel.find(mPid) != es_pid_to_dsc_channel.end()) {
       ca_free_chan(mSourceDemuxId, es_pid_to_dsc_channel[mPid]);
       es_pid_to_dsc_channel.erase(mPid);
     }
   }

   mAddedPid.erase(mPid);

   return Result::SUCCESS;

}

Return<Result> Descrambler::close() {
    TUNER_DSC_TRACE(mDescramblerId);

    if (mTunerService != nullptr) {
        mTunerService->detachDescramblerFromDemux(mDescramblerId, mSourceDemuxId);
        mTunerService->removeDescrambler(mDescramblerId);
    }

    {
      std::lock_guard<std::mutex> lock(mDescrambleLock);
      TUNER_DSC_INFO(mDescramblerId, "mCasSessionUsage = %d", mCasSessionUsage);
      if (mCasSessionUsage == DSM_PROP_SESSION_USAGE_LIVE) {
          TUNER_DSC_INFO(mDescramblerId, "playback type is demod live");
      } else {
          if (mCasSessionUsage == DSM_PROP_SESSION_USAGE_PLAYBACK) {
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
          } else if (mCasSessionUsage == DSM_PROP_SESSION_USAGE_RECORD ||
          mCasSessionUsage == DSM_PROP_SESSION_USAGE_TIMESHIFT) {
              TUNER_DSC_ERR(mDescramblerId, "to do unset record key token");
          }
        mAddedPid.clear();
        return Result::SUCCESS;
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
    pthread_join(mCaSetKeyThread, NULL);

    return Result::SUCCESS;
}

bool Descrambler::isPidSupported(uint16_t pid) {
  std::lock_guard<std::mutex> lock(mDescrambleLock);

  TUNER_DSC_VERB(mDescramblerId, "pid:0x%x", pid);

  return mAddedPid.find(pid) != mAddedPid.end();
}

bool Descrambler::bindDscChannelToKeyTable(uint32_t dsc_dev_id, uint32_t dsc_handle) {
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

    if (ca_set_key(dsc_dev_id, dsc_handle, kt_type, kt_id)) {
      TUNER_DSC_ERR(mDescramblerId, "ca_set_key(%d %d %d) failed!", dsc_handle, kt_type, kt_id);
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
    if (es_pid_to_dsc_channel.find(mPid) != es_pid_to_dsc_channel.end()) {
      dsc_chan = es_pid_to_dsc_channel[mPid];
      ca_free_chan(mSourceDemuxId, dsc_chan);
      es_pid_to_dsc_channel.erase(mPid);
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
        es_pid_to_dsc_channel[mPid] = handle;
      }
    }
    TUNER_DSC_DBG(mDescramblerId, "ca_alloc_chan(0x%x 0x%x) ok.", mPid, es_pid_to_dsc_channel[mPid]);
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
    if (es_pid_to_dsc_channel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
            CA_DSC_COMMON_TYPE, mPid)) != es_pid_to_dsc_channel.end()) {
      dsc_chan = es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
          CA_DSC_COMMON_TYPE, mPid)];
      ca_free_chan(mSourceDemuxId, dsc_chan);
      es_pid_to_dsc_channel.erase(
          ADD_DSC_TYPE_FLAG_WITH_PID(CA_DSC_COMMON_TYPE, mPid));

      TUNER_DSC_DBG(mDescramblerId, "ca_free_chan(0x%x 0x%x) ok.", mPid,
                    dsc_chan);
    }
    if (es_pid_to_dsc_channel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
            CA_DSC_TSD_TYPE, mPid)) != es_pid_to_dsc_channel.end()) {
      dsc_chan = es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
          CA_DSC_TSD_TYPE, mPid)];
      ca_free_chan(mSourceDemuxId, dsc_chan);
      es_pid_to_dsc_channel.erase(
          ADD_DSC_TYPE_FLAG_WITH_PID(CA_DSC_TSD_TYPE, mPid));

      TUNER_DSC_DBG(mDescramblerId, "ca_free_chan(0x%x 0x%x) ok.", mPid,
                    dsc_chan);
    }
    if (es_pid_to_dsc_channel.find(ADD_DSC_TYPE_FLAG_WITH_PID(
            CA_DSC_TSE_TYPE, mPid)) != es_pid_to_dsc_channel.end()) {
      dsc_chan = es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
          CA_DSC_TSE_TYPE, mPid)];
      ca_free_chan(mSourceDemuxId, dsc_chan);
      es_pid_to_dsc_channel.erase(
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
            es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
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
            es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
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
            es_pid_to_dsc_channel[ADD_DSC_TYPE_FLAG_WITH_PID(
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
    uint32_t dsmUsage = DSM_PROP_SESSION_USAGE_LIVE;
     dsmRet = DSM_GetProperty(mDsmFd, DSM_PROP_CAS_SESSION_USAGE, &dsmUsage);
     if (dsmRet != 0 || dsmUsage != DSM_PROP_SESSION_USAGE_LIVE)
         return mIsReady;

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
    TUNER_DSC_DBG(mDescramblerId, "KT count:%d ENC:%d USAGE:%d",
        mKeyslotList.count, mIsEnc, mCasSessionUsage);
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

bool Descrambler::getTsnSourceStatus(bool *is_local_mode) {
  TUNER_DSC_TRACE(mDescramblerId);
  char tsn_source[32] = {0};
  if (!FileSystem_readFile(TSN_SOURCE, tsn_source, sizeof(tsn_source))) {
    if (strstr(tsn_source, TSN_LOCAL)) {
      *is_local_mode = true;
    } else if (strstr(tsn_source, TSN_DEMOD)) {
      *is_local_mode = false;
    } else {
          TUNER_DSC_WRAN(mDescramblerId, "unknown tsn source %s!", tsn_source);
      return false;
    }
  } else {
      TUNER_DSC_WRAN(mDescramblerId, "read tsn source failed!");
    return false;
  }

  return true;
}

DVR_Result_t Descrambler::setKeyToken(int pid, int token) {
    DVR_Result_t result = DVR_FAILURE;
    TUNER_DSC_ERR(mDescramblerId, "pid:0x%x", pid);

    if (mCasSessionUsage == DSM_PROP_SESSION_USAGE_PLAYBACK) {
        mPlaybackhandle = mDemux->getPlaybackHandle();
        if (mPlaybackhandle) {
            result = dvr_playback_set_key_token(mPlaybackhandle, pid, token);
            if (result != DVR_SUCCESS) {
                TUNER_DSC_ERR(mDescramblerId, "set playback key token fail");
            }
        } else {
            TUNER_DSC_ERR(mDescramblerId, "mPlaybackhandle is null");
        }
    } else if (mCasSessionUsage == DSM_PROP_SESSION_USAGE_RECORD
        || mCasSessionUsage == DSM_PROP_SESSION_USAGE_TIMESHIFT) {
        mRecordHandle = mDemux->getRecordHandle();
        if (mRecordHandle) {
            result = dvr_record_set_key_token(mRecordHandle, pid, token);
            if (result != DVR_SUCCESS) {
                TUNER_DSC_ERR(mDescramblerId, "set record key token fail");
            }
        } else {
            TUNER_DSC_ERR(mDescramblerId, "mRecordHandle is null");
        }
    }
    return result;
}

std::vector<uint8_t> Descrambler::getBackUpKeyToken() {
    return mBackUpKeyToken;
}

void Descrambler::closeDsmSession() {
    if (mDsmFd >= 0) {
      DSM_CloseSession(mDsmFd);
      mDsmFd = -1;
    }
}
}  // namespace implementation
}  // namespace V1_0
}  // namespace tuner
}  // namespace tv
}  // namespace hardware
}  // namespace android
