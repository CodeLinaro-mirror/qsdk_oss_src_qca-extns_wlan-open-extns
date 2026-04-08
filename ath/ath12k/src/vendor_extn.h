/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef VENDOR_EXTN_H
#define VENDOR_EXTN_H

#include "../vendor.h"

extern const struct nla_policy
ath12k_240mhz_sta_info_policy[QCA_WLAN_VENDOR_ATTR_240MHZ_MAX + 1];

extern const struct nla_policy
ath12k_rule_config_policy[QCA_WLAN_VENDOR_ATTR_SCS_RULE_CONFIG_MAX + 1];

extern const struct nla_policy
ath12k_vendor_home_offchan_tx_rx_policy[
	QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_MAX + 1];

extern const struct nla_policy
ath12k_vendor_home_offchan_tx_rx_frame_policy[
	QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_MAX + 1];

extern const struct nla_policy
ath12k_vendor_home_offchan_tx_rx_pkt_status_policy[
	QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_PKT_STATUS_MAX + 1];

extern const struct nla_policy
ath12k_wifi_mac_config_policy[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MAX + 1];

extern const struct nla_policy
ath12k_ctl_table_policy[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_MAX + 1];

extern const struct nla_policy
ath12k_vendor_dcs_config_policy[QCA_WLAN_VENDOR_ATTR_DCS_MAX + 1];

extern const struct nla_policy
ath12k_rrop_info_policy[QCA_WLAN_VENDOR_ATTR_CONFIG_MAX + 1];

extern const struct nla_policy
ath12k_vendor_dcs_sim_policy[QCA_WLAN_VENDOR_ATTR_DCS_SIM_MAX + 1];

extern const struct nla_policy
ath12k_reg_params_policy[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_MAX + 1];

#if LINUX_VERSION_IS_LESS(6,6,116)
#define MAX(x,y) (x > y ? x : y)
#endif

/* CTL table size definitions */
#define QCA_WLAN_CTL_5G_SIZE    1536
#define QCA_WLAN_CTL_2G_SIZE    684
#define QCA_WLAN_MAX_CTL_SIZE   MAX(QCA_WLAN_CTL_5G_SIZE, QCA_WLAN_CTL_2G_SIZE)

#ifndef CPTCFG_QCN_EXTN

static inline int
ath12k_vendor_get_sta_240mhz_info(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  const void *data,
				  int data_len)
{
	return -1;
}

int ath12k_vendor_rule_config_notify(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     const void *data,
				     int data_len)
{
	return -1;
}

int ath12k_vendor_set_wifi_params_extn(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       struct ath12k_wifi_generic_params *params)
{
    return -1;
}

int ath12k_vendor_set_wiphy_params_extn(struct wiphy *wiphy,
					struct ath12k_wifi_generic_params *params)
{
    return -1;
}

int ath12k_vendor_get_wifi_params_extn(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       struct ath12k_wifi_generic_params *params,
				       u64 *value)
{
    return -1;
}

int ath12k_vendor_get_wiphy_params_extn(struct wiphy *wiphy,
					struct ath12k_wifi_generic_params *params,
					u32 *value)
{
    return -1;
}

static inline
int ath12k_vendor_set_wifi_config_extn(struct wiphy *wiphy, struct nlattr **tb,
				       struct wireless_dev *wdev)
{
    return -1;
}

static inline int ath12k_vendor_fw_recovery_event(struct wiphy *wiphy,
						  struct wireless_dev *wdev,
						  u8 hw_link_id,
						  int event_type)
{
    return -1;
}

static inline void ath12k_vendor_send_event(struct ath12k_base *ab,
					    u8 event_flag)
{

}

int ath12k_vendor_set_wiphy_hwaddr_extn(struct wiphy *wiphy,
					struct ath12k_wifi_generic_params *params)
{
	return -1;
}

int ath12k_vendor_derive_link_bss_addr_extn(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    const void *data,
					    int data_len)
{
	return -1;
}

static inline int ath12k_vendor_ctl_table(struct wiphy *wiphy,
                            		  struct wireless_dev *wdev,
                            		  const void *data,
                            		  int data_len)
{
	return -1;
}

static inline
void ath12k_compute_link_vif_mac(u8 *mac_addr,
				 const u8 *base,
				 u8 vif_id,
				 bool use_byte0)
{
}

static inline
void ath12k_compute_6ghz_mbssid_mac(u8 *mac,
				    u8 grp_id,
				    u8 grp_size,
				    u8 mbssid_idx)
{
}

int ath12k_vendor_dcs_config_handler(struct wiphy *wihpy,
				     struct wireless_dev *wdev,
				     const void *data,
				     int data_len)
{
	return -1;
}

static int ath12k_get_dp_rx_scan_radio_stats_len(void)
{
	return 0;
}

static int ath12k_fill_rx_scan_radio_stats(struct sk_buff *vendor_event,
		struct ath12k_dp_rx_scan_radio_stats *rx_scan_radio_stats)
{
	return 0;
}

int ath12k_vendor_dcs_sim_handler(struct wiphy *wihpy,
				  struct wireless_dev *wdev,
				  const void *data,
				  int data_len)
{
	return -1;
}

int ath12k_vendor_reg_params_handler(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     const void *data,
				     int data_len)
{
	return -1;
}

int ath12k_vendor_wifi_config_handler_extn(struct wiphy *wiphy,
					   struct wireless_dev *wdev,
					   struct ath12k_wifi_generic_params *wifi_params)
{
    return -1;
}

/**
 * ath12k_vendor_mesh_params_config_extn - Mesh peer configurations command handler.
 *
 * @wiphy - WIPHY pointer
 * @wdev - Wireless dev pointer
 * @params - Mesh peer configuration parameters
 *
 * Return 0 on success and -ve on failure
 */
static inline
int ath12k_vendor_mesh_params_config_extn(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  struct ath12k_wifi_generic_params *params)
{
	return -1;
}
#else

int ath12k_vendor_get_sta_240mhz_info(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data,
				      int data_len);

int ath12k_vendor_rule_config_notify(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     const void *data,
				     int data_len);

int ath12k_vendor_set_wifi_params_extn(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       struct ath12k_wifi_generic_params *params);

int ath12k_vendor_set_wiphy_params_extn(struct wiphy *wiphy,
					struct ath12k_wifi_generic_params *params);

int ath12k_vendor_get_wifi_params_extn(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       struct ath12k_wifi_generic_params *params,
				       u64 *value);

int ath12k_vendor_get_wiphy_params_extn(struct wiphy *wiphy,
					struct ath12k_wifi_generic_params *params,
					u32 *value);

int ath12k_vendor_set_wifi_config_extn(struct wiphy *wiphy, struct nlattr **tb,
				       struct wireless_dev *wdev);

int ath12k_vendor_fw_recovery_event(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    u8 hw_link_id,
				    int event_type);

void ath12k_vendor_send_event(struct ath12k_base *ab,
			      u8 event_flag);

int ath12k_vendor_set_wiphy_hwaddr_extn(struct wiphy *wiphy,
					struct ath12k_wifi_generic_params *params);

int ath12k_vendor_derive_link_bss_addr_extn(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    const void *data,
					    int data_len);

void ath12k_compute_link_vif_mac(u8 *mac_addr,
				 const u8 *base,
				 u8 vif_id,
				 bool use_byte0);

void ath12k_compute_6ghz_mbssid_mac(u8 mac[ETH_ALEN],
				    u8 grp_id,
				    u8 grp_size,
				    u8 mbssid_idx);

int ath12k_vendor_ctl_table(struct wiphy *wiphy,
			    struct wireless_dev *wdev,
			    const void *data,
			    int data_len);

int ath12k_vendor_dcs_config_handler(struct wiphy *wihpy,
				     struct wireless_dev *wdev,
				     const void *data,
				     int data_len);

int ath12k_vendor_get_rropinfo(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data,
				      int data_len);

int ath12k_get_dp_rx_scan_radio_stats_len(void);

int ath12k_fill_rx_scan_radio_stats(
		struct sk_buff *vendor_event,
		struct ath12k_dp_rx_scan_radio_stats *rx_scan_radio_stats);

int ath12k_vendor_dcs_sim_handler(struct wiphy *wihpy,
				  struct wireless_dev *wdev,
				  const void *data,
				  int data_len);

int ath12k_vendor_reg_params_handler(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     const void *data,
				     int data_len);

int ath12k_vendor_wifi_config_handler_extn(struct wiphy *wiphy, struct wireless_dev *wdev,
					   struct ath12k_wifi_generic_params *wifi_params);

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
/**
 * ath12k_vendor_mesh_params_config_extn - Mesh peer configurations command handler.
 *
 * @wiphy - WIPHY pointer
 * @wdev - Wireless dev pointer
 * @params - Mesh peer configuration parameters
 *
 * Return 0 on success and -ve on failure
 */
int ath12k_vendor_mesh_params_config_extn(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     struct ath12k_wifi_generic_params *params);
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */

/**
 * ath12k_vendor_get_wiphy_config_handler_extn - Wiphy configuration command handler
 *
 * @wiphy - WIPHY pointer
 * @tb - netlink attribute table
 * @skb - Pointer to Socket Buffer
 *
 * Return void
 */
void ath12k_vendor_get_wiphy_config_handler_extn(struct wiphy *wiphy,
						 struct nlattr **tb,
						 struct sk_buff *skb);
#endif /* CPTCFG_QCN_EXTN */

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
#define QCA_NL80211_VENDOR_WIFI_GENERIC_SUBCMD_MESH_PARAMS 436
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */

typedef enum _CLI_DPD_STATUS {
    CLI_DPD_STATUS_DISABLED   =  0x0,  /* DPD disabled via CLI dpd_enable */
    CLI_DPD_STATUS_PASS       =  0x1,  /* DPD triggered via CLI dpd_enable and calibration passed */
    CLI_DPD_CMD_INPROGRES     =  0x2,  /* DPD triggered via CLI dpd_enable and no response received yet from target */
    /* Add any new status if any here */
    CLI_DPD_NA_STATE          =  0xFE, /* DPD not triggered via CLI command hence invalid/NA state */
    CLI_DPD_STATUS_FAIL       =  0xFF, /* DPD triggered via CLI dpd_enable & calibration failed OR DPD disabled BDF is loaded*/
} CLI_DPD_STATUS;
#endif /* VENDOR_EXTN_H */
