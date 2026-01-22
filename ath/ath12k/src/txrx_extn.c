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
	struct wireless_dev *wdev = NULL;
	struct ieee80211_vif *vif;
	u8 idx = 0;
	u32 transaction_id;

	ar = skb_cb->u.ar;
	if (!ar) {
		dev_kfree_skb_any(skb);
		return;
	}

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

	vif = skb_cb->vif;
	if (!vif) {
		ath12k_warn(ar->ab, "failed to find vif to update txcompl mgmt stats\n");
		dev_kfree_skb_any(skb);
		return;
	}

	/* Extract transaction ID from skb control buffer */
	transaction_id = skb_cb->u.ar->ar_extn.raw_pkt_ctx.transaction_id;

	/* Get the wireless_dev for sending the event */
	if (ar->ah && ar->ah->hw && ar->ah->hw->wiphy)
		wdev = ieee80211_vif_to_wdev(vif);

	/* Send event to userspace if we have a valid wdev */
	if (wdev) {
		if (atomic_dec_and_test(&raw_pkt_ctx->num_cmpl_pending)) {
			ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
				   "All packets completed, sending event\n");
			/* Cancel timer if still running (must be done before lock) */
			del_timer_sync(&raw_pkt_ctx->offchan_timer);
			/* Acquire lock for cleanup operations */
			spin_lock_bh(&raw_pkt_ctx->chan_lock);
			/* Send event to userspace */
			schedule_home_offchan_stats_event_ext(wdev, transaction_id, raw_pkt_ctx);
			/* Clear the vif pointer to indicate operation complete */
			raw_pkt_ctx->vif = NULL;
			/* Clear custom_tx_status array */
			memset(raw_pkt_ctx->custom_tx_status, 0, sizeof(raw_pkt_ctx->custom_tx_status));
			/* Clear channel statistics */
			memset(&raw_pkt_ctx->chan_stat, 0, sizeof(raw_pkt_ctx->chan_stat));
			spin_unlock_bh(&raw_pkt_ctx->chan_lock);
		}
	}

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
	/* CCK bit positions: 3, 2, 1, 0 for rates 1, 2, 5.5, 11 Mbps */
	const u8 cck_bit_pos[] = {3, 2, 1, 0};
	/* OFDM bit positions: 10, 8, 6, 4, 11, 9, 7, 5 for
	 * rates 6, 9, 12, 18, 24, 36, 48, 54 Mbps
	 */
	const u8 ofdm_bit_pos[] = {10, 8, 6, 4, 11, 9, 7, 5};

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

	if (!skb_cb->vif->bss_conf.basic_rates) {
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "Basic rate not configured! skipping basic rate check");
		goto fill_tx_param;
	}

	if (preamble == PREAMBLE_CCK) {
		if (mcs > OFFCHAN_MAX_CCK_MCS) {
			ath12k_err(ar->ab, "Invalid rate for mgmt\n");
			return -EINVAL;
		}
		rate_idx = cck_bit_pos[mcs];
	} else
		rate_idx = ofdm_bit_pos[mcs];

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
	const u8 cck_bit_pos[] = {3, 2, 1, 0};
	/* OFDM bit positions: 10, 8, 6, 4, 11, 9, 7, 5 for
	 * rates 6, 9, 12, 18, 24, 36, 48, 54 Mbps
	 */
	const u8 ofdm_bit_pos[] = {10, 8, 6, 4, 11, 9, 7, 5};

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

	if (!tx_param->tx_param_configured)
		return;

	ath12k_process_rate_params(&mcs, &nss, preamble);

	/* Set power in dword0 [7:0] */
	*tx_param_dword0 |= le32_encode_bits(tx_param->power,
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
	*tx_param_dword1 |= le32_encode_bits((1 << preamble),
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
	tx_param->tx_param_configured = false;
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

	if (!(info->flags & IEEE80211_TX_CTL_TX_OFFCHAN))
		atomic_inc(&ar->num_pending_mgmt_tx);

	ath12k_mgmt_over_wmi_tx_work(hw->wiphy, &ar->wmi_mgmt_tx_work);

	return 0;

free_skb:
	dev_kfree_skb_any(skb);
	return ret;
}

void ath12k_update_offchan_stats_extn(struct ath12k *ar, u32 freq,
				      struct wmi_chan_info_event *ch_info)
{
	struct ath12k_raw_pkt_ctx *raw_ctx;
	struct ath12k_offchan_stat *stats;

	if (!ar) {
		ath12k_dbg(NULL, ATH12K_DBG_WMI,
			   "failed to update offchan stats, ar is NULL");
		return;
	}
	raw_ctx = &ar->ar_extn.raw_pkt_ctx;

	spin_lock_bh(&raw_ctx->chan_lock);

	/* Check if offchan operation is active:
	 * 1. Timer is pending (indicates offchan operation in progress)
	 * 2. Not on home channel
	 * 3. Frequency matches the current offchan frequency
	 */
	if (timer_pending(&raw_ctx->offchan_timer) &&
	    !raw_ctx->home_chan &&
	    raw_ctx->req_freq == freq) {
		stats = &raw_ctx->chan_stat;

		stats->noise_floor = (s16)le32_to_cpu(ch_info->noise_floor);
		stats->tx_frame_count = le32_to_cpu(ch_info->tx_frame_cnt);
		stats->rx_clear_count = le32_to_cpu(ch_info->rx_clear_count);
		stats->rx_frame_count = le32_to_cpu(ch_info->rx_frame_count);
		stats->cycle_count = le32_to_cpu(ch_info->cycle_count);
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "offchan stats updated: freq=%u nf=%d tx=%u rx=%u "
			   "rx_clear=%u cycle=%u\n",
			   freq, stats->noise_floor, stats->tx_frame_count,
			   stats->rx_frame_count, stats->rx_clear_count,
			   stats->cycle_count);
	} else {
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "Disgarding offchan stats update [%u, %u, %u, %u]",
			    timer_pending(&raw_ctx->offchan_timer),
			    raw_ctx->home_chan, raw_ctx->req_freq,
			    freq);
	}
	spin_unlock_bh(&raw_ctx->chan_lock);
}

/**
 * ath12k_compute_offchan_timing_stats - Compute offchan timing stats
 *                                       from timestamps
 *
 * @raw_ctx: Pointer to raw packet context
 *
 * Computes timing statistics:
 * - dwell_time: Time spent on foreign channel (foreign_exit - foreign_entry)
 * - chanswitch_time_htof: Time to switch from home to foreign
 *                         (foreign_entry - scan_start)
 * - chanswitch_time_ftoh: Time to switch from foreign to home
 *                         (scan_done - foreign_exit)
 */
static void ath12k_compute_offchan_timing_stats(struct ath12k_raw_pkt_ctx *raw_ctx)
{
	struct ath12k_offchan_stat *stats = &raw_ctx->chan_stat;
	s64 dwell_us, htof_us, ftoh_us;

	if (!raw_ctx->ts_scan_start)
		return;

	/* Compute dwell time if we have both entry and exit timestamps */
	if (raw_ctx->ts_foreign_entry && raw_ctx->ts_foreign_exit) {
		dwell_us = ktime_us_delta(raw_ctx->ts_foreign_exit,
					  raw_ctx->ts_foreign_entry);
		if (dwell_us > 0)
			stats->dwell_time = (u32)dwell_us;
	}

	/* Compute home-to-foreign channel switch time */
	htof_us = ktime_us_delta(raw_ctx->ts_foreign_entry,
				 raw_ctx->ts_scan_start);
	if (htof_us > 0)
		stats->chanswitch_time_htof = (u32)htof_us;

	/* Compute foreign-to-home channel switch time */
	if (raw_ctx->ts_foreign_exit && raw_ctx->ts_scan_done) {
		ftoh_us = ktime_us_delta(raw_ctx->ts_scan_done,
					 raw_ctx->ts_foreign_exit);
		if (ftoh_us > 0)
			stats->chanswitch_time_ftoh = (u32)ftoh_us;
	}

	ath12k_dbg(NULL, ATH12K_DBG_WMI,
		   "offchan timing: dwell=%u us, htof=%u us, ftoh=%u us",
		   stats->dwell_time, stats->chanswitch_time_htof,
		   stats->chanswitch_time_ftoh);
}

void ath12k_update_offchan_ts_extn(struct ath12k *ar, u32 event_type)
{
	struct ath12k_raw_pkt_ctx *raw_ctx;

	if (!ar)
		return;

	raw_ctx = &ar->ar_extn.raw_pkt_ctx;

	/* Only update timestamps if offchan operation is active */
	spin_lock_bh(&raw_ctx->chan_lock);
	if (!timer_pending(&raw_ctx->offchan_timer)) {
		spin_unlock_bh(&raw_ctx->chan_lock);
		return;
	}

	switch (event_type) {
	case WMI_SCAN_EVENT_STARTED:
		raw_ctx->ts_scan_start = ktime_get();
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "offchan: captured scan_start timestamp\n");
		break;
	case WMI_SCAN_EVENT_FOREIGN_CHAN:
		raw_ctx->ts_foreign_entry = ktime_get();
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "offchan: captured foreign_entry timestamp\n");
		break;
	case WMI_SCAN_EVENT_FOREIGN_CHAN_EXIT:
		raw_ctx->ts_foreign_exit = ktime_get();
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "offchan: captured foreign_exit timestamp\n");
		break;
	case WMI_SCAN_EVENT_COMPLETED:
		raw_ctx->ts_scan_done = ktime_get();
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "offchan: captured scan done timestamp\n");
		break;
	case WMI_SCAN_EVENT_START_FAILED:
	case WMI_SCAN_EVENT_DEQUEUED:
	case WMI_SCAN_EVENT_PREEMPTED:
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "offchan: event %u\n",
			   event_type);
		break;
	default:
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "offchan: event %u not handler\n", event_type);
		spin_unlock_bh(&raw_ctx->chan_lock);
		return;
	}
	ath12k_compute_offchan_timing_stats(raw_ctx);
	spin_unlock_bh(&raw_ctx->chan_lock);
}

/**
 * ath12k_custom_txrx_timeout_handler - Timeout handler for home off-channel operations
 * @t: Timer structure
 *
 * Called when home/off-channel operation times out. Cleans up and sends error response.
 */
static void ath12k_custom_txrx_timeout_handler(struct timer_list *t)
{
	struct ath12k_raw_pkt_ctx *raw_pkt_ctx = from_timer(raw_pkt_ctx, t, offchan_timer);
	struct ath12k_vif *ahvif;
	struct ath12k_hw *ah;
	struct ath12k *ar;
	struct wireless_dev *wdev;

	if (!raw_pkt_ctx || !raw_pkt_ctx->vif)
		return;

	ahvif = (struct ath12k_vif *)raw_pkt_ctx->vif->drv_priv;
	if (!ahvif)
		return;

	ah = ahvif->ah;
	if (!ah)
		return;

	ar = ah->radio;  /* Get ar from ah */
	if (!ar)
		return;

	wdev = ieee80211_vif_to_wdev(raw_pkt_ctx->vif);
	if (!wdev) {
		ath12k_err(ar->ab, "Failed to get wdev from vif\n");
		return;
	}

	ath12k_warn(ar->ab, "Home/off-channel TX timed out for transaction 0x%x\n",
		    raw_pkt_ctx->transaction_id);

	/* Drain the queue first (this acquires and releases the lock internally) */
	ath12k_home_offchan_tx_drain(raw_pkt_ctx);

	/* Now acquire lock for all remaining operations to ensure atomicity */
	spin_lock_bh(&raw_pkt_ctx->chan_lock);

	/* Send timeout response to user space while still holding lock */
	schedule_home_offchan_stats_event_ext(wdev, raw_pkt_ctx->transaction_id, raw_pkt_ctx);

	/* Clear the vif pointer to indicate operation complete */
	raw_pkt_ctx->vif = NULL;
	/* Clear custom_tx_status array */
	memset(raw_pkt_ctx->custom_tx_status, 0, sizeof(raw_pkt_ctx->custom_tx_status));
	/* Clear channel statistics */
	memset(&raw_pkt_ctx->chan_stat, 0, sizeof(raw_pkt_ctx->chan_stat));

	spin_unlock_bh(&raw_pkt_ctx->chan_lock);
}

void ath12k_custom_txrx_init(struct ath12k *ar)
{
	struct ath12k_raw_pkt_ctx *raw_ctx = &ar->ar_extn.raw_pkt_ctx;

	spin_lock_init(&raw_ctx->chan_lock);
	timer_setup(&raw_ctx->offchan_timer,
		    ath12k_custom_txrx_timeout_handler, 0);
	skb_queue_head_init(&raw_ctx->pkt_list);
}

void ath12k_custom_txrx_deinit(struct ath12k *ar)
{
	struct ath12k_raw_pkt_ctx *raw_ctx = &ar->ar_extn.raw_pkt_ctx;

	del_timer_sync(&raw_ctx->offchan_timer);

	spin_lock_bh(&raw_ctx->chan_lock);
	skb_queue_purge(&raw_ctx->pkt_list);
	spin_unlock_bh(&raw_ctx->chan_lock);
}

/**
 * ath12k_vendor_home_offchan_tx_rx_frame - Process and transmit home/off-channel frame
 * @params: Pointer to home/off-channel operation parameters
 *
 * This function parses the nested netlink attributes containing frame data and
 * transmission parameters (NSS, preamble, MCS, retry, power), allocates an SKB,
 * sets up the control buffers, and queues the frame for transmission. It determines
 * whether the transmission is on home channel or off-channel based on the current
 * operating frequency and sets appropriate flags. For off-channel transmissions,
 * it sets up a timer for timeout handling.
 *
 * Return: 0 on success, negative error code on failure
 */
static int ath12k_vendor_home_offchan_tx_rx_frame(struct ath12k_home_offchan_params *params)
{
	struct wireless_dev *wdev = params->wdev;
	struct nlattr *offchan_frame = params->offchan_frame;
	int band = params->band;
	int freq = params->freq;
	u32 transaction_id = params->transaction_id;
	u16 scan_dur = params->scan_dur;
	u8 num_frames = params->num_frames;
	u8 func = params->func;
	struct nlattr *tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_MAX + 1];
	int ret, nss = 0, preamble = 0, mcs = 0, retry = 0, power = 0, rem;
	uint8_t tx_bf = 0;
	char *buf;
	struct nlattr *frame;
	unsigned int attrsize = QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_MAX * sizeof(struct nlattr *);
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	struct ath12k_skb_cb *skb_cb;
	struct ath12k *ar;
	struct ath12k_vif *ahvif;
	struct ath12k_link_vif *arvif = NULL;
	struct ieee80211_vif *vif = NULL;
	int buf_len;
	struct ath12k_hw *ah;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wdev->wiphy);
	struct ieee80211_chanctx_conf *ctx = NULL;
	int cur_freq = 0;
	struct ieee80211_channel *chan;
	struct ath12k_raw_pkt_ctx *raw_pkt_ctx;
	bool is_home_channel = false;

	nla_for_each_nested(frame, offchan_frame, rem) {
		memset(tb, 0, attrsize);
		ret = nla_parse_nested(tb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_MAX,
							   frame,
							   ath12k_vendor_home_offchan_tx_rx_frame_policy, NULL);
		if (ret) {
			ath12k_err(NULL, "Invalid Offchan Frame policy\n");
			return ret;
		}

		if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_NSS])
			nss = nla_get_u8(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_NSS]);
		if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_PREAMBLE])
			preamble = nla_get_u8(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_PREAMBLE]);
		if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_MCS])
			mcs = nla_get_u8(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_MCS]);
		if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_RETRY])
			retry = nla_get_u8(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_RETRY]);
		if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_POWER])
			power = nla_get_u8(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_POWER]);
		if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_TX_BEAMFORMING])
			tx_bf = nla_get_u8(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_TX_BEAMFORMING]);
		if (!tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_DATA]) {
			ath12k_err(NULL, "Frame data missing\n");
			return -EINVAL;
		}

		buf = nla_data(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_DATA]);
		buf_len = nla_len(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_DATA]);

		ah = ath12k_hw_to_ah(hw);
		if (!ah) {
			ath12k_err(NULL, "Failed to get ah from wiphy\n");
			return -ENODEV;
		}
		/* Get the ar from the ah using band currently */
		ar = ath12k_ah_to_ar(ah, band);

		if (!ar) {
			ath12k_err(NULL, "Failed to get ar from ah\n");
			return -ENODEV;
		}

		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "Frame data received: len=%d, band=%d\n", buf_len, band);

		/* Get raw packet context structure */
		raw_pkt_ctx = &ar->ar_extn.raw_pkt_ctx;

		list_for_each_entry(arvif, &ar->arvifs, list) {
			if (arvif->is_up && arvif->vdev_id != 0xFF) {
				// Found a valid arvif
				ahvif = container_of((void *)arvif, struct ath12k_vif, deflink);
				vif = ahvif->vif;
				break;
			}
		}

		if (!vif || !arvif) {
			ath12k_err(ar->ab, "No active vif found\n");
			return -ENODEV;
		}

		/* Check if chanctx is valid before allocating SKB */
		if (!arvif->chanctx.def.chan) {
			ath12k_err(ar->ab, "Failed to send link info due to no chan ctx\n");
			return -EINVAL;
		}

		/* Allocate a new skb with enough headroom for the 802.11 header */
		skb = dev_alloc_skb(buf_len);
		if (!skb)
			return -ENOMEM;

		skb_put_data(skb, buf, buf_len);

		info = IEEE80211_SKB_CB(skb);
		skb_cb = ATH12K_SKB_CB(skb);

		/* Clear the skb_cb structure */
		memset(skb_cb, 0, sizeof(*skb_cb));

		skb_cb->vif = vif;

		/* Set the ar in the skb_cb */
		skb_cb->u.ar = ar;

		/* Set the link_id (usually 0 for non-MLO) */
		skb_cb->link_id = 0;

		/* Transaction ID is already stored in raw_pkt_ctx->transaction_id above */
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "Stored transaction ID 0x%x in raw_pkt_ctx\n", transaction_id);

		ctx = &arvif->chanctx;
		chan = ctx->def.chan;
		cur_freq = chan->center_freq;

		ret = ath12k_fill_tx_param(skb, nss, preamble, mcs, retry, power, tx_bf, band);
		if (ret) {
			ath12k_err(ar->ab, "Failed to fill tx param: %d\n", ret);
			dev_kfree_skb_any(skb);
			return ret;
		}

		/* Set the control.vif field */
		info->control.vif = vif;

		/* Determine if home channel or off-channel and set appropriate flags */
		is_home_channel = (freq == cur_freq);

		/*
		* Set flags to indicate to driver whether this is home channel or off-channel TX
		* This is CRITICAL for the driver to handle the packet correctly
		*/
		if (is_home_channel) {
			skb_cb->flags |= ATH12K_SKB_CUSTOM_MGMT_TX;
			ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
				   "Home channel TX - flag set to ATH12K_SKB_CUSTOM_MGMT_TX\n");
		} else {
			skb_cb->flags |= ATH12K_SKB_CUSTOM_OFFCHAN_MGMT_TX;
			ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
				   "Off-channel TX - flag set to ATH12K_SKB_CUSTOM_OFFCHAN_MGMT_TX\n");
		}

		/* Initialize context under lock protection BEFORE queuing packet */
		spin_lock_bh(&raw_pkt_ctx->chan_lock);
		/* Initialize context fields */
		raw_pkt_ctx->transaction_id = transaction_id;
		raw_pkt_ctx->func_type = func;
		raw_pkt_ctx->num_frames = num_frames;
		raw_pkt_ctx->home_chan = is_home_channel;
		raw_pkt_ctx->vif = vif;

		/* Initialize all packet statuses to 1 (failed) by default.
		 * They will be set to 0 (success) only when we receive completion.
		 * This way, any packet that times out or doesn't complete will
		 * already be marked as failed without needing explicit handling.
		 */
		memset(raw_pkt_ctx->custom_tx_status, 1, sizeof(raw_pkt_ctx->custom_tx_status));

		/* Set atomic counter to expected number of completions (num_frames).
		 * This must be done atomically with packet queuing to prevent race
		 * where completion handler runs before counter is set.
		 */
		atomic_set(&raw_pkt_ctx->num_cmpl_pending, num_frames);
		/* Now add packet to queue */
		skb_queue_tail(&raw_pkt_ctx->pkt_list, skb);
		spin_unlock_bh(&raw_pkt_ctx->chan_lock);

		if (is_home_channel) {
			/* Home channel - send immediately */
			ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
				   "Home channel TX - sending immediately\n");
			ath12k_custom_tx_send(vif, raw_pkt_ctx);
		} else {
			/* Off-channel - set timer and send */
			ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
				   "Off-channel TX - setting timer for %d ms and sending to driver\n",
				   ATH12K_MAX_OFFCHAN_WAIT + scan_dur);
			mod_timer(&raw_pkt_ctx->offchan_timer,
				   jiffies + msecs_to_jiffies(ATH12K_MAX_OFFCHAN_WAIT + scan_dur));
			ath12k_custom_tx_send(vif, raw_pkt_ctx);
		}
		// Only process the first frame for now
		break;
	}

	return 0;
}

void ath12k_home_offchan_tx_drain(struct ath12k_raw_pkt_ctx *raw_pkt_ctx)
{
	struct sk_buff *skb;

	if (!raw_pkt_ctx)
		return;

	spin_lock_bh(&raw_pkt_ctx->chan_lock);
	while ((skb = skb_dequeue(&raw_pkt_ctx->pkt_list)) != NULL) {
		/* Decrement counter for both home and off-channel packets
		 * since we increment it for both in tx_rx_frame function
		 */
		atomic_dec(&raw_pkt_ctx->num_cmpl_pending);
		dev_kfree_skb_any(skb);
	}
	spin_unlock_bh(&raw_pkt_ctx->chan_lock);
}

void ath12k_custom_tx_send(struct ieee80211_vif *vif,
			     struct ath12k_raw_pkt_ctx *raw_pkt_ctx)
{
	struct sk_buff *skb;
	struct ath12k_vif *ahvif;
	struct ath12k_hw *ah;
	struct ath12k *ar;
	struct ieee80211_tx_control control = {};
	struct ieee80211_hw *hw;

	if (!raw_pkt_ctx || !vif)
		return;

	ahvif = (struct ath12k_vif *)vif->drv_priv;
	ah = ahvif->ah;
	ar = ah->radio;  /* Get ar from ah */
	hw = ah->hw;

	spin_lock_bh(&raw_pkt_ctx->chan_lock);
	skb = skb_dequeue(&raw_pkt_ctx->pkt_list);
	spin_unlock_bh(&raw_pkt_ctx->chan_lock);

	if (!skb)
		return;

	/* Transmit the packet */
	control.sta = NULL;
	//ieee80211_tx_status(hw, skb);
	ath12k_custom_tx_extn(skb);
}

void ath12k_home_offchan_cleanup(struct ath12k *ar)
{
	struct ath12k_raw_pkt_ctx *raw_pkt_ctx = &ar->ar_extn.raw_pkt_ctx;

	if (!raw_pkt_ctx->vif)
		return;

	del_timer_sync(&raw_pkt_ctx->offchan_timer);
	ath12k_home_offchan_tx_drain(raw_pkt_ctx);

	/* Clear the vif pointer to indicate cleanup */
	raw_pkt_ctx->vif = NULL;
}

int ath12k_vendor_home_offchan_tx_rx_handler(struct wiphy *wiphy,
					      struct wireless_dev *wdev,
					      const void *data,
					      int data_len)
{
	struct nlattr *tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_MAX + 1];
	struct ath12k_home_offchan_params params;
	int ret = 0;
	int func = 0, chan = 0, band = 0, scan_dur = 0, freq = 0;
	u32 transaction_id = 0;
	u8 num_frames = 0;

	ret = nla_parse(tb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_MAX,
			data, data_len,
			ath12k_vendor_home_offchan_tx_rx_policy, NULL);
	if (ret) {
		ath12k_err(NULL, "Invalid attribute with vendor offchan %d\n", ret);
		return ret;
	}

	/* Extract transaction ID for event correlation */
	if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_TRANSACTION_ID])
		transaction_id = nla_get_u32(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_TRANSACTION_ID]);

	if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FUNC])
		func = nla_get_u8(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FUNC]);
	if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_CHAN])
		chan = nla_get_u16(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_CHAN]);
	if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_CHAN_BAND])
		band = nla_get_u8(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_CHAN_BAND]);
	if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_SCAN_DUR])
		scan_dur = nla_get_u16(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_SCAN_DUR]);
	if (tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_NUM_FRAMES])
		num_frames = nla_get_u8(tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_NUM_FRAMES]);

	freq = ieee80211_channel_to_frequency(chan,band);

	/* Handle CANCEL function - doesn't need frame data */
	if (func == QCA_VENDOR_WLAN_HOME_OFFCHAN_TX_RX_FUNC_CANCEL) {
		struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
		struct ath12k_hw *ah;
		struct ath12k *ar;
		struct ath12k_raw_pkt_ctx *raw_pkt_ctx;
		struct wireless_dev *cancel_wdev = wdev;

		ah = ath12k_hw_to_ah(hw);
		if (!ah) {
			ath12k_err(NULL, "Failed to get ah from wiphy for cancel\n");
			return -ENODEV;
		}
		/* Get ar from band */
		ar = ath12k_ah_to_ar(ah, band);
		if (!ar) {
			ath12k_err(NULL, "Failed to get ar from ah for cancel\n");
			return -ENODEV;
		}
		raw_pkt_ctx = &ar->ar_extn.raw_pkt_ctx;
		/* Check if there's an ongoing operation */
		if (!raw_pkt_ctx->vif) {
			ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
				   "No ongoing operation to cancel\n");
			return 0;
		}
		/* Verify transaction ID matches if provided */
		if (transaction_id != 0 && raw_pkt_ctx->transaction_id != transaction_id) {
			ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
				   "Transaction ID mismatch: expected 0x%x, got 0x%x\n",
				   raw_pkt_ctx->transaction_id, transaction_id);
			return -EINVAL;
		}
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "Cancelling operation for transaction 0x%x\n",
			   raw_pkt_ctx->transaction_id);
		/* 1. Cancel timer if running */
		if (timer_pending(&raw_pkt_ctx->offchan_timer)) {
			del_timer_sync(&raw_pkt_ctx->offchan_timer);
			ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
				   "Timer cancelled\n");
		}
		/* 2. Drain packet queue (stop ongoing transmission) */
		ath12k_home_offchan_tx_drain(raw_pkt_ctx);
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "Packet queue drained\n");
		/* 3. Send cancel confirmation event to userspace */
		if (cancel_wdev) {
			schedule_home_offchan_stats_event_ext(cancel_wdev, raw_pkt_ctx->transaction_id, raw_pkt_ctx);
			ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
				   "Cancel confirmation event scheduled\n");
		}
		/* 4. Clean up context */
		raw_pkt_ctx->vif = NULL;
		memset(raw_pkt_ctx->custom_tx_status, 0, sizeof(raw_pkt_ctx->custom_tx_status));
		memset(&raw_pkt_ctx->chan_stat, 0, sizeof(raw_pkt_ctx->chan_stat));
		ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
			   "CANCEL operation completed successfully\n");
		return 0;
	}
	if (!tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME]) {
		ath12k_err(NULL, "Offchan Frame missing\n");
		return -EINVAL;
	}

	/* Assign values to params structure */
	params.wdev = wdev;
	params.offchan_frame = tb[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME];
	params.band = band;
	params.freq = freq;
	params.transaction_id = transaction_id;
	params.scan_dur = scan_dur;
	params.num_frames = num_frames;
	params.func = func;

	ret = ath12k_vendor_home_offchan_tx_rx_frame(&params);
	if (ret) {
		ath12k_err(NULL, "failed to parse Offchan Frames\n");
		return ret;
	}

	return 0;
}

int send_home_offchan_stats_event_with_transaction_id(struct wireless_dev *wdev,
						      u32 transaction_id,
						      struct ath12k_raw_pkt_ctx *raw_pkt_ctx)
{
	struct sk_buff *skb;
	uint32_t data_len;
	void *hdr;
	struct ath12k_offchan_stat *stats = NULL;
	int i;

	/* Get the statistics from raw_pkt_ctx */
	if (raw_pkt_ctx)
		stats = &raw_pkt_ctx->chan_stat;

	/* Get the wireless device from vdev */
	if (!wdev) {
		ath12k_err(NULL, "Invalid wireless device\n");
		return -EINVAL;
	}

	/* Calculate the data length */
	data_len = sizeof(struct ath12k_offchan_stat) + (ATH12K_MAX_CUSTOM_TX_PKT * sizeof(u32));

	/* Allocate an skb for the vendor event */
	skb = cfg80211_vendor_event_alloc(wdev->wiphy, wdev, data_len + 200,
		QCA_NL80211_VENDOR_SUBCMD_WLAN_HOME_OFFCHAN_TX_RX_INDEX, GFP_KERNEL);
	if (!skb) {
		ath12k_err(NULL, "Failed to allocate skb for vendor event\n");
		return -ENOMEM;
	}

	/* Add the vendor data */
	hdr = nla_nest_start(skb, NL80211_ATTR_VENDOR_DATA);
	if (!hdr) {
		ath12k_err(NULL, "Failed to start vendor data\n");
		kfree_skb(skb);
		return -EINVAL;
	}

	/* Add transaction ID for correlation */
	if (transaction_id && nla_put_u32(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_TRANSACTION_ID, transaction_id)) {
		ath12k_err(NULL, "Failed to put transaction ID\n");
		kfree_skb(skb);
		return -EINVAL;
	}

	/* Add function type */
	if (raw_pkt_ctx && nla_put_u8(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FUNC, raw_pkt_ctx->func_type)) {
		ath12k_err(NULL, "Failed to put function type\n");
		kfree_skb(skb);
		return -EINVAL;
	}

	/* Add number of frames */
	if (raw_pkt_ctx && nla_put_u8(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_NUM_FRAMES, raw_pkt_ctx->num_frames)) {
		ath12k_err(NULL, "Failed to put num_frames\n");
		kfree_skb(skb);
		return -EINVAL;
	}

	/* Add channel statistics only for RX function type */
	if (raw_pkt_ctx && stats && raw_pkt_ctx->func_type == QCA_VENDOR_WLAN_HOME_OFFCHAN_TX_RX_FUNC_RX) {
		if (nla_put_s16(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_NOISE_FLOOR, stats->noise_floor) ||
			nla_put_u8(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_BLANKING_VALID, stats->blanking_params_valid) ||
			nla_put_u32(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_TX_FRAME_COUNT, stats->tx_frame_count) ||
			nla_put_u32(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_RX_FRAME_COUNT, stats->rx_frame_count) ||
			nla_put_u32(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_RX_CLEAR_COUNT, stats->rx_clear_count) ||
			nla_put_u32(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_CYCLE_COUNT, stats->cycle_count) ||
			nla_put_u32(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_DWELL_TIME, stats->dwell_time) ||
			nla_put_u32(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_CHANSWITCH_HTOF, stats->chanswitch_time_htof) ||
			nla_put_u32(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_CHANSWITCH_FTOH, stats->chanswitch_time_ftoh) ||
			nla_put_u32(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_BLANKING_COUNT, stats->blanking_count) ||
			nla_put_u32(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_BLANKING_DURATION, stats->blanking_duration)) {
			ath12k_err(NULL, "Failed to put offchan stats fields\n");
			kfree_skb(skb);
			return -EINVAL;
		}
	}

	/* Add per-packet TX status if raw_pkt_ctx is provided */
	if (raw_pkt_ctx && raw_pkt_ctx->num_frames > 0) {
		struct nlattr *pkt_status_nest;
		/* Create nested attribute for packet status array */
		pkt_status_nest = nla_nest_start(skb, QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_TX_PKT_STATUS);
		if (!pkt_status_nest) {
			ath12k_err(NULL, "Failed to create nested attribute for packet status\n");
			kfree_skb(skb);
			return -EINVAL;
		}
		/* Add status for all frames sent from application */
		for (i = 0; i < raw_pkt_ctx->num_frames && i < ATH12K_MAX_CUSTOM_TX_PKT; i++) {
			ath12k_dbg(NULL, ATH12K_DBG_WMI,
				   "Packet %d status: %u\n", i, raw_pkt_ctx->custom_tx_status[i]);
			/* Add packet status as indexed attribute within the nest */
			if (nla_put_u8(skb, i, (u8)raw_pkt_ctx->custom_tx_status[i])) {
				ath12k_err(NULL, "Failed to put packet %d status\n", i);
				kfree_skb(skb);
				return -EINVAL;
			}
		}
		nla_nest_end(skb, pkt_status_nest);
	}

	nla_nest_end(skb, hdr);

	ath12k_dbg(NULL, ATH12K_DBG_WMI,
		   "OFFCHAN EVENT generated with transaction ID 0x%x, sending to userspace\n",
		   transaction_id);
	/* Send the vendor event */
	cfg80211_vendor_event(skb, GFP_KERNEL);

	return 0;
}

/**
 * send_home_offchan_stats_work_fn_with_txn - Workqueue handler for
 * home/off-channel event with transaction ID
 * @work: Pointer to the work_struct member of offchan_event_work_with_txn structure
 *
 * This function is scheduled by schedule_home_offchan_stats_event_ext() and runs in
 * process context. It retrieves the parent offchan_event_work_with_txn structure
 * using container_of(), calls send_offchan_stats_event_with_transaction_id() to
 * send the vendor event to user space, and then frees the work structure.
 *
 * The function is used to defer the event sending from atomic context (where it
 * was scheduled) to process context (where cfg80211 vendor events can be sent).
 *
 * Context: Process context (workqueue)
 * Memory: Frees the offchan_event_work_with_txn structure
 */
static void send_home_offchan_stats_work_fn_with_txn(struct work_struct *work)
{
	struct offchan_event_work_with_txn *event_work =
			container_of(work, struct offchan_event_work_with_txn, work);

	send_home_offchan_stats_event_with_transaction_id(event_work->wdev,
							  event_work->transaction_id,
							  event_work->raw_pkt_ctx);

	kfree(event_work);
}

int schedule_home_offchan_stats_event_ext(struct wireless_dev *wdev, u32 transaction_id,
					   struct ath12k_raw_pkt_ctx *raw_pkt_ctx)
{
	struct offchan_event_work_with_txn *work;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work) {
		ath12k_err(NULL, "Failed to allocate memory for event work, transaction 0x%x\n",
			   transaction_id);
		return -ENOMEM;
	}

	work->wdev = wdev;
	work->transaction_id = transaction_id;
	work->raw_pkt_ctx = raw_pkt_ctx;
	INIT_WORK(&work->work, send_home_offchan_stats_work_fn_with_txn);
	schedule_work(&work->work);

	return 0;
}
