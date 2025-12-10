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

struct ath12k_extn {
	/* Add ath12k_extn structures here */
	struct esp_extn esp;
};

struct sta_240mhz_info {
	struct ieee80211_240mhz_vendor_oper_extn params_240MHz_extn;
	u8 mac_addr[ETH_ALEN];
	struct list_head list;
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

void ath12k_mac_setup_radio_iface_comb_extn(
		struct ieee80211_iface_combination *comb);

void ath12k_mac_setup_extn(struct ath12k *ar);

void ath12k_mac_cleanup_unregister_extn(struct ath12k *ar);

#endif /* CPTCFG_QCN_EXTN */
#endif /* ATH12K_CMN_EXTN_H */
