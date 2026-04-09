/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_ipa_stats.h"

void ath12k_dp_ipa_htt_vdev_txrx_stats_handler(struct ath12k_base *ab,
					       struct sk_buff *skb)
{
	struct htt_t2h_vdev_txrx_stats_ind *vdev_tlv;
	struct ath12k_link_vif *arvif;
	struct ath12k_dp_ipa_stats_vif_hw_stats *link_vif_stats;
	struct ath12k_dp_ipa_stats_vif_stats *tx_stats, *rx_stats;
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

		}

next:
		data += tlv_len >> BYTE_TO_DWORD_SHIFT;
		/* Add header size to tlv length */
		tlv_len += HTT_T2H_VDEV_TXRX_STATS_TLV_HDR_SIZE;
		payload_bytes -= tlv_len;
	}
	rcu_read_unlock();
}
