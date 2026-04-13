
/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <net/netlink.h>
#include <net/mac80211.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include <linux/etherdevice.h>
#include "../core.h"
#include "ath12k_cmn_extn.h"
#include "vendor_extn.h"
#include "../net/wireless/core.h"
#include "../debug.h"
#include "../mac.h"
#include "esp_extn.h"
#include "ini.h"
#include "../cmn_defs.h"
#include "../telemetry_agent_if.h"
#include "dcs_extn.h"
#include "rropinfo.h"
#include "dp_stats_extn.h"
#include "../net/mac80211/ieee80211_i.h"
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
#include "mesh_util.h"
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */

#define ATH12K_CONFIG_AGGR_MAX_AMPDU_SIZE 255
#define ATH12K_CONFIG_AGGR_MAX_AMSDU_SIZE 7
#define ATH12K_CONFIG_MAX_BA_BUFSIZE 6
#define ATH12K_CONFIG_MAX_TX_ENCAP_TYPE 2
#define ATH12K_CONFIG_MAX_RX_DECAP_TYPE 2
const struct nla_policy
ath12k_240mhz_sta_info_policy[QCA_WLAN_VENDOR_ATTR_240MHZ_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_240MHZ_BEAMFORMEE_SS] = { .type = NLA_U8 },
	[QCA_WLAN_VENDOR_ATTR_240MHZ_NUM_SOUNDING_DIMENSIONS] = {
		.type = NLA_U8 },
	[QCA_WLAN_VENDOR_ATTR_240MHZ_NON_OFDMA_UL_MUMIMO] = {
		.type = NLA_FLAG },
	[QCA_WLAN_VENDOR_ATTR_240MHZ_MU_BEAMFORMER] = { .type = NLA_FLAG },
	[QCA_WLAN_VENDOR_ATTR_240MHZ_MCS_MAP] = {
		.type = NLA_BINARY, .len = 3 },
};

const struct nla_policy
ath12k_rule_config_policy[QCA_WLAN_VENDOR_ATTR_SCS_RULE_CONFIG_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_SCS_RULE_CONFIG_DST_MAC_ADDR] = NLA_POLICY_EXACT_LEN_WARN(ETH_ALEN),
};

const struct nla_policy
ath12k_vendor_home_offchan_tx_rx_policy[
	QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_MAX + 1] = {
	/* Command attributes */
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FUNC] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_CHAN] = {.type = NLA_U16},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_CHAN_BAND] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_SCAN_DUR] = {.type = NLA_U32},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME] = {.type = NLA_NESTED},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_TRANSACTION_ID] = {.type = NLA_U32},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_BW_MODE] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_SEC_CHAN_OFFSET] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_NUM_FRAMES] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_IS_MLD] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_LINK_ID] = {.type = NLA_U8},

	/* Event/Statistics attributes */
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_STATUS] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_NOISE_FLOOR] = {.type = NLA_S16},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_BLANKING_VALID] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_TX_FRAME_COUNT] = {.type = NLA_U32},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_RX_FRAME_COUNT] = {.type = NLA_U32},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_RX_CLEAR_COUNT] = {.type = NLA_U32},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_CYCLE_COUNT] = {.type = NLA_U32},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_DWELL_TIME] = {.type = NLA_U32},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_CHANSWITCH_HTOF] = {.type = NLA_U32},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_CHANSWITCH_FTOH] = {.type = NLA_U32},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_BLANKING_COUNT] = {.type = NLA_U32},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_BLANKING_DURATION] = {.type = NLA_U32},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_EVENT_TX_PKT_STATUS] = {.type = NLA_NESTED},
};

const struct nla_policy
ath12k_vendor_home_offchan_tx_rx_frame_policy[
	QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_MAX + 1] = {
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_NSS] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_PREAMBLE] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_MCS] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_RETRY] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_POWER] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_TX_BEAMFORMING] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_FRAME_DATA] = {.type = NLA_BINARY},
};

const struct nla_policy
ath12k_vendor_home_offchan_tx_rx_pkt_status_policy[
	QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_PKT_STATUS_MAX + 1] = {
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_PKT_STATUS_ID] = {.type = NLA_U8},
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_PKT_STATUS_RESULT] = {.type = NLA_U8},
};

const struct nla_policy
ath12k_wifi_mac_config_policy[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_RADIO_INDEX] = {.type = NLA_U8 },
	[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_RADIO_BSS_ID] = { .type = NLA_U8 },
	[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MAC_ADDR] = { .len = ETH_ALEN },
	[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_IFTYPE] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_FLAGS] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MBSSID_GRP_ID] = { .type = NLA_U8 },
	[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MBSSID_GRP_SIZE] = { .type = NLA_U8 },
};

const struct nla_policy
ath12k_ctl_table_policy[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_BAND] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_CTL_RADIO_INDEX] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_LENGTH] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_DATA] = { .type = NLA_BINARY,
						.len = QCA_WLAN_MAX_CTL_SIZE },
};

const struct nla_policy
ath12k_vendor_dcs_config_policy[QCA_WLAN_VENDOR_ATTR_DCS_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_DCS_LINK_ID] = {.type = NLA_U8},
	[QCA_WLAN_VENDOR_ATTR_DCS_CMD_TYPE] = {.type = NLA_U8},
	[QCA_WLAN_VENDOR_ATTR_DCS_ENABLE] = {.type = NLA_U16},
	[QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_DETECTION_THRESHOLD] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_DCS_PHY_ERR_PENALTY] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_DCS_PHY_ERR_THRESHOLD] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_DCS_RADAR_ERR_THRESHOLD] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_DCS_TX_ERR_THRESHOLD] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_DETECTION_WINDOW] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_DCS_COCHANNEL_INTERFERENCE_THRESHOLD] = {.type = NLA_U8},
	[QCA_WLAN_VENDOR_ATTR_DCS_MAX_CU] = {.type = NLA_U8},
	[QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_BITMAP] = {.type = NLA_U32},
};

const struct nla_policy
ath12k_rrop_info_policy[QCA_WLAN_VENDOR_ATTR_CONFIG_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_CONFIG_RADIO_INDEX] = {.type = NLA_U8 },
};

const struct nla_policy
ath12k_vendor_dcs_sim_policy[QCA_WLAN_VENDOR_ATTR_DCS_SIM_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_DCS_SIM_LINK_ID] = {.type = NLA_U8},
	[QCA_WLAN_VENDOR_ATTR_DCS_SIM_TYPE] = {.type = NLA_U16},
	[QCA_WLAN_VENDOR_ATTR_DCS_SIM_INTERFERENCE_BITMAP] = {.type = NLA_U32},
};

const struct nla_policy
ath12k_reg_params_policy[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_CMD] = { .type = NLA_U8 },
	[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_OPCLASS_CHAN] = { .type = NLA_U8 },
	[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_LINKID] = { .type = NLA_U8 },
};

int ath12k_vendor_send_rule_config_notify(struct ieee80211_vif *vif,
					   u8 *mac_addr)
{
	struct wireless_dev *wdev;
	struct sk_buff *skb;

	wdev = ieee80211_vif_to_wdev(vif);
	if (!wdev)
		return -EINVAL;

	skb = cfg80211_vendor_event_alloc(wdev->wiphy, wdev,
					  NLMSG_DEFAULT_SIZE,
					  QCA_NL80211_VENDOR_SUBCMD_SCS_RULE_CONFIG_INDEX,
					  GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	if (nla_put(skb, QCA_WLAN_VENDOR_ATTR_SCS_RULE_CONFIG_DST_MAC_ADDR,
		    ETH_ALEN, mac_addr)) {
		kfree(skb);
		return -ENOBUFS;
	}

	cfg80211_vendor_event(skb, GFP_ATOMIC);
	ath12k_dbg(NULL, ATH12K_DBG_MAC, "rule config notify %pM", mac_addr);
	return 0;
}

int ath12k_vendor_rule_config_notify(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data,
				      int data_len)
{
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	u8 mac_addr[ETH_ALEN] = {0};
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_SCS_RULE_CONFIG_MAX + 1];
	int ret;

	ret = nla_parse(tb, QCA_WLAN_VENDOR_ATTR_SCS_RULE_CONFIG_MAX, data,
			data_len, ath12k_rule_config_policy, NULL);
	if (ret) {
		ath12k_err(NULL, "Invalid attribute in rule config notify %d\n", ret);
		return ret;
	}

	if (tb[QCA_WLAN_VENDOR_ATTR_SCS_RULE_CONFIG_DST_MAC_ADDR] &&
	    (nla_len(tb[QCA_WLAN_VENDOR_ATTR_SCS_RULE_CONFIG_DST_MAC_ADDR])
	     == ETH_ALEN)) {
		memcpy(mac_addr,
		       nla_data(tb[QCA_WLAN_VENDOR_ATTR_SCS_RULE_CONFIG_DST_MAC_ADDR]),
		       ETH_ALEN);
	} else {
		ath12k_err(NULL, "Invalid MAC address in rule config notify\n");
		return -EINVAL;
	}

	ath12k_vendor_send_rule_config_notify(vif, mac_addr);

	ath12k_info(NULL, "scs rule config mac addr : %pM\n", mac_addr);

	return 0;
}

int ath12k_vendor_get_sta_240mhz_info(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data,
				      int data_len)
{
	struct nlattr *vendor[QCA_WLAN_VENDOR_ATTR_240MHZ_MAX + 1];
	struct ieee80211_240mhz_vendor_oper_extn params_240mhz_extn = {0};
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	u8 *mac_addr = NULL;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct genl_info *info;

	if (!vif || !rdev || !ahvif)
		return -1;

	info = rdev->cur_cmd_info;

	if (vif->type != NL80211_IFTYPE_AP) {
		ath12k_err(NULL, "vif type: %d is invalid", vif->type);
		return -1;
	}

	if (nla_parse(vendor, QCA_WLAN_VENDOR_ATTR_240MHZ_MAX,
		      data, data_len,
		      ath12k_240mhz_sta_info_policy,
		      NULL)) {
		ath12k_err(NULL, "Failed to parse vednor attributes");
		return -1;
	}

	params_240mhz_extn.is5ghz240mhz = true;

	if (vendor[QCA_WLAN_VENDOR_ATTR_240MHZ_BEAMFORMEE_SS])
		params_240mhz_extn.bfmess320mhz =
			nla_get_u8(vendor[QCA_WLAN_VENDOR_ATTR_240MHZ_BEAMFORMEE_SS]);

	if (vendor[QCA_WLAN_VENDOR_ATTR_240MHZ_NUM_SOUNDING_DIMENSIONS])
		params_240mhz_extn.numsound320mhz =
			nla_get_u8(vendor[QCA_WLAN_VENDOR_ATTR_240MHZ_NUM_SOUNDING_DIMENSIONS]);

	if (vendor[QCA_WLAN_VENDOR_ATTR_240MHZ_NON_OFDMA_UL_MUMIMO])
		params_240mhz_extn.nonofdmaulmumimo320mhz = 1;

	if (vendor[QCA_WLAN_VENDOR_ATTR_240MHZ_MU_BEAMFORMER])
		params_240mhz_extn.mubfmr320mhz = 1;

	if (vendor[QCA_WLAN_VENDOR_ATTR_240MHZ_MCS_MAP]) {
		memcpy(params_240mhz_extn.mcs_map_320mhz,
		       nla_data(vendor[QCA_WLAN_VENDOR_ATTR_240MHZ_MCS_MAP]),
		       3);
	}

	if (info->attrs[NL80211_ATTR_CENTER_FREQ1])
		params_240mhz_extn.ccfs0 =
			nla_get_u32(info->attrs[NL80211_ATTR_CENTER_FREQ1]);

	if (info->attrs[NL80211_ATTR_CENTER_FREQ2])
		params_240mhz_extn.ccfs1 =
			nla_get_u32(info->attrs[NL80211_ATTR_CENTER_FREQ2]);

	if (info->attrs[NL80211_ATTR_PUNCT_BITMAP])
		params_240mhz_extn.punctured =
			nla_get_u32(info->attrs[NL80211_ATTR_PUNCT_BITMAP]);

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (!mac_addr ||
	    ath12k_add_sta_240mhz_info_extn(ahvif, mac_addr,
					    &params_240mhz_extn)) {
		ath12k_err(NULL, "Failed to add the 240MHz extn information");
		return -1;
	}

	return 0;
}

/* Send a 'reload' NL80211 event to userspace
 * link_id is 'invalid' for non-mlo wdev.
 * For mlo wdev, if 'invalid' link_id is passed, userspace will reload all
 * BSSes of the mlo
 * If valid link_id is passed for mlo wdev, only that BSS is reset
 * in userspace.
 */
static int ath12k_vendor_event_iface_reload_link(struct wiphy *wiphy,
                                            struct wireless_dev *wdev, u8 link_id)
{
        struct sk_buff *skb;

        skb = cfg80211_vendor_event_alloc(wiphy, wdev,
                                          NLMSG_DEFAULT_SIZE,
                                          QCA_NL80211_VENDOR_SUBCMD_IFACE_RELOAD_INDEX,
                                          GFP_KERNEL);
        if (!skb)
                return -ENOMEM;

        if (nla_put_u8(skb, QCA_WLAN_VENDOR_ATTR_IFACE_RELOAD_LINKID, link_id)) {
                kfree_skb(skb);
                return -EINVAL;
        }

        ath12k_dbg(NULL, ATH12K_DBG_CFG,
                   "send event to userspace link_id %d\n", link_id);
        cfg80211_vendor_event(skb, GFP_KERNEL);

        return 0;
}

/* Find the wdevs corresponding to the radio index of the wiphy
 * and send the reload event to userspace
 */
static int ath12k_vendor_event_iface_reload(struct wiphy *wiphy, u8 radio_idx)
{
        struct ieee80211_hw *hw = NULL;
        struct ath12k_hw *ah = NULL;
        struct ath12k *ar = NULL;
        struct ath12k_link_vif *arvif = NULL;
        struct wireless_dev *wdev = NULL;

        hw = wiphy_to_ieee80211_hw(wiphy);
        ah = hw->priv;
        ar = &ah->radio[radio_idx];
        if (!ar) {
                ath12k_err(NULL, "Failed to find ar\n");
                return -ENODATA;
        }
        list_for_each_entry(arvif, &ar->arvifs, list) {
                wdev = ieee80211_vif_to_wdev(arvif->ahvif->vif);
                if (!wdev) {
                        ath12k_err(NULL, "Failed to find wdev\n");
                        return -ENODATA;
                }
                ath12k_vendor_event_iface_reload_link(wiphy, wdev, arvif->link_id);
        }
	        return 0;
}

static int ath12k_set_ampdu_aggr_size_arvif(struct ath12k_link_vif *arvif, u32 value)
{
	struct ath12k *ar;
	struct ath12k_base *ab;
	struct set_custom_aggr_size_params params = {0};
	int ret;

	if (!arvif || !arvif->ar)
		return -ENODEV;

	ar = arvif->ar;
	ab = ar->ab;

	params.vdev_id = arvif->vdev_id;
	params.aggr_type = WMI_VDEV_CUSTOM_AGGR_TYPE_AMPDU;
	params.ac = 0;
	params.tx_aggr_size = value;
	params.rx_aggr_size = 0;
	params.tx_aggr_size_disable = false;
	params.rx_aggr_size_disable = true;
	params.tx_ac_enable = true;

	ret = ath12k_wmi_send_aggr_size_cmd(ar, &params);
	if (ret) {
		ath12k_warn(ar->ab,
				"AMPDU aggr size set failed vdev_id %d value %u ret %d\n",
				params.vdev_id, value, ret);
		return ret;
	}

	return 0;
}

static int ath12k_set_amsdu_aggr_size_arvif(struct ath12k_link_vif *arvif, u32 value)
{
	struct ath12k *ar;
	struct ath12k_base *ab;
	struct set_custom_aggr_size_params params = {0};
	int ret;

	if (!arvif || !arvif->ar)
		return -ENODEV;

	ar = arvif->ar;
	ab = ar->ab;

	params.vdev_id = arvif->vdev_id;
	params.aggr_type = WMI_VDEV_CUSTOM_AGGR_TYPE_AMSDU;
	params.ac = 0;
	params.tx_aggr_size = value;
	params.rx_aggr_size = 0;
	params.tx_aggr_size_disable = false;
	params.rx_aggr_size_disable = true;
	params.tx_ac_enable = true;

	ret = ath12k_wmi_send_aggr_size_cmd(ar, &params);
	if (ret) {
		ath12k_warn(ar->ab,
				"AMSDU aggr size set failed vdev_id %d value %u ret %d\n",
				params.vdev_id, value, ret);
		return ret;
	}
	return 0;
}

/**
 * ath12k_is_valid_basic_rate - Validate if a bitrate is a basic rate
 * @arvif: pointer to ath12k_link_vif structure
 * @bitrate: bitrate value in units of 100 kbps (e.g., 60 = 6 Mbps)
 *
 * Returns: true if the bitrate is a valid basic rate, false otherwise
 */
static bool ath12k_is_valid_basic_rate(struct ath12k_link_vif *arvif, u32 bitrate)
{
	struct ieee80211_vif *vif = NULL;
	struct ieee80211_bss_conf *bss_conf = NULL;
	struct ieee80211_hw *hw = NULL;
	const struct ieee80211_supported_band *sband = NULL;
	struct cfg80211_chan_def def;
	u8 rate_idx;
	int i;

	if (!arvif || !arvif->ar)
		return false;

	vif = ath12k_ahvif_to_vif(arvif->ahvif);
	if (!vif)
		return false;

	bss_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!bss_conf)
		return false;

	if (ath12k_mac_vif_link_chan(vif, arvif->link_id, &def))
		return false;

	hw = ath12k_ar_to_hw(arvif->ar);
	sband = ieee80211_get_link_sband(wiphy_dereference(hw->wiphy,
			vif_to_sdata(vif)->link[arvif->link_id]) ?:
			&vif_to_sdata(vif)->deflink);
	if (!sband)
		return false;

	/* Search for the bitrate in the supported rates table */
	for (i = 0; i < sband->n_bitrates; i++) {
		if (sband->bitrates[i].bitrate == bitrate) {
			rate_idx = i;
			/* Check if this rate index bit is set in basic_rates bitmap */
			return !!(bss_conf->basic_rates & BIT(rate_idx));
		}
	}

	/* Bitrate not found in supported rates */
	return false;
}

/**
 * ath12k_set_vdev_mcast_rate - Set multicast rate for a vdev
 * @arvif: pointer to ath12k_link_vif structure
 * @bitrate: bitrate in units of 100 kbps (e.g., 60 = 6 Mbps)
 *
 * Returns: 0 on success, negative error code on failure
 */
static int ath12k_set_vdev_mcast_rate(struct ath12k_link_vif *arvif, u32 bitrate)
{
	struct ieee80211_vif *vif = NULL;
	struct ieee80211_bss_conf *bss_conf = NULL;
	struct ieee80211_hw *hw = NULL;
	const struct ieee80211_supported_band *sband = NULL;
	struct cfg80211_chan_def def;
	enum nl80211_band band;
	struct ath12k *ar;
	u32 rate;
	u8 rate_idx;
	int ret;

	if (!arvif || !arvif->ar)
		return -EINVAL;

	ar = arvif->ar;

	/* Validate that the provided bitrate is a basic rate */
	if (!ath12k_is_valid_basic_rate(arvif, bitrate)) {
		ath12k_err(ar->ab, "Invalid mcast rate %u not a basic rate\n", bitrate);
		return -EINVAL;
	}

	vif = ath12k_ahvif_to_vif(arvif->ahvif);
	if (!vif) {
		ath12k_err(ar->ab, "unable to get vif\n");
		return -EINVAL;
	}

	bss_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!bss_conf) {
		ath12k_err(ar->ab, "unable to access bss link conf\n");
		return -EINVAL;
	}

	/* Get the current operating band */
	if (ath12k_mac_vif_link_chan(vif, arvif->link_id, &def)) {
		ath12k_err(ar->ab, "unable to get channel def\n");
		return -EINVAL;
	}
	band = def.chan->band;

	hw = ath12k_ar_to_hw(ar);
	sband = ieee80211_get_link_sband(wiphy_dereference(hw->wiphy,
			vif_to_sdata(vif)->link[arvif->link_id]) ?:
			&vif_to_sdata(vif)->deflink);
	if (!sband) {
		ath12k_err(ar->ab, "supported band not found\n");
		return -EINVAL;
	}

	/* Convert bitrate to rate index using existing helper */
	rate_idx = ath12k_mac_bitrate_to_idx(sband, bitrate);

	/* Convert bitrate to firmware rate format using existing helper */
	rate = ath12k_mac_get_rate_hw_value(bitrate);
	if (rate == -EINVAL) {
		ath12k_err(ar->ab, "Invalid bitrate %u\n", bitrate);
		return -EINVAL;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "mac vdev %d setting mcast_rate %x (bitrate %u)\n",
		   arvif->vdev_id, rate, bitrate);

	/* Send multicast rate to firmware */
	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_MCAST_DATA_RATE, rate);
	if (ret) {
		ath12k_err(ar->ab,
			    "failed to set mcast rate on vdev %i: %d\n",
			    arvif->vdev_id, ret);
		return ret;
	} else {
		ath12k_err(ar->ab,"Set mcast rate successful for vdev %i",
				arvif->vdev_id);
	}

	/*
	 * Only update mac80211's mcast_rate after firmware successfully accepts it.
	 * mac80211 stores mcast_rate as (rate_index + 1), where 0 means disabled.
	 */
	bss_conf->mcast_rate[band] = rate_idx + 1;

	return 0;
}

/**
 * ath12k_get_vdev_mcast_rate - Get multicast rate for a vdev
 * @arvif: pointer to ath12k_link_vif structure
 * @value: pointer to store the retrieved bitrate (in units of 100 kbps)
 *
 * Returns: 0 on success, negative error code on failure
 */
static int ath12k_get_vdev_mcast_rate(struct ath12k_link_vif *arvif, u64 *value)
{
	struct ieee80211_bss_conf *bss_conf;
	struct cfg80211_chan_def def;
	struct ieee80211_vif *vif;
	struct ieee80211_hw *hw;
	const struct ieee80211_supported_band *sband;
	enum nl80211_band band;
	int mcast_idx;

	if (!arvif || !arvif->ar || !value)
		return -EINVAL;

	vif = ath12k_ahvif_to_vif(arvif->ahvif);
	if (!vif)
		return -EINVAL;

	bss_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!bss_conf)
		return -EINVAL;

	/* Get the current operating band */
	if (ath12k_mac_vif_link_chan(vif, arvif->link_id, &def))
		return -EINVAL;

	if (!def.chan || def.chan->band >= NUM_NL80211_BANDS)
		return -EINVAL;

	band = def.chan->band;
	mcast_idx = bss_conf->mcast_rate[band];

	/* mcast_idx of 0 means default/disabled */
	if (mcast_idx == 0) {
		*value = 0;
		return 0;
	}

	hw = ath12k_ar_to_hw(arvif->ar);
	if (!hw || !hw->wiphy)
		return -EINVAL;

	sband = ieee80211_get_link_sband(wiphy_dereference(hw->wiphy,
			vif_to_sdata(vif)->link[arvif->link_id]) ?:
			&vif_to_sdata(vif)->deflink);
	if (!sband || !sband->bitrates)
		return -EINVAL;

	/* mcast_rate is stored as (rate_idx + 1), so subtract 1 to get actual index */
	if ((mcast_idx - 1) >= sband->n_bitrates) {
		ath12k_err(arvif->ar->ab,
			   "Invalid mcast rate index %d (max=%d)\n",
			   mcast_idx - 1, sband->n_bitrates - 1);
		return -EINVAL;
	}

	*value = sband->bitrates[mcast_idx - 1].bitrate;

	ath12k_dbg(arvif->ar->ab, ATH12K_DBG_MAC,
		   "mac vdev %d get mcast_rate bitrate=%llu (index=%d)\n",
		   arvif->vdev_id, *value, mcast_idx - 1);

	return 0;
}

/**
 * ath12k_get_vdev_bcast_rate - Get broadcast rate for a vdev
 * @arvif: pointer to ath12k_link_vif structure
 * @value: pointer to store the retrieved bitrate (in units of 100 kbps)
 *
 * Returns: 0 on success, negative error code on failure
 */
static int ath12k_get_vdev_bcast_rate(struct ath12k_link_vif *arvif, u64 *value)
{
	if (!arvif || !arvif->ar || !value)
		return -EINVAL;

	*value = arvif->bcast_rate;

	ath12k_err(NULL,
		       "mac vdev %d get bcast_rate bitrate=%llu (%s)\n",
		       arvif->vdev_id, *value,
		       arvif->bcast_rate_configured ? "user-configured" : "default");
	return 0;
}

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
int ath12k_mac_vif_setmhdr(struct ath12k_link_vif *arvif, uint32_t mhdr)
{
	struct ath12k_vif *ahvif;
	struct ieee80211_vif *vif;
	struct ath12k *ar;
	struct ath12k_base *ab;
	int preamble_type;

	/* Validate arvif, ar, and ab pointers first to get ab for logging */
	if (!arvif || !arvif->ar || !arvif->ar->ab) {
		pr_err("ath12k: invalid pointers in set_offload_mode\n");
		return -EINVAL;
	}

	ar = arvif->ar;
	ab = ar->ab;

	/* Validate ahvif pointer */
	ahvif = arvif->ahvif;
	if (!ahvif) {
		ath12k_err(ab, "ahvif is NULL for vdev %d\n", arvif->vdev_id);
		return -EINVAL;
	}


	/* Validate vif pointer */
	vif = ath12k_ahvif_to_vif(ahvif);
	if (!vif) {
		ath12k_err(ab, "vif is NULL for vdev %d\n", arvif->vdev_id);
		return -EINVAL;
	}

	preamble_type = (mhdr >> MESH_DBG_PRAMBLE_OFFSET) & MESH_NIBBLE_MASK;

	if (preamble_type > METAHDR_PREAMBLE_EHT) {
		pr_err("ERR: preamble type is greater than EHT\n");
		return -EINVAL;
	}

	ahvif->dp_vif.dp_extn.mhdr = mhdr;
	return 0;
}

int ath12k_mac_vif_set_mesh_dbg(struct ath12k_link_vif *arvif, uint32_t mdbg)
{
	struct ath12k_vif *ahvif;
	struct ieee80211_vif *vif;
	struct ath12k *ar;
	struct ath12k_base *ab;

	/* Validate arvif, ar, and ab pointers first to get ab for logging */
	if (!arvif || !arvif->ar || !arvif->ar->ab) {
		pr_err("ath12k: invalid pointers in set_offload_mode\n");
		return -EINVAL;
	}

	ar = arvif->ar;
	ab = ar->ab;

	/* Validate ahvif pointer */
	ahvif = arvif->ahvif;
	if (!ahvif) {
		ath12k_err(ab, "ahvif is NULL for vdev %d\n", arvif->vdev_id);
		return -EINVAL;
	}


	/* Validate vif pointer */
	vif = ath12k_ahvif_to_vif(ahvif);
	if (!vif) {
		ath12k_err(ab, "vif is NULL for vdev %d\n", arvif->vdev_id);
		return -EINVAL;
	}

	ahvif->dp_vif.dp_extn.mdbg = mdbg;
	return 0;
}

int ath12k_mac_vif_set_mesh_rx_filter(struct ath12k_link_vif *arvif, uint32_t rx_filter)
{
	struct ath12k_vif *ahvif;
	struct ieee80211_vif *vif;
	struct ath12k *ar;
	struct ath12k_base *ab;

	/* Validate arvif, ar, and ab pointers first to get ab for logging */
	if (!arvif || !arvif->ar || !arvif->ar->ab) {
		pr_err("ath12k: invalid pointers in set_offload_mode\n");
		return -EINVAL;
	}

	ar = arvif->ar;
	ab = ar->ab;

	/* Validate ahvif pointer */
	ahvif = arvif->ahvif;
	if (!ahvif) {
		ath12k_err(ab, "ahvif is NULL for vdev %d\n", arvif->vdev_id);
		return -EINVAL;
	}


	/* Validate vif pointer */
	vif = ath12k_ahvif_to_vif(ahvif);
	if (!vif) {
		ath12k_err(ab, "vif is NULL for vdev %d\n", arvif->vdev_id);
		return -EINVAL;
	}

	/* rx filter shouldn't have bits set beyond MESH_FILTER_OUT_TA */
	if (rx_filter > 0x1F)
		return -EINVAL;

	ahvif->dp_vif.dp_extn.rx_filter = rx_filter;
	return 0;
}

int ath12k_mac_vif_set_mesh_tx(struct ath12k_link_vif *arvif, uint32_t mesh_tx)
{
	struct ath12k_vif *ahvif;
	struct ieee80211_vif *vif;
	struct ath12k *ar;
	struct ath12k_base *ab;

	/* Validate arvif, ar, and ab pointers first to get ab for logging */
	if (!arvif || !arvif->ar || !arvif->ar->ab) {
		pr_err("ath12k: invalid pointers in set_offload_mode\n");
		return -EINVAL;
	}

	ar = arvif->ar;
	ab = ar->ab;

	/* Validate ahvif pointer */
	ahvif = arvif->ahvif;
	if (!ahvif) {
		ath12k_err(ab, "ahvif is NULL for vdev %d\n", arvif->vdev_id);
		return -EINVAL;
	}


	/* Validate vif pointer */
	vif = ath12k_ahvif_to_vif(ahvif);
	if (!vif) {
		ath12k_err(ab, "vif is NULL for vdev %d\n", arvif->vdev_id);
		return -EINVAL;
	}

	ahvif->dp_vif.dp_extn.mesh_tx = mesh_tx;
	return 0;
}

/**
 * ath12k_set_vdev_bcast_rate - Set broadcast rate for a vdev
 * @arvif: pointer to ath12k_link_vif structure
 * @bitrate: bitrate in units of 100 kbps (e.g., 60 = 6 Mbps)
 *
 * Returns: 0 on success, negative error code on failure
 */
static int ath12k_set_vdev_bcast_rate(struct ath12k_link_vif *arvif, u32 bitrate)
{
	struct ath12k *ar;
	u32 rate;
	u32 prev_bcast_rate;
	int ret;
	bool prev_bcast_configured;

	if (!arvif || !arvif->ar)
		return -EINVAL;

	ar = arvif->ar;

	/* Validate that the provided bitrate is a basic rate */
	if (!ath12k_is_valid_basic_rate(arvif, bitrate)) {
		ath12k_err(ar->ab, "Invalid bcast rate %u not a basic rate\n", bitrate);
		return -EINVAL;
	}

	/* Convert bitrate to firmware rate format using existing helper */
	rate = ath12k_mac_get_rate_hw_value(bitrate);
	if (rate == -EINVAL) {
		ath12k_err(ar->ab, "Invalid bitrate %u\n", bitrate);
		return -EINVAL;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "mac vdev %d setting bcast_rate %x (bitrate %u)\n",
		   arvif->vdev_id, rate, bitrate);

	/* Save previous state for rollback */
	prev_bcast_configured = arvif->bcast_rate_configured;
	prev_bcast_rate = arvif->bcast_rate;

	/* Send broadcast rate to firmware */
	ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					    WMI_VDEV_PARAM_BCAST_DATA_RATE, rate);
	if (ret) {
		ath12k_err(ar->ab,
			    "failed to set bcast rate on vdev %i %d\n",
			    arvif->vdev_id, ret);

		/* Attempt to restore previous broadcast rate if it was configured */
		if (prev_bcast_configured && prev_bcast_rate != 0) {
			u32 prev_rate = ath12k_mac_get_rate_hw_value(prev_bcast_rate);

			if (prev_rate != -EINVAL) {
				if (ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
								  WMI_VDEV_PARAM_BCAST_DATA_RATE,
								  prev_rate))
					ath12k_err(ar->ab,
						   "failed to restore previous bcast rate %u on vdev %i\n",
						   prev_bcast_rate, arvif->vdev_id);
			}
		}

		return ret;
	}

	/* Firmware accepted the rate - update our state */
	arvif->bcast_rate_configured = true;
	arvif->bcast_rate = bitrate;

	return 0;
}
#endif

/* Set link-vif level parameters
 * set 'reload' to true to send reload event to userspace */
static int ath12k_vendor_set_arvif_params(struct ath12k_link_vif *arvif, u32 param,
                                          u32 value, bool *reload)
{
	struct ath12k *ar = arvif->ar;
        int ret = -1;

	switch (param) {
        case QCA_WLAN_VENDOR_VDEV_PARAM_TEST_RELOAD:
                *reload = true;
                ret = 0;
                break;
        case PARAM_RADIO_TXCHAINSOFT:
                ret = ath12k_mac_set_tx_antenna(ar, value);
                break;
        case ACFG_PARAM_RADIO_RXCHAINMASK:
                ret = ath12k_mac_set_rx_antenna(ar, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_DYN_BW_RTS:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_DISABLE_DYN_BW_RTS, value);
		if (!ret ) {
			arvif->vap_cfg.dyn_bw_rts = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_DISABLE_DYN_BW_RTS to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_CWM_ENABLE:
		if (value >= 0) {
			ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					WMI_PDEV_PARAM_DYNAMIC_BW, value);
			if (!ret ) {
				arvif->vap_cfg.cwm_enable = value;
			} else {
				ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_DYNAMIC_BW to firmware");
			}
		} else {
			ath12k_err(NULL, "Enable Channel width management with a value of 0 or more.");
			ret = -EINVAL;
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RATE_DROPDOWN:
#define RATE_DROPDOWN_LIMIT 7 /* Maximum Value for Rate Drop Down Logic */
		if ((value >= 0) && (value <= RATE_DROPDOWN_LIMIT)) {
			ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					WMI_VDEV_PARAM_RATE_DROPDOWN_BMAP, value);
			if (!ret ) {
				arvif->vap_cfg.rate_dropdown = value;
			} else {
				ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_RATE_DROPDOWN_BMAP to firmware");
			}
		} else {
			ath12k_err(NULL, "Rate Control Logic is [0-7]");
			ret = -EINVAL;
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_CTSPROT_DTIM_BCN:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_DTIM_ENABLE_CTS, value);
		if (!ret ) {
			arvif->vap_cfg.cts_dtim_bcn = !!value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_DTIM_ENABLE_CTS to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_CABQ_MAXDUR:
		if (value > 0 && value < 100) {
			ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					WMI_VDEV_PARAM_CABQ_MAXDUR, value);
			if (!ret ) {
				arvif->vap_cfg.cabq_maxdur = value;
			} else {
				ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_CABQ_MAXDUR to firmware");
			}
		} else {
			ath12k_err(NULL, "Percentage should be between 0 and 100.");
			ret = -EINVAL;
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_MCAST_RC_STALE_PERIOD:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_MCAST_RC_STALE_PERIOD, value);
		if (!ret ) {
			arvif->vap_cfg.mcast_rc_stale_period = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_MCAST_RC_STALE_PERIOD to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_ENABLE_MCAST_RC:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_ENABLE_MCAST_RC, value);
		if (!ret ) {
			arvif->vap_cfg.mcast_rc = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_ENABLE_MCAST_RC to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RC_NUM_RETRIES:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_RC_NUM_RETRIES, value);
		if (!ret ) {
			arvif->vap_cfg.rc_num_retries = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_RC_NUM_RETRIES to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_DISABLE_CABQ:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_DISABLE_CABQ, value);
		if (!ret ) {
			arvif->vap_cfg.disable_cabq = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_DISABLE_CABQ to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_SOUNDING_MODE:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_SET_HE_SOUNDING_MODE, value);
		if (!ret ) {
			arvif->vap_cfg.he_snd_mode = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_SET_HE_SOUNDING_MODE to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_MAX_MTU_SIZE:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_MAX_MTU_SIZE, value);
		if (!ret ) {
			arvif->vap_cfg.max_mtu_size = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_MAX_MTU_SIZE to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_GTX_ENABLE:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_GTX_ENABLE, value);
		if (!ret ) {
			arvif->vap_cfg.gtx_enable = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_GTX_ENABLE to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HWCTS2SELF_OFDMA:
		if (value < 0 || value > 1) {
			ath12k_err(NULL, "Invalid value, it should be 0 or 1.");
		} else {
			ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					WMI_VDEV_PARAM_HWCTS2SELF_OFDMA, value);
			if (!ret ) {
				arvif->vap_cfg.hwcts2self_ofdma = value;
			} else {
				ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_HWCTS2SELF_OFDMA to firmware");
			}
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_BCN_TX_POWER:
		if ((s32)value < 0 || (s32)value > 255) {
			ath12k_warn(ar->ab,
				    "Ignoring invalid BCN tx power %d (valid range 0..255)\n",
				    (s32)value);
			ret = -EINVAL;
			break;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_MGMT_TX_POWER, value);
		if (!ret) {
			arvif->vap_cfg.bcn_tx_power = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_MGMT_TX_POWER to firmware");
		}
		break;
        case QCA_WLAN_VENDOR_VDEV_PARAM_RSSI_MIN_THRESH:
	case QCA_WLAN_VENDOR_VDEV_PARAM_RSSI_MAX_THRESH:
	case QCA_WLAN_VENDOR_VDEV_PARAM_ACKRSSI_MIN_THRESH:
	case QCA_WLAN_VENDOR_VDEV_PARAM_ACKRSSI_MAX_THRESH:
		if ((int32_t)value > 0 || (int32_t)value < -100) {
			ath12k_err(NULL, "Invalid RSSI/ACKRSSI threshold value %d. Valid range: -100 to 0 dBm\n",
				   (int32_t)value);
			ret = -EINVAL;
		} else {
			if (param == QCA_WLAN_VENDOR_VDEV_PARAM_RSSI_MIN_THRESH)
				ret = ath12k_telemetry_set_threshold(THRESHOLD_RSSI_MIN, value);
			else if (param == QCA_WLAN_VENDOR_VDEV_PARAM_RSSI_MAX_THRESH)
				ret = ath12k_telemetry_set_threshold(THRESHOLD_RSSI_MAX, value);
			else if (param == QCA_WLAN_VENDOR_VDEV_PARAM_ACKRSSI_MIN_THRESH)
				ret = ath12k_telemetry_set_threshold(THRESHOLD_ACKRSSI_MIN, value);
			else if (param == QCA_WLAN_VENDOR_VDEV_PARAM_ACKRSSI_MAX_THRESH)
				ret = ath12k_telemetry_set_threshold(THRESHOLD_ACKRSSI_MAX, value);

			if (ret)
				ath12k_err(NULL, "Failed to set RSSI/ACKRSSI threshold, param: %d\n", param);
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_TXRATE_MIN_THRESH:
	case QCA_WLAN_VENDOR_VDEV_PARAM_TXRATE_MAX_THRESH:
	case QCA_WLAN_VENDOR_VDEV_PARAM_RXRATE_MIN_THRESH:
	case QCA_WLAN_VENDOR_VDEV_PARAM_RXRATE_MAX_THRESH:
		if ((int32_t)value < 0) {
			ath12k_err(NULL, "Invalid rate threshold value %d. Rate must be positive\n",
				   (int32_t)value);
			ret = -EINVAL;
		} else {
			if (param == QCA_WLAN_VENDOR_VDEV_PARAM_TXRATE_MIN_THRESH)
				ret = ath12k_telemetry_set_threshold(THRESHOLD_TXRATE_MIN, value);
			else if (param == QCA_WLAN_VENDOR_VDEV_PARAM_TXRATE_MAX_THRESH)
				ret = ath12k_telemetry_set_threshold(THRESHOLD_TXRATE_MAX, value);
			else if (param == QCA_WLAN_VENDOR_VDEV_PARAM_RXRATE_MIN_THRESH)
				ret = ath12k_telemetry_set_threshold(THRESHOLD_RXRATE_MIN, value);
			else if (param == QCA_WLAN_VENDOR_VDEV_PARAM_RXRATE_MAX_THRESH)
				ret = ath12k_telemetry_set_threshold(THRESHOLD_RXRATE_MAX, value);

			if (ret)
				ath12k_err(NULL, "Failed to set rate threshold, param: %d\n", param);
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RSSI_RATE_BREACH_MASK:
		ret = ath12k_telemetry_set_breach_mask(value);
		if (ret)
			ath12k_err(NULL, "Failed to set RSSI/Rate breach mask\n");
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RSSI_HYSTERESIS:
		ret = ath12k_telemetry_set_hysteresis(HYSTERESIS_TYPE_RSSI, value);
		if (ret)
			ath12k_err(NULL, "Failed to set RSSI hysteresis\n");
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RATE_HYSTERESIS:
		ret = ath12k_telemetry_set_hysteresis(HYSTERESIS_TYPE_RATE, value);
		if (ret)
			ath12k_err(NULL, "Failed to set rate hysteresis\n");
		break;

	case QCA_WLAN_VENDOR_VDEV_PARAM_AMPDU:
		if (value > ATH12K_CONFIG_AGGR_MAX_AMPDU_SIZE) {
			ath12k_err(NULL, "Valid AMPDU Aggregation Size is in the range 0-255");
			return -EINVAL;
		} else {
			ret = ath12k_set_ampdu_aggr_size_arvif(arvif, value);
			if (!ret) {
				arvif->vap_cfg.ampdu_aggr_size = value;
			} else {
				ath12k_err(NULL,
						"Failed to send WMI_VDEV_CUSTOM_AGGR_TYPE_AMPDU to firmware");
			}
		}
		break;

	case QCA_WLAN_VENDOR_VDEV_PARAM_AMSDU:
		if (value > ATH12K_CONFIG_AGGR_MAX_AMSDU_SIZE) {
			ath12k_err(NULL, "Valid AMPDU Aggregation Size is in the range 0-7");
			return -EINVAL;
		} else {
			ret = ath12k_set_amsdu_aggr_size_arvif(arvif, value);
			if (!ret) {
				arvif->vap_cfg.amsdu_aggr_size = value;
			} else {
				ath12k_err(NULL,
						"Failed to send WMI_VDEV_CUSTOM_AGGR_TYPE_AMSDU to firmware");
			}
		}
		break;

	case QCA_WLAN_VENDOR_VDEV_PARAM_BA_BUFSIZE:
		if (value > ATH12K_CONFIG_MAX_BA_BUFSIZE) {
			ath12k_err(NULL, "Max buffer size is %d\n", ATH12K_CONFIG_MAX_BA_BUFSIZE);
			ret = -EINVAL;
		} else {
			ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					WMI_VDEV_PARAM_BA_MODE,
					value);
			if (!ret) {
				arvif->vap_cfg.ba_bufsize = value;
			} else {
				ath12k_err(NULL,
						"Failed to send WMI_VDEV_PARAM_BA_MODE to firmware\n");
			}
		}
		break;

	case QCA_WLAN_VENDOR_VDEV_PARAM_TX_ENCAP_TYPE:
		if (value > ATH12K_CONFIG_MAX_TX_ENCAP_TYPE) {
			ath12k_err(NULL, "Value out of range\n");
			ret = -EINVAL;
		} else {
			ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					WMI_VDEV_PARAM_TX_ENCAP_TYPE,
					value);
			if (!ret) {
				arvif->vap_cfg.tx_encap_type = value;
			} else {
				ath12k_err(NULL,
						"Failed to send WMI_VDEV_PARAM_TX_ENCAP_TYPE to firmware\n");
			}
		}
		break;

	case QCA_WLAN_VENDOR_VDEV_PARAM_RX_DECAP_TYPE:
		if (value > ATH12K_CONFIG_MAX_RX_DECAP_TYPE) {
			ath12k_err(NULL, "Value out of range\n");
			ret = -EINVAL;
		} else {
			ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
					WMI_VDEV_PARAM_RX_DECAP_TYPE,
					value);
			if (!ret) {
				arvif->vap_cfg.rx_decap_type = value;
			} else {
				ath12k_err(NULL,
						"Failed to send WMI_VDEV_PARAM_RX_DECAP_TYPE to firmware\n");
			}
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_MCAST_RATE:
		ret = ath12k_set_vdev_mcast_rate(arvif, value);
		if (ret)
		    ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_MCAST_DATA_RATE to firmware\n");
		break;
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
	case QCA_WLAN_VENDOR_VDEV_PARAM_BCAST_RATE:
		ret = ath12k_set_vdev_bcast_rate(arvif, value);
		if (ret)
		    ath12k_err(NULL, "Failed to set bcast rate\n");
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_PN_MGMT_RX_FILTER:
		ret = ath12k_wmi_vdev_set_pn_mgmt_rx_filter_cmd(ar, arvif->vdev_id, value);
		if (ret)
			ath12k_err(NULL, "Failed to set PN RX Filter");
		break;
	case QCA_WLAN_VENDOR_VDEV_MESH_MODE_HDR:
		ret = ath12k_mac_vif_setmhdr(arvif, value);
		if (ret) {
			ath12k_err(ar->ab, "failed to set mesh hdr 0x%x for vdev %d: %d\n",
				   value, arvif->vdev_id, ret);
			return ret;
		}
		ath12k_err(ar->ab,
			   "Vendor cmd: Set mesh hdr to 0x%x for vdev %d\n Success",
			   value, arvif->vdev_id);
	break;
	case QCA_WLAN_VENDOR_VDEV_MESH_MODE_DBG:
		ret = ath12k_mac_vif_set_mesh_dbg(arvif, value);
		if (ret) {
			ath12k_err(ar->ab, "failed to set mesh dbg %d for vdev %d: %d\n",
				   value, arvif->vdev_id, ret);
			return ret;
		}
		ath12k_err(ar->ab,
			   "Vendor cmd: Set mesh dbg to %d for vdev %d\n Success",
			   value, arvif->vdev_id);
	break;
	case QCA_WLAN_VENDOR_VDEV_RX_FILTER:
		ret = ath12k_mac_vif_set_mesh_rx_filter(arvif, value);
		if (ret) {
			ath12k_err(ar->ab, "failed to set mesh rx_filter 0x%x for vdev %d: %d\n",
				   value, arvif->vdev_id, ret);
			return ret;
		}
		ath12k_err(ar->ab,
			   "Vendor cmd: Set mesh rx_filter to 0x%x for vdev %d\n Success",
			   value, arvif->vdev_id);
	break;
	case QCA_WLAN_VENDOR_VDEV_TX_MESH:
		ret = ath12k_mac_vif_set_mesh_tx(arvif, value);
		if (ret) {
			ath12k_err(ar->ab, "failed to set mesh tx_%d for vdev %d: %d\n",
				   value, arvif->vdev_id, ret);
			return ret;
		}
		ath12k_err(ar->ab,
			   "Vendor cmd: Set mesh tx to %d for vdev %d\n Success",
			   value, arvif->vdev_id);
	break;
#endif
	default:
		ath12k_dbg(NULL, ATH12K_DBG_CFG,
				"Un-supported param: %d\n", param);
		break;
	}

	return ret;
}

/* Set radio level parameters
 * set 'reload' to true to send reload event to userspace */
static int ath12k_vendor_set_radio_params(struct ath12k *ar,
                                          u32 param, u32 value, bool *reload)
{
        int ret = -1;
	u16 rx_ack_timeout = (value & 0xFFFF);
	u16 pre_rx_ack_timeout = (value >> 16);
	enum nl80211_band band;

        switch (param) {
        case QCA_WLAN_VENDOR_RADIO_PARAM_TEST_RELOAD:
                *reload = true;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_MGMT_RETRY_LIMIT:
		if (value < ATH12K_MGMT_TX_RETRY_LIMIT_MIN ||
		    value > ATH12K_MGMT_TX_RETRY_LIMIT_MAX) {
			ath12k_err(NULL, "Invalid tx retry limit %u (range %d to %d)\n",
				   value, ATH12K_MGMT_TX_RETRY_LIMIT_MIN,
				   ATH12K_MGMT_TX_RETRY_LIMIT_MAX);
			return -EINVAL;
		}

		ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_MGMT_RETRY_LIMIT,
						value, ar->pdev->pdev_id);
		if (ret) {
			ath12k_err(ar->ab, "Failed to set tx retry limit for mgmt frame: %d\n",
				   ret);
			return -EINVAL;
		}
		ar->mgmt_tx_retry_limit = value;
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_RTS_CTS_RATE:
                if (value > 4) {
                    ath12k_dbg(NULL, ATH12K_DBG_CFG, "Invalid value for setctsrate Disabling it in Firmware \n");
                    value = WMI_FIXED_RATE_NONE;
                }
                ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_RTS_FIXED_RATE,
                        value, ar->pdev->pdev_id);
		if (!ret) {
		    ar->radio_cfg.rts_cts_rate = value;
		} else {
		    ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_RTS_FIXED_RATE to firmware");
		}
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_PS_STATE_CHANGE:
                ar->radio_cfg.ps_report = value;
                ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PEER_STA_PS_STATECHG_ENABLE,
                        value, ar->pdev->pdev_id);
		if (!ret ) {
		    ar->radio_cfg.ps_report = (u8) value;
		} else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PEER_STA_PS_STATECHG_ENABLE to firmware");
		}
           break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_NON_AGG_SW_RETRY_TH:
                ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_NON_AGG_SW_RETRY_TH,
                        value, ar->pdev->pdev_id);
		if (!ret) {
		    ar->radio_cfg.nonagg_swretry_th = value;
		} else {
		    ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_NON_AGG_SW_RETRY_TH to firmware");
		}
                break;
       case QCA_WLAN_VENDOR_RADIO_PARAM_AGG_SW_RETRY_TH:
                ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_AGG_SW_RETRY_TH,
                        value, ar->pdev->pdev_id);
		if (!ret) {
		    ar->radio_cfg.agg_swretry_th = value;
		} else {
		    ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_AGG_SW_RETRY_TH to firmware");
		}
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_STA_KICKOUT_TH:
                ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_STA_KICKOUT_TH,
                        value, ar->pdev->pdev_id);
		if (!ret) {
		    ar->radio_cfg.sta_kickout_th = value;
		} else {
		    ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_STA_KICKOUT_TH to firmware");
		}
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ARPDHCP_AC_OVERRIDE:
                if ((WME_AC_BE <= value) && (value <= WME_AC_VO)) {
                    ar->radio_cfg.arp_override = value;
                    ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ARP_AC_OVERRIDE,
                            value, ar->pdev->pdev_id);
		    if (!ret) {
			ar->radio_cfg.arp_override = (u8) value;
		    } else {
			ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ARP_AC_OVERRIDE to firmware");
		    }
                } else {
                    ret = -EINVAL;
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANI_ENABLE:
		value = !!value;
		ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANI_ENABLE,
			value, ar->pdev->pdev_id);
		if (!ret) {
		    ar->radio_cfg.is_ani_enable = value;
		} else {
		    ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANI_ENABLE to firmware");
		}
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANI_POLL_PERIOD:
                if (value > 0) {
                    ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANI_POLL_PERIOD,
                            value, ar->pdev->pdev_id);
		    if (!ret) {
			ar->radio_cfg.ani_poll_period = value;
		    } else {
			ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANI_POLL_PERIOD to firmware");
		    }
                } else {
                    ret = -EINVAL;
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANI_LISTEN_PERIOD:
                 if (value > 0) {
                     ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANI_LISTEN_PERIOD,
                             value, ar->pdev->pdev_id);
		     if (!ret) {
			 ar->radio_cfg.ani_listen_period = value;
		     } else {
			 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANI_LISTEN_PERIOD to firmware");
		     }
                 } else {
                     ret = -EINVAL;
		     }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANI_OFDM_LEVEL:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANI_OFDM_LEVEL,
                         value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.ani_ofdm_level = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANI_OFDM_LEVEL to firmware");
		 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANI_CCK_LEVEL:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANI_CCK_LEVEL,
                         value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.ani_cck_level = value;
		 } else {
		      ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANI_CCK_LEVEL to firmware");
		 }
                 break;
#define CCA_THRESHOLD_LIMIT_UPPER  -11
#define CCA_THRESHOLD_LIMIT_LOWER  -94
        case QCA_WLAN_VENDOR_RADIO_PARAM_CCA_THRESHOLD:
                 if ((int32_t)value && (int32_t)value > CCA_THRESHOLD_LIMIT_UPPER)
                     value = CCA_THRESHOLD_LIMIT_UPPER;
                 else if ((int32_t)value < CCA_THRESHOLD_LIMIT_LOWER)
                     value = CCA_THRESHOLD_LIMIT_LOWER;

                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_CCA_THRESHOLD,
                         value, ar->pdev->pdev_id);
                 if (!ret) {
		     ar->radio_cfg.cca_threshold = (int32_t)value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_CCA_THRESHOLD to firmware");
		 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_DYN_TX_CHAINMASK:
                 /****************************************
                  *Value definition:
                  * bit 0        dynamic TXCHAIN
                  * bit 1        single TXCHAIN
                  * bit 2        single TXCHAIN for ctrl frames
                  * For bit 0-1, if value =
                  * 0x1  ==>   Dyntxchain enabled,  single_txchain disabled
                  * 0x2  ==>   Dyntxchain disabled, single_txchain enabled
                  * 0x3  ==>   Both enabled
                  * 0x0  ==>   Both disabled
                  *
                  * bit 3-7      reserved
                  * bit 8-11     single txchain mask, only valid if bit 1 set
                  *
                  * For bit 8-11, the single txchain mask for this radio,
                  * only valid if single_txchain enabled, by setting bit 1.
                  * Single txchain mask need to be updated when txchainmask,
                  * is changed, e.g. 4x4(0xf) ==> 3x3(0x7)
                  ****************************************/
#define DYN_TXCHAIN         0x1
#define SINGLE_TXCHAIN      0x2
#define SINGLE_TXCHAIN_CTL  0x4
                 if( (value & SINGLE_TXCHAIN) ||
                         (value & SINGLE_TXCHAIN_CTL) ) {
                     value &= 0xf07;
                 } else {
                     value &= 0x1;
                 }

                 if (ar->radio_cfg.dtcs != value) {
                     ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_DYNTXCHAIN,
                             value, ar->pdev->pdev_id);

                     if (!ret) {
			 ar->radio_cfg.dtcs = value;
		     } else {
			 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_DYNTXCHAIN to firmware");
		     }
		 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_LTR_ENABLE:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_LTR_ENABLE,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.ltr_enable = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_LTR_ENABLE to firmware");
		 }
		 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_LTR_AC_LATENCY_BE:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_LTR_AC_LATENCY_BE,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.ac_be = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_LTR_AC_LATENCY_BE to firmware");
		 }
		 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_LTR_AC_LATENCY_BK:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_LTR_AC_LATENCY_BK,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
                     ar->radio_cfg.ac_bk = value;
                 } else {
                     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_LTR_AC_LATENCY_BK to firmware");
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_LTR_AC_LATENCY_VI:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_LTR_AC_LATENCY_VI,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
                     ar->radio_cfg.ac_vi = value;
                 } else {
                     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_LTR_AC_LATENCY_VI to firmware");
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_LTR_AC_LATENCY_VO:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_LTR_AC_LATENCY_VO,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
                     ar->radio_cfg.ac_vo = value;
                 } else {
                     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_LTR_AC_LATENCY_VO to firmware");
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_LTR_AC_LATENCY_TIMEOUT:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.ac_timeout = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT to firmware");
		 }
		 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_LTR_TX_ACTIVITY_TIMEOUT:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.tx_timeout = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT to firmware");
		 }
		 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_LTR_SLEEP_OVERRIDE:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_LTR_SLEEP_OVERRIDE,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.sleep_override = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_LTR_SLEEP_OVERRIDE to firmware");
		 }
		 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_LTR_RX_OVERRIDE:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_LTR_RX_OVERRIDE,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.rx_override = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_LTR_RX_OVERRIDE to firmware");
		 }
		 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_L1SS_ENABLE:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_L1SS_ENABLE,
			value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.l1ss_enable = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_L1SS_ENABLE to firmware");
		 }
		 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_DSLEEP_ENABLE:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_DSLEEP_ENABLE,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.dsleep_enable = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_DSLEEP_ENABLE to firmware");
		 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SENS_LEVEL:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SENSITIVITY_LEVEL,
                         value, ar->pdev->pdev_id);
                 if (!ret) {
                     ar->radio_cfg.rxsop_sens_lvl = value;
                 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SENSITIVITY_LEVEL to firmware");
		 }
                 break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_DYN_GROUPING:
		 value = !!value;
		 if (ar->radio_cfg.dyngroup == value)
		     break;
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_MU_GROUP_POLICY,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.dyngroup = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_MU_GROUP_POLICY to firmware");
		 }
		 break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_DPD_ENABLE:
		 value = !!value;
		 if (ar->radio_cfg.dpdenable == CLI_DPD_CMD_INPROGRES) {
		     ath12k_err(NULL, "Previous command is in progress");
		     break;
		 }
		 if (ar->radio_cfg.dpdenable == value) {
		     ath12k_err(NULL, "DPD enable already is in same state");
		 break;
		 }
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_DPD_ENABLE,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.dpdenable = value;
		 } else {
		      ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_DPD_ENABLE to firmware");
		 }
		 break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_BURST_DUR:
		 if (value >= 0 && value <= 8192) {
		     ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_BURST_DUR,
			     value, ar->pdev->pdev_id);
		     if (!ret) {
			 ar->radio_cfg.burst_dur = (u16) value;
		     } else {
			 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_BURST_DUR to firmware");
		     }
		 } else {
		     return - EINVAL;
		 }
		 break;
#define DEFAULT_BURST_DURATION 8160
	case QCA_WLAN_VENDOR_RADIO_PARAM_BURST_ENABLE:
		 value = !!value;
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_BURST_ENABLE,
			 value, ar->pdev->pdev_id);
		 if (!ret)
		     ar->radio_cfg.burst_enable = (u8) value;
		 if (!ar->radio_cfg.burst_dur) {
		     ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_BURST_DUR,
			     DEFAULT_BURST_DURATION, ar->pdev->pdev_id);
		     if (!ret) {
			 ar->radio_cfg.burst_dur = (u16) DEFAULT_BURST_DURATION;
		     } else {
			 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_BURST_DURATION to firmware");
		     }
		 }
		 break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_DISABLE_LPI_ANT:
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_DISABLE_LPI_ANT_OPTIMIZATION,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.disable_lpi_ant = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_DISABLE_LPI_ANT_OPTIMIZATION to firmware");
		 }
		 break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_EN_PROBE_ALL_BW:
		 value = !!value;
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_EN_PROBE_ALL_BW,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.en_probe_all_bw = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_EN_PROBE_ALL_BW to firmware");
		 }
		 break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_UL_OFDMA_RTD:
		 if (value > 600 || value < 0) {
		     ath12k_err(NULL, "Invalid value. Supported range is 0 to 600\n");
		     ret = -EINVAL;
		     break;
		 }
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_UL_OFDMA_RTD,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.ul_ofdma_rtd = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_UL_OFDMA_RTD to firmware");
		 }
		 break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_ENABLE_SMALL_MRU:
		 value = !!value;
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ENABLE_SMALL_MRU,
			  value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.enable_small_mru = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ENABLE_SMALL_MRU to firmware");
		 }
		 break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_ENABLE_LARGE_MRU:
		 value = !!value;
		 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ENABLE_LARGE_MRU,
			 value, ar->pdev->pdev_id);
		 if (!ret) {
		     ar->radio_cfg.enable_large_mru = value;
		 } else {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ENABLE_LARGE_MRU to firmware");
		 }
		 break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_PDEV_RESET:
                 if (value > 0 && value < 6) {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_PDEV_RESET,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ar->radio_cfg.pdev_reset = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SIGNED_TXPOWER_2G to firmware");
                         }
                 } else {
                         ath12k_dbg(NULL, ATH12K_DBG_CFG, "Invalid value : Use any one of the below values:\n"
                                         "    TX_FLUSH = 1\n"
                                         "    WARM_RESET = 2\n"
                                         "    COLD_RESET = 3\n"
                                         "    WARM_RESET_RESTORE_CAL = 4\n"
                                         "    COLD_RESET_RESTORE_CAL = 5\n");
                         ret = -EINVAL;
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_HW_MODE_CMDID:
                 if (!(value == WMI_HOST_HW_MODE_DBS ||
                                         value == WMI_HOST_HW_MODE_DBS_SBS)) {
                         ath12k_dbg(NULL, ATH12K_DBG_CFG,
                                         "HW mode %d not supported", value);
                         ret = -EINVAL;
                 } else {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_SET_HW_MODE_CMDID,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ar->radio_cfg.current_mode = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_SET_HW_MODE_CMDID to firmware");
                         }
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_TXPOWER_LIMIT2G:
                 /*
                  * Check if the current regulatory channel list has any valid channel in the specified
                  * regulatory band, at least one channel in that band should be present/enabled in the
                  * current regulatory context, returns true.
                  */
                 band = ath12k_get_band_based_on_freq(ar->chan_info.low_freq);
                 if (band == NL80211_BAND_2GHZ) {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_TXPOWER_LIMIT2G,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ar->radio_cfg.txpowlimit2G = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_TXPOWER_LIMIT2G to firmware");
                         }
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_TXPOWER_LIMIT5G:
                 band = ath12k_get_band_based_on_freq(ar->chan_info.low_freq);
                 if (band == NL80211_BAND_5GHZ || band == NL80211_BAND_6GHZ) {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_TXPOWER_LIMIT5G,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ar->radio_cfg.txpowlimit5G = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_TXPOWER_LIMIT5G to firmware");
                         }
                 }
                 break;
#define ANTENNA_GAIN_2G_MASK    0x0
#define ANTENNA_GAIN_5G_MASK    0x8000
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANTENNA_GAIN_2G:
                 if (value >= 0 && value <= 30) {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANTENNA_GAIN,
                                         (value | ANTENNA_GAIN_2G_MASK), ar->pdev->pdev_id);
                         if (!ret) {
                                 ar->radio_cfg.antenna_gain_2g = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANTENNA_GAIN to firmware");
                         }
                 } else {
                         ath12k_dbg(NULL, ATH12K_DBG_CFG,
                                         "The value %d for ANTENNA_GAIN_2G is out of range.\n", value);
                         ret = -EINVAL;
                 }
                 break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_ANTENNA_GAIN_5G:
		 if (value >= 0 && value <= 30) {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANTENNA_GAIN,
                                         (value | ANTENNA_GAIN_5G_MASK), ar->pdev->pdev_id);
                         if (!ret) {
                                 ar->radio_cfg.antenna_gain_5g = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANTENNA_GAIN to firmware");
                         }
                 } else {
                         ath12k_dbg(NULL, ATH12K_DBG_CFG,
                                         "The value %d for ANTENNA_GAIN_5G is out of range.\n", value);
                         ret = -EINVAL;
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_OFDM_LEVEL:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANI_OFDM_LEVEL,
                                 value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.ofdem_level = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANI_OFDM_LEVEL to firmware");
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_TXPOWER_SCALE:
                 if (WMI_HOST_TP_SCALE_MAX <= value && value <= WMI_HOST_TP_SCALE_MIN) {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_TXPOWER_SCALE,
                                         value, ar->pdev->pdev_id);
                         if(!ret) {
                                 ar->radio_cfg.txpower_scale = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_TXPOWER_SCALE to firmware");
                         }
                 } else {
                         ret = -EINVAL;
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_RX_FILTER:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_RX_FILTER,
                                 value, ar->pdev->pdev_id);
                 if(!ret) {
                         ar->radio_cfg.rx_filter = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_RX_FILTER to firmware");
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_BLOCK_INTERBSS:
		 value = !!value;
                 if (value == ar->radio_cfg.block_interbss) {
                         ret = 0;
                 } else {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_BLOCK_INTERBSS,
                                         value, ar->pdev->pdev_id);
                         if (ret == 0) {
                                 ar->radio_cfg.block_interbss = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_BLOCK_INTERBSS to firmware");
                         }
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_DISABLE_RESET_CMDID:
                 value = !!value;
                 if (ar->radio_cfg.fw_disable_reset != (u_int8_t)value) {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_DISABLE_RESET_CMDID,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ar->radio_cfg.fw_disable_reset = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_DISABLE_RESET_CMDID to firmware");
                         }
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_PPDU_DURATION_CMDID:
                /* Get PPDU max duration based on countryISO. */
                /* Get PPDU min duration from IC, put sanity on value based on min and max. */
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_PPDU_DURATION_CMDID,
                                 value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.ppdu_dur = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_PPDU_DURATION_CMDID to firmware");
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_TXBF_SOUND_PERIOD_CMDID:
                 if (value < 10 || value > 10000) {
                         ret = -EINVAL;
                 } else {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_TXBF_SOUND_PERIOD_CMDID,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ar->radio_cfg.txbf_sound_period = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_TXBF_SOUND_PERIOD_CMDID to firmware");
                         }
			 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_PROMISC_MODE_CMDID:
		 value = !!value;
                 if (value == ar->radio_cfg.promisc_mode) {
                         ret = 0;
                 } else {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_PROMISC_MODE_CMDID,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ar->radio_cfg.promisc_mode = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_PROMISC_MODE_CMDID to firmware");
                         }
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_BURST_MODE_CMDID:
                 if (value < 0 || value >= 3) {
                         ath12k_dbg(NULL, ATH12K_DBG_CFG,
                                         "Usage: burst_mode <0:data-cts 1:data-data 2:data-(data/cts)\n");
                         ret = -EINVAL;
                 } else {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_BURST_MODE_CMDID,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ar->radio_cfg.burst_mode = value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_BURST_MODE_CMDID to firmware");
                         }
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_MCAST_BCAST_ECHO:
                 /* Set global Burst mode data-cts:0 data-ping-pong:1 data-cts-ping-pong:2. */
                 if (value < 0 || value > 1) {
                         ath12k_dbg(NULL, ATH12K_DBG_CFG,
                                         "Usage: Mcast Bcast Echo mode usage 0:disable 1:enable\n");
                         ret = -EINVAL;
                 } else {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_MCAST_BCAST_ECHO,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ar->radio_cfg.mcast_bcast_echo = (u_int8_t)value;
                         } else {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_MCAST_BCAST_ECHO to firmware");
                         }
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANT_PLZN:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANT_PLZN, value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.ant_plzn = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANT_PLZN to firmware");
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ENABLE_AMSDU:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ENABLE_PER_TID_AMSDU,
                                 value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.amsdu_mask = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ENABLE_PER_TID_AMSDU to firmware");
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ENABLE_AMPDU:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ENABLE_PER_TID_AMPDU,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.ampdu_mask = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ENABLE_PER_TID_AMPDU to firmware");
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_HE_MBSSID_CTRL_FRAME_CONFIG:
#define IEEE80211_MBSSID_CTRL_FRAME_MAX_VAL       0xF
                if(value > IEEE80211_MBSSID_CTRL_FRAME_MAX_VAL) {
                        ath12k_err(NULL, "Invalid input: 0x%x\n"
                                        "MBSSID Control frame config bit interpretation:\n"
                                        "B0: Basic Trigger setting\n"
                                        "B1: BSR Trigger setting\n"
                                        "B2: MU RTS setting\n"
                                        "B3: UL MUMIMO setting\n"
                                        "B4-B31: Reserved\n", value);
                        ret = -EINVAL;

                } else {
                        ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ENABLE_MBSSID_CTRL_FRAME,
                                        value, ar->pdev->pdev_id);
                        if (!ret) {
                                ar->radio_cfg.mbssid_en_ctrl_frame = value;
                        } else {
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ENABLE_MBSSID_CTRL_FRAME to firmware");
                        }
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_PROBE_RESP_RETRY_LIMIT:
#define MAX_PRESP_RETRY_LIMIT 0x7F
                if (value > 0 && value <= MAX_PRESP_RETRY_LIMIT) {
                        ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_PROBE_RESP_RETRY_LIMIT,
                                        value, ar->pdev->pdev_id);
                        if (!ret) {
                                ar->radio_cfg.resp_retry_limit = value;
                        } else {
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_PROBE_RESP_RETRY_LIMIT to firmware");
                        }
                } else {
                        ath12k_err(NULL, "PROBERESP RETRY LIMIT should be between 1 and 127");
                        ret = -1;
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_CTS_TIMEOUT:
#define MAX_CTS_TIMEOUT 0xFF
                if (value > 0 && value <= MAX_CTS_TIMEOUT) {
                        ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_CTS_TIMEOUT,
                                        value, ar->pdev->pdev_id);
                        if (!ret) {
                                ar->radio_cfg.cts_timeout = value;
                        } else {
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_CTS_TIMEOUT to firmware");
				}
                } else {
                        ath12k_err(NULL, "CTS Timeout value should be between 0 and 0xFF");
                        ret = -1;
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SLOT_TIME:
#define MAX_SLOT_TIME 90
                if (value > 0 && value <= MAX_SLOT_TIME) {
                        ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SLOT_TIME,
                                        value, ar->pdev->pdev_id);
                        if (!ret) {
                                ar->radio_cfg.slot_time = value;
                        } else {
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SLOT_TIME to firmware");
                        }
                } else {
                        ath12k_err(NULL, "Slot Time value should be between 0 and 90");
                        ret = -1;
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ACK_TIMEOUT:
#define MIN_TX_ACK_TIMEOUT 0x40
#define MAX_TX_ACK_TIMEOUT 0xFF
                if (rx_ack_timeout >= MIN_TX_ACK_TIMEOUT &&
                                rx_ack_timeout <= (MAX_TX_ACK_TIMEOUT)) {
                        if (pre_rx_ack_timeout < MIN_TX_ACK_TIMEOUT ||
                                        pre_rx_ack_timeout > MAX_TX_ACK_TIMEOUT)
                                pre_rx_ack_timeout = rx_ack_timeout - 20;
                        value = (rx_ack_timeout | (pre_rx_ack_timeout << 16));
                        ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ACK_TIMEOUT,
                                        value, ar->pdev->pdev_id);
                        if (!ret) {
                                ar->radio_cfg.ack_timeout = value;
                        } else {
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ACK_TIMEOUT to firmware");
                        }
                } else {
                        ath12k_err(NULL, "TX ACK Time-out value should be between 0x70 and 0xFF");
                        ret = -1;
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_CCK_TX_ENABLE:
		value = !!value;
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_CCK_TX_ENABLE,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.cck_enable = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_CCK_TX_ENABLE to firmware");
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_EQUAL_RU_ALLOCATION_ENABLE:
                if(value == 0 || value == 1) {
                        ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_EQUAL_RU_ALLOCATION_ENABLE,
                                        value, ar->pdev->pdev_id);
                        if (!ret) {
                                ar->radio_cfg.ru_alloc_en = value;
                        } else {
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_EQUAL_RU_ALLOCATION_ENABLE to firmware");
                        }
                } else {
                        ath12k_err(NULL, "RU allocation enable value should be 0 or 1.");
                        ret = -1;
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANTENNA_GAIN_HALF_DB:
#define MAX_ANTENNA_GAIN 30
                if ((value >= 0) && (value <= (MAX_ANTENNA_GAIN * 2))) {
			band = ath12k_get_band_based_on_freq(ar->chan_info.low_freq);
			if (band == NL80211_BAND_2GHZ) {
				value = value | ANTENNA_GAIN_2G_MASK;
			} else {
				value = value | ANTENNA_GAIN_5G_MASK;
			}

                        ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANTENNA_GAIN_HALF_DB,
                                        value, ar->pdev->pdev_id);
                        if (!ret) {
                                ar->radio_cfg.antenna_gain_half_db = value;
                        } else {
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANTENNA_GAIN_HALF_DB to firmware");
                        }
                }
		break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_MGMT_TTL:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_MGMT_TTL,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.mgmt_ttl = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_MGMT_TTL to firmware");
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_PROBE_RESP_TTL:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_PROBE_RESP_TTL,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.prb_rsp_ttl = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_PROBE_RESP_TTL to firmware");
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_MU_PPDU_DURATION:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_MU_PPDU_DURATION,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.mu_ppdu_dur = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_MU_PPDU_DURATION to firmware");
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_TBTT_CTRL:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_TBTT_CTRL,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.tbtt_ctrl = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_TBTT_CTRL to firmware");
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_PREAM_PUNCT_BW:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_PREAM_PUNCT_BW,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.punct_bw = value;
                 } else {
                     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_PREAM_PUNCT_BW to firmware");
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_LOW_LATENCY_SCHED_MODE:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_LOW_LATENCY_SCHED_MODE,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ar->radio_cfg.low_lat_mode = value;
                 } else {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_LOW_LATENCY_SCHED_MODE to firmware");
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_GET_TEMPERATURE:
        {
                struct ath12k_wmi_pdev *wmi = ar->wmi;
                struct sk_buff *skb;
                struct wmi_pdev_get_temperature_cmd {
                        __le32 tlv_header;
			__le32 param;
                        __le32 pdev_id;
                } __packed;
                struct wmi_pdev_get_temperature_cmd *cmd;

                /* Allocate WMI command buffer */
                skb = ath12k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
                if (!skb) {
                        ath12k_err(ar->ab, "Failed to allocate skb for temperature query\n");
                        return -ENOMEM;
                }

                /* Construct WMI command */
                cmd = (struct wmi_pdev_get_temperature_cmd *)skb->data;
                cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
                                            WMI_TAG_PDEV_GET_TEMPERATURE_CMD) |
                                 FIELD_PREP(WMI_TLV_LEN,
                                            sizeof(*cmd) - TLV_HDR_SIZE);
                cmd->pdev_id = cpu_to_le32(ar->pdev->pdev_id);

                /* Send WMI command directly */
                ret = ath12k_wmi_cmd_send(wmi, skb,
                                         WMI_PDEV_GET_TEMPERATURE_CMDID);
                if (ret) {
                        ath12k_err(ar->ab,
                                  "Failed to send WMI_PDEV_GET_TEMPERATURE_CMDID: %d\n",
                                  ret);
                        dev_kfree_skb(skb);
                        return ret;
                }

                ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
                          "Temperature query sent for pdev %d\n",
                          ar->pdev->pdev_id);

                /* Store that query was sent - response will come via event */
                ar->radio_cfg.temperature_query_pending = 1;
                ret = 0;
                break;
        }
        case QCA_WLAN_VENDOR_RADIO_PARAM_GPIO_CONFIG:
        {
                struct ath12k_wmi_pdev *wmi = ar->wmi;
                struct wmi_gpio_config_cmd *cmd;
                struct sk_buff *skb;
                u32 gpio_pin, gpio_dir, gpio_pull, gpio_function, gpio_intr;

                /* Value encoding:
                 * [31:24] - GPIO pin number (0-31)
                 * [23:16] - GPIO direction (0=input, 1=output)
                 * [15:8]  - GPIO pull type (0=none, 1=up, 2=down)
                 * [7:4]   - GPIO interrupt mode
                 * [3:0]   - GPIO function/mux
                 */
                gpio_pin      = (value >> 24) & 0xFF;
                gpio_dir      = (value >> 16) & 0xFF;
                gpio_pull     = (value >> 8)  & 0xFF;
                gpio_intr     = (value >> 4)  & 0x0F;
                gpio_function = value & 0x0F;

                /* Validate basic parameters */
                if (gpio_pin >= 32) {
                        ath12k_err(NULL, "Invalid GPIO pin number: %u\n", gpio_pin);
                        ret = -EINVAL;
                        break;
                }

                if (gpio_dir > QCA_WLAN_GPIO_OUTPUT) {
                        ath12k_err(NULL, "Invalid GPIO direction: %u\n", gpio_dir);
                        ret = -EINVAL;
                        break;
                }

                if (gpio_pull > QCA_WLAN_GPIO_PULL_DOWN) {
                        ath12k_err(NULL, "Invalid GPIO pull type: %u\n", gpio_pull);
                        ret = -EINVAL;
                        break;
                }

                /* Allocate SKB for WMI command */
                skb = ath12k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
                if (!skb) {
                        ath12k_err(NULL, "Failed to allocate SKB for GPIO config\n");
                        ret = -ENOMEM;
                        break;
                }

                /* Build WMI GPIO config command */
                cmd = (struct wmi_gpio_config_cmd *)skb->data;
                cmd->tlv_header =
                        FIELD_PREP(WMI_TLV_TAG, WMI_TAG_GPIO_CONFIG_CMD) |
                        FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
                cmd->gpio_num       = cpu_to_le32(gpio_pin);
                cmd->input          = cpu_to_le32(gpio_dir == QCA_WLAN_GPIO_INPUT ? 1 : 0);
                cmd->pull_type      = cpu_to_le32(gpio_pull);
                cmd->intr_mode      = cpu_to_le32(gpio_intr);
                cmd->mux_config_val = cpu_to_le32(gpio_function);

                /* Send WMI command */
                ret = ath12k_wmi_cmd_send(wmi, skb, WMI_GPIO_CONFIG_CMDID);
                if (ret) {
                        ath12k_err(NULL, "Failed to send GPIO config command: %d\n", ret);
                        dev_kfree_skb(skb);
                        break;
                }

                /* Cache configuration on success */
                ar->radio_cfg.gpio_cfg[gpio_pin].gpio_pin       = gpio_pin;
                ar->radio_cfg.gpio_cfg[gpio_pin].gpio_function  = gpio_function;
                ar->radio_cfg.gpio_cfg[gpio_pin].gpio_pull_type = gpio_pull;
                ar->radio_cfg.gpio_cfg[gpio_pin].gpio_dir       = gpio_dir;
                ar->radio_cfg.gpio_cfg[gpio_pin].gpio_intr_mode = gpio_intr;
                ar->radio_cfg.gpio_cfg[gpio_pin].configured     = true;

                ath12k_dbg(NULL, ATH12K_DBG_CFG,
                           "GPIO %u configured: dir=%u pull=%u intr=%u func=0x%x\n",
                           gpio_pin, gpio_dir, gpio_pull, gpio_intr, gpio_function);
                break;
        }
        case QCA_WLAN_VENDOR_RADIO_PARAM_GPIO_OUTPUT:
        {
                struct ath12k_wmi_pdev *wmi = ar->wmi;
                struct wmi_gpio_output_cmd *cmd;
                struct sk_buff *skb;
                u32 gpio_pin, gpio_value;

                /* Suggested encoding:
                 * [7:0]  - GPIO pin number (0-31)
                 * [8]    - value (0/1)
                 * others reserved
                 */
                gpio_pin   = value & 0xFF;
                gpio_value = (value >> 8) & 0x01;

                /* Validate pin */
                if (gpio_pin >= 32) {
                        ath12k_err(NULL, "Invalid GPIO pin number: %u\n", gpio_pin);
                        ret = -EINVAL;
                        break;
                }

                /* Check cached configuration */
                if (!ar->radio_cfg.gpio_cfg[gpio_pin].configured) {
                        ath12k_err(NULL, "GPIO %u not configured\n", gpio_pin);
                        ret = -EINVAL;
                        break;
                }

                if (ar->radio_cfg.gpio_cfg[gpio_pin].gpio_dir != QCA_WLAN_GPIO_OUTPUT) {
                        ath12k_err(NULL, "GPIO %u not configured as output\n", gpio_pin);
                        ret = -EINVAL;
                        break;
                }

                /* Allocate SKB */
                skb = ath12k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
                if (!skb) {
                        ath12k_err(NULL, "Failed to allocate SKB for GPIO output\n");
                        ret = -ENOMEM;
                        break;
                }

                /* Build WMI GPIO output command */
                cmd = (struct wmi_gpio_output_cmd *)skb->data;
                cmd->tlv_header =
                        FIELD_PREP(WMI_TLV_TAG, WMI_TAG_GPIO_OUTPUT_CMD) |
                        FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
                cmd->gpio_num = cpu_to_le32(gpio_pin);
                cmd->set      = cpu_to_le32(gpio_value);

                /* Send WMI command */
                ret = ath12k_wmi_cmd_send(wmi, skb, WMI_GPIO_OUTPUT_CMDID);
                if (ret) {
                        ath12k_err(NULL, "Failed to send GPIO output command: %d\n", ret);
                        dev_kfree_skb(skb);
                        break;
                }

                /* Cache value on success */
                ar->radio_cfg.gpio_cfg[gpio_pin].gpio_value = gpio_value;

                ath12k_dbg(NULL, ATH12K_DBG_CFG,
                           "GPIO %u output set to %u\n", gpio_pin, gpio_value);
                break;
        }
        case QCA_WLAN_VENDOR_RADIO_PARAM_MSDU_TTL:
                ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_MSDU_TTL_CMDID,
                                               value, ar->pdev->pdev_id);
                if (!ret) {
                        ar->radio_cfg.msdu_ttl = value;
                } else {
                        ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_MSDU_TTL_CMDID to firmware");
                }
                break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_DFS_NOL_SUBCHANNEL_MARKING:
	{
		bool enable;
		enable = !!value;
		if (ar->dfs_sub_channel_marking == enable) {
			ret = 0;
			break;
		}

		ar->dfs_sub_channel_marking = enable;
		ret = 0;

		if (ar->ah->state != ATH12K_HW_STATE_ON) {
			break;
		}

		ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SUB_CHANNEL_MARKING,
						enable ? 1 : 0, ar->pdev->pdev_id);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set sub channel marking: %d\n", ret);
			break;
		}

		break;
	}
        default:
                ath12k_dbg(NULL, ATH12K_DBG_CFG,
                           "Un-supported param: %d\n", param);
                break;
        }

        return ret;
}

/* Find the link-vif of the wdev with the link id to set params
 * If link id is invalid use the default link
 */
int ath12k_vendor_set_wifi_params_extn(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       struct ath12k_wifi_generic_params *params)
{
        struct ieee80211_vif *vif = NULL;
        struct ath12k_vif *ahvif;
        struct ath12k_link_vif *arvif = NULL;
        struct ath12k_hw *ah = NULL;
        struct ath12k *ar;
        u32 *data = (u32 *)params->data;
        u32 param = params->value;
        bool reload = false;
        u32 value;
        int ret = -1;

        lockdep_assert_wiphy(wiphy);

        vif = wdev_to_ieee80211_vif(wdev);
        if (!vif || !data)
                return -EINVAL;

        value = *data;

        ahvif = ath12k_vif_to_ahvif(vif);
        if (!ahvif)
                return -EINVAL;

        ah = ahvif->ah;

        rcu_read_lock();
        if (ah && params->link_id == INVALID_LINK_ID) {
                arvif = &ahvif->deflink;
        } else {
                if (params->link_id < ATH12K_NUM_MAX_LINKS)
                        arvif = rcu_dereference(ahvif->link[params->link_id]);
        }
        if (!arvif || !arvif->ar) {
                rcu_read_unlock();
                return -EINVAL;
        }

        ar = arvif->ar;
        rcu_read_unlock();

        ath12k_dbg(NULL, ATH12K_DBG_CFG,
                   "vif: %p param: %d value: %d if: %d link: %d\n",
                   vif, param, value,
                   params->ifindex, params->link_id);

        /* Call sleepable WMI path outside RCU read-side critical section. */
        ret = ath12k_vendor_set_arvif_params(arvif, param, value, &reload);

        if (!ret && reload)
                ath12k_vendor_event_iface_reload_link(wiphy, wdev, params->link_id);

        return ret;
}

int ath12k_vendor_wifi_config_handler_extn(struct wiphy *wiphy, struct wireless_dev *wdev,
					   struct ath12k_wifi_generic_params *wifi_params)
{
	int ret = 0;

	if (!wiphy || !wdev || !wifi_params) {
		ath12k_err(NULL, "NULL input params received\n");
		return -EINVAL;
	}

	if (!wifi_params->data) {
		ath12k_err(NULL, "Empty mesh params received\n");
		return -EINVAL;
	}

	switch (wifi_params->command) {
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
	case QCA_NL80211_VENDOR_WIFI_GENERIC_SUBCMD_MESH_PARAMS:
		if (wdev->vap_submode != QCA_WLAN_VENDOR_VAP_SUBMODE_MESH) {
			ath12k_err(NULL, "Received command for vap_submode: %d\n"
				   "Command valid only for mesh vap\n",
				   wdev->vap_submode);
			return -EINVAL;
		}

		ret = ath12k_vendor_mesh_params_config_extn(wiphy, wdev, wifi_params);

		if (ret) {
			ath12k_err(NULL,"Failed to set mesh params \n");
			return ret;
		}
		break;
#endif
	default:
		ath12k_dbg(NULL, ATH12K_DBG_CFG, "Un-supported generic command\n");
		return -EOPNOTSUPP;
	}

	return ret;
}

/* Find the 'ar' radio instance using the radio index of the wiphy
 * to set params
 */
int ath12k_vendor_set_wiphy_params_extn(struct wiphy *wiphy,
					struct ath12k_wifi_generic_params *params)
{
        struct ieee80211_hw *hw = NULL;
        struct ath12k_hw *ah = NULL;
        struct ath12k *ar = NULL;
        u32 param = params->value;
        u32 *data = (u32 *)params->data;
        u32 value = *data;
        int ret = -1;
        bool reload = false;

        lockdep_assert_wiphy(wiphy);

        hw = wiphy_to_ieee80211_hw(wiphy);
        ah = hw->priv;

	if (params->radio_idx >= ah->num_radio) {
		ath12k_err(NULL, "Invalid radio index %u (valid range: 0..%u)\n",
			   params->radio_idx, ah->num_radio ? ah->num_radio - 1 : 0);
		return -ENODATA;
	}

        ar = &ah->radio[params->radio_idx];
        if (!ar) {
                ath12k_err(NULL, "Failed to find ar\n");
                return -ENODATA;
        }

        ath12k_dbg(NULL, ATH12K_DBG_CFG,
                   "ar: %p param: %d value: %d if: %d radio: %d\n",
                   ar, param, value,
                   params->ifindex, params->radio_idx);

        ret = ath12k_vendor_set_radio_params(ar, param, value, &reload);

        if (!ret && reload)
                ath12k_vendor_event_iface_reload(wiphy, params->radio_idx);

        return ret;
}

static int ath12k_vendor_get_rssi_rate_threshold(u8 type, u64 *value)
{
	int threshold = ath12k_get_rssi_rate_threshold(type);

	if (threshold == -ENOTCONN) {
		ath12k_err(NULL, "Telemetry agent not loaded\n");
		return -ENOTCONN;
	}

	*value = threshold;
	return 0;
}

/* Get link-vif level parameters */
static int ath12k_vendor_get_arvif_params(struct ath12k_link_vif *arvif,
                                          u32 param, u64 *value)
{
	struct ath12k *ar = arvif->ar;
        int ret = -1;

        switch (param) {
        case QCA_WLAN_VENDOR_VDEV_PARAM_TEST:
                *value = 0;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_DYN_BW_RTS:
                *value = arvif->vap_cfg.dyn_bw_rts;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_CWM_ENABLE:
                *value = arvif->vap_cfg.cwm_enable;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RATE_DROPDOWN:
                *value = arvif->vap_cfg.rate_dropdown;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_CTSPROT_DTIM_BCN:
                *value = arvif->vap_cfg.cts_dtim_bcn;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_MCAST_RC_STALE_PERIOD:
                *value = arvif->vap_cfg.mcast_rc_stale_period;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_ENABLE_MCAST_RC:
                *value = arvif->vap_cfg.mcast_rc;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RC_NUM_RETRIES:
                *value = arvif->vap_cfg.rc_num_retries;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_DISABLE_CABQ:
                *value = arvif->vap_cfg.disable_cabq;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_SOUNDING_MODE:
                *value = arvif->vap_cfg.he_snd_mode;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_MAX_MTU_SIZE:
                *value = arvif->vap_cfg.max_mtu_size;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_VDEV_TSF:
		if (!ar) {
			ath12k_err(NULL, "No radio, use link ID \n");
			return -EINVAL;
		}
		reinit_completion(&ar->tsf_report_done);
		ret = ath12k_wmi_vdev_tsf_tstamp_action_cmd(ar, arvif->vdev_id);
		if (ret) {
			ath12k_err(ar->ab,
					"Failed to send WMI_VDEV_TSF_TSTAMP_ACTION_CMDID to firmware: %d\n",
					ret);
			break;
		}
		ret = wait_for_completion_timeout(&ar->tsf_report_done,
                                                  msecs_to_jiffies(1000));
		if (!ret) {
			ath12k_err(ar->ab, "TSF report event timeout\n");
			reinit_completion(&ar->tsf_report_done);
			ret = -ETIMEDOUT;
			break;
                }
                *value = ar->tsf_report.tsf;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_BCN_TX_POWER:
		*value = arvif->vap_cfg.bcn_tx_power;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RSSI_MIN_THRESH:
		ret = ath12k_vendor_get_rssi_rate_threshold(THRESHOLD_RSSI_MIN, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RSSI_MAX_THRESH:
		ret = ath12k_vendor_get_rssi_rate_threshold(THRESHOLD_RSSI_MAX, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_ACKRSSI_MIN_THRESH:
		ret = ath12k_vendor_get_rssi_rate_threshold(THRESHOLD_ACKRSSI_MIN, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_ACKRSSI_MAX_THRESH:
		ret = ath12k_vendor_get_rssi_rate_threshold(THRESHOLD_ACKRSSI_MAX, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_TXRATE_MIN_THRESH:
		ret = ath12k_vendor_get_rssi_rate_threshold(THRESHOLD_TXRATE_MIN, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_TXRATE_MAX_THRESH:
		ret = ath12k_vendor_get_rssi_rate_threshold(THRESHOLD_TXRATE_MAX, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RXRATE_MIN_THRESH:
		ret = ath12k_vendor_get_rssi_rate_threshold(THRESHOLD_RXRATE_MIN, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RXRATE_MAX_THRESH:
		ret = ath12k_vendor_get_rssi_rate_threshold(THRESHOLD_RXRATE_MAX, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RSSI_RATE_BREACH_MASK:
		ret = ath12k_get_rssi_rate_breach_mask();
		if (ret == -ENOTCONN) {
			ath12k_err(NULL, "Telemetry agent not loaded\n");
		} else {
			*value = ret;
			ret = 0;
		}
		break;
        case QCA_WLAN_VENDOR_VDEV_PARAM_GET_RSSI_RATE_THRESHOLDS:
		ret = ath12k_telemetry_print_thresholds();
		if (ret)
			ath12k_err(NULL, "Failed to print RSSI/Rate thresholds\n");
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RSSI_HYSTERESIS:
		ret = ath12k_telemetry_get_hysteresis(HYSTERESIS_TYPE_RSSI, value);
		if (ret == -ENOTCONN) {
			ath12k_err(NULL, "Telemetry agent not loaded\n");
		} else if (ret == -EINVAL) {
			ath12k_err(NULL, "Failed to get RSSI hysteresis\n");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RATE_HYSTERESIS:
		ret = ath12k_telemetry_get_hysteresis(HYSTERESIS_TYPE_RATE, value);
		if (ret == -ENOTCONN) {
			ath12k_err(NULL, "Telemetry agent not loaded\n");
		} else if (ret == -EINVAL) {
			ath12k_err(NULL, "Failed to get rate hysteresis\n");
		}
		break;

	case QCA_WLAN_VENDOR_VDEV_PARAM_AMPDU:
		*value = arvif->vap_cfg.ampdu_aggr_size;
		ret = 0;
		break;

	case QCA_WLAN_VENDOR_VDEV_PARAM_AMSDU:
		*value = arvif->vap_cfg.amsdu_aggr_size;
		ret = 0;
		break;

	case QCA_WLAN_VENDOR_VDEV_PARAM_BA_BUFSIZE:
		*value = arvif->vap_cfg.ba_bufsize;
		ret = 0;
		break;

	case QCA_WLAN_VENDOR_VDEV_PARAM_TX_ENCAP_TYPE:
		*value = arvif->vap_cfg.tx_encap_type;
		ret = 0;
		break;

	case QCA_WLAN_VENDOR_VDEV_PARAM_RX_DECAP_TYPE:
		*value = arvif->vap_cfg.rx_decap_type;
		ret = 0;
		break;

	case QCA_WLAN_VENDOR_VDEV_PARAM_MCAST_RATE:
		ret = ath12k_get_vdev_mcast_rate(arvif, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_BCAST_RATE:
		ret = ath12k_get_vdev_bcast_rate(arvif, value);
		break;
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
	case QCA_WLAN_VENDOR_VDEV_MESH_MODE_HDR:
		*value = arvif->ahvif->dp_vif.dp_extn.mhdr;
		ret = 0;
	break;
	case QCA_WLAN_VENDOR_VDEV_MESH_MODE_DBG:
		*value = arvif->ahvif->dp_vif.dp_extn.mdbg;
		ret = 0;
	break;
	case QCA_WLAN_VENDOR_VDEV_RX_FILTER:
		*value = arvif->ahvif->dp_vif.dp_extn.rx_filter;
		ret = 0;
	break;
	case QCA_WLAN_VENDOR_VDEV_TX_MESH:
		*value = arvif->ahvif->dp_vif.dp_extn.mesh_tx;
		ret = 0;
	break;
#endif
        default:
                ath12k_dbg(NULL, ATH12K_DBG_CFG,
                           "Un-supported param: %d\n", param);
                break;
        }

        return ret;
}

/* Get radio level parameters */
static int ath12k_vendor_get_radio_params(struct ath12k *ar,
                                          u32 param, u32 *value)
{
        int ret = -1;

        switch (param) {
        case QCA_WLAN_VENDOR_RADIO_PARAM_TEST:
                *value = 0;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_MGMT_RETRY_LIMIT:
		*value = ar->mgmt_tx_retry_limit;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_RTS_CTS_RATE:
                *value = ar->radio_cfg.rts_cts_rate;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_PS_STATE_CHANGE:
                *value = ar->radio_cfg.ps_report;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ARPDHCP_AC_OVERRIDE:
                *value = ar->radio_cfg.arp_override;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANI_ENABLE:
                *value = ar->radio_cfg.is_ani_enable;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_CCA_THRESHOLD:
                *value = ar->radio_cfg.cca_threshold;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_DYN_TX_CHAINMASK:
                *value = ar->radio_cfg.dtcs;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SENS_LEVEL:
                *value = ar->radio_cfg.rxsop_sens_lvl;
                ret = 0;
                break;
	 case QCA_WLAN_VENDOR_RADIO_PARAM_DYN_GROUPING:
		*value = ar->radio_cfg.dyngroup;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_DPD_ENABLE:
		{
		    *value = ar->radio_cfg.dpdenable;
		    switch(ar->radio_cfg.dpdenable) {
			case CLI_DPD_CMD_INPROGRES:
			    ath12k_dbg(NULL, ATH12K_DBG_CFG,
				    "DPD cal in progess");
			    break;
			case CLI_DPD_STATUS_FAIL:
			    ath12k_dbg(NULL, ATH12K_DBG_CFG,
				    "DPD cal failed Or DPD disabled BDF loaded");
			    break;
			case CLI_DPD_STATUS_DISABLED:
			    ath12k_dbg(NULL, ATH12K_DBG_CFG,
				    "DPD cal disabled");
			    break;
			case CLI_DPD_STATUS_PASS:
			    ath12k_dbg(NULL, ATH12K_DBG_CFG,
				    "DPD cal Passed !!");
			    break;
			case CLI_DPD_NA_STATE:
			    ath12k_dbg(NULL, ATH12K_DBG_CFG,
				    "INVALID!! DPD not triggered via CLI command");
			    break;
			default:
			    ath12k_dbg(NULL, ATH12K_DBG_CFG,
				    "unknown state");
		    }
		    ret = 0;
		}
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_BURST_DUR:
		*value = ar->radio_cfg.burst_dur;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_BURST_ENABLE:
		*value = ar->radio_cfg.burst_enable;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_EN_PROBE_ALL_BW:
		*value = ar->radio_cfg.en_probe_all_bw;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_SET_HW_MODE_CMDID:
                *value = ar->radio_cfg.current_mode;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_TXPOWER_LIMIT2G:
                *value = ar->radio_cfg.txpowlimit2G;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_TXPOWER_LIMIT5G:
		*value = ar->radio_cfg.txpowlimit5G;
		ret = 0;
		break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_TXPOWER_SCALE:
                *value = ar->radio_cfg.txpower_scale;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_BLOCK_INTERBSS:
                *value = ar->radio_cfg.block_interbss;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_TXBF_SOUND_PERIOD_CMDID:
                *value = ar->radio_cfg.txbf_sound_period;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_PROMISC_MODE_CMDID:
                *value = ar->radio_cfg.promisc_mode;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_BURST_MODE_CMDID:
                *value = ar->radio_cfg.burst_mode;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_MCAST_BCAST_ECHO:
		*value = ar->radio_cfg.mcast_bcast_echo;
		ret = 0;
		break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ENABLE_AMSDU:
                *value = ar->radio_cfg.amsdu_mask;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ENABLE_AMPDU:
                *value = ar->radio_cfg.ampdu_mask;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_HE_MBSSID_CTRL_FRAME_CONFIG:
                *value = ar->radio_cfg.mbssid_en_ctrl_frame;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_PROBE_RESP_RETRY_LIMIT:
                *value = ar->radio_cfg.resp_retry_limit;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_CTS_TIMEOUT:
                *value = ar->radio_cfg.cts_timeout;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SLOT_TIME:
                *value = ar->radio_cfg.slot_time;
                ret = 0;
		break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ACK_TIMEOUT:
                *value = ar->radio_cfg.ack_timeout;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_CCK_TX_ENABLE:
                *value = ar->radio_cfg.cck_enable;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_EQUAL_RU_ALLOCATION_ENABLE:
                *value = ar->radio_cfg.ru_alloc_en;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_MGMT_TTL:
                *value = ar->radio_cfg.mgmt_ttl;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_PROBE_RESP_TTL:
                *value = ar->radio_cfg.prb_rsp_ttl;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_MU_PPDU_DURATION:
                *value = ar->radio_cfg.mu_ppdu_dur;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_TBTT_CTRL:
                *value = ar->radio_cfg.tbtt_ctrl;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_PREAM_PUNCT_BW:
                *value = ar->radio_cfg.punct_bw;
                ret = 0;
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_GPIO_INPUT:
        {
		u32 i = 0;

		for (i = 0; i < 32; i++) {
			if (ar->radio_cfg.gpio_cfg[i].configured) {
				ath12k_err(NULL, "GPIO %u get: value=%u dir=%u pull=%u intr=%u func=0x%x\n",
					   i, ar->radio_cfg.gpio_cfg[i].gpio_value,
					   ar->radio_cfg.gpio_cfg[i].gpio_dir,
					   ar->radio_cfg.gpio_cfg[i].gpio_pull_type,
					   ar->radio_cfg.gpio_cfg[i].gpio_intr_mode,
					   ar->radio_cfg.gpio_cfg[i].gpio_function);
					/* Return a packed status:
					* [31:24] - GPIO value (0 or 1)
					* [23:16] - GPIO direction
						* [15:8]  - GPIO pull type
					* [7:4]   - GPIO interrupt mode
					* [3:0]   - GPIO function
					*/

					*value =
					(ar->radio_cfg.gpio_cfg[i].gpio_value      << 24) |
					(ar->radio_cfg.gpio_cfg[i].gpio_dir        << 16) |
					(ar->radio_cfg.gpio_cfg[i].gpio_pull_type  << 8)  |
					(ar->radio_cfg.gpio_cfg[i].gpio_intr_mode  << 4)  |
					(ar->radio_cfg.gpio_cfg[i].gpio_function);
			}
		}
		ret = 0;
		break;
        }
        case QCA_WLAN_VENDOR_RADIO_PARAM_GET_TEMPERATURE:
                /* Return cached temperature value */
                *value = ar->thermal.temperature;
                ret = 0;

                ath12k_dbg(NULL, ATH12K_DBG_CFG,
                          "Temperature read: %d°C (0x%x)\n",
                          (s32)*value, *value);
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_MSDU_TTL:
                *value = ar->radio_cfg.msdu_ttl;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_DFS_NOL_SUBCHANNEL_MARKING:
		*value = ar->dfs_sub_channel_marking;
		ret = 0;
		break;
        default:
                ath12k_dbg(NULL, ATH12K_DBG_CFG,
                           "Un-supported param: %d\n", param);
                break;
        }

        return ret;
}

/* Find the link-vif of the wdev with the link id to get params
 * If link id is invalid use the default link
 */
int ath12k_vendor_get_wifi_params_extn(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       struct ath12k_wifi_generic_params *params,
				       u64 *value)

{
        struct ieee80211_vif *vif = NULL;
        struct ath12k_vif *ahvif;
        struct ath12k_link_vif *arvif = NULL;
        struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
        struct ath12k_hw *ah = hw->priv;
        int param = params->value;
        int ret = -1;


        vif = wdev_to_ieee80211_vif(wdev);
        if (!vif)
                return -EINVAL;

        ahvif = ath12k_vif_to_ahvif(vif);
       if (!ahvif)
                return -EINVAL;

        ah = ahvif->ah;

        rcu_read_lock();
        if (ah && params->link_id == INVALID_LINK_ID) {
                arvif = &ahvif->deflink;
        } else {
                if (params->link_id < ATH12K_NUM_MAX_LINKS)
                        arvif = rcu_dereference(ahvif->link[params->link_id]);
        }
        if (!arvif) {
                rcu_read_unlock();
                return -EINVAL;
        }
	rcu_read_unlock();
        ath12k_dbg(NULL, ATH12K_DBG_CFG,
                   "vif: %p param: %d if: %d link: %d\n",
                   vif, param, params->ifindex, params->link_id);
        ret = ath12k_vendor_get_arvif_params(arvif, param, value);

        return ret;
}

/* Find the 'ar' radio instance using the radio index of the wiphy
 * to get params
 */
int ath12k_vendor_get_wiphy_params_extn(struct wiphy *wiphy,
					struct ath12k_wifi_generic_params *params,
					u32 *value)
{
        struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
        struct ath12k_hw *ah = hw->priv;
        struct ath12k *ar = NULL;
        int param = params->value;
        int ret = -1;

	if (params->radio_idx >= ah->num_radio) {
		ath12k_err(NULL, "Invalid radio index %u (valid range: 0..%u)\n",
			   params->radio_idx, ah->num_radio ? ah->num_radio - 1 : 0);
		return -ENODATA;
	}

        ar = &ah->radio[params->radio_idx];
 if (!ar) {
                ath12k_err(NULL, "Failed to find ar\n");
                return -ENODATA;
        }

        ath12k_dbg(NULL, ATH12K_DBG_CFG,
                   "ar: %p param: %d if: %d radio: %d\n",
                   ar, param, params->ifindex, params->radio_idx);
        ret = ath12k_vendor_get_radio_params(ar, param, value);

        return ret;
}

int ath12k_vendor_set_wifi_config_extn(struct wiphy *wiphy, struct nlattr **tb,
				       struct wireless_dev *wdev)
{
	int ret;

	ret = ath12k_vendor_set_esp_params_extn(wiphy, tb, wdev);
	if (ret) {
		ath12k_err(NULL, "Failed to set ESP params: %d\n", ret);
		return ret;
	}

	return 0;
}

void ath12k_vendor_get_wiphy_config_handler_extn(struct wiphy *wiphy,
						 struct nlattr **tb,
						 struct sk_buff *skb)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath12k_hw *ah = hw->priv;
	struct ath12k *ar;
	u8 radio_idx = 0;
	u16 thresh_freq;
	int pdev_idx;

	if (tb[QCA_WLAN_VENDOR_ATTR_CONFIG_6GHZ_VLP_PRIORITY_THRESH_FREQ]) {
		if (tb[QCA_WLAN_VENDOR_ATTR_CONFIG_RADIO_INDEX])
			radio_idx = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_CONFIG_RADIO_INDEX]);

		if (radio_idx >= ah->num_radio) {
			ath12k_err(NULL,
				   "Invalid radio index %u (valid range: 0..%u)\n",
				   radio_idx, ah->num_radio ? ah->num_radio - 1 : 0);
			return;
		}

		ar = &ah->radio[radio_idx];
		pdev_idx = ar->pdev_idx;
		if (pdev_idx < 0 || pdev_idx >= MAX_RADIOS) {
			ath12k_err(NULL, "Invalid pdev index %d for radio index %u\n",
				   pdev_idx, radio_idx);
			return;
		}

		thresh_freq = ar->ab->ath12k_base_extn.reg_6ghz_thresh_priority_freq[pdev_idx];
		if (nla_put_u16(skb,
				QCA_WLAN_VENDOR_ATTR_CONFIG_6GHZ_VLP_PRIORITY_THRESH_FREQ,
				thresh_freq)) {
			ath12k_err(NULL, "failed to put VLP priority threshold frequency\n");
			return;
		}
	}
}

int ath12k_vendor_get_rropinfo(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int data_len)
{
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_CONFIG_MAX + 1];
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	u8 radio_id;
	struct ath12k *ar = NULL;
	struct sk_buff *skb;
	struct nlattr *nla_attr;
	struct nlattr *rtplinst_attr;
	bool info_available = false;
	int i;
	int count = 0;

	if (nla_parse(tb, QCA_WLAN_VENDOR_ATTR_CONFIG_MAX, data,
		      data_len, ath12k_rrop_info_policy, NULL)) {
		ath12k_err(NULL,
			   "QCA_WLAN_VENDOR_ATTR_CONFIG_MAX parsing failed");
		return -EINVAL;
	}

	if (!tb[QCA_WLAN_VENDOR_ATTR_CONFIG_RADIO_INDEX])
		return -EINVAL;

	radio_id = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_CONFIG_RADIO_INDEX]);
	if (radio_id >= ah->num_radio) {
		ath12k_err(NULL, "Invalid radio id %d", radio_id);
		return -EINVAL;
	}

	ar = ath12k_ah_to_ar(ah, radio_id);

	if (!ar) {
		ath12k_err(NULL, "RROPINFO: failed to resolve ar from wdev\n");
		return -EINVAL;
	}

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, NLMSG_DEFAULT_SIZE);
	if (!skb) {
		ath12k_err(NULL, "RROPINFO: Reply skb alloc failed\n");
		return -ENOMEM;
	}

	nla_attr = nla_nest_start(skb, QCA_WLAN_VENDOR_ATTR_RROP_INFO_RTPL);
	if (!nla_attr) {
		ath12k_err(NULL, "RROPINFO: nla nest start failed for RTPL info\n");
		kfree_skb(skb);
		return -EINVAL;
	}

	for (i = 0; i < ATH12K_NUM_CHANS; i++) {
		const struct rtplinst_extn *ci = &ar->ar_extn.rtplinst[i];

		if ((ci->primary_freq == 0) &&
		    (ci->txpower_throughput == 0) &&
		    (ci->txpower_range == 0))
			continue;

		info_available = true;

		rtplinst_attr = nla_nest_start(skb, i);
		if (!rtplinst_attr) {
			ath12k_err(NULL, "RROPINFO: nla nest start failed for RTPL instance %d\n", i);
			kfree_skb(skb);
			return -EINVAL;
		}

		if (nla_put_u32(skb, QCA_WLAN_VENDOR_ATTR_RTPLINST_PRIMARY_FREQUENCY,
				ci->primary_freq) ||
		    nla_put_s32(skb, QCA_WLAN_VENDOR_ATTR_RTPLINST_TXPOWER_THROUGHPUT,
				ci->txpower_throughput) ||
		    nla_put_s32(skb, QCA_WLAN_VENDOR_ATTR_RTPLINST_TXPOWER_RANGE,
				ci->txpower_range)) {
			ath12k_err(NULL, "RROPINFO: failed to put RTPL instance %d\n", i);
			kfree_skb(skb);
			return -EINVAL;
		}

		nla_nest_end(skb, rtplinst_attr);
		count++;
		ath12k_dbg(NULL, ATH12K_DBG_CFG, "RROPINFO: rtplinst[%d]: freq=%u th=%d range=%d\n",
			   i, ci->primary_freq, ci->txpower_throughput, ci->txpower_range);
	}

	nla_nest_end(skb, nla_attr);

	if (!info_available) {
		kfree_skb(skb);
		return -EINVAL;
	}

	ath12k_dbg(NULL, ATH12K_DBG_CFG, "RROPINFO: reply sent with %d instances\n", count);
	return cfg80211_vendor_cmd_reply(skb);
}

/**
 * ath12k_vendor_fw_recovery_event - Send a firmware recovery
 * vendor NL80211 event to userspace
 * @wiphy: Pointer to the wireless PHY context
 * @wdev: Pointer to the wireless device context
 * @hw_link_id: Hardware link identifier
 * @event_type: Vendor-defined recovery event type
 * (e.g., ASSERT, RECOVERY_DONE, COREDUMP_COMPLETE)
 *
 * Return: 0 on success or a negative error code on failure.
 */
int ath12k_vendor_fw_recovery_event(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    u8 hw_link_id, int event_type)
{
	struct sk_buff *skb;

	skb = cfg80211_vendor_event_alloc(wiphy, wdev,
					  NLMSG_DEFAULT_SIZE,
					  QCA_NL80211_VENDOR_SUBCMD_WLAN_FW_RECOVERY_INDEX,
					  GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	if (nla_put_u8(skb, QCA_WLAN_VENDOR_FW_RECOVERY_HW_LINK_ID, hw_link_id)) {
		kfree_skb(skb);
		return -EINVAL;
	}
	if (nla_put_u8(skb, QCA_VENDOR_ATTR_FW_RECOVERY_EVENT_TYPE, event_type)) {
		kfree_skb(skb);
		return -EINVAL;
	}

	ath12k_info(NULL, "send event to userspace hw_link_id %d event type is %d\n", hw_link_id, event_type );
	cfg80211_vendor_event(skb, GFP_KERNEL);

	return 0;
}

int ath12k_vendor_set_wiphy_hwaddr_extn(struct wiphy *wiphy,
					struct ath12k_wifi_generic_params *params)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath12k_hw *ah = hw->priv;
	struct ath12k *ar = NULL;

	lockdep_assert_wiphy(wiphy);

	if (params->radio_idx >= ah->num_radio) {
		ath12k_err(NULL, "Invalid radio index\n");
		return -EINVAL;
	}

	if (params->data_len != ETH_ALEN) {
		ath12k_err(NULL, "Invalid radio MAC address len\n");
		return -EINVAL;
	}

	if (!is_valid_ether_addr(params->data)) {
		ath12k_err(NULL, "Invalid radio MAC address\n");
		return -EINVAL;
	}

	ar = &ah->radio[params->radio_idx];

	ath12k_dbg(NULL, ATH12K_DBG_CFG,
		   "ar: %p hwaddr: %pM if: %d radio: %d\n",
		   ar, params->data,
		   params->ifindex, params->radio_idx);

	/* Configure the base MAC address of radio */
	if (wiphy->addresses && params->radio_idx < wiphy->n_addresses) {
		ether_addr_copy(ar->mac_addr, (u8 *)params->data);
		if (ar->ab->pdevs_macaddr_valid)
			ether_addr_copy(ar->pdev->mac_addr, ar->mac_addr);

		if (params->radio_idx == 0) {
			ether_addr_copy(ar->ab->mac_addr, ar->mac_addr);
			SET_IEEE80211_PERM_ADDR(hw, ar->mac_addr);
		}

		ether_addr_copy((u8 *)(&wiphy->addresses[params->radio_idx]),
				ar->mac_addr);
	}

	return 0;
}

/**
 * ath12k_vendor_send_event - Driver helper to
 * send a device-wide firmware recovery vendor event
 * @ab: Pointer to the ath12k base device context
 * @event_flag: Vendor-defined recovery event type
 * (e.g., ASSERT, RECOVERY_DONE, COREDUMP_COMPLETE)
 */
void ath12k_vendor_send_event(struct ath12k_base *ab,
			      u8 event_flag)
{
	struct ath12k_pdev *pdev;
	struct ath12k *ar;

	if (ab->num_radios > 0) {
		/*
		* Use the first radio's pdev context to send the vendor event.
		* This assumes that the recovery event is device-wide and not
		* specific to a particular radio.
		*/
		pdev = &ab->pdevs[0];
		ar = pdev->ar;

		if (ar && ar->ah && ar->ah->hw && ar->ah->hw->wiphy) {
			ath12k_vendor_fw_recovery_event(ar->ah->hw->wiphy, NULL,
							ar->hw_link_id,
							event_flag);
			ath12k_info(ab, "FW recovery vendor event send: event_type: %u link: %d\n",
					event_flag, ar->hw_link_id);
		} else {
			ath12k_err(ab, "Cannot send recovery vendor event: invalid radio context\n");

		}
	} else {
		ath12k_err(ab, "Cannot send recovery vendor event: no radios available\n");
	}
}

/*
 * Derive 6 GHz MBSSID MAC address by modifying only the lower 4 bits
 * of the last octet. The algorithm supports grouping via grp_id/grp_size
 * and keeps the base MAC for grp_id 0, mbssid_idx 0.
 */
void ath12k_compute_6ghz_mbssid_mac(u8 mac[ETH_ALEN], u8 grp_id,
				    u8 grp_size, u8 mbssid_idx)
{
	u8 mask, base_low, block_start, idx_in_block, new_low;

	/* Ensure locally administered and unicast */
	mac[0] &= ~0x01;
	mac[0] |= 0x02;

	mask = (grp_size - 1) & 0x0F;
	base_low = mac[5] & 0x0F;
	block_start = (base_low & ~mask);
	block_start = (block_start + ((grp_id & 0x0F) * grp_size)) & 0x0F;

	idx_in_block = mbssid_idx & mask;

	if ((grp_id & 0x0F) == 0)
		new_low = block_start | ((base_low + idx_in_block) & mask);
	else
		new_low = block_start | idx_in_block;

	mac[5] = (mac[5] & 0xF0) | new_low;
}

void ath12k_compute_link_vif_mac(u8 *mac_addr,
				 const u8 *base,
				 u8 vif_id,
				 bool use_byte0)
{
	u8 cur, add, new;

	ether_addr_copy(mac_addr, base);

	/* For vif_id 0, return base mac; also set LA bit for cohost mode */
	if (!vif_id) {
		if (!use_byte0)
			mac_addr[0] |= 0x02;
		return;
	}

	if (use_byte0) {
		cur = (mac_addr[0] >> 2) & 0x3F;
		add = vif_id & 0x3F;
		new = (cur + add) & 0x3F;
		mac_addr[0] = (mac_addr[0] & ~0xFC) | (new << 2);
	} else {
		cur = mac_addr[5] & 0xF;
		add = vif_id & 0xF;
		new = (cur + add) & 0xF;
		mac_addr[5] = (mac_addr[5] & ~0xF) | new;
	}

	/* Ensure locally administered and unicast */
	mac_addr[0] &= ~0x01;
	mac_addr[0] |= 0x02;
}

#define ATH12K_VENDOR_MAC_CONFIG_FLAG_ALLOC_BSSID 0x00000001
#define ATH12K_VENDOR_MAC_CONFIG_FLAG_FREE_BSSID 0x00000002

int ath12k_vendor_derive_link_bss_addr_extn(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    const void *data,
					    int data_len)
{
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MAX + 1];
	struct sk_buff *skb;
	struct ieee80211_hw *hw;
	struct ath12k_hw *ah;
	struct ath12k *ar = NULL;
	struct ath12k_link_vif *arvif;
	u8 mac_addr[ETH_ALEN], *base_addr;
	u8 radio_idx, vif_id = 0xff;
	u8 grp_id = 0xff, grp_size = 0;
	u32 flags = 0;
	enum nl80211_iftype iftype = NL80211_IFTYPE_UNSPECIFIED;
	u8 pdev_bitmap;
	bool byte0 = true;
	int ret;

	ret = nla_parse(tb, QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MAX,
			data, data_len,
			ath12k_wifi_mac_config_policy, NULL);
	if (ret)
		return ret;

	if (tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_RADIO_INDEX])
		radio_idx = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_RADIO_INDEX]);
	else {
		ath12k_err(NULL, "Missing radio index\n");
		return -EINVAL;
	}

	if (tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_RADIO_BSS_ID])
		vif_id = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_RADIO_BSS_ID]);
	else {
		ath12k_err(NULL, "Missing BSS id\n");
		return -EINVAL;
	}

	/* Optional flags provided by userspace */
	if (tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_FLAGS])
		flags = nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_FLAGS]);

	/* Optional interface type provided by userspace */
	if (tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_IFTYPE]) {
		iftype = nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_IFTYPE]);
		if (iftype > NL80211_IFTYPE_MAX) {
			ath12k_err(NULL, "Invalid iftype\n");
			return -EINVAL;
		}
	}

	if (!wiphy->addresses || radio_idx >= wiphy->n_addresses) {
		ath12k_err(NULL, "No base addresses available\n");
		return -EINVAL;
	}

	if (vif_id > (TARGET_NUM_VDEVS - 1)) {
		ath12k_err(NULL, "Invalid BSS id\n");
		return -EINVAL;
	}

	/* Optional MBSSID group parameters from userspace */
	if (tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MBSSID_GRP_ID])
		grp_id = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MBSSID_GRP_ID]);
	if (tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MBSSID_GRP_SIZE])
		grp_size = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MBSSID_GRP_SIZE]);

	/* Get hw/ah to know which byte index to use for VIF ID */
	hw = wiphy_to_ieee80211_hw(wiphy);
	ah = hw ? hw->priv : NULL;
	ar = ah ? &ah->radio[radio_idx] : NULL;

	if (!ar)
		return -EINVAL;

	base_addr = wiphy->addresses[radio_idx].addr;
	/*
	 * 6 GHz MBSSID: per spec, only the last octet may vary and
	 * only the lower 4 bits encode the BSSID index (0..15).
	 * Preserve upper 4 bits of the last octet and modify lower 4.
	 */
	if (ar->supports_6ghz && iftype == NL80211_IFTYPE_AP && grp_size) {
		u8 group_size = grp_size;

		if (grp_size < 2 || grp_size > 8 ||
		    vif_id >= grp_size || grp_id >= 8) {
			ath12k_err(NULL, "Check group size:%d id:%d idx:%d\n",
				   grp_size, grp_id, vif_id);
			return -EINVAL;
		}

		/* Check grp_size is pwr of 2 or not */
		while (group_size != 1) {
			if (group_size % 2 != 0) {
				ath12k_err(NULL, "group size:%d not pwr of 2\n",
					   grp_size);
				return -EINVAL;
			}
			group_size = group_size / 2;
		}

		ether_addr_copy(mac_addr, base_addr);
		ath12k_compute_6ghz_mbssid_mac(mac_addr, grp_id,
					       grp_size, vif_id);

		vif_id = ((16 + (mac_addr[5] & 0x0F)) - (base_addr[5] & 0x0F)) % 16;
	} else {
		/* Select byte index to encode VIF ID: 0 or 5 */
		if (iftype == NL80211_IFTYPE_AP) {
			pdev_bitmap = ath12k_cfg_get(ar->ab,
					ATH12K_CFG_COHOSTED_BSS_IND_ENABLE);
			if (pdev_bitmap & (1 << ar->pdev_idx))
				byte0 = false;
		}

		ath12k_compute_link_vif_mac(mac_addr,
					    base_addr,
					    vif_id,
					    byte0);
	}

	/* Check and set the used MAC bit only when ALLOC flag is set */
	if (flags & ATH12K_VENDOR_MAC_CONFIG_FLAG_ALLOC_BSSID) {
		if (ar->vendor_mac_used_bitmap & (1 << vif_id)) {
			ath12k_err(NULL, "Requested id %d addr %pM in use\n",
				   vif_id, mac_addr);
			return -EADDRINUSE;
		}

		list_for_each_entry(arvif, &ar->arvifs, list) {
			if (!is_zero_ether_addr(arvif->bssid) &&
			    ether_addr_equal(arvif->bssid, mac_addr)) {
				ath12k_err(NULL, "Requested id %d addr %pM in use\n",
					   vif_id, mac_addr);
				return -EADDRINUSE;
			}
		}

		ar->vendor_mac_used_bitmap |= (1 << vif_id);
		ath12k_err(NULL, "Allocated BSS id %d MAC %pM \n",
			   vif_id, mac_addr);
	} else if (flags & ATH12K_VENDOR_MAC_CONFIG_FLAG_FREE_BSSID) {
		ar->vendor_mac_used_bitmap &= ~(1 << vif_id);
		ath12k_err(NULL, "Freed BSS id %d MAC %pM \n",
			   vif_id, mac_addr);
	}

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, nla_total_size(ETH_ALEN));
	if (!skb)
		return -ENOMEM;

	if (nla_put(skb, QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MAC_ADDR, ETH_ALEN, mac_addr))
		goto nla_put_failure;

	return cfg80211_vendor_cmd_reply(skb);

nla_put_failure:
	kfree_skb(skb);
	return -EMSGSIZE;
}

/*
 * ath12k_get_radio_by_index - Get ath12k instance by radio index
 * @wiphy: Pointer to wiphy
 * @tb: Array of parsed attributes
 *
 * This function retrieves the ath12k instance corresponding to the
 * specified radio index from the parsed attributes. It checks if the
 * radio index attribute is present and valid.
 * Returns a pointer to the ath12k instance or NULL if not found or invalid.
 */
static struct ath12k *ath12k_get_radio_by_index(struct wiphy *wiphy,
                                                struct nlattr *tb[])
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath12k_hw *ah = hw->priv;
	u8 radio_id;

	radio_id = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_CTL_RADIO_INDEX]);
	if (radio_id >= ah->num_radio) {
		ath12k_err(NULL, "Invalid radio id %d", radio_id);
		return NULL;
	}

	return &ah->radio[radio_id];
}

/*
 * ath12k_vendor_ctl_table- Set CTL table It provides
 * simplified power values intended for helping external Auto channel Selection
 * applications compare potential Tx power performance between channels, other
 * operating conditions remaining identical.
 * @wiphy: Pointer to wiphy
 * @wdev: wireless wdev
 * @data: vendor data
 * @data_len: length of data
 * Returns 0 if success.
 */
int ath12k_vendor_ctl_table(struct wiphy *wiphy,
		      	    struct wireless_dev *wdev,
			    const void *data,
			    int data_len)
{
	struct nlattr *vendor[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_MAX + 1];
	struct wmi_pdev_set_ctl_table_cmd_fixed_param *cmd;
	u32 ctl_cmd_len, ctl_tlv_len, band, len;
	struct ath12k_wmi_pdev *wmi;
	const uint8_t *ctl_data;
	uint32_t *ctl_array;
	struct sk_buff *skb;
	struct wmi_tlv *tlv;
	struct ath12k *ar;
	void *ptr;
	int ret;

	if (nla_parse(vendor, QCA_WLAN_VENDOR_ATTR_CTL_TABLE_MAX,
		      data, data_len,
		      ath12k_ctl_table_policy,
		      NULL)) {
		ath12k_err(NULL, "Failed to parse vendor attributes");
		return -1;
	}

	if (!vendor[QCA_WLAN_VENDOR_ATTR_CTL_RADIO_INDEX] ||
	    !vendor[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_LENGTH] ||
	    !vendor[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_BAND] ||
	    !vendor[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_DATA]) {
		ath12k_err(NULL, "Invalid input");
		return -1;
	}

	ar = ath12k_get_radio_by_index(wiphy, vendor);
	if (!ar) {
		ath12k_err(NULL, "ar is NULL in %s", __func__);
		return -ENODATA;
	}

	ctl_cmd_len = nla_get_u16(vendor[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_LENGTH]);
	ctl_cmd_len += sizeof(uint32_t);
	ctl_tlv_len = TLV_HDR_SIZE + roundup(ctl_cmd_len, sizeof(u32));
	len = sizeof(*cmd) + ctl_tlv_len;
	wmi = ar->wmi;
	skb = ath12k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_set_ctl_table_cmd_fixed_param*)skb->data;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PDEV_SET_CTL_TABLE_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->pdev_id = cpu_to_le32(ar->pdev->pdev_id);
	cmd->ctl_len = cpu_to_le32(ctl_cmd_len);

	ptr = skb->data + sizeof(*cmd);
	tlv = ptr;
	tlv->header = ath12k_wmi_tlv_hdr(WMI_TAG_ARRAY_UINT32, ctl_cmd_len);
	ptr += TLV_HDR_SIZE;
	ctl_array = (uint32_t *)ptr;
	band = nla_get_u32(vendor[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_BAND]);
	ctl_data = nla_data(vendor[QCA_WLAN_VENDOR_ATTR_CTL_TABLE_DATA]);
	ctl_array[0] = cpu_to_le32(band);
	memcpy(&ctl_array[1], ctl_data, ctl_cmd_len);

	ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
		   "WMI CTL TABLE: pdev %d ctl len %d\n",
		   cmd->pdev_id, cmd->ctl_len);

	ret = ath12k_wmi_cmd_send(wmi, skb, WMI_PDEV_SET_CTL_TABLE_CMDID);
	if (ret) {
		ath12k_warn(ar->ab, "Failed to send WMI_PDEV_SET_CTL_TABLE_CMDID\n");
		dev_kfree_skb(skb);
	}

	return 0;
}

int ath12k_vendor_dcs_config_handler(struct wiphy *wihpy,
				     struct wireless_dev *wdev,
				     const void *data,
				     int data_len)
{
	int ret, link_id = 0, cmd_type, dcs_enable;
	struct ath12k *ar;
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_DCS_MAX + 1];
	struct dcs_wlan_intr_user_cfg *cfg;

	ret = nla_parse(tb, QCA_WLAN_VENDOR_ATTR_DCS_MAX, data, data_len,
			ath12k_vendor_dcs_config_policy, NULL);

	if (ret) {
		ath12k_err(NULL, "Invalid attribute in dcs_handler %d\n", ret);
		return ret;
	}

	if (wdev->valid_links) { /* MLO case */
		if (!tb[QCA_WLAN_VENDOR_ATTR_DCS_LINK_ID])
			return -EINVAL;
		link_id = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_DCS_LINK_ID]);
		if (!(wdev->valid_links & BIT(link_id)))
			return -ENOLINK;
	} else { /* NON-MLO case */
		if (tb[QCA_WLAN_VENDOR_ATTR_DCS_LINK_ID])
			return -EINVAL;
		link_id = 0;
	}

	if (!tb[QCA_WLAN_VENDOR_ATTR_DCS_CMD_TYPE])
		return -EINVAL;

	cmd_type = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_DCS_CMD_TYPE]);

	if (!tb[QCA_WLAN_VENDOR_ATTR_DCS_ENABLE])
		return -EINVAL;

	dcs_enable = nla_get_u16(tb[QCA_WLAN_VENDOR_ATTR_DCS_ENABLE]);

	if (dcs_enable & ~ATH12K_DCS_VALID_INTF_BITMAP) {
		ath12k_err(NULL, "Invalid DCS enable bitmap: 0x%x\n", dcs_enable);
		return -EINVAL;
	}

	ar = ath12k_get_ar_from_wdev(wdev, link_id);
	if (!ar)
		return -ENODATA;

	ath12k_mac_set_intf_detect(ar, dcs_enable);

	/* Store thresholds into wlan_intf_extn.dcs_cfg directly (hostapd manages defaults) */
	if (cmd_type == ATH12K_WLAN_INTR_CMD) {
		cfg = &ar->ar_extn.wlan_intf.dcs_cfg;

		if (tb[QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_DETECTION_THRESHOLD])
			cfg->intr_detection_threshold =
				nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_DETECTION_THRESHOLD]);
		if (tb[QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_DETECTION_WINDOW])
			cfg->sample_size =
				nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_DETECTION_WINDOW]);
		if (tb[QCA_WLAN_VENDOR_ATTR_DCS_PHY_ERR_PENALTY])
			cfg->phyerr_penalty =
				nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_DCS_PHY_ERR_PENALTY]);
		if (tb[QCA_WLAN_VENDOR_ATTR_DCS_PHY_ERR_THRESHOLD])
			cfg->phyerr_threshold =
				nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_DCS_PHY_ERR_THRESHOLD]);
		if (tb[QCA_WLAN_VENDOR_ATTR_DCS_RADAR_ERR_THRESHOLD])
			cfg->radarerr_threshold =
				nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_DCS_RADAR_ERR_THRESHOLD]);
		if (tb[QCA_WLAN_VENDOR_ATTR_DCS_TX_ERR_THRESHOLD])
			cfg->txerr_threshold =
				nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_DCS_TX_ERR_THRESHOLD]);
		if (tb[QCA_WLAN_VENDOR_ATTR_DCS_COCHANNEL_INTERFERENCE_THRESHOLD])
			cfg->coch_intr_threshold =
				nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_DCS_COCHANNEL_INTERFERENCE_THRESHOLD]);
		if (tb[QCA_WLAN_VENDOR_ATTR_DCS_MAX_CU])
			cfg->user_max_cu =
				nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_DCS_MAX_CU]);
	}
	return 0;
}

int ath12k_get_dp_rx_scan_radio_stats_len(void)
{
	struct ath12k_dp_rx_scan_radio_stats stats;
	int total_size = 0;

	total_size += nla_total_size(sizeof(stats.rx_ok_bytes));
	total_size += nla_total_size(sizeof(stats.rx_err_bytes));
	total_size += nla_total_size(sizeof(stats.rx_ok_pkts));
	total_size += nla_total_size(sizeof(stats.rx_err_pkts));
	total_size += nla_total_size(sizeof(stats.rx_mgmt_pkts));
	total_size += nla_total_size(sizeof(stats.rx_ctrl_pkts));
	total_size += nla_total_size(sizeof(stats.rx_data_pkts));
	total_size = nla_total_size_nested(total_size);
	return total_size;
}

int ath12k_fill_rx_scan_radio_stats(
		struct sk_buff *vendor_event,
		struct ath12k_dp_rx_scan_radio_stats *rx_scan_radio_stats)
{
	struct nlattr *attr;

	if (!rx_scan_radio_stats)
		return -EINVAL;

	attr = nla_nest_start(vendor_event,
			      QCA_VENDOR_ATTR_WLAN_TELEMETRY_RX_MON_SCAN_STATS);
	if (!attr) {
		ath12k_err(NULL, "nla nest failure: Device reo error");
		return -EINVAL;
	}

	if (nla_put_u32(vendor_event, QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_OK_PKTS,
			rx_scan_radio_stats->rx_ok_pkts)) {
		ath12k_err(NULL, "nla put failed: rx_scan_radio stats %d",
			   QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_OK_PKTS);
		nla_nest_cancel(vendor_event, attr);
		return -EMSGSIZE;
	}

	if (nla_put_u64_64bit(vendor_event,
			      QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_OK_BYTES,
			      rx_scan_radio_stats->rx_ok_bytes, NL80211_ATTR_PAD)) {
		ath12k_err(NULL, "nla put failed: rx_scan_radio stats %d",
			   QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_OK_BYTES);
		nla_nest_cancel(vendor_event, attr);
		return -EMSGSIZE;
	}

	if (nla_put_u32(vendor_event, QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_ERR_PKTS,
			rx_scan_radio_stats->rx_err_pkts)) {
		ath12k_err(NULL, "nla put failed: rx_scan_radio stats %d",
			   QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_ERR_PKTS);
		nla_nest_cancel(vendor_event, attr);
		return -EMSGSIZE;
	}

	if (nla_put_u64_64bit(vendor_event,
			      QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_ERR_BYTES,
			      rx_scan_radio_stats->rx_err_bytes, NL80211_ATTR_PAD)) {
		ath12k_err(NULL, "nla put failed: rx_scan_radio stats %d",
			   QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_ERR_BYTES);
		nla_nest_cancel(vendor_event, attr);
		return -EMSGSIZE;
	}

	if (nla_put_u32(vendor_event, QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_MGMT_PKTS,
			rx_scan_radio_stats->rx_mgmt_pkts)) {
		ath12k_err(NULL, "nla put failed: rx_scan_radio stats %d",
			   QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_MGMT_PKTS);
		nla_nest_cancel(vendor_event, attr);
		return -EMSGSIZE;
	}

	if (nla_put_u32(vendor_event, QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_CTRL_PKTS,
			rx_scan_radio_stats->rx_ctrl_pkts)) {
		ath12k_err(NULL, "nla put failed: rx_scan_radio stats %d",
			   QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_CTRL_PKTS);
		nla_nest_cancel(vendor_event, attr);
		return -EMSGSIZE;
	}

	if (nla_put_u32(vendor_event, QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_DATA_PKTS,
			rx_scan_radio_stats->rx_data_pkts)) {
		ath12k_err(NULL, "nla put failed: rx_scan_radio stats %d",
			   QCA_WLAN_VENDOR_ATTR_RX_MON_SCAN_STATS_RX_DATA_PKTS);
		nla_nest_cancel(vendor_event, attr);
		return -EMSGSIZE;
	}

	nla_nest_end(vendor_event, attr);

	return 0;
}

int ath12k_vendor_dcs_sim_handler(struct wiphy *wihpy,
				  struct wireless_dev *wdev,
				  const void *data,
				  int data_len)
{
	u16 intf_type_bitmap;
	u32 intf_bitmap = 0;
	int ret, link_id = 0;
	struct ath12k *ar;
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_DCS_SIM_MAX + 1];

	ath12k_err(NULL, "Inside ath12k_vendor_dcs_sim_handler");

	if (!wdev)
		return -EINVAL;

	ret = nla_parse(tb, QCA_WLAN_VENDOR_ATTR_DCS_SIM_MAX, data, data_len,
			ath12k_vendor_dcs_sim_policy, NULL);

	if (ret) {
		ath12k_err(NULL, "Invalid attribute in dcs_sim handler %d\n", ret);
		return ret;
	}

	if (wdev->valid_links) { /* MLO case */
		if (!tb[QCA_WLAN_VENDOR_ATTR_DCS_SIM_LINK_ID])
			return -EINVAL;
		link_id = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_DCS_SIM_LINK_ID]);
		if (!(wdev->valid_links & BIT(link_id)))
			return -ENOLINK;
	} else { /* NON-MLO case */
		if (tb[QCA_WLAN_VENDOR_ATTR_DCS_SIM_LINK_ID])
			return -EINVAL;
		link_id = 0;
	}

	if (!tb[QCA_WLAN_VENDOR_ATTR_DCS_SIM_TYPE])
		return -EINVAL;

	intf_type_bitmap = nla_get_u16(tb[QCA_WLAN_VENDOR_ATTR_DCS_SIM_TYPE]);

	if (tb[QCA_WLAN_VENDOR_ATTR_DCS_SIM_INTERFERENCE_BITMAP])
		intf_bitmap = nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_DCS_SIM_INTERFERENCE_BITMAP]);

	rcu_read_lock();
	ar = ath12k_get_ar_from_wdev(wdev, link_id);

	if (!ar) {
		ath12k_err(NULL, "Invalid ar from wdev\n");
		goto exit;
	}

	ath12k_vendor_send_intf_notify_extn(ar, intf_type_bitmap, intf_bitmap);
	rcu_read_unlock();
	return 0;
exit:
	rcu_read_unlock();
	return -1;
}

static int ath12k_vendor_send_reg_params_update(struct wiphy *wiphy,
						u8 cmd, u8 opclass,
						u16 hw_value,
						int max_reg_power,
						u8 num_opclasses,
						u8 *opclass_lst,
						u8 *chansize_lst,
						u8 **channel_lists)
{
	struct sk_buff *skb;
	struct nlattr *nla_attr;
	int i, j, temp;

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, NLMSG_DEFAULT_SIZE);
	if (!skb)
		return -ENOMEM;

	if (cmd == QCA_WLAN_VENDOR_REG_PARAMS_NUM_OPCLASS) {
		if (nla_put_u8(skb,
			       QCA_WLAN_VENDOR_ATTR_REG_PARAMS_UPDATE_NUM_OPCLASS,
			       num_opclasses))
			goto fail;
		else
			ath12k_dbg(NULL, ATH12K_DBG_REG,
				   "Number of opclasses is %d",
				   num_opclasses);
	} else if (cmd == QCA_WLAN_VENDOR_REG_PARAMS_CHAN_NUM) {
		if (nla_put_u8(skb,
			       QCA_WLAN_VENDOR_ATTR_REG_PARAMS_UPDATE_CHAN_NUM,
			       hw_value))
			goto fail;
		else
			ath12k_dbg(NULL, ATH12K_DBG_REG,
				   "hw_value is %d", hw_value);
	} else if (cmd == QCA_WLAN_VENDOR_REG_PARAMS_TXPOWER) {
		if (nla_put_s32(skb,
				QCA_WLAN_VENDOR_ATTR_REG_PARAMS_UPDATE_TXPOWER,
				max_reg_power))
			goto fail;
		else
			ath12k_dbg(NULL, ATH12K_DBG_REG,
				   "max_reg_power is %d", max_reg_power);
	} else if (cmd == QCA_WLAN_VENDOR_REG_PARAMS_OPCLASS_LIST) {
		temp = QCA_WLAN_VENDOR_ATTR_REG_PARAMS_UPDATE_OPCLASS_LIST;
		nla_attr = nla_nest_start(skb, temp);
		if (!nla_attr)
			goto fail;

		for (i = 0; i < num_opclasses; i++) {
			if (nla_put_u8(skb, i, opclass_lst[i]))
				goto fail;
			else
				ath12k_dbg(NULL, ATH12K_DBG_REG,
					   "opclasslist[%d] is %d",
					   i, opclass_lst[i]);
		}
		nla_nest_end(skb, nla_attr);
	}

	if (opclass) {
		for (i = 0; i < num_opclasses; i++) {
			if (opclass == opclass_lst[i]) {
				j = i;
				break;
			}
		}

		if (i == num_opclasses)
			goto fail;

		temp = QCA_WLAN_VENDOR_ATTR_REG_PARAMS_UPDATE_OPCLASS_CHAN;
		nla_attr = nla_nest_start(skb, temp);
		if (!nla_attr)
			goto fail;

		for (i = 0; i < chansize_lst[j]; i++) {
			if (nla_put_u8(skb, i, channel_lists[j][i]))
				goto fail;
			else
				ath12k_dbg(NULL, ATH12K_DBG_REG,
					   "channellist[%d] is %d",
					   i, channel_lists[j][i]);
		}
		nla_nest_end(skb, nla_attr);
	}

	return cfg80211_vendor_cmd_reply(skb);

fail:
	kfree_skb(skb);
	return -EMSGSIZE;
}

int ath12k_vendor_reg_params_handler(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     const void *data,
				     int data_len)
{
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_MAX + 1];
	enum nl80211_band band;
	int ret_val = 0;
	u8 cmd = 0;
	u8 opclass = 0;
	u8 link_id, num_op = 0;
	u8 *op_lst  = NULL, *chansize = NULL, **chan_lst = NULL;
	u16 hw_value = 0;
	int max_reg_power = 0;
	int curr_reg_power;
	bool need_clean = false;

	if (!wdev || !data || !data_len) {
		ath12k_err(NULL, "Invalid input to REG PARAMS handler");
		return -EINVAL;
	}

	if (nla_parse(tb, QCA_WLAN_VENDOR_ATTR_REG_PARAMS_MAX, data,
		      data_len, ath12k_reg_params_policy, NULL)) {
		ath12k_err(NULL, "Failed to parse REG PARAMS vendor attributes");
		return -EINVAL;
	}

	if (!tb[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_LINKID]) {
		ath12k_err(NULL, "Missing mandatory Link_id attributes");
		return -EINVAL;
	}

	link_id = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_LINKID]);

	if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return -EINVAL;

	if (!wdev->links[link_id].ap.chandef.chan) {
		ath12k_err(NULL, "No channel defined for link_id %u\n", link_id);
		return -EINVAL;
	}

	band = wdev->links[link_id].ap.chandef.chan->band;
	curr_reg_power = wdev->links[link_id].ap.chandef.chan->max_reg_power;

	if (tb[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_CMD])
		cmd = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_CMD]);

	if (tb[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_OPCLASS_CHAN])
		opclass = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_REG_PARAMS_OPCLASS_CHAN]);

	if ((cmd >= QCA_WLAN_VENDOR_REG_PARAMS_NUM_OPCLASS &&
	     cmd <= QCA_WLAN_VENDOR_REG_PARAMS_OPCLASS_LIST) || opclass) {
		if (cmd == QCA_WLAN_VENDOR_REG_PARAMS_CHAN_NUM) {
			hw_value = wdev->links[link_id].ap.chandef.chan->hw_value;
		}

		if (cmd == QCA_WLAN_VENDOR_REG_PARAMS_TXPOWER) {
			max_reg_power = curr_reg_power;
		}

		if (cmd == QCA_WLAN_VENDOR_REG_PARAMS_NUM_OPCLASS ||
		    cmd == QCA_WLAN_VENDOR_REG_PARAMS_OPCLASS_LIST ||
		    opclass) {
			ret_val = ath12k_get_opclasses_and_channels(NULL,
								    &num_op,
								    &op_lst,
								    &chansize,
								    &chan_lst,
								    band);
			need_clean = true;
			if (ret_val) {
				ath12k_err(NULL,
					   "Failed to retrieve opclass list and channel");
				goto clean;
			}
		}

		ret_val = ath12k_vendor_send_reg_params_update(wiphy, cmd, opclass,
							       hw_value,
							       max_reg_power,
							       num_op, op_lst,
							       chansize,
							       chan_lst);

	} else {
		ret_val = -EINVAL;
	}

	if (need_clean)
		goto clean;
	return ret_val;

clean:
	ath12k_free_opclasses_and_channels(num_op, op_lst,
					   chansize, chan_lst);
	return ret_val;
}

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
static const char *ath12k_phymode_str(enum wmi_phy_mode mode)
{
	switch (mode) {
	case MODE_11A:
		return "11a";
	case MODE_11G:
		return "11g";
	case MODE_11B:
		return "11b";
	case MODE_11GONLY:
		return "11gonly";
	case MODE_11NA_HT20:
		return "11na-ht20";
	case MODE_11NG_HT20:
		return "11ng-ht20";
	case MODE_11NA_HT40:
		return "11na-ht40";
	case MODE_11NG_HT40:
		return "11ng-ht40";
	case MODE_11AC_VHT20:
		return "11ac-vht20";
	case MODE_11AC_VHT40:
		return "11ac-vht40";
	case MODE_11AC_VHT80:
		return "11ac-vht80";
	case MODE_11AC_VHT160:
		return "11ac-vht160";
	case MODE_11AC_VHT80_80:
		return "11ac-vht80+80";
	case MODE_11AC_VHT20_2G:
		return "11ac-vht20-2g";
	case MODE_11AC_VHT40_2G:
		return "11ac-vht40-2g";
	case MODE_11AC_VHT80_2G:
		return "11ac-vht80-2g";
	case MODE_11AX_HE20:
		return "11ax-he20";
	case MODE_11AX_HE40:
		return "11ax-he40";
	case MODE_11AX_HE80:
		return "11ax-he80";
	case MODE_11AX_HE80_80:
		return "11ax-he80+80";
	case MODE_11AX_HE160:
		return "11ax-he160";
	case MODE_11AX_HE20_2G:
		return "11ax-he20-2g";
	case MODE_11AX_HE40_2G:
		return "11ax-he40-2g";
	case MODE_11AX_HE80_2G:
		return "11ax-he80-2g";
	case MODE_11BE_EHT20:
		return "11be-eht20";
	case MODE_11BE_EHT40:
		return "11be-eht40";
	case MODE_11BE_EHT80:
		return "11be-eht80";
	case MODE_11BE_EHT80_80:
		return "11be-eht80+80";
	case MODE_11BE_EHT160:
		return "11be-eht160";
	case MODE_11BE_EHT160_160:
		return "11be-eht160+160";
	case MODE_11BE_EHT320:
		return "11be-eht320";
	case MODE_11BE_EHT20_2G:
		return "11be-eht20-2g";
	case MODE_11BE_EHT40_2G:
		return "11be-eht40-2g";
	case MODE_11BN_UHR20:
		return "11bn-uhr20";
	case MODE_11BN_UHR40:
		return "11bn-uhr40";
	case MODE_11BN_UHR80:
		return "11bn-uhr80";
	case MODE_11BN_UHR80_80:
		return "11bn-uhr80+80";
	case MODE_11BN_UHR160:
		return "11bn-uhr160";
	case MODE_11BN_UHR160_160:
		return "11bn-uhr160+160";
	case MODE_11BN_UHR320:
		return "11bn-uhr320";
	case MODE_11BN_UHR20_2G:
		return "11bn-uhr20-2g";
	case MODE_11BN_UHR40_2G:
		return "11bn-uhr40-2g";
	case MODE_UNKNOWN:
		/* skip */
		break;
	}

	return "<unknown>";
}

/**
 * ath12k_vendor_mesh_peer_dump - Dump mesh peer information
 *
 * @wiphy: wiphy pointer
 * @vif: virtual interface pointer
 * @peer_mld_mac: MAC address of specific peer to dump, or NULL for all peers
 *
 * Prints the MAC address, AID, and all mesh_peer_info fields for a specific
 * mesh peer (when peer_mld_mac is non-NULL) or for every mesh peer on the
 * interface (when peer_mld_mac is NULL).
 *
 * Return: 0 on success, -ENOENT if the specified peer was not found,
 *         or a negative error code on other failures.
 */
static int ath12k_vendor_mesh_peer_dump(struct wiphy *wiphy,
					struct ieee80211_vif *vif,
					const u8 *peer_mld_mac)
{
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	struct mesh_peer_info *peer_info;
	bool found = false;

	if (!vif)
		return -EINVAL;

	sdata = vif_to_sdata(vif);

	if (sdata->wdev.vap_submode != IEEE80211_EXTN_VAP_SUBMODE_MESH) {
		ath12k_err(NULL, "mesh_peer_dump: not a mesh VAP\n");
		return -EINVAL;
	}

	lockdep_assert_wiphy(wiphy);

	list_for_each_entry_rcu(sta, &sdata->local->sta_list, list) {
		if (sta->sdata != sdata)
			continue;

		if (!sta->sta.sta_extn.peer_info.is_mesh_peer)
			continue;

		/* If a specific MAC is provided, skip non-matching peers */
		if (peer_mld_mac && !ether_addr_equal(sta->sta.addr, peer_mld_mac))
			continue;

		peer_info = &sta->sta.sta_extn.peer_info;
		found = true;

		pr_info("\t----------------------------------------------------------\n");
		pr_info("\tMesh Peer MAC : %pM\n", sta->sta.addr);
		pr_info("\tAID           : %u\n", sta->sta.aid);
		pr_info("\t  -- Peer Info --\n");
		pr_info("\t  is_mesh_peer   : %s\n",
		       peer_info->is_mesh_peer ? "true" : "false");
		pr_info("\t  phymode        : %s\n",
		       ath12k_phymode_str(peer_info->mesh_peer_phymode));
		pr_info("\t  nss            : %u\n",
		       peer_info->mesh_peer_nss);
		pr_info("\t  -- Beacon Tracking --\n");
		pr_info("\t  miss_count     : %u\n",
		       peer_info->beacon_miss_count);
		{
			unsigned long now = jiffies;
			unsigned long delta_ms = peer_info->last_beacon_time ?
						 jiffies_to_msecs(now - peer_info->last_beacon_time) : 0;
			pr_info("\t  current_time   : %lu jiffies\n", now);
			pr_info("\t  last_beacon    : %lu jiffies\n",
			       peer_info->last_beacon_time);
			pr_info("\t  beacon_age     : %lu sec\n", delta_ms / 1000);
		}
		pr_info("\t  -- Beacon Intersection --\n");
		pr_info("\t  done           : %s\n",
		       peer_info->intersection_done ? "true" : "false");
		pr_info("\t  attempts       : %u\n",
		       peer_info->intersect_attempts);
		pr_info("\t  successes      : %u\n",
		       peer_info->intersect_successes);
		pr_info("\t  failures       : %u\n",
		       peer_info->intersect_failures);
		pr_info("\t----------------------------------------------------------\n");

		if (peer_mld_mac)
			break;
	}

	if (peer_mld_mac && !found) {
		ath12k_err(NULL, "mesh_peer_dump: peer %pM not found\n",
			   peer_mld_mac);
		return -ENOENT;
	}

	return 0;
}

int ath12k_vendor_mesh_params_config_extn(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     struct ath12k_wifi_generic_params *params)
{
	struct ieee80211_vif *vif = NULL;
	struct ieee80211_mesh_config_params_extn *mesh_params = NULL;
	int ret = -1;

	lockdep_assert_wiphy(wiphy);

	if (!params) {
		ath12k_err(NULL, "Mesh config params is NULL\n");
		return -EINVAL;
	}

	if (!params->data || !params->data_len) {
		ath12k_err(NULL, "Missing data for Mesh peer config\n");
		return -EINVAL;
	}

	if (params->data_len < sizeof(struct ieee80211_mesh_config_params_extn)) {
		ath12k_err(NULL, "Insufficient data length for mesh config\n");
		return -EINVAL;
	}

	vif = wdev_to_ieee80211_vif(wdev);
	if (!vif) {
		ath12k_err(NULL, "vif pointer is NULL\n");
		return -EINVAL;
	}

	mesh_params = (struct ieee80211_mesh_config_params_extn *)params->data;

	switch (mesh_params->cmd_type) {
	case ATH12K_EXTN_MESH_CONF_ADD_LOCAL_PEER:
		ret = ieee80211_vendor_mesh_add_local_peer(wiphy, vif, mesh_params);
		break;
	case ATH12K_EXTN_MESH_CONF_ALLOW_DATA:
		ret = ieee80211_vendor_mesh_authorize_peer(wiphy, vif, mesh_params->peer_mld_mac);
		break;
	case ATH12K_EXTN_MESH_CONF_DEL_LOCAL_PEER:
		ret = ieee80211_vendor_mesh_delete_local_peer(wiphy, vif, mesh_params->peer_mld_mac);
		break;
	case ATH12K_EXTN_MESH_CONF_SET_PEER_TIMEOUT_CNT:
		ret = ieee80211_vendor_mesh_set_peer_timeout_cnt(wiphy, vif,
								 mesh_params->mesh_peer_timeout_cnt);
		break;
	case ATH12K_EXTN_MESH_CONF_PEER_TIMEOUT_EN:
		ret = ieee80211_vendor_mesh_peer_timeout_en(wiphy, vif,
							    mesh_params->
							    peer_cleanup_timer_enable);
		break;
	case ATH12K_EXTN_MESH_CONF_PEER_DUMP:
		ret = ath12k_vendor_mesh_peer_dump(wiphy, vif,
						   is_zero_ether_addr(mesh_params->peer_mld_mac) ?
						   NULL : mesh_params->peer_mld_mac);
		break;
	default:
		ath12k_err(NULL, "Invalid cmd type: %d\n", mesh_params->cmd_type);
		return -EINVAL;
	}

	return ret;
}
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */
