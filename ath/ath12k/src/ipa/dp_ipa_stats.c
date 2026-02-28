/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_ipa_stats.h"

static void ath12k_dp_ipa_stats_update_ucast(struct ath12k_dp_ipa_stats_vif_stats *stats)
{
	stats->ucast.bytes = stats->total.bytes - stats->bcast.bytes - stats->mcast.bytes;
	stats->ucast.packets = stats->total.packets - stats->bcast.packets -
			       stats->mcast.packets;
}

void ath12k_dp_ipa_tx_rx_mcbc_stats(struct ath12k_dp_ipa_stats_vif_stats *stats,
				    bool is_mcast,
				    bool is_bcast,
				    u64 skb_len)
{
	if (is_mcast) {
		stats->mcast.bytes += skb_len;
		stats->mcast.packets++;
	}

	if (is_bcast) {
		stats->bcast.bytes += skb_len;
		stats->bcast.packets++;
	}

	ath12k_dp_ipa_stats_update_ucast(stats);
}

void ath12k_dp_ipa_tx_mcbc_stats(struct sk_buff *skb,
				 struct ath12k_pdev_dp *dp_pdev)
{
	struct ath12k_link_vif *arvif;
	struct ath12k_dp_ipa_stats_vif_hw_stats *link_vif_stats;
	struct ath12k_skb_cb *skb_cb = ATH12K_SKB_CB(skb);
	struct ieee80211_hdr *hdr = NULL;

	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(skb_cb->vif);
	bool is_mcast, is_bcast;
	u64 skb_len = skb->len;
	struct ethhdr *eth = NULL;
	u8 *dst_addr;

	if (!dp_pdev) {
		ath12k_err(NULL, "Got dp_pdev NULL");
		return;
	}

	if (!dp_pdev->ar->fw_stats.en_vdev_stats_ol) {
		ath12k_info(NULL, "Vdev offload stats disabled");
		return;
	}

	if (skb_cb->flags & ATH12K_SKB_HW_80211_ENCAP) {
		eth = (struct ethhdr *)skb->data;
		dst_addr = eth->h_dest;
	} else {
		hdr = (void *)skb->data;
		dst_addr = hdr->addr1;
	}

	is_mcast = is_multicast_ether_addr(dst_addr);
	is_bcast = false;

	if (is_mcast) {
		rcu_read_lock();
		arvif = rcu_dereference(ahvif->link[skb_cb->link_id]);
		link_vif_stats = arvif->extn.stats;
		is_bcast = is_broadcast_ether_addr(dst_addr);
		if (is_bcast)
			is_mcast = false;

		ath12k_dp_ipa_tx_rx_mcbc_stats(&link_vif_stats->tx.stats,
					       is_mcast, is_bcast,
					       skb_len);
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL(ath12k_dp_ipa_tx_mcbc_stats);

void ath12k_dp_ipa_rx_error_stats(struct ath12k_pdev_dp *dp_pdev,
				  struct ath12k_skb_rxcb *rxcb, u8 link_id)
{
	struct ath12k_dp_link_peer *link_peer;
	struct ath12k_vif *ahvif;
	struct ath12k *ar;
	struct ath12k_wbm_rx_stats *err;

	if (!dp_pdev)
		return;

	ar = dp_pdev->ar;
	if (!ar && !ar->fw_stats.en_vdev_stats_ol)
		return;

	rcu_read_lock();
	link_peer = ath12k_dp_link_peer_find_by_peerid_index(dp_pdev->dp, dp_pdev,
							     rxcb->peer_id);
	if (!link_peer)
		return;
	ahvif = ath12k_vif_to_ahvif(link_peer->vif);

	if (!ahvif)
		return;
	struct ath12k_link_vif *arvif = rcu_dereference(ahvif->link[link_id]);

	if (!arvif)
		return;
	err = &arvif->extn.stats->rx.err;
	switch (rxcb->err_rel_src) {
	case HAL_WBM_REL_SRC_MODULE_REO:
		err->reo_error[rxcb->err_code]++;
		break;
	case HAL_WBM_REL_SRC_MODULE_RXDMA:
		err->rxdma_error[rxcb->err_code]++;
		break;
	default:
		break;
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(ath12k_dp_ipa_rx_error_stats);

static inline void
ath12k_dp_ipa_stats_ieee80211_rx_stats(struct net_device *dev,
					u64 packets, u64 bytes)
{
	struct pcpu_sw_netstats *tstats = this_cpu_ptr(netdev_tstats(dev));

	u64_stats_update_begin(&tstats->syncp);

	u64_stats_set(&tstats->rx_packets, packets);
	u64_stats_set(&tstats->rx_bytes, bytes);

	u64_stats_update_end(&tstats->syncp);
}

static inline void
ath12k_dp_ipa_stats_ieee80211_tx_stats(struct net_device *dev,
					u64 packets, u64 bytes)
{
	struct pcpu_sw_netstats *tstats = this_cpu_ptr(netdev_tstats(dev));

	u64_stats_update_begin(&tstats->syncp);

	u64_stats_set(&tstats->tx_packets, packets);
	u64_stats_set(&tstats->tx_bytes, bytes);

	u64_stats_update_end(&tstats->syncp);
}

void ath12k_dp_ipa_htt_vdev_txrx_stats_handler(struct ath12k_base *ab,
					       struct sk_buff *skb)
{
	struct htt_t2h_vdev_txrx_stats_ind *vdev_tlv;
	struct ath12k_link_vif *arvif;
	struct ath12k_dp_ipa_stats_vif_hw_stats *link_vif_stats;
	struct ath12k_dp_ipa_stats_vif_stats *tx_stats, *rx_stats;
	struct ieee80211_vif *vif = NULL;
	struct wireless_dev *wdev = NULL;
	struct net_device *net_dev = NULL;
	u32 *tx_err;
	u32 *data = (u32 *)skb->data;
	u32 vdev_id;
	u16 payload_bytes, tlv_tag, tlv_len;

	payload_bytes = u32_get_bits(*data, HTT_T2H_VDEV_STATS_PERIODIC_PAYLOAD_BYTES);
	if (payload_bytes > skb->len) {
		ath12k_err(ab, "Payload bytes incorrect in vdev offload stats");
		return;
	}

	/* Adjust msg_word to point to the first TLV in buffer */
	data += HTT_T2H_VDEV_TXRX_STATS_PERIODIC_IND_HDR_WORDS;

	rcu_read_lock();
	while (payload_bytes > 0) {
		tlv_tag = u32_get_bits(*data, HAL_STATS_TLV_HDR_TAG);
		tlv_len = u32_get_bits(*data, HAL_STATS_TLV_HDR_LEN);

		/* Adjust data to point to the data value after TLV header */
		data += HTT_T2H_VDEV_TXRX_STATS_TLV_HDR_WORDS;

		if (tlv_tag == HTT_STATS_VDEV_TXRX_STATS_HW_STATS_TAG) {
			vdev_tlv = (struct htt_t2h_vdev_txrx_stats_ind *)data;
			vdev_id = __le32_to_cpu(vdev_tlv->vdev_id);
			arvif = ath12k_mac_get_arvif_by_vdev_id(ab, vdev_id);
			if (!arvif) {
				ath12k_err(ab,
					   "ol vdev stats error in getting arvif from vdev id:%d\n",
					   vdev_id);
				goto next;
			}

			link_vif_stats = arvif->extn.stats;
			if (!link_vif_stats) {
				ath12k_err(ab,
					   "ol vdev stats error in getting stats from arvif\n");
				goto next;
			}

			rx_stats = &link_vif_stats->rx.stats;
			tx_stats = &link_vif_stats->tx.stats;
			tx_err = link_vif_stats->tx.err;

			rx_stats->total.bytes =
				HTT_VDEV_GET_STATS_U64(vdev_tlv->rx_msdu_byte_cnt_lo,
						       vdev_tlv->rx_msdu_byte_cnt_hi);
			rx_stats->total.packets =
				HTT_VDEV_GET_STATS_U64(vdev_tlv->rx_msdu_cnt_lo,
						       vdev_tlv->rx_msdu_cnt_hi);
			tx_stats->total.bytes =
				HTT_VDEV_GET_STATS_U64(vdev_tlv->tx_msdu_byte_cnt_lo,
						       vdev_tlv->tx_msdu_byte_cnt_hi);
			tx_stats->total.packets =
				HTT_VDEV_GET_STATS_U64(vdev_tlv->tx_msdu_cnt_lo,
						       vdev_tlv->tx_msdu_cnt_hi);

			/* Retry Drop packet count */
			tx_err[ATH12K_DP_IPA_STATS_RETRY] =
				HTT_VDEV_GET_STATS_U64(vdev_tlv->tx_retry_cnt_lo,
						       vdev_tlv->tx_retry_cnt_hi);
			/* Expired Packet count */
			tx_err[ATH12K_DP_IPA_STATS_EXPIRED] =
				HTT_VDEV_GET_STATS_U64(vdev_tlv->tx_drop_cnt_lo,
						       vdev_tlv->tx_drop_cnt_hi);

			/* Congestion Control drop(Age out) Packet count */
			tx_err[ATH12K_DP_IPA_STATS_AGE_OUT] =
				HTT_VDEV_GET_STATS_U64(vdev_tlv->msdu_ttl_cnt_lo,
						       vdev_tlv->msdu_ttl_cnt_hi);

			ath12k_dp_ipa_stats_update_ucast(&link_vif_stats->tx.stats);
			ath12k_dp_ipa_stats_update_ucast(&link_vif_stats->rx.stats);

			vif = ath12k_ahvif_to_vif(arvif->ahvif);
			if (!vif) {
				ath12k_err(ab,
					   "ol vif stats error in getting vif from arvif\n");
				goto next;
			}

			wdev = ieee80211_vif_to_wdev(vif);
			net_dev = wdev ? wdev->netdev : NULL;
			if (!net_dev) {
				ath12k_err(ab,
					   "Net device is Null");
				goto next;
			}

			ath12k_dp_ipa_stats_ieee80211_rx_stats(net_dev,
							       rx_stats->total.packets,
							       rx_stats->total.bytes);
			ath12k_dp_ipa_stats_ieee80211_tx_stats(net_dev,
							       tx_stats->total.packets,
							       tx_stats->total.bytes);
		}

next:
		data += tlv_len >> BYTE_TO_DWORD_SHIFT;
		/* Add header size to tlv length */
		tlv_len += HTT_T2H_VDEV_TXRX_STATS_TLV_HDR_SIZE;
		payload_bytes -= tlv_len;
	}
	rcu_read_unlock();
}
