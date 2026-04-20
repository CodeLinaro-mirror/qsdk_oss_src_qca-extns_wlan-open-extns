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

#define WMI_HE_EXTRA_MCS_L80_SHIFT	16
#define WMI_HE_EXTRA_MCS_G80_SHIFT	24
#define WMI_HE_EXTRA_MCS_L80_MASK	GENMASK(23, 16)
#define WMI_HE_EXTRA_MCS_G80_MASK	GENMASK(31, 24)

/* Pack a 16-bit HE MCS 12/13 capability value into the upper 16 bits of a
 * u32 mcs_set word.  bits[7:0] of cap → bits[23:16], bits[15:8] → bits[31:24].
 */
#define WMI_HE_EXTRA_MCS_PACK(cap)	\
	((((u32)((cap) & 0xff)) << WMI_HE_EXTRA_MCS_L80_SHIFT) |	\
	 (((u32)((cap) >> 8))   << WMI_HE_EXTRA_MCS_G80_SHIFT))

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

static inline int
ath12k_vendor_get_wifi_config_handler_extn(struct wiphy *wiphy, struct nlattr **tb,
					   u64 *value)
{
	return -EOPNOTSUPP;
}

static inline void
ath12k_peer_assoc_h_he_mcs_12_13_extn(struct ath12k *ar,
				      struct ath12k_wmi_peer_assoc_arg *arg)
{
	return;
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

int ath12k_vendor_set_wiphy_sr_params_extn(struct wiphy *wiphy,
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

int ath12k_vendor_get_wiphy_sr_params_extn(struct wiphy *wiphy,
					   struct ath12k_wifi_generic_params *params,
					   char value[], size_t length)
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

static const struct nla_policy
ath12k_vendor_tdma_schedule_policy[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_MAX + 1] = {};

static inline int ath12k_vendor_tdma_schedule_config(struct wiphy *wiphy,
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

int ath12k_vendor_enable_sounding_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params)
{
    return -1;
}

int ath12k_vendor_set_sounding_params_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params)
{
    return -1;
}

int ath12k_vendor_set_mu_sounding_params_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params)
{
    return -1;
{

int ath12k_vendor_sched_mu_enable(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params)
{
    return -1;
}

int ath12k_vendor_sched_ofdma_enable(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params)
{
    return -1;
}

int ath12k_vendor_set_nav_override_config_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params)
{
    return -1;
}

int ath12k_vendor_get_nav_override_config_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params)
{
    return -1;
}

int ath12k_vendor_peer_ulofdma_rtd_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params)
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

int ath12k_vendor_set_wiphy_sr_params_extn(struct wiphy *wiphy,
					   struct ath12k_wifi_generic_params *params);

int ath12k_vendor_get_wifi_params_extn(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       struct ath12k_wifi_generic_params *params,
				       u64 *value);

int ath12k_vendor_get_wiphy_params_extn(struct wiphy *wiphy,
					struct ath12k_wifi_generic_params *params,
					u32 *value);

int ath12k_vendor_get_wiphy_sr_params_extn(struct wiphy *wiphy,
					   struct ath12k_wifi_generic_params *params,
					   char value[], size_t length);

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

int ath12k_vendor_enable_sounding_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params);

int ath12k_vendor_set_sounding_params_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params);

int ath12k_vendor_set_mu_sounding_params_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params);

int ath12k_vendor_sched_mu_enable(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params);

int ath12k_vendor_sched_ofdma_enable(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params);

int ath12k_vendor_set_nav_override_config_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params);

int ath12k_vendor_get_nav_override_config_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params);

int ath12k_vendor_peer_ulofdma_rtd_extn(struct wiphy *wiphy,
                  struct wireless_dev *wdev,
                  struct ath12k_wifi_generic_params *params);

extern const struct nla_policy
ath12k_vendor_tdma_schedule_policy[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_MAX + 1];

int ath12k_vendor_tdma_schedule_config(struct wiphy *wiphy,
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
 * ath12k_vendor_get_wifi_config_handler_extn - Handle GET_WIFI_CONFIGURATION
 * for extension subcmds.
 *
 * @wiphy:    wiphy pointer
 * @tb:       parsed netlink attribute table
 * @value:    output — fetched capability value (u64, caller casts as needed)
 *
 * Returns 0 on success, -EOPNOTSUPP for unknown subcmds,
 * negative error code on failure.
 */
int ath12k_vendor_get_wifi_config_handler_extn(struct wiphy *wiphy, struct nlattr **tb,
					       u64 *value);

/**
 * ath12k_peer_assoc_h_he_mcs_12_13_extn - Apply intersected HE MCS 12/13
 * peer capability to the WMI peer assoc argument.
 * @ar:  ath12k radio instance
 * @arg: WMI peer assoc argument being prepared
 *
 * Populates the peer capability for HE MCS 12/13, which is used during the
 * WMI PEER ASSOC command sent to the firmware.
 */
void ath12k_peer_assoc_h_he_mcs_12_13_extn(struct ath12k *ar,
					   struct ath12k_wmi_peer_assoc_arg *arg);

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

/*
 * overlapping spatial reuse
 */
#define IEEE80211_SRP_SR_CONTROL                            0x3

/* Self SR Defaults */
#define IEEE80211_SELF_NON_SRG_OBSS_PD_THRESHOLD_DB         -128
#define IEEE80211_SELF_NON_SRG_OBSS_PD_THRESHOLD_DBM        -128
#define IEEE80211_SELF_NON_SRG_OBSS_PD_ENABLE               0
#define IEEE80211_SELF_SRG_OBSS_PD_THRESHOLD_DB             -128
#define IEEE80211_SELF_SRG_OBSS_PD_THRESHOLD_DBM            -128
#define IEEE80211_SELF_SRG_OBSS_PD_ENABLE                   0
#define IEEE80211_SELF_SRG_OBSS_COLOR_ENABLE_BITMAP         0xFFFFFFFF
#define IEEE80211_SELF_SRG_OBSS_BSSID_ENABLE_BITMAP         0xFFFFFFFF
#define IEEE80211_SELF_NON_SRG_OBSS_COLOR_ENABLE_BITMAP     0xFFFFFFFF
#define IEEE80211_SELF_NON_SRG_OBSS_BSSID_ENABLE_BITMAP     0xFFFFFFFF
#define IEEE80211_SELF_PSR_TX_ENABLE                        0
#define IEEE80211_SELF_SRG_BSS_COLOR_BITMAP                 0x0
#define IEEE80211_SELF_SRG_PARTIAL_BSSID_BITMAP             0x0
#define IEEE80211_SELF_HESIGA_SR15_ENABLE                   0
#define IEEE80211_SELF_OBSS_PD_THRESHOLD_IN_DBM             1
#define IEEE80211_SELF_OBSS_PD_THRESHOLD_IN_DB              0
#define IEEE80211_SELF_PSR_SAFETY_MARGIN                    0

/* Self SR Defaults at vdev level */
#define IEEE80211_VDEV_SELF_PSR_TX_ENABLE                   1
#define IEEE80211_VDEV_SELF_SRG_OBSS_PD_ENABLE              1
#define IEEE80211_VDEV_SELF_NON_SRG_OBSS_PD_ENABLE          1
#define IEEE80211_VDEV_SELF_PSR_PER_AC                      0xF
#define IEEE80211_VDEV_SELF_OBSS_PD_PER_AC                  0xF

/* BSS Non-SRG OBSS SR Deaults */
#define IEEE80211_SRP_NON_SRG_OBSS_PD_MAX_OFFSET        0

/* BSS SRG OBSS SR Defaults */
#define IEEE80211_SRP_SRG_OBSS_PD_MIN_OFFSET            0
#define IEEE80211_SRP_SRG_OBSS_PD_MAX_OFFSET            0
#define IEEE80211_SRP_SRG_BSS_COLOR_BITMAP              0
#define IEEE80211_SRP_SRG_PARTIAL_BSSID_BITMAP          0

/* Spatial Reuse Control Field bitmasks */
#define IEEE80211_PSR_DISALLOWED_MASK                    0x01
#define IEEE80211_SRP_NON_SRG_OBSS_PD_SR_DISALLOWED_MASK 0x02
#define IEEE80211_SRP_SRG_INFO_PRESENT_MASK              0x08
#define IEEE80211_SRP_HESIGA_SR_VALUE15_ALLOWED_MASK     0x10

/* Misc SR Defaults */
#define IEEE80211_ALLOW_MON_VAPS_IN_SR                  0
#define IEEE80211_SR_ENABLE_PER_AC                      0x0

/* Spatial Reuse */
#define IEEE80211_HE_SR_VALUE15_ENABLE        1

#define MAX_SELF_SR_MIN_TXOP 65535

#define SR_GET_FIELD(_v, _f)   \
       (((_v) & (_f##_MASK)) >> (_f##_LSB))

#define SR_SET_FIELD(_var, _v, _f)     \
       do {    \
               (_var) &= ~(_f##_MASK); \
               (_var) |= (((_v) << (_f##_LSB)) & (_f##_MASK)); \
       } while (0)

/* SRG and non-SRG based OBSS PD */
#define SELF_OBSS_PD_ENABLE      1
#define SELF_OBSS_PD_LOWER_THRESH -128
#define SELF_OBSS_PD_UPPER_THRESH 127

/* Units of OBSS PD Threshold */
#define SELF_OBSS_PD_THRESH_UNITS_LSB 29
#define SELF_OBSS_PD_THRESH_UNITS_MASK (1 << SELF_OBSS_PD_THRESH_UNITS_LSB)

/* Non-SRG based OBSS PD Spatial Reuse */
#define SELF_NON_SRG_OBSS_PD_ENABLE_LSB  31
#define SELF_NON_SRG_OBSS_PD_ENABLE_MASK (1 << SELF_NON_SRG_OBSS_PD_ENABLE_LSB)

#define SELF_NON_SRG_OBSS_PD_THRESH_LSB  0
#define SELF_NON_SRG_OBSS_PD_THRESH_MASK 0x000000FF

/* SRG based OBSS PD Spatial Reuse */
#define SELF_SRG_OBSS_PD_ENABLE_LSB  30
#define SELF_SRG_OBSS_PD_ENABLE_MASK (1 << SELF_SRG_OBSS_PD_ENABLE_LSB)

#define SELF_SRG_OBSS_PD_THRESH_LSB  8
#define SELF_SRG_OBSS_PD_THRESH_MASK 0x0000FF00

#define GET_SELF_OBSS_PD_THRESH(type, _var) \
        SR_GET_FIELD(_var, SELF_##type##_OBSS_PD_THRESH)

#define SET_SELF_OBSS_PD_THRESH(type, _var, _v) \
        SR_SET_FIELD(_var, _v, SELF_##type##_OBSS_PD_THRESH)

#define GET_SELF_OBSS_PD_ENABLE(type, _var) \
        SR_GET_FIELD(_var, SELF_##type##_OBSS_PD_ENABLE)

#define SET_SELF_OBSS_PD_ENABLE(type, _var, _v) \
        SR_SET_FIELD(_var, _v, SELF_##type##_OBSS_PD_ENABLE)

#define SELF_OBSS_PD_ENABLE_PER_AC_LSB  0
#define SELF_OBSS_PD_ENABLE_PER_AC_MASK 0x0000000F

#define SELF_PSR_ENABLE_PER_AC_LSB  16
#define SELF_PSR_ENABLE_PER_AC_MASK 0x000F0000

#define GET_SELF_SR_PER_AC(type, _var) \
        SR_GET_FIELD(_var, SELF_##type##_ENABLE_PER_AC)

#define SET_SELF_SR_PER_AC(type, _var, _v) \
        SR_SET_FIELD(_var, _v, SELF_##type##_ENABLE_PER_AC)

enum {
    SR_SELF_OBSS_PD_TX_ENABLE                  = 0,
    SR_SELF_OBSS_PD_THRESHOLD_DB               = 1,
    SR_SELF_SRG_BSS_COLOR_BITMAP               = 2,
    SR_SELF_SRG_PARTIAL_BSSID_BITMAP           = 3,
    SR_SELF_ENABLE_PER_AC                      = 5,
    SR_SELF_HESIGA_SR15_ENABLE                 = 6,
    SR_SELF_SRG_OBSS_COLOR_ENABLE_BITMAP       = 7,
    SR_SELF_SRG_OBSS_BSSID_ENABLE_BITMAP       = 8,
    SR_SELF_NON_SRG_OBSS_COLOR_ENABLE_BITMAP   = 9,
    SR_SELF_NON_SRG_OBSS_BSSID_ENABLE_BITMAP   = 10,
    SR_SELF_PSR_TX_ENABLE                      = 11,
    SR_SELF_SAFETY_MARGIN_PSR                  = 12,
    SR_SELF_OBSS_PD_THRESHOLD_DBM              = 13,
    SR_SELF_ENABLE_TRUNCATION                  = 14,
    SR_SELF_OBSS_PPDU_MIN_DUR                  = 15,
    SR_SELF_OBSS_PD_CTRL_FRAME                 = 16,
};

/**
 * enum srtype - Spatial reuse type
 * @SR_TYPE_NON_SRG_OBSS_PD : Non-SRG based OBDD PD Spatial reuse
 * @SR_TYPE_SRG_OBSS_PD : SRG based OBDD PD Spatial reuse
 * @SR_TYPE_PSR: PSR based Spatial Reuse
 * @SR_TYPE_OBSS_PD : OBSS PD Spatial reuse. Considers both SRG and Non-SRG based.
 */
enum srtype {
    SR_TYPE_NON_SRG_OBSS_PD = 0,
    SR_TYPE_SRG_OBSS_PD,
    SR_TYPE_PSR,
    SR_TYPE_OBSS_PD,
    SR_TYPE_MAX,
};

#define SR_TYPE_LENGTH 16
#define IEEE80211_EHT_LTF_4X      0x3
#define IEEE80211_EHT_BW_IDX_MAX  4

/* GI Modes. */
#define IEEE80211_GI_0DOT8_US           0   /*800 nano secs/0.8 us is default/GI disabled mode in HT,VHT, HE*/
#define IEEE80211_GI_0DOT4_US           1   /*400 nano secs/0.4 us is supported in HT and VHT modes */
#define IEEE80211_GI_1DOT6_US           2   /*1600 nano secs/1.6 us is only in HE mode */
#define IEEE80211_GI_3DOT2_US           3   /*3600 nano secs/3.2 us is only in HE mode */

#define IEEE80211_RTS_CTS_MASK          0x0f
#define IEEE80211_RTS_CTS_SHIFT         0
#define IEEE80211_RTS_CTS_PROFILE_MASK  0xf0
#define IEEE80211_RTS_CTS_PROFILE_SHIFT 4

/* macro indicating max available IEEE80211_RTSCTS_PROFILE. To be updated as and when a new profile is added */
#define IEEE80211_RTSCTS_PROFILE_MAX_VALUE 4

/* macro indicating max available IEEE80211_RTS_CTS flag. To be updated as and when a new flag is added */
#define IEEE80211_RTSCTS_FLAG_MAX_VALUE 2

#define IEEE80211_HE_BW_IDX_MAX               3

/* Default VHT80 rate mask support all MCS rates except NSS=3 MCS 6 */
#define MAX_VHT80_RATE_MASK   0x3bffffff /* NSS=1 MCS 0-9, NSS=2 MCS 0-9, NSS=3 MCS 0-5 7-9 */

#define MAX_VHT_SGI_MASK   0x3ff /* MCS 0-9 */
#endif /* VENDOR_EXTN_H */
