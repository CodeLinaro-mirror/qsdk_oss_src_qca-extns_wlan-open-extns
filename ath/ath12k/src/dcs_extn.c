// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <net/netlink.h>
#include <net/mac80211.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include "../core.h"
#include "../wmi.h"
#include "../mac.h"
#include "../debug.h"
#include "../vendor.h"
#include "ath12k_cmn_extn.h"
#include "dcs_extn.h"

void ath12k_mac_set_intf_detect(struct ath12k *ar, u16 intf_detect_bitmap)
{
	u16 dcs_config_bitmap, prev_config_bitmap;
	int ret;

	spin_lock_bh(&ar->data_lock);
	intf_detect_bitmap &= ATH12K_DCS_VALID_INTF_BITMAP;
	prev_config_bitmap = ar->ar_extn.dcs_config_bitmap;
	ar->ar_extn.dcs_config_bitmap |= intf_detect_bitmap;
	dcs_config_bitmap = ar->ar_extn.dcs_config_bitmap;
	spin_unlock_bh(&ar->data_lock);
	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_DCS,
					dcs_config_bitmap, ar->pdev->pdev_id);
	if (ret) {
		ath12k_warn(ar->ab, "Failed to set DCS config: %d\n", ret);
		spin_lock_bh(&ar->data_lock);
		ar->ar_extn.dcs_config_bitmap = prev_config_bitmap;
		spin_unlock_bh(&ar->data_lock);
	}
}

int ath12k_mac_nlwidth_to_chwidth(enum nl80211_chan_width ch_width)
{
	switch (ch_width) {
		case NL80211_CHAN_WIDTH_320:
			return ATH12K_CHWIDTH_320;
		case NL80211_CHAN_WIDTH_160:
		case NL80211_CHAN_WIDTH_80P80:
			return ATH12K_CHWIDTH_160;
		case NL80211_CHAN_WIDTH_80:
			return ATH12K_CHWIDTH_80;
		case NL80211_CHAN_WIDTH_40:
			return ATH12K_CHWIDTH_40;
		case NL80211_CHAN_WIDTH_20:
		case NL80211_CHAN_WIDTH_20_NOHT:
			return ATH12K_CHWIDTH_20;
		case NL80211_CHAN_WIDTH_16:
			return ATH12K_CHWIDTH_16;
		case NL80211_CHAN_WIDTH_10:
			return ATH12K_CHWIDTH_10;
		case NL80211_CHAN_WIDTH_8:
			return ATH12K_CHWIDTH_8;
		case NL80211_CHAN_WIDTH_4:
			return ATH12K_CHWIDTH_4;
		case NL80211_CHAN_WIDTH_2:
			return ATH12K_CHWIDTH_2;
		case NL80211_CHAN_WIDTH_1:
			return ATH12K_CHWIDTH_1;
		default:
			return ATH12K_CHWIDTH_20;
	}
}

	static u32
ath12k_transform_intf_bitmap(int input_bitmap,
		struct cfg80211_chan_def *chandef)
{
	u16 input_bits[ATH12K_MAX_20MHZ_SEGMENTS] = {0};
	u16 output_bits[ATH12K_MAX_20MHZ_SEGMENTS] = {0};
	u32 start_freq, segment_freq;
	u16 bandwidth, num_sub_chans;
	u32 output_bitmap = 0;
	u16 primary_index = -1;

	bandwidth = ath12k_mac_nlwidth_to_chwidth(chandef->width);
	num_sub_chans = bandwidth / ATH12K_CHWIDTH_20;
	start_freq = (chandef->center_freq1 - bandwidth / 2) + 10;

	for (int i = 0; i < ATH12K_MAX_20MHZ_SEGMENTS; i++) {
		segment_freq = start_freq + (i * 20);
		if (segment_freq == chandef->chan->center_freq) {
			primary_index = i;
			break;
		}
	}
	if (primary_index == -1)
		return 0;

	for (int i = 0; i < ATH12K_MAX_20MHZ_SEGMENTS; ++i)
		input_bits[i] = BIT(i) & input_bitmap;

	for (int i = 0; i < num_sub_chans; ++i) {
		int src = i, dst = i;

		if (bandwidth == ATH12K_CHWIDTH_40) {
			if (primary_index == 1)
				dst = 1 - i;
		} else if (bandwidth == ATH12K_CHWIDTH_80) {
			dst = intf_map_80[primary_index][i];
		} else if (bandwidth == ATH12K_CHWIDTH_160) {
			dst = intf_map_160[primary_index][i];
		} else if (bandwidth == ATH12K_CHWIDTH_320) {
			dst = intf_map_320[primary_index][i];
		}
		output_bits[dst] = input_bits[src];
	}

	for (int i = 0; i < ATH12K_MAX_20MHZ_SEGMENTS; ++i)
		output_bitmap |= output_bits[i] ? BIT(i) : 0;

	return output_bitmap;
}

void cfg80211_intf_notify_extn(struct wiphy *wiphy, struct ath12k *ar, u16 type,
			       u32 interference_bitmap)
{
	struct ath12k_link_vif *tmp_arvif = NULL, *arvif;
	struct sk_buff *vendor_event;
	struct wireless_dev *wdev;
	int ret;

	rcu_read_lock();
	spin_lock_bh(&ar->data_lock);
	if (list_empty(&ar->arvifs)) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
			   "arvifs list is empty\n");
		spin_unlock_bh(&ar->data_lock);
		rcu_read_unlock();
		return;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "CW intf notify event to userspace\n");

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
			   "Checking arvif: is_started=%d ahvif=%p\n",
			   arvif->is_started, arvif->ahvif);
		if (!tmp_arvif && arvif->is_started) {
			tmp_arvif = arvif;
			break;
		}
	}

	spin_unlock_bh(&ar->data_lock);

	if (!tmp_arvif || !tmp_arvif->ahvif) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
			   "No valid arvif found for CW Intf update\n");
		rcu_read_unlock();
		return;
	}

	if (!tmp_arvif->ahvif->vif) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
			   "vif is NULL for CW Intf update\n");
		rcu_read_unlock();
		return;
	}

	wdev = ieee80211_vif_to_wdev(tmp_arvif->ahvif->vif);

	rcu_read_unlock();

	if (!wdev) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
			   "wdev is NULL for CW Intf update\n");
		return;
	}

	vendor_event = cfg80211_vendor_event_alloc(wiphy, wdev,
			NLMSG_DEFAULT_SIZE,
			QCA_NL80211_VENDOR_SUBCMD_DCS_INTERFERENCE_COMPUTE_INDEX,
			GFP_ATOMIC);

	if (!vendor_event) {
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
			   "SKB alloc failed for CW Intf evt\n");
		return;
	}

	if (wdev->valid_links) {
		ret = nla_put_u8(vendor_event,
				 QCA_WLAN_VENDOR_ATTR_DCS_LINK_ID,
				 tmp_arvif->link_id);
		if (ret) {
			ath12k_warn(ar->ab, "Failed to put link id\n");
			goto nla_put_failure;
		}
	}

	ret = nla_put_u16(vendor_event,
			QCA_WLAN_VENDOR_ATTR_DCS_ENABLE,
			type);

	if (ret) {
		ath12k_warn(ar->ab, "Failed to put DCS interference type\n");
		goto nla_put_failure;
	}

	if (type == WMI_DCS_OBSS_INTF) {
		ret = nla_put_u32(vendor_event,
				QCA_WLAN_VENDOR_ATTR_DCS_INTERFERENCE_BITMAP,
				interference_bitmap);

		if (ret) {
			ath12k_warn(ar->ab, "Failed to put DCS interference bitmap\n");
			goto nla_put_failure;
		}
	}

	cfg80211_vendor_event(vendor_event, GFP_ATOMIC);
	return;

nla_put_failure:
	nlmsg_free(vendor_event);
}

void ath12k_vendor_send_intf_notify_extn(struct ath12k *ar, u16 type,
					 u32 intf_bitmap)
{
	struct ieee80211_hw *hw = ath12k_ar_to_hw(ar);

	cfg80211_intf_notify_extn(hw->wiphy, ar, type, intf_bitmap);
}

void ath12k_wmi_dcs_cw_interference_event_extn(struct ath12k_base *ab,
					       struct sk_buff *skb,
					       u32 pdev_id)
{
	struct ath12k *ar;
	struct wmi_dcs_cw_info cw_info = {};
	int ret;

	ret = ath12k_wmi_tlv_iter(ab, skb->data, skb->len,
				  ath12k_wmi_dcs_event_parser,
				  &cw_info);
	if (ret) {
		ath12k_warn(ab, "failed to parse cw tlv %d\n", ret);
		return;
	}

	rcu_read_lock();
	ar = ath12k_mac_get_ar_by_pdev_id(ab, pdev_id);
	if (!ar) {
		ath12k_warn(ab, "CW detected in invalid pdev id(%d)\n",
			    pdev_id);
		goto exit;
	}

	spin_lock_bh(&ar->data_lock);
	if (!(ar->ar_extn.dcs_config_bitmap & WMI_DCS_CW_INTF)) {
		spin_unlock_bh(&ar->data_lock);
		goto exit;
	}
	spin_unlock_bh(&ar->data_lock);
	ath12k_dbg(ab, ATH12K_DBG_WMI, "CW Interference detected for pdev=%d\n",
		   pdev_id);

	ath12k_vendor_send_intf_notify_extn(ar, WMI_DCS_CW_INTF, ATH12K_DCS_SEG_PRI20);
exit:
	rcu_read_unlock();
}

void ath12k_wmi_dcs_obss_interference_event_extn(struct ath12k_base *ab,
						 struct sk_buff *skb,
						 u32 pdev_id)
{
	struct ath12k *ar;
	struct wmi_dcs_obss_info obss_info = {};
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ath12k_mac_get_any_chanctx_conf_arg arg;
	u32 intf_bitmap, modified_host_bitmap;
	struct ath12k_hw *ah;
	int ret;

	ret = ath12k_wmi_tlv_iter(ab, skb->data, skb->len,
				  ath12k_wmi_dcs_event_parser,
				  &obss_info);
	if (ret) {
		ath12k_err(ab, "failed to parse cw tlv %d\n", ret);
		return;
	}

	rcu_read_lock();
	ar = ath12k_mac_get_ar_by_pdev_id(ab, pdev_id);
	if (!ar) {
		ath12k_err(ab, "CW detected in invalid pdev id(%d)\n",
			    pdev_id);
		goto exit;
	}

	spin_lock_bh(&ar->data_lock);
	if (!(ar->ar_extn.dcs_config_bitmap & WMI_DCS_OBSS_INTF)) {
		spin_unlock_bh(&ar->data_lock);
		goto exit;
	}
	spin_unlock_bh(&ar->data_lock);

	ath12k_dbg(ab, ATH12K_DBG_WMI, "OBSS Interference detected for pdev=%d\n",
		   pdev_id);

	ah = ar->ah;

	arg.ar = ar;
	arg.chanctx_conf = NULL;
	ieee80211_iter_chan_contexts_atomic(ah->hw, ath12k_mac_get_any_chanctx_conf_iter,
					    &arg);
	chanctx_conf = arg.chanctx_conf;
	if (!chanctx_conf) {
		ath12k_err(ab, "chanctx_conf is not available\n");
		goto exit;
	}

	intf_bitmap = obss_info.chan_bw_interference_bitmap;
	if (!intf_bitmap) {
		ath12k_err(ab, "intf_bitmap is not available\n");
		goto exit;
	}

	intf_bitmap &= (~ATH12K_DCS_SEG_PRI20);

	modified_host_bitmap = ath12k_transform_intf_bitmap(intf_bitmap, &chanctx_conf->def);

	if (modified_host_bitmap == 0 && intf_bitmap != 0) {
		ath12k_err(ab, "Bitmap transformation failed, using original bitmap\n");
	}

	ath12k_vendor_send_intf_notify_extn(ar, WMI_DCS_OBSS_INTF, modified_host_bitmap);

exit:
	rcu_read_unlock();
}
