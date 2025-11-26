
/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <net/netlink.h>
#include <net/mac80211.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include "../core.h"
#include "ath12k_cmn_extn.h"
#include "vendor_extn.h"
#include "../net/wireless/core.h"
#include "../debug.h"
#include "../mac.h"


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

int ath12k_vendor_send_rule_config_notify(struct ieee80211_vif *vif, u8 *mac_addr)
{
	struct wireless_dev *wdev;
	struct sk_buff *skb;

	wdev = ieee80211_vif_to_wdev(vif);
	if (!wdev)
		return -EINVAL;

	skb = cfg80211_vendor_event_alloc(wdev->wiphy, wdev, NLMSG_DEFAULT_SIZE,
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
	    (nla_len(tb[QCA_WLAN_VENDOR_ATTR_SCS_RULE_CONFIG_DST_MAC_ADDR]) == ETH_ALEN)) {
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

/* Set link-vif level parameters
 * set 'reload' to true to send reload event to userspace */
static int ath12k_vendor_set_arvif_params(struct ath12k *ar, u32 param,
                                          u32 value, bool *reload)
{
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

		/*
		 * Firmware accounts the very first frame (without retry bit set) also
		 * in the retry count. Increase the user provided tx retry count by 1
		 * to match the number of tx frames with retry bit set in the air.
		 */
		ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_MGMT_RETRY_LIMIT,
						value + 1, ar->pdev->pdev_id);
		if (ret) {
			ath12k_err(ar->ab, "Failed to set tx retry limit for mgmt frame: %d\n",
				   ret);
			return -EINVAL;
		}
		ar->mgmt_tx_retry_limit = value;
		break;
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

        ret = ath12k_vendor_set_arvif_params(ar, param, value, &reload);

        if (!ret && reload)
                ath12k_vendor_event_iface_reload_link(wiphy, wdev, params->link_id);

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

/* Get link-vif level parameters */
static int ath12k_vendor_get_arvif_params(struct ath12k_link_vif *arvif,
                                          u32 param, u32 *value)
{
        int ret = -1;

        switch (param) {
        case QCA_WLAN_VENDOR_VDEV_PARAM_TEST:
                *value = 0;
                ret = 0;
                break;
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
				       u32 *value)

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
        ath12k_dbg(NULL, ATH12K_DBG_CFG,
                   "vif: %p param: %d if: %d link: %d\n",
                   vif, param, params->ifindex, params->link_id);

        ret = ath12k_vendor_get_arvif_params(arvif, param, value);
        rcu_read_unlock();

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
