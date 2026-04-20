/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.*/

#ifndef ATH12K_SA_CORE_H
#define ATH12K_SA_CORE_H

/* Smart antenna configuration command and event interface structures
 * for pdev and peer.
 */
#define WMI_SMART_ANT_MAX_RATE_SERIES	2
#define WMI_CCK_OFDM_RATES_MAX		12
#define WMI_MCS_RATES_MAX		96
#define WMI_RATE_COUNT_MAX		4
#define WMI_SMART_ANT_MODE_SERIAL	0
#define WMI_SMART_ANT_MODE_PARALLEL	1
#define WMI_SMART_ANT_HAL_MAX		4
#define WMI_SMART_ANT_MAX_SERIAL_ANT	2
/* Smart Antenna Enable Mask */
#define SA_ENABLE_MASK			0x01
/* Indicates vap re-init */
#define SA_NEW_RECONFIG			0x00
/* Indicates vap init */
#define SA_NEW_CONFIG			0x01
/* Mask to validate PER threashold */
#define SA_MASK_PER_TH			0xFF
/* Mask to validate minimum training packets */
#define SA_MASK_MIN_PKTS		0xFFFF
/* Mask to check invalid parameter */
#define SA_INVALID_PARAM_VALUE		0xFFFF
/* 11-bit Rate Code Mask */
#define SA_MASK_RCODE			0x7FF
/* Number of bytes in Dword */
#define SA_BYTES_IN_DWORD		4
/* Mask for byte */
#define SA_MASK_BYTE			0xFF
/* Mask for Set command type detection */
#define SA_PARM_TYPE_MASK		0xFFFF0000
/* 24th bit represent command type.
 * 1 for Peer and 0 for Radio
 */
#define SA_PARM_TYPE_SHIFT		24

/* Forward declaration */
struct ath12k_link_vif;
struct ath12k_link_sta;

enum sa_state {
	SA_DEFAULT_STATE,
	SA_INIT_DONE_STATE,
	SA_DEINIT_DONE_STATE,
};

enum sa_sta_opmode {
	SA_OPMODE_CCK_OFDM,
	SA_OPMODE_HT,
	SA_OPMODE_VHT,
	SA_OPMODE_HE,
	SA_OPMODE_EHT,
	SA_OPMODE_UHR,
};

struct sa_sta_extn {
	void *ctx;
};

struct sa_ar_extn {
	u8 enable_mask;
	u8 sa_mode;
	u8 radio_id;
	enum sa_state state;
};

struct sa_base_extn {
	bool sa_enable;
	u8 sa_sw_test;
	u8 sa_support;
	s8 sa_mode_overwrite;
};

/* Smart antenna training info */
struct smart_ant_train_info {
	u8 vdev_id;
	u16 minpkts;
	u16 per_th;
	/* rate codes for training: two per series (lo/hi) */
	u32 rate_array[SA_MAX_RATE_SERIES];
	/* antenna bitmask per series */
	u32 antenna_array[SA_MAX_SA_CHAINS];
	u32 numpkts;
};

struct sa_sta_ratecode_list {
	u8 mac_addr[ETH_ALEN];
	u8 legacy_index;
	u8 mcs_index;
	u32 pdev_id;
	struct ath12k_sa_rate_cap rate_cap;
};

/**
 * sa_get_cfg_flags - Read Smart Antenna configuration flags
 * @ab: ath12k base pointer
 * @sa_en: output flag indicating if Smart Antenna is enabled by config
 * @sa_sw_test: output flag indicating Smart Antenna software test mode
 *
 * Fetches Smart Antenna related configuration from the driver config store
 * and updates the provided output flags. Safe to pass NULL for unused outputs.
 */
void sa_get_cfg_flags(struct ath12k_base *ab, u8 *sa_en, u8 *sa_sw_test);

/**
 * sa_core_init - Initialize Smart Antenna core for a vdev
 * @ar: ath12k device context
 * @arvif: link vif associated with the vdev
 * @init_flags: initialization flags (e.g. SA_NEW_CONFIG)
 *
 * Initializes Smart Antenna state for the given vdev, evaluates operating
 * mode, programs initial Tx/Rx antenna configuration and subscribes to PPDU
 * notifications when needed.
 *
 * Return: 0 on success, negative errno on failure.
 */
int sa_core_init(struct ath12k *ar,
		 struct ath12k_link_vif *arvif, u8 init_flags);

/**
 * sa_core_deinit - Deinitialize Smart Antenna core for a vdev
 * @ar: ath12k device context
 * @init_flags: deinitialization flags
 *
 * Tears down Smart Antenna state for the given vdev, optionally notifies the
 * algorithm implementation, disables firmware configuration and unregisters
 * PPDU notifications as required.
 *
 * Return: 0 on success, negative errno on failure.
 */
int sa_core_deinit(struct ath12k *ar, u8 init_flags);

/**
 * sa_core_wmi_peer_rc_list_ev_handler - Handle peer ratecode list WMI event
 * @ab: ath12k base pointer
 * @skb: event buffer containing rate code list payload
 *
 * Parses the peer ratecode list event and updates Smart Antenna rate
 * capabilities for the corresponding peer in the algorithm context.
 */
void sa_core_wmi_peer_rc_list_ev_handler(struct ath12k_base *ab,
					 struct sk_buff *skb);

/**
 * sa_core_peer_disconnect_notify - Notify peer disconnection to SA core
 * @arsta: link sta representing the disconnected peer
 *
 * Notifies the Smart Antenna core and algorithm that a peer has disconnected
 * so that per-peer context can be released.
 */
void sa_core_peer_disconnect_notify(struct ath12k_link_sta *arsta);

#endif /* ATH12K_SA_CORE_H */
