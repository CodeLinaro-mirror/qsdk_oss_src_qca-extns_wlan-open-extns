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

	ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
		   "custom mgmt tx completion: status %d\n",
		   status);

	raw_pkt_ctx = &ar->ar_extn.raw_pkt_ctx;

	spin_lock_bh(&raw_pkt_ctx->chan_lock);
	skb_queue_walk(&raw_pkt_ctx->pkt_list, tmp_skb) {
		if (tmp_skb == skb) {
			raw_pkt_ctx->custom_tx_status[idx] = status;
			__skb_unlink(skb, &raw_pkt_ctx->pkt_list);
			break;
		}
		idx++;
		if (idx == ATH12K_MAX_CUSTOM_TX_PKT)
			break;
	}
	spin_unlock_bh(&raw_pkt_ctx->chan_lock);

	dev_kfree_skb_any(skb);
}

/**
 * ath12k_fill_tx_param() - Save Tx param config in ar
 * @skb: Pointer to the skb
 * @nss: NSS
 * @preamble: Preamble
 * @mcs: MCS
 * @retry: Number of Retry
 * @power: Tx power
 * tx_beamforming: Tx Beamforming
 * band: Band
 *
 * This function saves user configured Tx Parameters
 * raw_pkt_ctx which is later used to send these
 * configs to target
 *
 * Return: 0 On success -E* on error
 */
#define OFFCHAN_MAX_MCS 7
#define OFFCHAN_MAX_CCK_MCS 3
static int ath12k_fill_tx_param(struct sk_buff *skb,
				u8 nss, u8 preamble, u8 mcs,
				u8 retry, u8 power, u8 tx_beamforming,
				u8 band)
{
	struct ath12k_skb_cb *skb_cb;
	struct ath12k *ar;
	struct ath12k_skb_tx_param *tx_param;
	struct ieee80211_hdr *hdr;
	u8 rate_idx;
	__le16 fc;

	skb_cb = ATH12K_SKB_CB(skb);
	ar = skb_cb->u.ar;
	tx_param = &ar->ar_extn.raw_pkt_ctx.tx_param;

	if (preamble >= PREAMBLE_INVALID) {
		ath12k_err(ar->ab, "preamble %d not supported", preamble);
		return -EINVAL;
	}

	if (mcs > OFFCHAN_MAX_MCS) {
		ath12k_err(ar->ab, "mcs %d not supported", mcs);
		return -EINVAL;
	}

	/* Check if CCK rate is valid for 5GHz */
	if (band == BAND_5GHZ && preamble == PREAMBLE_CCK) {
		ath12k_err(ar->ab, "Invalid rate: CCK rate is not valid for 5G\n");
		return -EINVAL;
	}

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;

	memset(tx_param, 0, sizeof(*tx_param));
	if (ieee80211_is_data(fc)) {
		tx_param->is_data = true;
		goto fill_tx_param;
	}

	if (preamble == PREAMBLE_CCK) {
		if (mcs > OFFCHAN_MAX_CCK_MCS) {
			ath12k_err(ar->ab, "Invalid rate for mgmt\n");
			return -EINVAL;
		}
		rate_idx = mcs;
	} else
		rate_idx = mcs + 4;

	if (!(skb_cb->vif->bss_conf.basic_rates & BIT(rate_idx))) {
		ath12k_err(ar->ab, "Basic rate expected for mgmt packets\n");
		return -EINVAL;
	}

fill_tx_param:
	tx_param->nss = nss;
	tx_param->preamble = preamble;
	tx_param->mcs = mcs;
	tx_param->retry = retry;
	tx_param->power = power;
	tx_param->tx_beamforming = tx_beamforming;
	tx_param->tx_param_configured = true;

	return 0;
}

/**
 * ath12k_process_rate_params() - Process rate parameters for WMI command
 * @mcs: Pointer to the MCS value (will be updated)
 * @nss: Pointer to the NSS value (will be updated)
 * @preamble: The preamble type
 *
 * This function processes the rate parameters for CCK and OFDM rates,
 * updating the MCS and NSS values as needed.
 */
static void ath12k_process_rate_params(u32 *mcs, u32 *nss,
				       enum preamble_type preamble)
{
	/* CCK bit positions: 3, 2, 1, 0 for rates 1, 2, 5.5, 11 Mbps */
	static const u8 cck_bit_pos[] = {3, 2, 1, 0};
	/* OFDM bit positions: 10, 8, 6, 4, 11, 9, 7, 5 for
	 * rates 6, 9, 12, 18, 24, 36, 48, 54 Mbps
	 */
	static const u8 ofdm_bit_pos[] = {10, 8, 6, 4, 11, 9, 7, 5};

	if (preamble == PREAMBLE_CCK) {
		if (*mcs < ARRAY_SIZE(cck_bit_pos))
			*mcs = (0x1 << cck_bit_pos[*mcs]);
		else
			*mcs = 0;
		/* For CCK, NSS is always 1 */
		*nss = 1;
	} else if (preamble == PREAMBLE_OFDM) {
		if (*mcs < ARRAY_SIZE(ofdm_bit_pos))
			*mcs = (0x1 << ofdm_bit_pos[*mcs]);
		else
			*mcs = 0;
		/* For OFDM, NSS is always 1 */
		*nss = 1;
	}
}

void ath12k_wmi_prepare_tx_params_extn(struct ath12k *ar, void *ptr)
{
	struct ath12k_skb_tx_param *tx_param = &ar->ar_extn.raw_pkt_ctx.tx_param;
	struct wmi_mgmt_send_params *params = (struct wmi_mgmt_send_params *)ptr;
	u32 *tx_param_dword0 = &params->tx_param_dword0;
	u32 *tx_param_dword1 = &params->tx_param_dword1;
	u32 mcs = tx_param->mcs;
	u32 nss = tx_param->nss;
	u32 preamble = tx_param->preamble;

	/* Initialize the output values */
	*tx_param_dword0 = 0;
	*tx_param_dword1 = 0;

	if (!tx_param->tx_param_configured)
		return;

	ath12k_process_rate_params(&mcs, &nss, preamble);

	/* Set power in dword0 [7:0] */
	*tx_param_dword0 = le32_encode_bits(tx_param->power,
					    WMI_TX_PARAMS_DWORD0_POWER) |
			   /* Set MCS in dword0 [19:8] */
			   le32_encode_bits(mcs,
					    WMI_TX_PARAMS_DWORD0_MCS_MASK) |
			   /* Set NSS in dword0 [27:20] */
			   le32_encode_bits((nss ? (1 << (nss - 1)) : 0),
					    WMI_TX_PARAMS_DWORD0_NSS_MASK) |
			   /* Set retry limit in dword0 [31:28] */
			   le32_encode_bits((tx_param->retry ? (tx_param->retry & 0xF) : 1),
					    WMI_TX_PARAMS_DWORD0_RETRY_LIMIT);

	/* Set preamble type in dword1 [19:15] */
	*tx_param_dword1 = le32_encode_bits((1 << preamble),
					    WMI_TX_PARAMS_DWORD1_PREAMBLE_TYPE) |
			   /* Set frame type in dword1 [20] */
			   le32_encode_bits(tx_param->is_data,
					    WMI_TX_PARAMS_DWORD1_FRAME_TYPE) |
			   /* Set beamforming in dword1 [22] */
			   le32_encode_bits(tx_param->tx_beamforming,
					    WMI_TX_PARAMS_DWORD1_BEAMFORM) |
			   /* Set retry limit ext in dword1 [25:23] */
			   le32_encode_bits((tx_param->retry ? ((tx_param->retry & 0x70) >> 4) : 0),
					    WMI_TX_PARAMS_DWORD1_RETRY_LIMIT_EXT);
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
	/* This API will be invoked from Vendor command path.
	 * For now, adding dummy call here to resolve dependency.
	 * It will be removed later from here once Vendor command implemented.
	 */
	ath12k_fill_tx_param(skb, 0, 0, 0, 0, 0, 0, 0);

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
