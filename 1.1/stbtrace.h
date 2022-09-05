/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef STB_TRACE_H
#define STB_TRACE_H
#include <stdlib.h>
#include <memory>

struct stbtrace_info {
    char module_name[64];
};

void init_time_trace(stbtrace_info* trace_info);
void table_time_trace_log(stbtrace_info* trace_info, const char* step_str, uint8_t table_id, uint64_t filter_id, uint32_t demux_id, timeval elapsed_time);
void tune_time_trace_log(stbtrace_info* trace_info, const char* step_str, timeval elapsed_time);

#endif
