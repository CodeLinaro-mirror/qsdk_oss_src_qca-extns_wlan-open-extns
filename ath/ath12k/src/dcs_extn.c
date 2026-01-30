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

void cfg80211_intf_notify_extn(struct wiphy *wiphy, struct ath12k *ar)
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

	cfg80211_vendor_event(vendor_event, GFP_ATOMIC);
	return;

nla_put_failure:
	nlmsg_free(vendor_event);
}

void ath12k_vendor_send_intf_notify_extn(struct ath12k *ar)
{
	struct ieee80211_hw *hw = ath12k_ar_to_hw(ar);

	cfg80211_intf_notify_extn(hw->wiphy, ar);
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

	ath12k_vendor_send_intf_notify_extn(ar);
exit:
	rcu_read_unlock();
}

