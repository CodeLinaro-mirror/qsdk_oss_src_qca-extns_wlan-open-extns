/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef DP_STATS_EXTN_H
#define DP_STATS_EXTN_H
#include "../core.h"
#include "../dp_stats.h"

#ifndef CPTCFG_QCN_EXTN
static void ath12k_dp_rx_undecoded_stats_update(
		struct ath12k_telemetry_dp_vif *telemetry_vif_stats,
		struct ath12k_pdev_mon_dp *dp_mon_pdev)
{
}

#else
void ath12k_dp_rx_undecoded_stats_update(
		struct ath12k_telemetry_dp_vif *telemetry_vif_stats,
		struct ath12k_pdev_mon_dp *dp_mon_pdev);
#endif //CPTCFG_QCN_EXTN
#endif //DP_STATS_EXTN_H
