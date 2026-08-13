/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "../../core.h"
#include "../../qcn_extns/ath12k_cmn_extn.h"
#include "../../mac.h"

static void ath12k_nol_regdom_change_locked_extn(struct ieee80211_hw *hw,
						 struct ieee80211_channel *channel)
{
	struct ath12k *ar;

	ar = ath12k_mac_get_ar_by_center_freq(hw, channel->center_freq);
	if (!ar)
		return;

	ath12k_core_nol_extn_regdom_change_radio_locked(ar);
}

static const struct ieee80211_ops_extn ath12k_ops_wifi8_extn = {
	.validate_6ghz_chandef_extn = ath12k_validate_6ghz_chandef_extn,
	.nol_regdom_change_locked_extn = ath12k_nol_regdom_change_locked_extn,
};

void ath12k_wifi8_hw_init_extn(struct ath12k_base *ab)
{
	ab->ath12k_ops_extn = &ath12k_ops_wifi8_extn;
}
