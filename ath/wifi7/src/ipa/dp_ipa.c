/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_ipa.h"
#include "../../../dp_peer.h"
#include "../../../peer.h"
#include "../../../mac.h"
#include "../../../dp.h"
#include "../../dp_tx.h"

u32 ath12k_wifi7_ipa_config_set(void)
{
	return 0x46d;
};

bool
ath12k_wifi7_dp_ipa_tx(struct ath12k_base *ab, struct sk_buff *skb, bool is_mcbc)
{
	struct ath12k *tmp_ar;
	struct ath12k_dp *dp;
	struct ath12k_dp_link_peer *peer;
	struct ath12k_link_sta *arsta = NULL;
	struct ath12k_link_vif *arvif;
	struct ath12k_link_vif *tmp_arvif;
	struct ath12k_pdev_dp *dp_pdev;
	struct ath12k_pdev_dp *tmp_dp_pdev;
	struct ath12k_skb_cb *skb_cb;
	struct ath12k_vif *ahvif;
	struct ethhdr *eth;
	struct ieee80211_hdr *hdr;
	struct ieee80211_key_conf *key;
	struct ieee80211_vif *vif;
	struct sk_buff *skb_copied;
	u16 mcbc_gsn;
	u8 link_id;
	u8 ring_id = 0;
	unsigned long links_map;
	enum ath12k_dp_tx_enq_error err;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	int peer_id = (uint8_t)skb->cb[DP_IPA_SKB_CB_PEER_ID_OFFSET];
	skb_cb = ATH12K_SKB_CB(skb);
	skb_cb->flags |= ATH12K_SKB_HW_80211_ENCAP;
	tx_info->flags |= IEEE80211_TX_CTL_HW_80211_ENCAP;
	dp = ath12k_ab_to_dp(ab);

	if (!is_mcbc) {
		/* unicast packet tx */
		eth = skb_eth_hdr(skb);
		spin_lock_bh(&dp->dp_lock);
		peer = ath12k_dp_link_peer_find_by_addr(dp, eth->h_dest);
		if (!peer)
			goto unicast_err1;

		/* TODO: check if we can send pkt without arsta */
		arsta = ath12k_peer_get_link_sta(ab, peer);
		if (!arsta) {
			ath12k_err(ab, "arsta is NULL");
			goto unicast_err1;
		}

		ahvif = ath12k_vif_to_ahvif(peer->vif);

		rcu_read_lock();
		arvif = rcu_dereference(ahvif->link[peer->link_id]);
		if (!arvif) {
			ath12k_err(ab, "arvif is NULL");
			goto unicast_err2;
		}

		dp_pdev = ath12k_dp_to_dp_pdev(dp, peer->pdev_idx);
		if (!dp_pdev) {
			ath12k_err(ab, "dp_pdev is NULL");
			goto unicast_err2;
		}

		spin_unlock_bh(&dp->dp_lock);

		err = ath12k_wifi7_dp_tx(dp_pdev, arvif, skb, false, 0, false, arsta,
					 ring_id, 0);
		rcu_read_unlock();

		if (unlikely(err)) {
			ath12k_err(ab, "failed to transmit frame %d", err);
			return false;
		}

		return true;
unicast_err2:
		rcu_read_unlock();
unicast_err1:
		spin_unlock_bh(&dp->dp_lock);
		return false;
	}

	/* multicast packet tx */
	spin_lock_bh(&dp->dp_lock);
	peer = ath12k_dp_link_peer_find_by_id(dp, peer_id);
	if (!peer) {
		spin_unlock_bh(&dp->dp_lock);
		return false;
	}
	spin_unlock_bh(&dp->dp_lock);
	vif = peer->vif;
	ahvif = ath12k_vif_to_ahvif(vif);
	mcbc_gsn = atomic_inc_return(&ahvif->dp_vif.mcbc_gsn) & 0xfff;
	links_map = ahvif->links_map;

	rcu_read_lock();
	for_each_set_bit(link_id, &links_map, IEEE80211_MLD_MAX_NUM_LINKS) {
		tmp_arvif = rcu_dereference(ahvif->link[link_id]);
		if (!tmp_arvif || !tmp_arvif->is_up) {
			ath12k_err(ab, "tmp_arvif is NULL or not UP");
			continue;
		}
		tmp_ar = tmp_arvif->ar;
		tmp_dp_pdev = ath12k_dp_to_dp_pdev(tmp_ar->ab->dp, tmp_ar->pdev_idx);
		if (!tmp_dp_pdev) {
			ath12k_err(ab, "tmp_dp_pdev is NULL");
			continue;
		}
		skb_copied = skb_clone(skb, GFP_ATOMIC);
		if (!skb_copied) {
			ath12k_err(ab, "failed to copy skb");
			continue;
		}

		skb_cb = ATH12K_SKB_CB(skb_copied);
		skb_cb->link_id = link_id;
		skb_cb->vif = vif;
		skb_cb->u.ar = tmp_ar;

		spin_lock_bh(&tmp_ar->ab->dp->dp_lock);

		peer = ath12k_dp_link_peer_find_by_addr(tmp_ar->ab->dp,
							tmp_arvif->bssid);
		if (!peer) {
			spin_unlock_bh(&tmp_ar->ab->dp->dp_lock);
			ath12k_err(ab, "dst_peer is NULL");
			continue;
		}

		if (!peer->dp_peer)
			goto skip_key;

		key = peer->dp_peer->keys[peer->dp_peer->mcast_keyidx];
		if (key) {
			skb_cb->cipher = key->cipher;
			skb_cb->flags |= ATH12K_SKB_CIPHER_SET;
			hdr = (struct ieee80211_hdr *)skb_copied->data;
			if (!ieee80211_has_protected(hdr->frame_control))
				hdr->frame_control |=
					cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		}

skip_key:
		spin_unlock_bh(&tmp_ar->ab->dp->dp_lock);

		err = ath12k_wifi7_dp_tx(tmp_dp_pdev, tmp_arvif, skb_copied, true,
					 mcbc_gsn, true, arsta, ring_id, 0);
		if (unlikely(err))
			ath12k_err(ab, "failed to transmit frame %d\n", err);
	}
	rcu_read_unlock();
	return true;
}

static const struct ipa_wifi_ops ipa_wifi7_ops = {
	.tx = ath12k_wifi7_dp_ipa_tx,
};

void ath12k_wifi7_dp_ipa_init(struct ath12k_base *ab)
{
	if (!ab->ath12k_base_extn.ipa_ctx)
		return;

	ab->ath12k_base_extn.ipa_ctx->wifi_hw_ops = &ipa_wifi7_ops;
}
EXPORT_SYMBOL(ath12k_wifi7_dp_ipa_init);
