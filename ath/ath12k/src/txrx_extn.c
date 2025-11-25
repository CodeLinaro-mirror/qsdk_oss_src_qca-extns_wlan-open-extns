// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <linux/skbuff.h>
#include <net/mac80211.h>
#include <linux/ieee80211.h>
#include <linux/bitfield.h>
#include "../core.h"
#include "../mac.h"
#include "../wmi.h"
#include "../debug.h"
#include "ath12k_cmn_extn.h"
#include "vendor_extn.h"

void ath12k_custom_tx_free_extn(struct sk_buff *skb, u32 status)
{
	struct ath12k_skb_cb *skb_cb = ATH12K_SKB_CB(skb);
	struct ath12k *ar;
	struct ath12k_raw_pkt_ctx *raw_pkt_ctx;
	struct sk_buff *tmp_skb;
	u8 idx = 0;


	ar = skb_cb->u.ar;
	if (!ar) {
		dev_kfree_skb_any(skb);
		return;
	}

	raw_pkt_ctx = ar->ar_extn.raw_pkt_ctx;
	if (!raw_pkt_ctx) {
		dev_kfree_skb_any(skb);
		return;
	}

	spin_lock_bh(&raw_pkt_ctx->chan_lock);
	skb_queue_walk(&raw_pkt_ctx->pkt_list, tmp_skb) {
		if (tmp_skb == skb) {
			raw_pkt_ctx->custom_tx_status[idx] = status;
			break;
		}
		idx++;
	}
	spin_unlock_bh(&raw_pkt_ctx->chan_lock);

	dev_kfree_skb_any(skb);
}

int ath12k_custom_tx_extn(struct sk_buff *skb)
{
	struct ath12k_skb_cb *skb_cb = ATH12K_SKB_CB(skb);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif = &ahvif->deflink;
	struct ath12k *ar;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct sk_buff_head *q;
	struct ieee80211_hw *hw;
	bool is_prb_rsp;
	int ret;


	if (!arvif || !arvif->ar) {
		ret = -EINVAL;
		goto free_skb;
	}
	hw = arvif->ar->ah->hw;

	lockdep_assert_wiphy(hw->wiphy);

	if (ahvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ret = -EOPNOTSUPP;
		goto free_skb;
	}

	ar = arvif->ar;

	if (unlikely(test_bit(ATH12K_FLAG_RECOVERY, &ar->ab->dev_flags))) {
		ret = -EPERM;
		goto free_skb;
	}

	if (test_bit(ATH12K_FLAG_CRASH_FLUSH, &ar->ab->dev_flags)) {
		ret = -EPERM;
		goto free_skb;
	}

	if (test_bit(ATH12K_FLAG_CAC_RUNNING, &ar->dev_flags)) {
		ret = -EAGAIN;
		goto free_skb;
	}

	if (ar->csa_active_cnt) {
		ret = -EAGAIN;
		goto free_skb;
	}

	if (ath12k_mac_is_bridge_vdev(arvif)) {
		ret = -EPERM;
		goto free_skb;
	}

	if (skb_cb->flags & ATH12K_SKB_CUSTOM_OFFCHAN_MGMT_TX)
		info->flags |= IEEE80211_TX_CTL_TX_OFFCHAN;

	is_prb_rsp = ieee80211_is_probe_resp(hdr->frame_control);
	if (is_prb_rsp &&
	    atomic_read(&ar->num_pending_mgmt_tx) > ATH12K_PRB_RSP_DROP_THRESHOLD) {
		ret = -EBUSY;
		goto free_skb;
       }
	q = &ar->wmi_mgmt_tx_queue;
	if (skb_queue_len_lockless(q) >= ATH12K_TX_MGMT_NUM_PENDING_MAX) {
		ret = -EBUSY;
		goto free_skb;
	}
	skb_queue_tail(q, skb);
	atomic_inc(&ar->num_pending_mgmt_tx);

	ath12k_mgmt_over_wmi_tx_work(hw->wiphy, &ar->wmi_mgmt_tx_work);

	return 0;

free_skb:
	dev_kfree_skb_any(skb);
	return ret;
}
