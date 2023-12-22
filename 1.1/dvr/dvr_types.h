/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * dvr types header file
 */

#ifndef _DVR_TYPES_H
#define _DVR_TYPES_H

#ifdef __cplusplus
extern "C"
{
#endif

/**Log facilities.*/
#define LOG_LV_DEFAULT  2

#define LOG_LV_DEBUG    1
#define LOG_LV_INFO     2
#define LOG_LV_ERROR    3

#define DVR_LOG_TAG "dvr"
#ifdef DEBUG_ON_PC
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

#define DVR_LOG_PRINT(level, tag, ...) \
  do { \
    if (level >= LOG_LV_DEFAULT) { \
      printf(__VA_ARGS__); \
    } \
  } while(0)
#else
#include <android/log.h>
#define DVR_LOG_PRINT(level, tag, ...) \
  do { \
    if (level >= LOG_LV_DEFAULT) { \
      __android_log_print(level, tag, __VA_ARGS__); \
    } \
  } while(0)
#endif

#define DVR_DBG(...)   DVR_LOG_PRINT(LOG_LV_DEBUG, DVR_LOG_TAG, __VA_ARGS__)
#define DVR_INFO(...)   DVR_LOG_PRINT(LOG_LV_INFO, DVR_LOG_TAG, __VA_ARGS__)
#define DVR_ERROR(...)  DVR_LOG_PRINT(LOG_LV_ERROR, DVR_LOG_TAG, __VA_ARGS__)

#define DVR_CHECK(expr) \
  do { \
    if (!(expr)) { \
      DVR_ERROR("%s error, line%d\n", __func__, __LINE__); \
      return DVR_FAILURE; \
    } \
  } while(0)

#define DVR_CHECK_WITH_UNLOCK(expr, lock) \
  do { \
    if (!(expr)) { \
      DVR_ERROR("%s failed, line%d\n", __func__, __LINE__); \
      pthread_mutex_unlock(lock); \
      return DVR_FAILURE; \
    } \
  } while(0)

/**Function result*/
typedef enum {
  DVR_FAILURE = -1, /**< Generic error.*/
  DVR_SUCCESS = 0   /**< Success*/
} DVR_Result_t;

/**Invalid PID value*/
#define DVR_INVALID_PID 0x1fff
#define DVR_MAX_CA_CHAN_CNT         (2)

#ifdef __cplusplus
}
#endif
#endif
