/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <net/mac80211.h>
#include "../../core.h"
#include "../../debug.h"
#include "../../wmi.h"
#include "../../mac.h"
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

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
static int ath12k_mac_op_mesh_peer_update_caps_extn_wifi8(struct ieee80211_hw *hw,
							  struct ieee80211_vif *vif,
							  struct ieee80211_sta *sta,
							  struct ieee80211_link_sta *link_sta)
{
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_sta *arsta;
	struct ath12k_link_vif *arvif;
	struct ath12k *ar;
	struct ath12k_wmi_peer_assoc_arg *peer_arg;
	int ret = 0;

	lockdep_assert_wiphy(hw->wiphy);

	arvif = wiphy_dereference(hw->wiphy, ahvif->link[link_sta->link_id]);
	if (!arvif) {
		ath12k_err(NULL, "wifi8 mesh: failed to get arvif for link %u\n",
			   link_sta->link_id);
		return -EFAULT;
	}

	arsta = wiphy_dereference(hw->wiphy, ahsta->link[link_sta->link_id]);
	if (!arsta) {
		ath12k_warn(arvif->ar->ab,
			    "wifi8 mesh: failed to get arsta for link %u\n",
			    link_sta->link_id);
		return -EFAULT;
	}

	ar = arvif->ar;

	peer_arg = kzalloc(sizeof(*peer_arg), GFP_KERNEL);
	if (!peer_arg)
		return -ENOMEM;

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "wifi8 mesh: sta update caps for %pM on vdev %d\n",
		   sta->addr, arvif->vdev_id);

	ath12k_peer_assoc_prepare(ar, arvif, arsta, peer_arg, true, link_sta);

	sta->sta_extn.peer_info.mesh_peer_phymode = peer_arg->peer_phymode;
	sta->sta_extn.peer_info.mesh_peer_nss = peer_arg->peer_nss;

	peer_arg->is_assoc = false;

	ret = ath12k_wmi_send_peer_assoc_cmd(ar, peer_arg);
	if (ret) {
		ath12k_warn(ar->ab,
			    "wifi8 mesh: failed to send peer assoc for %pM vdev %d: %d\n",
			    sta->addr, arvif->vdev_id, ret);
		goto out;
	}

	if (!wait_for_completion_timeout(&ar->peer_assoc_done, 1 * HZ))
		ath12k_warn(ar->ab,
			    "wifi8 mesh: peer assoc timeout for %pM vdev %d\n",
			    sta->addr, arvif->vdev_id);

out:
	kfree(peer_arg);
	return ret;
}
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */

static const struct ieee80211_ops_extn ath12k_ops_wifi8_extn = {
	.validate_6ghz_chandef_extn = ath12k_validate_6ghz_chandef_extn,
	.nol_regdom_change_locked_extn = ath12k_nol_regdom_change_locked_extn,
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
	.mesh_peer_update_caps_extn = ath12k_mac_op_mesh_peer_update_caps_extn_wifi8,
#endif
	.get_best_scan_radio_6ghz_power_mode = ath12k_get_best_6ghz_power_extn,
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
