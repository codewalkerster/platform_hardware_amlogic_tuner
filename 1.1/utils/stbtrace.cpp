/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "TIME_TRACE"
#include "stbtrace.h"
#include <utils/Log.h>
#include <stdlib.h>
#include <memory>

void init_time_trace(stbtrace_info* trace_info) {
    // ALOGI("init_stb_trace\n");
    if (trace_info == NULL) {
        ALOGE("input parameter was NULL, init_stb_trace failed!\n");
        return;
    }
}

void table_time_trace_log(stbtrace_info* trace_info, const char* step_str, uint8_t table_id, uint64_t filter_id, uint32_t demux_id, timeval elapsed_time) {
    if (trace_info == NULL || step_str == NULL) {
        ALOGE("input parameter was NULL, add time_trace_log failed!");
        return;
    }

    ALOGD("[%s]: %s: tableId = %d, fid = %llu, demuxId = %d, consume time: %ld ms \n",trace_info->module_name, step_str, table_id, filter_id, demux_id, elapsed_time.tv_sec * 1000 + elapsed_time.tv_usec / 1000);
}

void tune_time_trace_log(stbtrace_info* trace_info, const char* step_str, timeval elapsed_time) {
    if (trace_info == NULL || step_str == NULL) {
        ALOGE("input parameter was NULL, add time_trace_log failed!");
        return;
    }

    ALOGD("[%s]: %s, consume time: %ld ms \n",trace_info->module_name, step_str, elapsed_time.tv_sec * 1000 + elapsed_time.tv_usec / 1000);
}
