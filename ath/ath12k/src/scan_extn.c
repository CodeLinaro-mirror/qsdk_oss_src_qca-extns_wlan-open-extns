// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include "../wmi.h"
#include "ath12k_cmn_extn.h"
#include "../core.h"

/**
 * ath12k_wmi_update_strict_passive_scan_extn - Update strict passive scan
 *                                              setting for 5 GHz scans
 * @ar: Pointer to the ath12k radio structure
 * @arg: Pointer to the scan request parameters
 *
 * Update the strict passive scan flag in the scan request based on the
 * userspace radio configuration. The flag is applied only when the scan
 * request includes a 5 GHz channel.
 */
void ath12k_wmi_update_strict_passive_scan_extn(struct ath12k *ar,
						struct ath12k_wmi_scan_req_arg *arg)
{
	struct ieee80211_channel *channel;

	if (!arg->chan_list.num_chan || !arg->chan_list.chan)
		return;

	channel = ieee80211_get_channel(ar->ah->hw->wiphy,
					arg->chan_list.chan[0].freq);

	if (channel && channel->band == NL80211_BAND_5GHZ)
		arg->scan_f_strict_passive_pch =
			ar->radio_cfg.radio_cfg_extn.scan_f_strict_passive_pch;
}

/**
 * ath12k_wmi_start_scan_init_extn - Initialize scan offset in scan args
 * @ar: Pointer to the ath12k radio structure
 * @arg: Pointer to the scan parameter
 *
 * This function initializes the scan_start_offset field in the scan request
 * argument
 */
void ath12k_wmi_start_scan_init_extn(struct ath12k *ar,
				     struct ath12k_wmi_scan_req_arg *arg)
{
	arg->extn.scan_start_offset = 0;
}

/**
 * ath12k_wmi_send_scan_start_cmd_extn - Copy scan offset into WMI command
 * @ar: Pointer to the ath12k radio structure
 * @cmd: Pointer to the WMI start scan command
 * @arg: Pointer to the scan parameter
 *
 * This function copies the scan_start_offset from the scan request argument
 * into the WMI start scan command.
 */
void ath12k_wmi_send_scan_start_cmd_extn(struct ath12k *ar,
					 struct wmi_start_scan_cmd *cmd,
					 struct ath12k_wmi_scan_req_arg *arg)
{
	cmd->scan_start_offset = cpu_to_le32(arg->extn.scan_start_offset);
}
