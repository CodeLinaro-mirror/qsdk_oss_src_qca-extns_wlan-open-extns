
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
#include "esp_extn.h"


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

/*
 * Check if at least one enabled channel exists in the given band
 * based on the current regulatory context of the wiphy.
 */
static bool ath12k_reg_is_band_present(struct ath12k *ar, enum nl80211_band band)
{
        struct ieee80211_hw *hw = ar->ah->hw;
        struct ieee80211_supported_band **bands;
        int i;

        if (!hw || band < 0 || band >= NUM_NL80211_BANDS)
                return false;

        bands = hw->wiphy->bands;
        if (!bands || !bands[band])
                return false;

        /* Ensure ath12k has sbands populated and matches mac80211 bands */
        if (!ar->mac.sbands[band].channels)
                return false;

        for (i = 0; i < bands[band]->n_channels; i++) {
                const struct ieee80211_channel *ch = &bands[band]->channels[i];

                if (!(ch->flags & IEEE80211_CHAN_DISABLED))
                        return true;
        }
        return false;
}

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
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_SET_HW_MODE_CMDID to firmware");
                         } else {
                                 ar->radio_cfg.current_mode = value;
                         }
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_TXPOWER_LIMIT2G:
                 /*
                  * Check if the current regulatory channel list has any valid channel in the specified
                  * regulatory band, at least one channel in that band should be present/enabled in the
                  * current regulatory context, returns true.
                  */
                 if (ath12k_reg_is_band_present(ar, (enum nl80211_band)value)) {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_TXPOWER_LIMIT2G,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_TXPOWER_LIMIT2G to firmware");
                         } else {
                                 ar->radio_cfg.txpowlimit2G = value;
                         }
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_TXPOWER_LIMIT5G:
                 if (ath12k_reg_is_band_present(ar, (enum nl80211_band)value)) {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_TXPOWER_LIMIT5G,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_TXPOWER_LIMIT5G to firmware");
                         } else {
                                 ar->radio_cfg.txpowlimit5G = value;
				  }
                 }
                 break;
#define ANTENNA_GAIN_2G_MASK    0x0
#define ANTENNA_GAIN_5G_MASK    0x8000
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANTENNA_GAIN_2G:
                 if (value >= 0 && value <= 30) {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANTENNA_GAIN,
                                         (value | ANTENNA_GAIN_2G_MASK), ar->pdev->pdev_id);
                         if (ret < 0) {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANTENNA_GAIN to firmware");
                         } else {
                                 ar->radio_cfg.antenna_gain_2g = value;
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
                         if (ret < 0) {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANTENNA_GAIN to firmware");
                         } else {
                                 ar->radio_cfg.antenna_gain_5g = value;
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
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANI_OFDM_LEVEL to firmware");
                 } else {
                         ar->radio_cfg.ofdem_level = value;
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_TXPOWER_SCALE:
                 if (WMI_HOST_TP_SCALE_MAX <= value && value <= WMI_HOST_TP_SCALE_MIN) {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_TXPOWER_SCALE,
                                         value, ar->pdev->pdev_id);
                         if(!ret) {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_TXPOWER_SCALE to firmware");
                         } else {
                                 ar->radio_cfg.txpower_scale = value;
                         }
                 } else {
                         ret = -EINVAL;
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_RX_FILTER:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_RX_FILTER,
                                 value, ar->pdev->pdev_id);
                 if(!ret) {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_RX_FILTER to firmware");
                 } else {
                         ar->radio_cfg.rx_filter = value;
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_BLOCK_INTERBSS:
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
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_DISABLE_RESET_CMDID to firmware");
                         } else {
                                 ar->radio_cfg.fw_disable_reset = value;
                         }
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_PPDU_DURATION_CMDID:
                /* Get PPDU max duration based on countryISO. */
                /* Get PPDU min duration from IC, put sanity on value based on min and max. */
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_PPDU_DURATION_CMDID,
                                 value, ar->pdev->pdev_id);
                 if (!ret) {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_PPDU_DURATION_CMDID to firmware");
                 } else {
                         ar->radio_cfg.ppdu_dur = value;
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_TXBF_SOUND_PERIOD_CMDID:
                 if (value < 10 || value > 10000) {
                         ret = -EINVAL;
                 } else {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_TXBF_SOUND_PERIOD_CMDID,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_TXBF_SOUND_PERIOD_CMDID to firmware");
                         } else {
                                 ar->radio_cfg.txbf_sound_period = value;
                         }
			 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_PROMISC_MODE_CMDID:
                 if (value == ar->radio_cfg.promisc_mode) {
                         ret = 0;
                 } else {
                         ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_PROMISC_MODE_CMDID,
                                         value, ar->pdev->pdev_id);
                         if (!ret) {
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_PROMISC_MODE_CMDID to firmware");
                         } else {
                                 ar->radio_cfg.promisc_mode = value;
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
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_BURST_MODE_CMDID to firmware");
                         } else {
                                 ar->radio_cfg.burst_mode = value;
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
                                 ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_MCAST_BCAST_ECHO to firmware");
                         } else {
                                 ar->radio_cfg.mcast_bcast_echo = (u_int8_t)value;
                         }
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANT_PLZN:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANT_PLZN, value, ar->pdev->pdev_id);
                 if (!ret) {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANT_PLZN to firmware");
                 } else {
                         ar->radio_cfg.ant_plzn = value;
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ENABLE_AMSDU:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ENABLE_PER_TID_AMSDU,
                                 value, ar->pdev->pdev_id);
                 if (!ret) {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ENABLE_PER_TID_AMSDU to firmware");
			  } else {
                         ar->radio_cfg.amsdu_mask = value;
                 }
                 break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ENABLE_AMPDU:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ENABLE_PER_TID_AMPDU,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ENABLE_PER_TID_AMPDU to firmware");
                 } else {
                         ar->radio_cfg.ampdu_mask = value;
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
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ENABLE_MBSSID_CTRL_FRAME to firmware");
                        } else {
                                ar->radio_cfg.mbssid_en_ctrl_frame = value;
                        }
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_PROBE_RESP_RETRY_LIMIT:
#define MAX_PRESP_RETRY_LIMIT 0x7F
                if (value > 0 && value <= MAX_PRESP_RETRY_LIMIT) {
                        ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ENABLE_MBSSID_CTRL_FRAME,
                                        value, ar->pdev->pdev_id);
                        if (!ret) {
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ENABLE_MBSSID_CTRL_FRAME to firmware");
                        } else {
                                ar->radio_cfg.resp_retry_limit = value;
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
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_CTS_TIMEOUT to firmware");
                        } else {
                                ar->radio_cfg.cts_timeout = value;
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
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SLOT_TIME to firmware");
                        } else {
                                ar->radio_cfg.slot_time = value;
                        }
                } else {
                        ath12k_err(NULL, "Slot Time value should be between 0 and 90");
                        ret = -1;
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ACK_TIMEOUT:
#define MIN_TX_ACK_TIMEOUT 0x40
#define MAX_TX_ACK_TIMEOUT 0xFF
                u16 rx_ack_timeout = (value & 0xFFFF);
                u16 pre_rx_ack_timeout = (value >> 16);
                if (rx_ack_timeout >= MIN_TX_ACK_TIMEOUT &&
                                rx_ack_timeout <= (MAX_TX_ACK_TIMEOUT)) {
                        if (pre_rx_ack_timeout < MIN_TX_ACK_TIMEOUT ||
                                        pre_rx_ack_timeout > MAX_TX_ACK_TIMEOUT)
                                pre_rx_ack_timeout = rx_ack_timeout - 20;
                        value = (rx_ack_timeout | (pre_rx_ack_timeout << 16));
                        ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ACK_TIMEOUT,
                                        value, ar->pdev->pdev_id);
                        if (!ret) {
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ACK_TIMEOUT to firmware");
                        } else {
                                ar->radio_cfg.ack_timeout = value;
                        }
                } else {
                        ath12k_err(NULL, "TX ACK Time-out value should be between 0x70 and 0xFF");
                        ret = -1;
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_CCK_TX_ENABLE:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_CCK_TX_ENABLE,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_CCK_TX_ENABLE to firmware");
                 } else {
                         ar->radio_cfg.cck_enable = value;
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_EQUAL_RU_ALLOCATION_ENABLE:
                if(value == 0 || value == 1) {
                        ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_EQUAL_RU_ALLOCATION_ENABLE,
                                        value, ar->pdev->pdev_id);
                        if (!ret) {
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_EQUAL_RU_ALLOCATION_ENABLE to firmware");
				} else {
                                ar->radio_cfg.ru_alloc_en = value;
                        }
                } else {
                        ath12k_err(NULL, "RU allocation enable value should be 0 or 1.");
                        ret = -1;
                }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_ANTENNA_GAIN_HALF_DB:
#define MAX_ANTENNA_GAIN 30
                if ((value >= 0) && (value <= (MAX_ANTENNA_GAIN * 2))) {
                        ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ANTENNA_GAIN_HALF_DB,
                                        value, ar->pdev->pdev_id);
                        if (!ret) {
                                ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_ANTENNA_GAIN_HALF_DB to firmware");
                        } else {
                                ar->radio_cfg.antenna_gain_half_db = value;
                        }
                }
		break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_MGMT_TTL:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_MGMT_TTL,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_MGMT_TTL to firmware");
                 } else {
                         ar->radio_cfg.mgmt_ttl = value;
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_PROBE_RESP_TTL:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_PROBE_RESP_TTL,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_PROBE_RESP_TTL to firmware");
                 } else {
                         ar->radio_cfg.prb_rsp_ttl = value;
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_MU_PPDU_DURATION:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_MU_PPDU_DURATION,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_MU_PPDU_DURATION to firmware");
                 } else {
                         ar->radio_cfg.mu_ppdu_dur = value;
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_TBTT_CTRL:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_TBTT_CTRL,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_TBTT_CTRL to firmware");
                 } else {
                         ar->radio_cfg.tbtt_ctrl = value;
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_SET_PREAM_PUNCT_BW:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_PREAM_PUNCT_BW,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
		     ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_SET_PREAM_PUNCT_BW to firmware");
                 } else {
                         ar->radio_cfg.punct_bw = value;
                 }
                break;
        case QCA_WLAN_VENDOR_RADIO_PARAM_LOW_LATENCY_SCHED_MODE:
                 ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_LOW_LATENCY_SCHED_MODE,
                                                value, ar->pdev->pdev_id);
                 if (!ret) {
                         ath12k_err(NULL, "Failed to send WMI_PDEV_PARAM_LOW_LATENCY_SCHED_MODE to firmware");
                 } else {
                         ar->radio_cfg.low_lat_mode = value;
                 }
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

        ath12k_dbg(NULL, ATH12K_DBG_CFG,
                   "vif: %p param: %d value: %d if: %d link: %d\n",
                   vif, param, value,
                   params->ifindex, params->link_id);

        ret = ath12k_vendor_set_arvif_params(arvif, param, value, &reload);
        rcu_read_unlock();

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
