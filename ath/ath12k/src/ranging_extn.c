/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/netdevice.h>
#include <net/netlink.h>
#include <net/net_namespace.h>
#include <net/mac80211.h>
#include "../core.h"
#include "../mac.h"
#include "../wmi.h"
#include "../debug.h"
#include "../ranging.h"
#include "ath12k_cmn_extn.h"
#include "ranging_extn.h"

static int ath12k_wmi_lowi_oem_resp_tlv_iter_extn(struct ath12k_base *ab,
		u16 tag, u16 len, const void *ptr,
		void *data)
{
	struct ath12k_wmi_lowi_oem_resp *resp = data;

	switch (tag) {
		case WMI_TAG_ARRAY_BYTE:
			if (!resp->data1) {
				resp->data1 = ptr;
				resp->len1 = len;
			} else if (!resp->data2) {
				resp->data2 = ptr;
				resp->len2 = len;
			}
			break;
		case WMI_TAG_OEM_INDIRECT_DATA:
			resp->indirect = true;
			break;
		default:
			break;
	}

	return 0;
}

void ath12k_wmi_lowi_oem_response_event_extn(struct ath12k_base *ab,
					     struct sk_buff *skb)
{
	struct ath12k_wmi_lowi_oem_resp resp = {0};
	struct ieee80211_hw *hw;
	struct ath12k *ar;
	u8 *data;
	u32 len;
	int ret;

	ath12k_dbg(ab, ATH12K_DBG_RTT, "LOWI trace: received WMI_OEM_RESPONSE_EVENTID skb_len %u\n",
		   skb->len);

	ret = ath12k_wmi_tlv_iter(ab, skb->data, skb->len,
				  ath12k_wmi_lowi_oem_resp_tlv_iter_extn, &resp);
	if (ret) {
		ath12k_warn(ab, "failed to parse LOWI OEM response event: %d\n", ret);
		return;
	}

	if (resp.indirect)
		ath12k_warn(ab,
			    "LOWI OEM response includes indirect DMA data, unsupported by passthrough\n");

	ath12k_dbg(ab, ATH12K_DBG_RTT, "LOWI trace: OEM response parsed len1 %u len2 %u indirect %d\n",
		   resp.len1, resp.len2, resp.indirect);

	if (!resp.data1 || !resp.len1) {
		ath12k_warn(ab, "LOWI OEM response event without direct data\n");
		return;
	}

	rcu_read_lock();
	ar = ath12k_mac_get_any_ar(ab);
	if (!ar) {
		rcu_read_unlock();
		ath12k_warn(ab, "invalid ar for LOWI OEM data event\n");
		return;
	}
	hw = ar->ah->hw;
	rcu_read_unlock();

	len = resp.len1 + resp.len2;
	if (!resp.len2) {
		ret = ath12k_vendor_send_lowi_oem_data_extn(hw, ar, resp.data1, resp.len1);
		if (ret)
			ath12k_warn(ab, "failed to send LOWI OEM vendor response: %d\n",
				    ret);
		else
			ath12k_dbg(ab, ATH12K_DBG_RTT, "LOWI trace: sent OEM response vendor event len %u\n",
				   resp.len1);
		goto signal_done;
	}

	data = kmalloc(len, GFP_ATOMIC);
	if (!data) {
		ath12k_warn(ab, "failed to allocate LOWI OEM response buffer len %u\n", len);
		goto signal_done;
	}

	memcpy(data, resp.data1, resp.len1);
	memcpy(data + resp.len1, resp.data2, resp.len2);

	ret = ath12k_vendor_send_lowi_oem_data_extn(hw, ar, data, len);
	if (ret)
		ath12k_warn(ab, "failed to send split LOWI OEM vendor response: %d\n",
			    ret);
	else
		ath12k_dbg(ab, ATH12K_DBG_RTT, "LOWI trace: sent split OEM response vendor event len %u\n",
			   len);
	kfree(data);

signal_done:
	/* Unblock ath12k_wmi_send_lowi_oem_data_extn() which is waiting for this
	 * response before it may send the next command in a loop. */
	complete(&ar->ar_extn.lowi_oem_done);
}

int ath12k_wmi_send_lowi_oem_data_extn(struct ath12k *ar, u32 pdev_id,
				       u32 data_len, const u8 *data)
{
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	size_t aligned_len;
	u32 max_msg_len;
	int len, ret;

	if (!ar || (data_len && !data)) {
		ath12k_err(ar ? ar->ab : NULL,
			   "LOWI trace: invalid WMI OEM data args ar %pK data_len %u data %pK\n",
			   ar, data_len, data);
		return -EINVAL;
	}

	aligned_len = roundup(data_len, sizeof(u32));
	if (aligned_len < data_len) {
		ath12k_err(ar->ab, "LOWI trace: WMI OEM req length overflow data_len %u\n",
			   data_len);
		return -EINVAL;
	}

	len = TLV_HDR_SIZE + aligned_len;
	max_msg_len = ar->wmi->wmi_ab->max_msg_len[ar->pdev_idx];
	if (max_msg_len && len > max_msg_len) {
		ath12k_err(ar->ab, "LOWI trace: WMI OEM req too large len %d max %u data_len %u\n",
			   len, max_msg_len, data_len);
		return -EMSGSIZE;
	}

	skb = ath12k_wmi_alloc_skb(ar->wmi->wmi_ab, len);
	if (!skb) {
		ath12k_err(ar->ab, "LOWI trace: failed to allocate WMI OEM req skb len %d\n",
			   len);
		return -ENOMEM;
	}

	tlv = (struct wmi_tlv *)skb->data;
	tlv->header = ath12k_wmi_tlv_hdr(WMI_TAG_ARRAY_BYTE, data_len);
	if (aligned_len)
		memset(tlv->value, 0, aligned_len);
	if (data_len)
		memcpy(tlv->value, data, data_len);

	ath12k_dbg(ar->ab, ATH12K_DBG_RTT, "LOWI trace: sending WMI_OEM_REQ_CMDID pdev %u data_len %u aligned_len %zu total_len %d max_msg_len %u\n",
		   pdev_id, data_len, aligned_len, len, max_msg_len);

	ret = ath12k_wmi_cmd_send(ar->wmi, skb, WMI_OEM_REQ_CMDID);
	if (ret) {
		ath12k_warn(ar->ab, "failed to send LOWI OEM req WMI cmd: %d\n",
			    ret);
		dev_kfree_skb(skb);
		return ret;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_RTT, "LOWI trace: WMI_OEM_REQ_CMDID sent successfully pdev %u data_len %u\n",
		   pdev_id, data_len);

	return 0;
}

static const char *ath12k_lowi_evt_type_str_extn(u32 cmd_type)
{
	switch (cmd_type) {
	case QCA_LOWI_EVT_MEAS_RESULT:
		return "MEAS_RESULT";
	case QCA_LOWI_EVT_ERROR:
		return "ERROR";
	case QCA_LOWI_EVT_CHANNEL_INFO_RSP:
		return "CH_INFO_RSP";
	case QCA_LOWI_EVT_CAPS_RSP:
		return "CAPS_RSP";
	case QCA_LOWI_EVT_REG_RSP:
		return "REG_RSP";
	case QCA_LOWI_EVT_SESSION_DONE:
		return "SESSION_DONE";
	case QCA_LOWI_EVT_RESPONDER_CH_INFO:
		return "RESP_CH_INFO";
	case QCA_LOWI_EVT_CFG_RESP_MEAS_RSP:
		return "CFG_RESP_MEAS_RSP";
	case QCA_LOWI_EVT_RESP_MEAS_INFO:
		return "RESP_MEAS_INFO";
	default:
		return "UNKNOWN";
	}
}

static int ath12k_vendor_send_lowi_event_extn(struct ieee80211_hw *hw,
					      u32 cmd_type,
					      const u8 *payload, u32 len,
					      u8 radio_idx)
{
	ath12k_dbg(NULL, ATH12K_DBG_RTT, "LOWI event->userspace: cmd_type=%u (%s) len=%u radio_idx=%u\n",
		   cmd_type, ath12k_lowi_evt_type_str_extn(cmd_type),
		   len, radio_idx);
	struct sk_buff *vendor_event;
	u32 alloc_sz;

	if (!hw || (len && !payload))
		return -EINVAL;

	alloc_sz = nla_total_size(sizeof(u32)) +        /* CMD_TYPE */
		   nla_total_size(sizeof(u8)) +         /* LINK_ID (radio_idx) */
		   (len ? nla_total_size(len) : 0);

	vendor_event = cfg80211_vendor_event_alloc(hw->wiphy, NULL, alloc_sz,
						   QCA_NL80211_VENDOR_SUBCMD_OEM_DATA_INDEX,
						   GFP_ATOMIC);
	if (!vendor_event)
		return -ENOMEM;

	if (nla_put_u32(vendor_event, QCA_WLAN_VENDOR_ATTR_OEM_DATA_CMD_TYPE,
			cmd_type)) {
		kfree_skb(vendor_event);
		return -EMSGSIZE;
	}

	/* radio_idx 0xff means not applicable (global / no specific radio) */
	if (radio_idx != 0xff &&
	    nla_put_u8(vendor_event, QCA_WLAN_VENDOR_ATTR_OEM_DATA_LINK_ID,
		       radio_idx)) {
		kfree_skb(vendor_event);
		return -EMSGSIZE;
	}

	if (len && nla_put(vendor_event, QCA_WLAN_VENDOR_ATTR_OEM_DATA_CMD_DATA,
			   len, payload)) {
		kfree_skb(vendor_event);
		return -EMSGSIZE;
	}

	cfg80211_vendor_event(vendor_event, GFP_ATOMIC);
	return 0;
}

int ath12k_vendor_send_lowi_oem_data_extn(struct ieee80211_hw *hw,
					  struct ath12k *ar,
					  const u8 *data, u32 len)
{
	u8 radio_idx = ar ? ar->radio_idx : 0xff;

	return ath12k_vendor_send_lowi_event_extn(hw, QCA_LOWI_EVT_MEAS_RESULT,
						  data, len, radio_idx);
}

/*
 * Return the widest channel width that resolves to a known phy_mode for this
 * band on the given radio. Falls back to successively narrower widths rather
 * than returning a width that maps to MODE_UNKNOWN in the active phymode table.
 */
static enum nl80211_chan_width
ath12k_lowi_max_chan_width_extn(struct ath12k *ar,
				struct ieee80211_supported_band *sband,
				struct ieee80211_channel *channel)
{
	const struct ieee80211_sband_iftype_data *iftype = NULL;
	bool ax_only = ath12k_scan_radio_supported(ar->pdev);

	if (sband->n_iftype_data > 0)
		iftype = sband->iftype_data;

	switch (channel->band) {
	case NL80211_BAND_6GHZ:
		if (iftype && iftype->eht_cap.has_eht)
			return ax_only ? NL80211_CHAN_WIDTH_160
				       : NL80211_CHAN_WIDTH_320;
		if (iftype && iftype->he_cap.has_he)
			return NL80211_CHAN_WIDTH_160;
		return NL80211_CHAN_WIDTH_80;

	case NL80211_BAND_5GHZ:
		if (iftype && iftype->eht_cap.has_eht)
			return ax_only ? NL80211_CHAN_WIDTH_160
				       : NL80211_CHAN_WIDTH_320;
		if (iftype && iftype->he_cap.has_he) {
			if (iftype->he_cap.he_cap_elem.phy_cap_info[0] &
			    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G)
				return NL80211_CHAN_WIDTH_160;
			return NL80211_CHAN_WIDTH_80;
		}
		if (sband->vht_cap.vht_supported) {
			u32 cap = sband->vht_cap.cap &
				  IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;
			if (cap == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ ||
			    cap == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ)
				return NL80211_CHAN_WIDTH_160;
			return NL80211_CHAN_WIDTH_80;
		}
		if (sband->ht_cap.ht_supported &&
		    (sband->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40))
			return NL80211_CHAN_WIDTH_40;
		return NL80211_CHAN_WIDTH_20;

	case NL80211_BAND_2GHZ:
	default:
		if (iftype && (iftype->eht_cap.has_eht || iftype->he_cap.has_he))
			return NL80211_CHAN_WIDTH_40;
		if (sband->ht_cap.ht_supported &&
		    (sband->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40))
			return NL80211_CHAN_WIDTH_40;
		return NL80211_CHAN_WIDTH_20;
	}
}

/*
 * Return the ath12k radio (ar) that owns the given channel frequency.
 */
static struct ath12k *
ath12k_lowi_ar_for_freq_extn(struct ath12k_hw *ah, u32 center_freq,
			     enum nl80211_band band)
{
	struct ath12k *ar;
	int i;

	if (ah->num_radio == 1)
		return &ah->radio[0];

	for_each_ar(ah, ar, i) {
		if (band == NL80211_BAND_5GHZ || band == NL80211_BAND_6GHZ) {
			if (center_freq >= KHZ_TO_MHZ(ar->freq_range.start_freq) &&
			    center_freq <= KHZ_TO_MHZ(ar->freq_range.end_freq) &&
			    ar->mac.sbands[band].channels)
				return ar;
		} else if (ar->mac.sbands[band].channels) {
			return ar;
		}
	}

	return &ah->radio[0];
}

static int ath12k_vendor_send_lowi_channel_info_rsp_extn(struct ieee80211_hw *hw)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k_lowi_channel_info_entry *chan_info;
	struct ieee80211_supported_band *sband;
	u32 max_channels;
	u32 body_len;
	u32 count = 0;
	u8 *body;
	int band;
	int i;
	int ret;

	max_channels = QCA_VENDOR_WLAN_OEM_DATA_BUF_MAX_SIZE /
		       sizeof(*chan_info);
	body_len = 1 + max_channels * sizeof(*chan_info);
	body = kzalloc(body_len, GFP_KERNEL);
	if (!body)
		return -ENOMEM;

	chan_info = (struct ath12k_lowi_channel_info_entry *)(body + 1);
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		sband = hw->wiphy->bands[band];
		if (!sband)
			continue;

		for (i = 0; i < sband->n_channels && count < max_channels; i++) {
			struct ieee80211_channel *channel = &sband->channels[i];
			struct ath12k_lowi_channel_info_entry *entry = &chan_info[count];
			struct ath12k *ar;
			enum nl80211_chan_width max_width;
			enum wmi_phy_mode phy_mode;
			u32 info = 0;
			u32 reg_info_1;
			u32 reg_info_2;

			if (channel->flags & IEEE80211_CHAN_DISABLED)
				continue;

			if (channel->flags & IEEE80211_CHAN_RADAR)
				info |= ATH12K_LOWI_CH_INFO_FLAG_DFS;

			ar = ath12k_lowi_ar_for_freq_extn(ah, channel->center_freq,
							  (enum nl80211_band)band);
			max_width = ath12k_lowi_max_chan_width_extn(ar, sband, channel);
			phy_mode = ath12k_mac_get_phymode(ar,
							  (enum nl80211_band)band,
							  max_width);
			if (phy_mode != MODE_UNKNOWN)
				info |= (phy_mode & ATH12K_LOWI_CH_INFO_MODE_MASK);

			reg_info_1 = ATH12K_LOWI_REG_INFO1_SET_PWR(channel->max_reg_power);
			reg_info_2 = ATH12K_LOWI_REG_INFO2_SET_PWR(channel->max_power);

			entry->chan_id = cpu_to_le32(
				ieee80211_frequency_to_channel(channel->center_freq));
			entry->reserved0 = cpu_to_le32(ATH12K_LOWI_RESERVED0_SET_RADIO_IDX(ar->radio_idx));
			entry->channel_info.mhz =
				cpu_to_le32(channel->center_freq);
			entry->channel_info.band_center_freq1 =
				cpu_to_le32(channel->center_freq);
			entry->channel_info.band_center_freq2 = 0;
			entry->channel_info.info = cpu_to_le32(info);
			entry->channel_info.reg_info_1 = cpu_to_le32(reg_info_1);
			entry->channel_info.reg_info_2 = cpu_to_le32(reg_info_2);
			count++;
		}
	}

	body[0] = (u8)count;
	body_len = 1 + count * sizeof(*chan_info);
	ret = ath12k_vendor_send_lowi_event_extn(hw, QCA_LOWI_EVT_CHANNEL_INFO_RSP,
						 body, body_len, 0xff);
	kfree(body);
	return ret;
}

static u8 ath12k_vendor_lowi_get_vdev_id_extn(struct wireless_dev *wdev, u8 link_id)
{
	struct ieee80211_vif *vif;
	struct ath12k_vif *ahvif;
	struct ath12k_link_vif *arvif;

	if (!wdev)
		return 0;

	vif = wdev_to_ieee80211_vif(wdev);
	if (!vif)
		return 0;

	ahvif = ath12k_vif_to_ahvif(vif);
	if (!ahvif)
		return 0;

	if (!vif->valid_links)
		return ahvif->deflink.vdev_id;

	arvif = ath12k_get_arvif_from_link_id(ahvif, link_id);
	if (!arvif) {
		ath12k_err(NULL, "LOWI: no arvif for link_id %u\n", link_id);
		return 0;
	}

	return arvif->vdev_id;
}

static const char *ath12k_lowi_cmd_type_str_extn(u32 cmd_type)
{
	switch (cmd_type) {
	case QCA_LOWI_CMD_RTT_REGISTER:
		return "RTT_REGISTER";
	case QCA_LOWI_CMD_RTT_START:
		return "RTT_START";
	case QCA_LOWI_CMD_RTT_CANCEL:
		return "RTT_CANCEL";
	case QCA_LOWI_CMD_CHANNEL_INFO:
		return "CHANNEL_INFO";
	case QCA_LOWI_CMD_CAPABILITIES:
		return "CAPABILITIES";
	case QCA_LOWI_CMD_AVAIL_CH:
		return "AVAIL_CH";
	case QCA_LOWI_CMD_CONFIG_LCI:
		return "CONFIG_LCI";
	default:
		return "UNKNOWN";
	}
}

int ath12k_vendor_oem_data_lowi_extn(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     struct ieee80211_hw *hw,
				     struct ath12k_hw *ah,
				     struct nlattr **tb)
{
	struct ath12k *ar;
	u32 cmd_type;
	u8 pdev_id;
	int link_id = 0;
	int radio_idx = -1;
	int vdev_id = -1;
	int ret;
	u8 *data_ptr;
	int payload_len;
	struct ieee80211_vif *vif = NULL;
	struct ath12k_link_vif *arvif = NULL;

	/* If IFINDEX is present in the vendor data, resolve wdev from it. */
	if (!wdev && tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_IFINDEX]) {
		u32 ifidx = nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_IFINDEX]);
		struct net_device *dev = dev_get_by_index(wiphy_net(wiphy), ifidx);

		if (!dev) {
			ath12k_err(NULL, "LOWI: ifindex %u not found\n", ifidx);
			return -ENODEV;
		} else {
			ath12k_dbg(NULL, ATH12K_DBG_RTT, "LOWI: ifindex %u found and dev name: %s\n",
				   ifidx, dev->name);
		}
		wdev = dev->ieee80211_ptr;
		dev_put(dev);
		if (!wdev) {
			ath12k_err(NULL, "LOWI: ifindex %u has no wdev\n", ifidx);
			return -EINVAL;
		}
	}

	if (!tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_CMD_TYPE])
		return -EAGAIN;

	cmd_type = nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_CMD_TYPE]);

	/* Resolve link_id using the same MLO-aware pattern */
	if (wdev && wdev->valid_links) { /* MLO case */
		ath12k_dbg(NULL, ATH12K_DBG_RTT, "valid_links: %d\n", wdev->valid_links);
		if (!tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_LINK_ID]) {
			ath12k_err(NULL, "Link ID attribute NULL for MLO case\n");
			return -EINVAL;
		}
		link_id = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_LINK_ID]);
		ath12k_dbg(NULL, ATH12K_DBG_RTT, "link_id: %d\n", link_id);

		if (!(wdev->valid_links & BIT(link_id))) {
			ath12k_err(NULL, "Corresponding link_id not found in valid link bitmap\n");
			return -ENOLINK;
		}
	} else { /* non-MLO case */
		ath12k_dbg(NULL, ATH12K_DBG_RTT, "LOWI - Non MLD\n");
		if (tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_LINK_ID]) {
			ath12k_err(NULL, "Link ID attribute not NULL for legacy case\n");
			return -EINVAL;
		}
		link_id = 0;
	}

	if (tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_RADIO_IDX]) {
		radio_idx = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_RADIO_IDX]);
		ath12k_dbg(NULL, ATH12K_DBG_RTT, "LOWI - Radio_idx: %d\n", radio_idx);
	}

	data_ptr = tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_CMD_DATA] ?
		   nla_data(tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_CMD_DATA]) : NULL;
	payload_len = tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_CMD_DATA] ?
		      nla_len(tb[QCA_WLAN_VENDOR_ATTR_OEM_DATA_CMD_DATA]) : 0;

	ath12k_dbg(NULL, ATH12K_DBG_RTT, "LOWI cmd: type=%u (%s) link_id=%d payload=%d bytes\n",
		   cmd_type, ath12k_lowi_cmd_type_str_extn(cmd_type),
		   link_id, payload_len);

	switch (cmd_type) {
	case QCA_LOWI_CMD_RTT_REGISTER:
		return ath12k_vendor_send_lowi_event_extn(hw, QCA_LOWI_EVT_REG_RSP,
							  NULL, 0, 0xff);

	case QCA_LOWI_CMD_CHANNEL_INFO:
	case QCA_LOWI_CMD_AVAIL_CH:
		return ath12k_vendor_send_lowi_channel_info_rsp_extn(hw);

	case QCA_LOWI_CMD_RTT_START:
	case QCA_LOWI_CMD_RTT_CANCEL:
	case QCA_LOWI_CMD_CONFIG_LCI:

		if (!data_ptr || payload_len <= 0) {
			ath12k_err(NULL, "LOWI: cmd %u missing payload\n",
				   cmd_type);
			return -EINVAL;
		}

		vif = wdev_to_ieee80211_vif(wdev);

		/* AP & STA assoc case */
		if (wdev->iftype == NL80211_IFTYPE_AP ||
		    (wdev->iftype == NL80211_IFTYPE_STATION && vif->cfg.assoc)) {
			ar = ath12k_get_ar_from_wdev(wdev, link_id);
			if (!ar && radio_idx >= 0) {
				ar = ath12k_ah_to_ar(ah, (u8)radio_idx);
				ath12k_dbg(NULL, ATH12K_DBG_RTT, "LOWI: ar resolved via radio_idx=%d\n",
						radio_idx);
			}
			if (!ar) {
				ath12k_err(NULL, "LOWI: no ar for wdev %p cmd %u link_id=%d radio_idx=%d\n",
						wdev, cmd_type, link_id, radio_idx);
				return -EINVAL;
			}
			vdev_id = ath12k_vendor_lowi_get_vdev_id_extn(wdev, link_id);
			pdev_id = ar->pdev ? ar->pdev->pdev_id : -1;
		} else {
			/* STA Un assoc case */
			ar = ath12k_ah_to_ar(ah, (u8)radio_idx);
			ath12k_info(NULL, "LOWI: STA Un Assoc case ar resolved via radio_idx=%d wdev=%p netdev=%s\n",
					radio_idx, wdev,
					(wdev && wdev->netdev) ? wdev->netdev->name : "<none>");

			if (ar->ar_extn.rtt_vif) {
				arvif = ar->ar_extn.rtt_vif;
				vdev_id = arvif->vdev_id;
				ath12k_wmi_vdev_set_param_cmd(ar, vdev_id,
						WMI_VDEV_PARAM_ENABLE_DISABLE_RTT_INITIATOR_ROLE,
						1);
			} else {
				ath12k_err(NULL, "LOWI:  Un Assoc case no rtt_vif return error\n");
				return -EINVAL;
			}
		}

		ath12k_info(NULL, "LOWI RTT_START/CANCEL vdev=%d pdev=%u, "
				"link_id=%d allocated_vdev_map=0x%llx\n",
				vdev_id, pdev_id, link_id,
				ar->allocated_vdev_map);

		ret = ath12k_wmi_send_lowi_oem_data_extn(ar, pdev_id,
						    payload_len, data_ptr);

		/* On cancel: clear both initiator and responder roles */
		if (!ret && cmd_type == QCA_LOWI_CMD_RTT_CANCEL && vdev_id >= 0) {
			ath12k_wmi_vdev_set_param_cmd(ar, vdev_id,
				WMI_VDEV_PARAM_ENABLE_DISABLE_RTT_INITIATOR_ROLE, 0);
			ath12k_wmi_vdev_set_param_cmd(ar, vdev_id,
				WMI_VDEV_PARAM_ENABLE_DISABLE_RTT_RESPONDER_ROLE, 0);
		}

		return ret;

	case QCA_LOWI_CMD_CAPABILITIES: {
		int i;

		for_each_ar(ah, ar, i) {
			pdev_id = ar->pdev ? ar->pdev->pdev_id : ar->pdev_idx + 1;
			reinit_completion(&ar->ar_extn.lowi_oem_done);
			ret = ath12k_wmi_send_lowi_oem_data_extn(ar, pdev_id,
							    payload_len, data_ptr);
			if (ret) {
				ath12k_warn(ar->ab,
					    "LOWI CAP req failed for pdev %u: %d\n",
					    pdev_id, ret);
				return ret;
			}
			if (!wait_for_completion_timeout(&ar->ar_extn.lowi_oem_done,
							 ATH12K_LOWI_OEM_TIMEOUT_HZ)) {
				ath12k_warn(ar->ab,
					    "LOWI CAP response timed out pdev %u\n",
					    pdev_id);
				return -ETIMEDOUT;
			}
		}
		return 0;
	}

	default:
		ath12k_err(NULL, "LOWI: unsupported cmd_type %u\n", cmd_type);
		return ath12k_vendor_send_lowi_event_extn(hw, QCA_LOWI_EVT_ERROR,
							  NULL, 0, 0xff);
	}
}

static struct ath12k_link_vif *
ath12k_find_rtt_arvif_from_wdev(struct wireless_dev *wdev)
{
	ath12k_dbg(NULL, ATH12K_DBG_RTT,
		   "RTT: unassoc PASN ranging vif lookup not implemented yet\n");
	return NULL;
}

struct ath12k_link_vif *ath12k_arvif_from_peer_addr_extn(struct wireless_dev *wdev,
							 const u8 *peer_addr)
{
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct ath12k_link_sta *arsta;
	struct ath12k_vif *ahvif;

	if (!vif)
		return NULL;

	ath12k_dbg(NULL, ATH12K_DBG_RTT,
		   "RTT: arvif_from_peer_addr peer=%pM assoc=%d\n",
		   peer_addr, vif->cfg.assoc);

	if (!vif->cfg.assoc)
		return ath12k_find_rtt_arvif_from_wdev(wdev);

	ahvif = ath12k_vif_to_ahvif(vif);
	if (!ahvif || !ahvif->ah)
		return NULL;

	arsta = ath12k_link_sta_find_by_addr_all_radios_extn(ahvif->ah, peer_addr);
	if (!arsta)
		return NULL;

	ath12k_dbg(NULL, ATH12K_DBG_RTT,
		   "RTT: arvif_from_peer_addr found peer=%pM link_id=%u\n",
		   peer_addr, arsta->link_id);

	return arsta->arvif;
}

static int ath12k_wmi_send_ltf_key_seed_link_extn(struct ath12k_link_vif *lvif,
						   struct ath12k_wmi_ltf_keyseed_arg *arg,
						   const u8 *peer_addr)
{
	int ret;

	ath12k_dbg(lvif->ar->ab, ATH12K_DBG_RTT,
		   "RTT: ltf_keyseed link_id=%u vdev_id=%u peer=%pM\n",
		   lvif->link_id, lvif->vdev_id, peer_addr);

	arg->vdev_id = lvif->vdev_id;
	ret = ath12k_wmi_send_ltf_key_seed(lvif->ar, arg);
	if (ret)
		return ret;

	ath12k_pasn_peer_set_ltf_keyseed(lvif, peer_addr, true);
	return 0;
}

int ath12k_wmi_send_ltf_key_seed_all_radios_extn(struct wireless_dev *wdev,
						 struct ath12k_wmi_ltf_keyseed_arg *arg,
						 const u8 *peer_addr)
{
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct ath12k_vif *ahvif;
	struct ath12k_link_vif *lvif;
	unsigned long links_map;
	u8 link_id;
	int ret;

	if (!vif)
		return -ENOLINK;

	ahvif = ath12k_vif_to_ahvif(vif);

	/* SLO/non-MLO vifs never populate links_map or link[]; the single
	 * link vdev lives in the preallocated deflink instead.
	 */
	if (!ieee80211_vif_is_mld(vif)) {
		lvif = &ahvif->deflink;
		if (!lvif->ar || !lvif->is_started)
			return -ENOLINK;

		return ath12k_wmi_send_ltf_key_seed_link_extn(lvif, arg, peer_addr);
	}

	links_map = ahvif->links_map;
	for_each_set_bit(link_id, &links_map, ATH12K_NUM_MAX_LINKS) {
		lvif = wiphy_dereference(ahvif->ah->hw->wiphy, ahvif->link[link_id]);
		if (!lvif || !lvif->ar || !lvif->is_started)
			continue;

		ret = ath12k_wmi_send_ltf_key_seed_link_extn(lvif, arg, peer_addr);
		if (ret)
			return ret;
	}

	return 0;
}

/* --- RTT vdev helpers (LOWI, unassociated STA) ---
 * Bare STA vdev created solely for WMI_VDEV_PARAM_ENABLE_DISABLE_RTT_INITIATOR_ROLE
 * and the RTT OEM data blob. Not registered with mac80211; kept off ar->arvifs list.
 */
int ath12k_create_rtt_vdev_extn(struct ath12k *ar, const u8 *sta_mac)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_link_vif *arvif;
	struct ath12k_wmi_vdev_create_arg arg = {};
	u8 mac_addr[ETH_ALEN];
	int vdev_id, ret;

	if (ath12k_scan_radio_supported(ar->pdev)) {
		ath12k_warn(ab, "LOWI: cannot create RTT vdev on scan radio\n");
		return -ENODEV;
	}

	arvif = kzalloc(sizeof(*arvif), GFP_KERNEL);
	if (!arvif)
		return -ENOMEM;

	arvif->ar         = ar;
	arvif->arvif_extn.is_rtt_vif = true;
	INIT_LIST_HEAD(&arvif->list); /* stays off ar->arvifs */

	spin_lock_bh(&ab->base_lock);
	if (!ab->free_vdev_map) {
		spin_unlock_bh(&ab->base_lock);
		ath12k_warn(ab, "LOWI: no free vdev_id for RTT vdev\n");
		kfree(arvif);
		return -ENOSPC;
	}
	vdev_id = __ffs64(ab->free_vdev_map);
	ab->free_vdev_map &= ~(1LL << vdev_id);
	spin_unlock_bh(&ab->base_lock);

	arvif->vdev_id      = vdev_id;
	arvif->vdev_subtype = WMI_VDEV_SUBTYPE_NONE;

	/* Use the STA interface MAC so PASN Auth frames carry a valid SA
	 * that cfg80211_mlme_mgmt_tx accepts. Fall back to random if not
	 * provided (e.g. pre-creation during scan before association).
	 */
	if (sta_mac && !is_zero_ether_addr(sta_mac))
		ether_addr_copy(mac_addr, sta_mac);
	else
		eth_random_addr(mac_addr);
	ether_addr_copy(arvif->bssid, mac_addr);
	ether_addr_copy(arvif->addr,  mac_addr);

	arg.if_id        = vdev_id;
	arg.type         = WMI_VDEV_TYPE_STA;
	arg.subtype      = WMI_VDEV_SUBTYPE_NONE;
	arg.pdev_id      = ar->pdev->pdev_id;
	arg.mbssid_flags = WMI_VDEV_MBSSID_FLAGS_NON_MBSSID_AP;
	arg.if_stats_id  = ATH12K_INVAL_VDEV_STATS_ID;

	if (ar->pdev->cap.supported_bands & WMI_HOST_WLAN_2GHZ_CAP) {
		arg.chains[NL80211_BAND_2GHZ].tx = ar->num_tx_chains;
		arg.chains[NL80211_BAND_2GHZ].rx = ar->num_rx_chains;
	}
	if (ar->pdev->cap.supported_bands & WMI_HOST_WLAN_5GHZ_CAP) {
		arg.chains[NL80211_BAND_5GHZ].tx = ar->num_tx_chains;
		arg.chains[NL80211_BAND_5GHZ].rx = ar->num_rx_chains;
	}

	ret = ath12k_wmi_vdev_create(ar, mac_addr, &arg);
	if (ret) {
		ath12k_warn(ab, "LOWI: failed to create RTT vdev %d: %d\n",
			    vdev_id, ret);
		spin_lock_bh(&ab->base_lock);
		ab->free_vdev_map |= 1LL << vdev_id;
		spin_unlock_bh(&ab->base_lock);
		kfree(arvif);
		return ret;
	}

	ar->num_created_vdevs++;
	ar->allocated_vdev_map |= 1LL << vdev_id;
	arvif->is_created = true;
	ar->ar_extn.rtt_vif = arvif;

	/* rtt_vif has no ahvif/vif so ath12k_mac_init_arvif_extn() cannot be
	 * called directly. Initialise only the RTT/PASN context it needs.
	 */
	ath12k_rtt_init_link_vif(arvif);
	ath12k_info(ab, "LOWI: RTT vdev created vdev_id=%d pdev=%u\n",
		    vdev_id, ar->pdev->pdev_id);
	return 0;
}
EXPORT_SYMBOL(ath12k_create_rtt_vdev_extn);

/*
 * ath12k_destroy_rtt_vdev_extn - destroy the dedicated RTT vdev.
 *
 * Tears down the FW PASN peers, cleans SW tracking, resets RTT state,
 * deletes the WMI vdev, and frees host memory. Safe to call when
 * ar->ar_extn.rtt_vif is NULL (no-op).
 */
void ath12k_destroy_rtt_vdev_extn(struct ath12k *ar)
{

        struct ath12k_link_vif *arvif = ar->ar_extn.rtt_vif;
        int ret;

        if (!arvif)
                return;

        /* Delete all FW PASN peers, cancel pending work, and free SW list.
         * ath12k_rtt_deinit_link_vif() handles peer_delete_all + cleanup.
         */
        ath12k_rtt_deinit_link_vif(arvif);

        arvif->arvif_extn.rtt_initiator_role = false;

        ath12k_info(ar->ab,
                   "[radio_idx:%u] rtt_vdev destroy: vdev_id=%u is_started=%d allocated_map=0x%llx\n",
                   ar->radio_idx, arvif->vdev_id, arvif->is_started,
                   ar->allocated_vdev_map);

        /* FW vdev state machine requires STOP → DELETE, but only if
         * the vdev was actually started. Sending WMI_VDEV_STOP_CMDID to a
         * vdev in INIT state causes the FW to not send VDEV_STOPPED_EVENTID,
         * which would make ath12k_mac_vdev_setup_sync() time out for 5s.
         */
        if (arvif->is_started) {
                reinit_completion(&ar->vdev_setup_done);
                ret = ath12k_wmi_vdev_stop(ar, arvif->vdev_id);
                if (ret) {
                        ath12k_warn(ar->ab, "LOWI: failed to stop RTT vdev %d: %d\n",
                                    arvif->vdev_id, ret);
                } else {
                        ret = ath12k_mac_vdev_setup_sync(ar);
                        if (ret)
                                ath12k_warn(ar->ab,
                                            "LOWI: failed to sync RTT vdev %d stop: %d\n",
                                            arvif->vdev_id, ret);
                }
                ar->num_started_vdevs--;
                arvif->is_started = false;
        }


        reinit_completion(&ar->vdev_delete_done);
        ret = ath12k_wmi_vdev_delete(ar, arvif->vdev_id);
        if (ret) {
                ath12k_warn(ar->ab, "LOWI: failed to delete RTT vdev %d: %d\n",
                            arvif->vdev_id, ret);
        } else {
                unsigned long time_left;

                time_left = wait_for_completion_timeout(&ar->vdev_delete_done,
                                                    ATH12K_VDEV_DELETE_TIMEOUT_HZ);
                if (time_left == 0)
                        ath12k_warn(ar->ab,
                                    "LOWI: timeout waiting for RTT vdev %d delete response\n",
                                    arvif->vdev_id);
        }

        spin_lock_bh(&ar->ab->base_lock);
        ar->ab->free_vdev_map |= 1LL << arvif->vdev_id;
        spin_unlock_bh(&ar->ab->base_lock);

        ar->allocated_vdev_map &= ~(1LL << arvif->vdev_id);
        ar->num_created_vdevs--;

        ath12k_info(ar->ab, "LOWI: RTT vdev destroyed vdev_id=%d\n",
                    arvif->vdev_id);
        kfree(arvif);
        ar->ar_extn.rtt_vif = NULL;
}
EXPORT_SYMBOL(ath12k_destroy_rtt_vdev_extn);

/*
 * ath12k_get_rtt_vif_by_vdev_id_extn - find the rtt_vif matching a vdev_id.
 *
 * ath12k_mac_get_arvif_by_vdev_id() only searches mac80211-registered vdevs.
 * The rtt_vif is a bare WMI vdev not registered with mac80211, so it cannot
 * be found that way. Walk all active pdevs and match via ar_extn.rtt_vif.
 *
 * Called with rcu_read_lock held. Returns NULL if no match is found.
 */
struct ath12k_link_vif *
ath12k_get_rtt_vif_by_vdev_id_extn(struct ath12k_base *ab, u32 vdev_id)
{
	struct ath12k_link_vif *arvif = NULL;
	struct ath12k_pdev *pdev;
	struct ath12k *ar;
	int i;

	/* rtt_vif is not registered with mac80211 so
	 * ath12k_mac_get_arvif_by_vdev_id() cannot find it. Walk
	 * pdevs and match via ar_extn.rtt_vif instead.
	 */
	for (i = 0; i < ab->num_radios; i++) {
		pdev = rcu_dereference(ab->pdevs_active[i]);
		if (!pdev)
			continue;
		ar = pdev->ar;
		if (!ar || !ar->ar_extn.rtt_vif)
			continue;
		if (ar->ar_extn.rtt_vif->vdev_id == vdev_id) {
			arvif = ar->ar_extn.rtt_vif;
			ath12k_dbg(ab, ATH12K_DBG_RTT,
				   "RTT: found rtt_vif vdev_id=%u radio_idx=%u\n",
				   vdev_id, ar->radio_idx);
			break;
		}
	}

	return arvif;
}
EXPORT_SYMBOL(ath12k_get_rtt_vif_by_vdev_id_extn);

/*
 * ath12k_ar_has_ap_vdev - return true if radio @ar has at least one AP vdev.
 *
 * Must be called with ar->data_lock held.
 */
static bool ath12k_ar_has_ap_vdev(struct ath12k *ar)
{
	struct ath12k_link_vif *arvif;

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (!arvif->ahvif)
			continue;
		if (arvif->ahvif->vdev_type == WMI_VDEV_TYPE_AP)
			return true;
	}
	return false;
}

/*
 * ath12k_add_rtt_interface_extn - RTT vdev lifecycle at add_interface time.
 *
 * Called from ath12k_mac_op_add_interface_extn() for each new vif.
 *
 * has_ap_vap reflects the whole ah group, not a single radio: an AP vdev
 * on ANY radio means unassociated-STA LOWI ranging can use that real AP
 * vdev as context, so the dedicated RTT vdev is redundant EVERYWHERE, not
 * just on the radio that happens to host the AP.
 *
 * has_ap_vap true (AP vif being added, or an AP vdev already exists on
 * some radio):
 *   Destroy the RTT vdev on every radio that still has one.
 *
 * has_ap_vap false (STA vif being added and no radio has an AP vdev):
 *   Create the RTT vdev on every radio so unassociated LOWI ranging has a
 *   vdev context.
 */
void ath12k_add_rtt_interface_extn(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct wireless_dev *wdev = ieee80211_vif_to_wdev(vif);
	struct ath12k *ar;
	const u8 *sta_mac;
	bool has_ap_vap;
	int i;

	sta_mac = (wdev && wdev->netdev) ? wdev->netdev->dev_addr : NULL;

	ath12k_dbg(NULL, ATH12K_DBG_RTT,
		   "RTT: add_interface vdev_type=%u mac=%pM\n",
		   ahvif->vdev_type, vif->addr);

	/* Pass 1: determine has_ap_vap across ALL radios before deciding
	 * anything, so pass 2 applies the same decision uniformly.
	 */
	has_ap_vap = (ahvif->vdev_type == WMI_VDEV_TYPE_AP);

	if (!has_ap_vap) {
		for_each_ar(ah, ar, i) {
			spin_lock_bh(&ar->data_lock);
			if (ath12k_ar_has_ap_vdev(ar))
				has_ap_vap = true;
			spin_unlock_bh(&ar->data_lock);
			if (has_ap_vap)
				break;
		}
	}

	/* Pass 2: apply has_ap_vap uniformly to every radio. */
	for_each_ar(ah, ar, i) {
		ath12k_info(NULL,
			   "RTT: add_interface radio_idx=%u vdev_type=%u has_ap_vap=%d rtt_vif=%s\n",
			   ar->radio_idx, ahvif->vdev_type, has_ap_vap,
			   ar->ar_extn.rtt_vif ? "present" : "absent");

		if (has_ap_vap) {
			if (ar->ar_extn.rtt_vif)
				ath12k_destroy_rtt_vdev_extn(ar);
		} else if (ahvif->vdev_type == WMI_VDEV_TYPE_STA) {
			ath12k_create_rtt_vdev_extn(ar, sta_mac);
		}
	}
}
EXPORT_SYMBOL(ath12k_add_rtt_interface_extn);
