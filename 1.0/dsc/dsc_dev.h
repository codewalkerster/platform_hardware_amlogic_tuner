/*
 * Copyright (c) 2021 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef _TUNER_DSC_DEV_H_
#define _TUNER_DSC_DEV_H_

#include <stdio.h>
#include "dsc_ca.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CA_DSC_ERROR -1
#define CA_DSC_OK 0

int ca_init();

int ca_open(
    int devno);

int ca_alloc_chan(
    int devno,
    unsigned int pid,
    int algo,
    int dsc_type);

int ca_free_chan(
    int devno,
    int index);

int ca_set_key(
    int devno,
    int index,
    int parity,
    uint32_t key_index);

int ca_close(
    int devno);
#ifdef __cplusplus
 }
#endif

#endif
