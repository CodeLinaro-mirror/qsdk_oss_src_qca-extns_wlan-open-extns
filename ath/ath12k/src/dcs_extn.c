// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <net/netlink.h>
#include <net/mac80211.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include "../core.h"
#include "../wmi.h"
#include "../mac.h"
#include "../debug.h"
#include "../vendor.h"
#include "ath12k_cmn_extn.h"
#include "dcs_extn.h"

void ath12k_mac_set_intf_detect(struct ath12k *ar, u16 intf_detect_bitmap)
{
	u16 dcs_config_bitmap, prev_config_bitmap;
	int ret;

	spin_lock_bh(&ar->data_lock);
	intf_detect_bitmap &= ATH12K_DCS_VALID_INTF_BITMAP;
	prev_config_bitmap = ar->ar_extn.dcs_config_bitmap;
	ar->ar_extn.dcs_config_bitmap |= intf_detect_bitmap;
	dcs_config_bitmap = ar->ar_extn.dcs_config_bitmap;
	spin_unlock_bh(&ar->data_lock);
	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_DCS,
					dcs_config_bitmap, ar->pdev->pdev_id);
	if (ret) {
		ath12k_warn(ar->ab, "Failed to set DCS config: %d\n", ret);
		spin_lock_bh(&ar->data_lock);
		ar->ar_extn.dcs_config_bitmap = prev_config_bitmap;
		spin_unlock_bh(&ar->data_lock);
	}
}

