/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_WIFI6_H
#define ATH12K_DP_WIFI6_H

#include "../../dp_cmn.h"
#include "hw.h"

#define HAL_DSCP_TID_MAP_TBL_NUM_ENTRIES_MAX_WIFI6     48

struct ath12k_base;
struct ath12k_dp;

struct ath12k_dp *ath12k_wifi6_dp_init(struct ath12k_base *ab);
void ath12k_wifi6_dp_deinit(struct ath12k_dp *dp);
int ath12k_wifi6_dp_pdev_alloc(struct ath12k_base *ab);
void ath12k_wifi6_dp_pdev_free(struct ath12k_base *ab);
#endif
