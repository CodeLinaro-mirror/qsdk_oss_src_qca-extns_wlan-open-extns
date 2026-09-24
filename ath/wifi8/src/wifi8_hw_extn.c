/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "../../core.h"
#include "../../qcn_extns/ath12k_cmn_extn.h"
#include "../../mac.h"
#include "../../qcn_extns/ranging_extn.h"

static void ath12k_nol_regdom_change_locked_extn(struct ieee80211_hw *hw)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar;
	int i;

	for_each_ar(ah, ar, i)
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

/*
 * ath12k_wifi8_mgmt_handler_extn - extension hook called from
 * ath12k_wifi8_mgmt_handler() when the mgmt frame's arvif could not be
 * resolved from ahvif->link[link_id] (e.g. an unassociated STA sending a
 * PASN Auth frame while parked on the dedicated RTT/ranging vdev).
 *
 * Delegates to get_ranging_mgmt_arvif() in ranging_extn.c to redirect
 * the frame to the rtt_vif when applicable.
 *
 * Returns the resolved link vif, or NULL if it still cannot be resolved.
 */
struct ath12k_link_vif *ath12k_wifi8_mgmt_handler_extn(struct ieee80211_hw *hw,
							struct ieee80211_vif *vif,
							struct sk_buff *skb)
{
	return get_ranging_mgmt_arvif(hw, vif, skb);
}
