/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef WIFI7_DP_EXTN_H
#define WIFI7_DP_EXTN_H

#include "../../core.h"
#include "../dp_tx.h"

/**
 * ath12k_dp_tx_check_fast_path() - Handle fast-path TX frames
 * @skb: Socket buffer to be transmitted
 * @info_flags: ieee80211 TX control flags
 * @dp_vif: Datapath VIF context
 * @skb_ctrl: Per-skb control structure to set offload/fast-path flags
 * @qos_nw_delay: QoS network delay metadata used by fast-path handler
 *
 * Checks if @skb carries fast-path markers (fast_xmit + MLO assist tag) and is
 * eligible for the architecture-specific unicast fast path. When eligible, it
 * sets skb control features/flags (e.g. DP_ETH_OFFLOAD, DP_SKB_RECYCLED) and
 * calls ath12k_dp_arch_ucast_handler().
 *
 * Return: true if the skb was consumed by the fast path, false otherwise
 */
static inline
bool ath12k_dp_tx_check_fast_path(struct sk_buff *skb, u32 info_flags,
				  struct ath12k_dp_vif *dp_vif,
				  struct ath12k_dp_skb_ctrl *skb_ctrl,
				  u32 qos_nw_delay)
{
	struct ath12k_vif *ahvif = container_of(dp_vif, struct ath12k_vif, dp_vif);
	bool is_recycler = false;
	u8 link_id;

	/* Check for fast path markers */
	if (likely(skb->fast_xmit &&
				((skb->mark & ATH12K_MLO_METADATA_MLO_ASSIST_TAG_MASK) ==
				 ATH12K_MLO_METADATA_MLO_ASSIST_TAG))) {
		/* Extract link ID from mark */
		link_id = u32_get_bits(skb->mark, ATH12K_MLO_METADATA_LINKID_MASK);
		skb_ctrl->flags |= DP_SKB_FAST_TX;
		is_recycler = (skb->fast_recycled = !!skb->is_from_recycler);
		if (likely(is_recycler))
			skb_ctrl->flags |= DP_SKB_RECYCLED;
		ath12k_wifi7_ucast_handler(dp_vif, link_id, NULL, skb, skb_ctrl,
					   qos_nw_delay, false, NULL);
		return true;
	} else if (skb->fast_xmit) {
		ath12k_err(NULL,
			   "drop fast_xmit skb: missing MLO assist tag (mark 0x%x)\n",
			   skb->mark);
		ath12k_mac_ieee80211_free_txskb(ahvif->ah->hw, skb, NULL, NULL, dp_vif,
						DP_TX_ENQ_DROP_INV_ENCAP_FAST,
						0, false);
		return true;
	}

	return false;
}
#endif // WIFI7_DP_EXTN_H
