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
struct ath12k_vif_sysfs_entry;

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

/* Band types used in offchan tx */
enum band_type {
	BAND_DEFAULT,
	BAND_2GHZ,
	BAND_5GHZ,
	BAND_6GHZ,
	BAND_INVALID
};

/* Preamble types used in offchan tx */
enum preamble_type {
	PREAMBLE_OFDM,
	PREAMBLE_CCK,
	PREAMBLE_INVALID
};

/**
 * struct ath12k_blanking_params - Scan blanking parameters
 * @valid: Indicates if blanking parameters are valid
 * @blanking_count: Number of blanking events during scan
 * @blanking_duration: Total blanking duration in msec
 */
struct ath12k_blanking_params {
	__le32 valid;
	__le32 blanking_count;
	__le32 blanking_duration;
} __packed;

/**
 * struct ath12k_offchan_stat - statistics for off-channel activity
 * @noise_floor:        Noise floor in dBm (signed 16-bit).
 * @tx_frame_count:        Number of frames transmitted.
 * @rx_frame_count:        Number of frames received.
 * @rx_clear_count:        RX clear time (units: driver-specific ticks).
 * @cycle_count:           Total channel cycle time (units: ticks).
 * @dwell_time:            Dwell time on the off-channel (ms).
 * @chanswitch_time_htof:  Time to switch from home to off-channel (us).
 * @chanswitch_time_ftoh:  Time to switch from off-channel to home (us).
 * @blank_params:          Blanking Param stats
 */
struct ath12k_offchan_stat {
	s16 noise_floor;
	u32 tx_frame_count;
	u32 rx_frame_count;
	u32 rx_clear_count;
	u32 cycle_count;
	u32 dwell_time;
	u32 chanswitch_time_htof;
	u32 chanswitch_time_ftoh;
	struct ath12k_blanking_params blank_params;
};

/**
 * struct ath12k_skb_tx_param - TX parameters for offchannel tx
 * @nss: Number of spatial streams
 * @preamble: Preamble type
 * @mcs: MCS value
 * @retry: Retry count
 * @power: Power level
 * @tx_beamforming: Beamforming enable flag
 * @is_data: Is skb has data packet
 * @tx_param_configured: Is tx params configured
 */
struct ath12k_skb_tx_param {
	u32 nss;
	enum preamble_type preamble;
	u32 mcs;
	u8 retry;
	u8 power;
	u8 tx_beamforming;
	bool is_data;
	bool tx_param_configured;
};

/**
 * struct ath12k_raw_pkt_ctx - context for raw packet off-channel ops
 * @pkt_list:            Queue of pending raw packets.
 * @chan_lock:            Lock protecting @pkt_list and channel state.
 * @offchan_timer:        Timeout timer for off-channel operations.
 * @chan_stat:            Accumulated off-channel statistics.
 * @vif:                  Associated virtual interface.
 * @tx_params:            Tx params set by user.
 * @transaction_id:       Transaction identifier per request.
 * @num_cmpl_pending:     Number of pending completions.
 * @req_freq:             Requested freq(if set) for offchan operation.
 * @custom_tx_status:     Tx completion status of queued packets.
 * @home_chan:            True if currently on the home channel.
 * @operation_status:     Overall operation status (0=success, 1=failure).
 * @func_type:            Function type for the operation.
 * @num_frames:           Number of frames sent from userspace.
 * @ts_scan_start:        Scan start timestamp
 * @ts_foreign_entry:     Foreign chan entry timestamp
 * @ts_foreign_exit:      Foreign chan exit timestamp
 * @ts_scan_done:         Scan complete timestamp
 */
struct ath12k_raw_pkt_ctx {
	struct sk_buff_head  pkt_list;
	spinlock_t chan_lock;
	struct timer_list offchan_timer;
	struct ath12k_offchan_stat chan_stat;
	struct ieee80211_vif  *vif;
	struct ath12k_skb_tx_param tx_param;
	u32 transaction_id;
	atomic_t num_cmpl_pending;
	u32 req_freq;
	u32 custom_tx_status[ATH12K_MAX_CUSTOM_TX_PKT];
	bool home_chan;
	u8 operation_status;
	u8 func_type;
	u8 num_frames;
	ktime_t ts_scan_start;
	ktime_t ts_foreign_entry;
	ktime_t ts_foreign_exit;
	ktime_t ts_scan_done;

};

/**
 * struct ath12k_extn - ath12k driver private extensions
 * @esp:  esp extensions structure
 * @raw_pkt_ctx:   raw packet home/off-channel context.
 * @radio_kobj:    pointer to radio kernel object.
 * @vifs_kobj:     Pointer to vifs kernel object.
 * @vif_entries:   Pointer to 17 sysfs vif entries.
 */
struct ath12k_extn {
	struct esp_extn esp;
	struct ath12k_raw_pkt_ctx raw_pkt_ctx;

	/* sysfs entries under ieee80211: radioX */
	struct kobject *radio_kobj;
	struct kobject *vifs_kobj;
	struct ath12k_vif_sysfs_entry *vif_entries[17];
};

/**
 * struct offchan_event_work_with_txn - Work structure for off-channel event
 * @work: Work structure for scheduling the event
 * @wdev: Pointer to the wireless device
 * @transaction_id: Transaction ID for correlation with user-space request
 * @raw_pkt_ctx: Pointer to raw packet context for per-packet status
 *
 * This structure is used to schedule off-channel statistics events to user space
 * with transaction ID support for request-response correlation.
 */
struct offchan_event_work_with_txn {
	struct work_struct work;
	struct wireless_dev *wdev;
	u32 transaction_id;
	struct ath12k_raw_pkt_ctx *raw_pkt_ctx;
};

/**
 * struct ath12k_home_offchan_params - Parameters for home/off-channel operation
 * @wdev: Pointer to the wireless device
 * @offchan_frame: Netlink attribute containing the frame data and parameters
 * @band: Operating band (e.g., 2.4GHz, 5GHz, 6GHz)
 * @freq: Channel frequency in MHz
 * @transaction_id: Transaction ID for correlation with user-space request
 * @scan_dur: Scan duration in milliseconds for off-channel operation
 * @num_frames: Number of frames to transmit
 * @func: Function type (TX_MGMT, TX_DATA, or RX)
 * @link_id: MLO link ID (0 for non-MLO or default link)
 * @is_mld: Whether this is an MLO device
 */
struct ath12k_home_offchan_params {
	struct wireless_dev *wdev;
	struct nlattr *offchan_frame;
	int band;
	int freq;
	u32 transaction_id;
	u16 scan_dur;
	u8 num_frames;
	u8 func;
	u8 link_id;
	bool is_mld;
};

/**
 * struct hal_rx_frm_type_info - Per-PPDU 802.11 frame type counters
 * @rx_mgmt_cnt: Number of 802.11 management frames decoded for this PPDU
 * @rx_ctrl_cnt: Number of 802.11 control frames decoded for this PPDU
 * @rx_data_cnt: Number of 802.11 data frames decoded for this PPDU
 */
struct hal_rx_frm_type_info {
	u8 rx_mgmt_cnt;
	u8 rx_ctrl_cnt;
	u8 rx_data_cnt;
};

/**
 * struct hal_mon_ppdu_info_extn - Monitor-mode PPDU extension metadata
 * @rx_state: RX pipeline state at the time this PPDU was reported
 * @rx_antenna: RX antenna bitmap (one bit per RF chain) indicating chains that contributed
 * @rssi: Per-chain RSSI(signed), indexed by RF chain (up to 8 entries)
 * @sw_frame_group_id: Software-defined frame group identifier for this PPDU
 * @frm_type_info: Aggregated 802.11 frame-type counters for the PPDU
 */

struct hal_mon_ppdu_info_extn {
	u8 rx_state;
	u32 rx_antenna;
	s8 rssi[8];
	u8 sw_frame_group_id;
	struct hal_rx_frm_type_info frm_type_info;
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

static inline void ath12k_sysfs_init_extn(struct ath12k *ar)
{
}

static inline void ath12k_sysfs_cleanup_extn(struct ath12k *ar)
{
}

static inline void ath12k_wmi_prepare_tx_params_extn(struct ath12k *ar,
						     void *ptr)
{
}

static inline void ath12k_update_offchan_stats_extn(struct ath12k *ar,
						    struct sk_buff *skb,
						    u32 freq,
						    struct wmi_chan_info_event *ch_info)
{
}

static inline void ath12k_update_offchan_ts_extn(struct ath12k *ar,
						 u32 event_type)
{
}

static inline int schedule_home_offchan_stats_event_ext(struct wireless_dev *wdev,
                                                         u32 transaction_id,
							 struct ath12k_raw_pkt_ctx *raw_pkt_ctx)
{
	return -1;
}

static int ath12k_vendor_home_offchan_tx_rx_handler(struct wiphy *wiphy,
						    struct wireless_dev *wdev,
						    const void *data,
						    int data_len)
{
	return -1;
}

static void ath12k_home_offchan_tx_drain(struct ath12k_raw_pkt_ctx *raw_pkt_ctx) {
	return;
}

static void ath12k_home_offchan_cleanup(struct ath12k *ar) {
	return;
}

static void ath12k_custom_tx_send(struct ieee80211_vif *vif,
				  struct ath12k_raw_pkt_ctx *raw_pkt_ctx) {
	return;
}

static inline int
ath12k_mac_setup_vdev_create_arg_scan_radio_extn(struct ath12k_link_vif *arvif,
						 struct ath12k_wmi_vdev_create_arg *arg)
{
	return 0;
}

static inline int
ath12k_wmi_tlv_scan_radio_caps_ext2(struct ath12k_base *ab, u16 tag,
				    u16 len, const void *ptr,
				    void *data)
{
	return 0;
}

static inline void
ath12k_wmi_offchan_txrx_update_scan_params_extn(struct ath12k *ar,
						struct ath12k_wmi_scan_req_arg *arg)
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

int ath12k_wmi_tlv_scan_radio_caps_ext2(struct ath12k_base *ab, u16 tag,
					u16 len, const void *ptr,
					void *data);

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

/*
 * ath12k_wmi_prepare_tx_params_extn() - Prepare tx params for WMI command
 * @ar: Pointer to the ath12k structure
 * @ptr: Pointer to the WMI buffer
 *
 * This function prepares the tx params for the WMI command by:
 * Setting the appropriate fields in the tx_param_dword0 and tx_param_dword1 values
 */
void ath12k_wmi_prepare_tx_params_extn(struct ath12k *ar, void *ptr);

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

/**
 * ath12k_update_offchan_stats_extn - Update off-channel statistics
 * @ar: ath12k radio instance
 * @skb: Point to skb containing chaninfo event buffer
 * @freq: Frequency in MHz from chan_info event
 * @ch_info: Channel info event data
 *
 * Updates the off-channel statistics if an off-channel operation is in progress
 * and the frequency matches the current off-channel request.
 *
 * Return: Void
 */
void ath12k_update_offchan_stats_extn(struct ath12k *ar,
				      struct sk_buff *skb,
				      u32 freq,
				      struct wmi_chan_info_event *ch_info);

/**
 * ath12k_update_offchan_ts_extn - Update offchan timestamps based on scan events
 * @ar: ath12k radio instance
 * @event_type: Scan event type from WMI
 *
 * Captures timestamps at key scan event transitions
 * - WMI_SCAN_EVENT_STARTED: Start of offchan operation
 * - WMI_SCAN_EVENT_FOREIGN_CHAN: Entry to foreign channel
 * - WMI_SCAN_EVENT_FOREIGN_CHAN_EXIT: Exit from foreign channel
 * - WMI_SCAN_EVENT_COMPLETED: Offchan operation finished
 *
 * Return: Void
 */
void ath12k_update_offchan_ts_extn(struct ath12k *ar, u32 event_type);

/**
 * ath12k_custom_txrx_init - Initialize custom txrx context
 * @ar: ath12k radio instance
 *
 * This function initializes raw pkt context, lock, list and timer
 *
 * Return: Void
 */
void ath12k_custom_txrx_init(struct ath12k *ar);

/**
 * ath12k_custom_txrx_deinit - Deinitialize custom txrx context
 * @ar: ath12k radio instance
 *
 * This function destroys the timer and raw pkt list.
 *
 * Return: Void
 */
void ath12k_custom_txrx_deinit(struct ath12k *ar);

void ath12k_mac_setup_radio_iface_comb_extn(
		struct ieee80211_iface_combination *comb);

void ath12k_mac_setup_extn(struct ath12k *ar);

void ath12k_mac_cleanup_unregister_extn(struct ath12k *ar);


void ath12k_sysfs_init_extn(struct ath12k *ar);
void ath12k_sysfs_cleanup_extn(struct ath12k *ar);

/**
 * schedule_home_offchan_stats_event_ext - Schedule home/off-channel statistics event
 * @wdev: Pointer to the wireless device
 * @transaction_id: Transaction ID for correlation with user-space request
 * @raw_pkt_ctx: Pointer to raw packet context for per-packet TX status
 *
 * This function schedules a work item to send home/off-channel statistics as a vendor
 * event to user space. The transaction ID is used to correlate the event with the
 * original request from user space. The raw_pkt_ctx provides per-packet TX status.
 *
 * Return: 0 on success
 */
int schedule_home_offchan_stats_event_ext(struct wireless_dev *wdev,
					   u32 transaction_id,
					   struct ath12k_raw_pkt_ctx *raw_pkt_ctx);

/**
 * ath12k_vendor_home_offchan_tx_rx_handler - Vendor command handler for home/off-channel TX/RX
 * @wiphy: Pointer to the wireless hardware
 * @wdev: Pointer to the wireless device
 * @data: Pointer to the vendor command data
 * @data_len: Length of the vendor command data
 *
 * This function handles the vendor command for transmitting and receiving frames
 * on home channel or off-channel. It parses the netlink attributes, prepares the
 * frame for transmission, and schedules the transmission based on whether it's
 * home channel or off-channel operation.
 *
 * Return: 0 on success, negative error code on failure
 */
int ath12k_vendor_home_offchan_tx_rx_handler(struct wiphy *wiphy,
					     struct wireless_dev *wdev,
					     const void *data,
					     int data_len);

/**
 * ath12k_home_offchan_tx_drain - Drain all packets from home/off-channel transmission queue
 * @raw_pkt_ctx: Pointer to the raw packet context structure
 *
 * This function removes and frees all packets from the home/off-channel transmission
 * queue. It should be called during cleanup or when a home/off-channel operation
 * times out or is cancelled.
 */
void ath12k_home_offchan_tx_drain(struct ath12k_raw_pkt_ctx *raw_pkt_ctx);

/**
 * ath12k_home_offchan_cleanup - Cleanup home/off-channel resources
 * @ar: Pointer to the ath12k radio structure
 *
 * This function cleans up all home/off-channel related resources including stopping
 * the timer, draining the packet queue, and clearing the vif pointer. Should be
 * called during driver cleanup or detach.
 */
void ath12k_home_offchan_cleanup(struct ath12k *ar);

/**
 * ath12k_custom_tx_send - Send packet from queue on home offchan
 * @vif: Pointer to the virtual interface
 * @raw_pkt_ctx: Pointer to the raw packet context structure
 *
 * This function dequeues and transmits a packet from the home offchan queue.
 * It handles the transmission of custom management frames on the home offchan.
 */
void ath12k_custom_tx_send(struct ieee80211_vif *vif,
			   struct ath12k_raw_pkt_ctx *raw_pkt_ctx);

/**
 * ath12k_wmi_offchan_txrx_update_scan_params_extn - Update scan params required
 *                                                    for custom txrx
 * @ar: Pointer to the ath12k radio structure
 * @arg: Pointer to the scan parameter
 *
 * This function sets priority and chan stats flags in scan parameter for
 * custom txrx.
 */
void ath12k_wmi_offchan_txrx_update_scan_params_extn(struct ath12k *ar,
						     struct ath12k_wmi_scan_req_arg *arg);


                   int ath12k_mac_setup_vdev_create_arg_scan_radio_extn(struct ath12k_link_vif *arvif,
						     struct ath12k_wmi_vdev_create_arg *arg);

#endif /* CPTCFG_QCN_EXTN */
#endif /* ATH12K_CMN_EXTN_H */
