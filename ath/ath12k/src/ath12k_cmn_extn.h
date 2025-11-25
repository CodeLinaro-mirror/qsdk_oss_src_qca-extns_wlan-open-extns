/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_CMN_EXTN_H
#define ATH12K_CMN_EXTN_H

#include <linux/mhi.h>
#include <linux/uuid.h>
#include "../../net/mac80211/qcn_extns/cmn_extn.h"


struct ath12k_base;
struct ieee80211_240mhz_vendor_oper_extn;
struct ieee80211_ops_extn;
struct ieee80211_iface_combination;
struct ath12k_vif;

struct ath12k_vif_extn {
	struct list_head peer_240mhz_list_extn;
	spinlock_t data_lock;
};

/**
 * struct esp_extn - ESP (Estimated Service Parameters) extension
 * @air_time_fraction: Configured airtime fraction
 * @ppdu_duration: Configured PPDU duration
 * @ba_window: Configured Block ACK window size
 * @periodicity: ESP Airtime fraction indication period in seconds
 * @fw_esp_air_time: Firmware-estimated airtime (from WMI event)
 * @airtime_update_work: Work struct for vendor event delivery to report
 * airtime received from firmware
 */
struct esp_extn {
	u32 air_time_fraction;
	u8 ppdu_duration;
	u8 ba_window;
	u16 periodicity;
	u32 fw_esp_air_time;
	struct wiphy_work airtime_update_work;
};

struct sta_240mhz_info {
	struct ieee80211_240mhz_vendor_oper_extn params_240MHz_extn;
	u8 mac_addr[ETH_ALEN];
	struct list_head list;
};

/* Maximum wait time for off-channel operations (milliseconds). */
#define ATH12K_MAX_OFFCHAN_WAIT        50
/* Maximum number of custom pkts for tx per request */
#define ATH12K_MAX_CUSTOM_TX_PKT	20
/* Checks whether SKB has custom pkt */
#define ATH12K_IS_CUSTOM_PKT(cb)	(cb->flags & (ATH12K_SKB_CUSTOM_MGMT_TX |\
						      ATH12K_SKB_CUSTOM_OFFCHAN_MGMT_TX))
/**
 * struct ath12k_offchan_stat - statistics for off-channel activity
 * @noise_floor:        Noise floor in dBm (signed 16-bit).
 * @blanking_params_valid: Blanking parameters validity flag.
 * @tx_frame_count:        Number of frames transmitted.
 * @rx_frame_count:        Number of frames received.
 * @rx_clear_count:        RX clear time (units: driver-specific ticks).
 * @cycle_count:           Total channel cycle time (units: ticks).
 * @dwell_time:            Dwell time on the off-channel (ms).
 * @chanswitch_time_htof:  Time to switch from home to off-channel (us).
 * @chanswitch_time_ftoh:  Time to switch from off-channel to home (us).
 * @blanking_count:        Number of blanking events.
 * @blanking_duration:     Total blanking duration (ms).
 */
struct ath12k_offchan_stat {
	s16 noise_floor;
	u8  blanking_params_valid;
	u32 tx_frame_count;
	u32 rx_frame_count;
	u32 rx_clear_count;
	u32 cycle_count;
	u32 dwell_time;
	u32 chanswitch_time_htof;
	u32 chanswitch_time_ftoh;
	u32 blanking_count;
	u32 blanking_duration;
};

/**
 * struct ath12k_raw_pkt_ctx - context for raw packet off-channel ops
 * @pkt_list:            Queue of pending raw packets.
 * @chan_lock:            Lock protecting @pkt_list and channel state.
 * @offchan_timer:        Timeout timer for off-channel operations.
 * @chan_stat:            Accumulated off-channel statistics.
 * @vif:                  Associated virtual interface.
 * @transaction_id:       Transaction identifier per request.
 * @num_cmpl_pending:     Number of pending completions.
 * @custom_tx_status:     Tx completion status of queued packets.
 * @home_chan:            True if currently on the home channel.
 */
struct ath12k_raw_pkt_ctx {
	struct sk_buff_head  pkt_list;
	spinlock_t chan_lock;
	struct timer_list offchan_timer;
	struct ath12k_offchan_stat chan_stat;
	struct ieee80211_vif  *vif;
	u32 transaction_id;
	atomic_t num_cmpl_pending;
	u32 custom_tx_status[ATH12K_MAX_CUSTOM_TX_PKT];
	bool home_chan;
};

/**
 * struct ath12k_extn - ath12k driver private extensions
 * @esp:  esp extensions structure
 * @raw_pkt_ctx:   Pointer to raw packet home/off-channel context.
 */
struct ath12k_extn {
	struct esp_extn esp;
	struct ath12k_raw_pkt_ctx  *raw_pkt_ctx;
};

#ifndef CPTCFG_QCN_EXTN

static inline void
ath12k_mac_hw_allocate_extn(struct ieee80211_hw *hw,
			    const struct ieee80211_ops_extn *ops_extn)
{
	return;
}

static inline void ath12k_wifi7_hw_init_extn(struct ath12k_base *ab)
{
	return;
}

static inline void ath12k_mac_init_arvif_extn(struct ath12k_vif *ahvif)
{
	return;
}

static inline void ath12k_wmi_peer_migration_event_extn(struct ath12k_vif *ahvif)
{
	return;
}

static inline
int ath12k_wmi_op_rx_extn(enum wmi_tlv_event_id id, struct ath12k_base *ab,
			  struct sk_buff *skb)
{
	return -1;
}

static inline int
ath12k_add_sta_240mhz_info_extn(struct ath12k_vif *ahvif,
				u8 *mac,
				struct ieee80211_240mhz_vendor_oper_extn
				*params)
{
	return -1;
}

static inline void ath12k_mac_setup_radio_iface_comb_extn(
		struct ieee80211_iface_combination *comb)
{
	return;
}

static inline void ath12k_mac_setup_extn(struct ath12k *ar)
{
}

static inline void ath12k_mac_cleanup_unregister_extn(struct ath12k *ar)
{
}

static inline int ath12k_custom_tx_extn(struct sk_buff *skb)
{
	return -1;
}


static inline void ath12k_custom_tx_free_extn(struct sk_buff *skb, u32 status)
{
}
#else

void ath12k_mac_hw_allocate_extn(struct ieee80211_hw *hw,
			  const struct ieee80211_ops_extn *ops_extn);

void ath12k_wifi7_hw_init_extn(struct ath12k_base *ab);

void ath12k_mac_init_arvif_extn(struct ath12k_vif *ahvif);

const void **ath12k_wmi_tlv_parse_alloc(struct ath12k_base *ab,
					struct sk_buff *skb, gfp_t gfp);

void ath12k_wmi_peer_migration_event_extn(struct ath12k_vif *ahvif);

int ath12k_wmi_op_rx_extn(enum wmi_tlv_event_id id, struct ath12k_base *ab,
			  struct sk_buff *skb);

int ath12k_add_sta_240mhz_info_extn(struct ath12k_vif *ahvif,
				     u8 *mac,
				     struct ieee80211_240mhz_vendor_oper_extn
				     *params);
/**
 * ath12k_custom_tx_extn() - Extended custom transmission handler for
 *                           management frames
 * @skb: skb the frame to transmit
 *
 * This function handles the transmission of custom management frames,
 * including both regular and off-channel transmissions.
 * It performs various validation checks,
 * manages the transmission queue, and directly invokes the workq handler.
 *
 * Return: 0 on success (frame queued for transmission)
 *         -EINVAL if arvif or ar is NULL
 *         -EOPNOTSUPP if called on monitor interface
 *         -EPERM if device is in recovery, crash flush, or bridge mode
 *         -EAGAIN if CAC is running or CSA is active
 *         -EBUSY if queue is full or probe response threshold exceeded
 */
int ath12k_custom_tx_extn(struct sk_buff *skb);

/**
 * ath12k_custom_tx_free_extn() - Free custom management TX skb and
 *                                update status
 * @skb: Socket buffer containing the transmitted frame
 * @status: Transmission status code from firmware
 *
 * This function handles the cleanup of custom management frames (both regular
 * and off-channel) after transmission. It updates the transmission status in
 * the virtual interface structure and frees the socket buffer.
 *
 * Return: Void
 */
void ath12k_custom_tx_free_extn(struct sk_buff *skb, u32 status);

void ath12k_mac_setup_radio_iface_comb_extn(
		struct ieee80211_iface_combination *comb);

void ath12k_mac_setup_extn(struct ath12k *ar);

void ath12k_mac_cleanup_unregister_extn(struct ath12k *ar);

#endif /* CPTCFG_QCN_EXTN */
#endif /* ATH12K_CMN_EXTN_H */
