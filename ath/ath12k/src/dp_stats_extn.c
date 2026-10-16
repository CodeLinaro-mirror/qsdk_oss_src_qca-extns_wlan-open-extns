// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_stats_extn.h"
#include "ath12k_cmn_extn.h"
#include "../dp_mon.h"

void ath12k_dp_rx_undecoded_stats_update(
		struct ath12k_telemetry_dp_vif *telemetry_vif_stats,
		struct ath12k_pdev_mon_dp *dp_mon_pdev)
{
	struct ath12k_pdev_mon_dp_extn *mon_dp_pdev_extn;
	struct ath12k_undecoded_metadata *undecoded_meta;

	if (!dp_mon_pdev)
		return;

	mon_dp_pdev_extn = &dp_mon_pdev->pdev_mon_dp_extn;
	undecoded_meta = mon_dp_pdev_extn->undecoded_meta;


	if (!telemetry_vif_stats || !undecoded_meta)
		return;

	if (!undecoded_meta->undecoded_metadata_capture)
		return;

	telemetry_vif_stats->rx_undecoded_stats.rx_undecoded_count =
		undecoded_meta->rx_undecoded_count;
	telemetry_vif_stats->rx_undecoded_stats.phyrx_error_mask =
		undecoded_meta->phyrx_error_mask;
	telemetry_vif_stats->rx_undecoded_stats.phyrx_error_mask_cont =
		undecoded_meta->phyrx_error_mask_cont;
	memcpy(telemetry_vif_stats->rx_undecoded_stats.rx_undecoded_error,
	       undecoded_meta->rx_undecoded_error,
	       sizeof(undecoded_meta->rx_undecoded_error));
	telemetry_vif_stats->rx_undecoded_stats.is_set = 1;
}
