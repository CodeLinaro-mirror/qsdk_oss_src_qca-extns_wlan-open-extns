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
#include "ath12k_cmn_extn.h"
#include "ranging_extn.h"

/**
 * ath12k_wmi_vdev_add_mac_addr_rx_filter_extn() - install/remove a MAC-addr
 * RX filter on a vdev
 * @ar: ath12k radio the vdev belongs to
 * @vdev_id: vdev to install the filter on
 * @mac: MAC address to filter on
 * @freq: channel frequency (MHz) the filter applies to
 * @enable: true to install the filter, false to remove it
 *
 * Sends WMI_VDEV_ADD_MAC_ADDR_TO_RX_FILTER_CMDID so the dedicated rtt_vif
 * can receive PASN Auth frames addressed to @mac while parked on @freq.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ath12k_wmi_vdev_add_mac_addr_rx_filter_extn(struct ath12k *ar, u32 vdev_id,
						const u8 *mac, u32 freq, bool enable)
{
	struct ath12k_wmi_pdev *wmi = ar->wmi;
	struct wmi_vdev_add_mac_addr_to_rx_filter_cmd *cmd;
	struct sk_buff *skb;
	int ret;

	skb = ath12k_wmi_alloc_skb(wmi->wmi_ab, sizeof(*cmd));
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_add_mac_addr_to_rx_filter_cmd *)skb->data;
	cmd->tlv_header = ath12k_wmi_tlv_cmd_hdr(
			WMI_TAG_VDEV_ADD_MAC_ADDR_TO_RX_FILTER_CMD,
			sizeof(*cmd));
	cmd->vdev_id = cpu_to_le32(vdev_id);
	cmd->freq    = cpu_to_le32(freq);
	ether_addr_copy(cmd->mac_addr, mac);
	cmd->enable  = cpu_to_le32(enable ? 1 : 0);

	ath12k_dbg(ar->ab, ATH12K_DBG_RTT,
		   "RTT PASN rx_filter: vdev=%u freq=%u mac=%pM enable=%d\n",
		   vdev_id, freq, mac, enable);

	ret = ath12k_wmi_cmd_send(wmi, skb,
				  WMI_VDEV_ADD_MAC_ADDR_TO_RX_FILTER_CMDID);
	if (ret) {
		ath12k_warn(ar->ab,
			    "failed to send WMI_VDEV_ADD_MAC_ADDR_TO_RX_FILTER vdev=%u: %d\n",
			    vdev_id, ret);
		dev_kfree_skb(skb);
	}

	return ret;
}
EXPORT_SYMBOL(ath12k_wmi_vdev_add_mac_addr_rx_filter_extn);

int start_rtt_vif(struct ath12k *ar, struct ieee80211_hw *hw,
		struct cfg80211_chan_def *chandef, int duration,
		enum ieee80211_roc_type type)
{
	struct ieee80211_channel *chan;
	chan = chandef->chan;
	struct wmi_vdev_start_req_arg vstart = {};
	int ret;
	/* PASN Auth direct path: use rtt_vif without scan vdev or WMI scan.
	 * pasn_auth_freq is learned here from chandef (CMD_FRAME freq).
	 * vdev_stop + vdev_start are required for channel retune.
	 */
	if (!ar->ar_extn.rtt_vif)
		return -1;

	ath12k_err(ar->ab,
			"[radio_idx:%u] ROC: PASN direct path - starting rtt vdev rtt_vif vdev=%u pasn_freq=%u\n",
			ar->radio_idx, ar->ar_extn.rtt_vif->vdev_id, chan->center_freq);

	ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.pasn_auth_freq = chan->center_freq;

	/* rtt_vif has ahvif=NULL — ath12k_mac_vdev_stop/start dereference
	 * ahvif and crash.  Use the low-level WMI primitives directly,
	 * same pattern as pasn_roc_request_extn.
	 */
	if (ar->ar_extn.rtt_vif->is_started) {
		reinit_completion(&ar->vdev_setup_done);
		ret = ath12k_wmi_vdev_stop(ar, ar->ar_extn.rtt_vif->vdev_id);
		if (ret) {
			ath12k_err(ar->ab,
					"[radio_idx:%u] ROC: PASN wmi_vdev_stop failed ret=%d\n",
					ar->radio_idx, ret);
			return ret;
		}
		ret = ath12k_mac_vdev_setup_sync(ar);
		if (ret)
			ath12k_warn(ar->ab,
					"[radio_idx:%u] ROC: PASN vdev_stop sync failed ret=%d\n",
					ar->radio_idx, ret);
		ar->ar_extn.rtt_vif->is_started = false;
	}

	vstart.vdev_id           = ar->ar_extn.rtt_vif->vdev_id;
	vstart.freq              = chan->center_freq;
	vstart.band_center_freq1 = chan->center_freq;
	vstart.band_center_freq2 = 0;
	vstart.mode              = ath12k_mac_get_phymode(ar, chan->band,
			NL80211_CHAN_WIDTH_20);
	vstart.min_power         = 0;
	vstart.max_power         = chan->max_power;
	vstart.max_reg_power     = chan->max_reg_power;
	vstart.max_antenna_gain  = chan->max_antenna_gain;
	vstart.pref_tx_streams   = ar->num_tx_chains;
	vstart.pref_rx_streams   = ar->num_rx_chains;
	vstart.bcn_intval        = 100;
	vstart.dtim_period       = 1;
	vstart.disable_hw_ack    = true;

	reinit_completion(&ar->vdev_setup_done);
	ret = ath12k_wmi_vdev_start(ar, &vstart, false);
	if (ret) {
		ath12k_err(ar->ab,
				"[radio_idx:%u] ROC: PASN wmi_vdev_start failed ret=%d\n",
				ar->radio_idx, ret);
		ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.pasn_auth_freq = 0;
		return ret;
	}
	ret = ath12k_mac_vdev_setup_sync(ar);
	if (ret) {
		ath12k_err(ar->ab,
				"[radio_idx:%u] ROC: PASN vdev_start sync failed ret=%d\n",
				ar->radio_idx, ret);
		ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.pasn_auth_freq = 0;
		return ret;
	}
	ar->ar_extn.rtt_vif->is_started = true;

	/* Install RX filter on rtt_vif at the AP's channel so Auth2 arrives.
	 * Use current_ranging_wdev's MAC as the filter -- this is the
	 * interface MAC wpa_supplicant uses as SA in PASN Auth frames.
	 */
	{
		struct wireless_dev *ranging_wdev =
			ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev;
		const u8 *filter_mac;

		if (ranging_wdev && ranging_wdev->netdev)
			filter_mac = ranging_wdev->netdev->dev_addr;
		else
			filter_mac = ar->ar_extn.rtt_vif->addr;

		/* Store filter_mac in rtt_ctx so ath12k_mac_op_remain_on_channel can
		 * install the filter on the scan vdev with the correct MAC.
		 */
		ether_addr_copy(ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.filter_mac,
				filter_mac);
	}

	if (!is_zero_ether_addr(ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.filter_mac)) {
		int filter_ret = ath12k_wmi_vdev_add_mac_addr_rx_filter_extn(
				ar, ar->ar_extn.rtt_vif->vdev_id,
				ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.filter_mac,
				chan->center_freq, true);
		if (filter_ret)
			ath12k_warn(ar->ab,
					"[radio_idx:%u] ROC: PASN rx_filter failed ret=%d\n",
					ar->radio_idx, filter_ret);
	}

	/* Signal ROC ready immediately — no WMI scan needed */
	ieee80211_ready_on_channel(hw);
	return 0;
}

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

	/* Mark session as waiting for FW measurement result */
	if (ar->ar_extn.rtt_vif)
		ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.ranging_state |=
			ATH12K_RTT_STATE_WAITING_MEAS;

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

	/* Clear WAITING_MEAS bit; clear current_ranging_wdev when state reaches 0 */
	if (ar && ar->ar_extn.rtt_vif) {
		ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.ranging_state &=
			~ATH12K_RTT_STATE_WAITING_MEAS;
		if (!ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.ranging_state) {
			ath12k_dbg(ar->ab, ATH12K_DBG_RTT,
				   "LOWI: ranging_state=0, clearing current_ranging_wdev on rtt_vif vdev=%u\n",
				   ar->ar_extn.rtt_vif->vdev_id);
			ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev = NULL;
		} else {
			ath12k_dbg(ar->ab, ATH12K_DBG_RTT,
				   "LOWI: ranging_state=0x%x, NOT clearing current_ranging_wdev on rtt_vif vdev=%u\n",
				   ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.ranging_state,
				   ar->ar_extn.rtt_vif->vdev_id);
		}
	}

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

struct ath12k_link_vif *get_ranging_mgmt_arvif(struct ieee80211_hw *hw,
					       struct ieee80211_vif *vif,
					       struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ieee80211_mgmt *auth_mgmt;
	struct wireless_dev *sta_wdev = NULL;
	struct ath12k_link_vif *arvif = NULL;
	struct ath12k *ar_iter;
	int i;

	if (!ieee80211_is_auth(hdr->frame_control))
		return NULL;

	auth_mgmt = (struct ieee80211_mgmt *)hdr;
	if (skb->len < (IEEE80211_MIN_ACTION_SIZE +
			sizeof(auth_mgmt->u.auth.auth_alg)) ||
	    le16_to_cpu(auth_mgmt->u.auth.auth_alg) != 7 /* WLAN_AUTH_PASN */)
		return NULL;

	sta_wdev = ieee80211_vif_to_wdev(vif);

	ath12k_dbg(NULL, ATH12K_DBG_RTT,
		   "get_ranging_mgmt_arvif: PASN Auth frame detected, searching rtt_vif for wdev=%p\n",
		   sta_wdev);

	/* Use current_ranging_wdev (set at RTT_START, cleared only on
	 * rtt_vif destroy) rather than pasn_auth_freq (cleared by retune
	 * after each session) so the redirect works on the 3rd+ session
	 * when the coalescing ROC path skips remain_on_channel.
	 */
	for_each_ar(ah, ar_iter, i) {
		if (ar_iter->ar_extn.rtt_vif &&
		    ar_iter->ar_extn.rtt_vif->is_started &&
		    ar_iter->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev &&
		    ar_iter->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev
			== sta_wdev) {
			ath12k_err(ar_iter->ab,
				   "[radio_idx:%u] wifi7_mgmt_handler: PASN Auth redirect to rtt_vif vdev=%u\n",
				   ar_iter->radio_idx,
				   ar_iter->ar_extn.rtt_vif->vdev_id);
			arvif = ar_iter->ar_extn.rtt_vif;
			break;
		}
	}

	if (!arvif)
		ath12k_dbg(NULL, ATH12K_DBG_RTT,
			   "get_ranging_mgmt_arvif: no matching rtt_vif found for wdev=%p\n",
			   sta_wdev);

	return arvif;
}
EXPORT_SYMBOL(get_ranging_mgmt_arvif);

struct wireless_dev *get_pasn_auth_wdev_extn(struct ath12k_link_vif *arvif)
{
        struct wireless_dev *wdev;

        if (arvif->ahvif && arvif->ahvif->vif) {
                wdev = ieee80211_vif_to_wdev(arvif->ahvif->vif);
                if (!wdev)
                        return NULL;
                ath12k_dbg(arvif->ar->ab, ATH12K_DBG_RTT,
                           "RTT PASN send_event: wdev=%p netdev=%s\n",
                           wdev,
                           wdev->netdev ? wdev->netdev->name : "<no netdev>");
        } else {
                /* rtt_arvif: use the wdev captured from the RTT_START command so
                 * the event is delivered to the correct LOWI netlink socket.
                 */
                wdev = arvif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev;
                ath12k_dbg(arvif->ar->ab, ATH12K_DBG_RTT,
                           "RTT PASN send_event: rtt_arvif using current_ranging_wdev=%p netdev=%s\n",
                           wdev, (wdev && wdev->netdev) ? wdev->netdev->name : "<none>");
        }
	return wdev;
}


/**
 * ath12k_pasn_vendor_event_sent_extn() - mark rtt_vif as awaiting PASN auth
 * status from userspace
 * @arvif: link vif on which the PASN vendor event was just sent
 *
 * Call right after a successful ath12k_vendor_send_pasn_event() so a
 * subsequent LOWI OEM data send knows a PASN handshake is still pending on
 * this rtt_vif.
 */
void ath12k_pasn_vendor_event_sent_extn(struct ath12k_link_vif *arvif)
{
	if (!arvif->arvif_extn.is_rtt_vif)
		return;

	arvif->rtt_ctx.rtt_ctx_extn.ranging_state |= ATH12K_RTT_STATE_WAITING_PASN;
}
EXPORT_SYMBOL(ath12k_pasn_vendor_event_sent_extn);

/**
 * ath12k_rtt_arvif_cleanup_extn() - stop the rtt_vif and clear PASN state
 * once auth status has been delivered for all peers
 * @arvif: link vif on which the PASN auth status WMI command was just sent
 *
 * Stops the radio's dedicated rtt_vif and retunes away from the PASN Auth
 * channel -- FW manages the ranging channel after WMI auth status, so no
 * vdev_start is needed here. Clears ATH12K_RTT_STATE_WAITING_PASN, and if no
 * other ranging_state bits remain set, clears current_ranging_wdev too.
 * Finally signals mac80211 that the ROC session for the PASN Auth exchange
 * is done -- the PASN direct path never runs a WMI scan, so scan_finish
 * never calls ieee80211_remain_on_channel_expired(); without this the ROC
 * entry is never destroyed and every subsequent CMD_FRAME takes the
 * coalescing path.
 */
void ath12k_rtt_arvif_cleanup_extn(struct ath12k_link_vif *arvif)
{
	struct ath12k *rtt_ar = arvif->ar;

	ath12k_dbg(rtt_ar->ab, ATH12K_DBG_RTT,
			"RTT: rtt_arvif_cleanup_extn enter vdev=%u\n", arvif->vdev_id);

	/* Only the dedicated rtt_vif used for STA unassociated ranging needs
	 * this vdev-stop/ROC-expiry cleanup -- AP-mode PASN peers never go
	 * through offchannel ROC, so skip for any other arvif.
	 */
	if (!arvif->arvif_extn.is_rtt_vif) {
		ath12k_dbg(rtt_ar->ab, ATH12K_DBG_RTT,
				"RTT: rtt_arvif_cleanup_extn skip, vdev=%u is not the STA unassoc rtt_vif\n",
				arvif->vdev_id);
		return;
	}

	if (rtt_ar && rtt_ar->ar_extn.rtt_vif) {
		int stop_ret;

		ath12k_err(rtt_ar->ab,
				"[radio_idx:%u] PASN cmd: stopping rtt_arvif vdev=%u (pasn_freq=%u, FW handles ranging channel)\n",
				rtt_ar->radio_idx, rtt_ar->ar_extn.rtt_vif->vdev_id,
				rtt_ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.pasn_auth_freq);

		/* Stop rtt_vif after PASN -- no vdev_start needed here.
		 * FW manages the ranging channel after WMI auth status.
		 */
		stop_ret = ath12k_wmi_vdev_stop(rtt_ar,
				rtt_ar->ar_extn.rtt_vif->vdev_id);
		if (stop_ret)
			ath12k_err(rtt_ar->ab,
					"[radio_idx:%u] PASN cmd: wmi_vdev_stop failed ret=%d\n",
					rtt_ar->radio_idx, stop_ret);

		rtt_ar->ar_extn.rtt_vif->is_started = false;
		rtt_ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.pasn_auth_freq = 0;

		/* Clear WAITING_PASN bit; clear current_ranging_wdev if idle */
		rtt_ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.ranging_state &=
			~ATH12K_RTT_STATE_WAITING_PASN;
		/*
		 * Once PASN auth comes we can clear current_ranging_wdev irrespective of state
		 * Sometimes if FW does not send response, hw_scan: can be blocked
		 * clear current_ranging_wdev.
                 */
		if (!rtt_ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.ranging_state) {
			ath12k_err(rtt_ar->ab,
					"[radio_idx:%u] PASN cmd: ranging_state=0 clearing current_ranging_wdev\n",
					rtt_ar->radio_idx);
			rtt_ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev = NULL;
		} else {
			ath12k_err(rtt_ar->ab,
					"[radio_idx:%u] PASN cmd: Force clearing current_ranging_wdev\n",
					rtt_ar->radio_idx);
			rtt_ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev = NULL;
		} 

	} else {

		ath12k_dbg(rtt_ar->ab, ATH12K_DBG_RTT,
				"RTT: rtt_arvif_cleanup_extn enter vdev=%u  This should not happen\n", arvif->vdev_id);

	}

	/* Signal mac80211 that the ROC (Remain-On-Channel) session for
	 * the PASN Auth exchange is done. In the PASN direct path there
	 * is no WMI scan, so scan_finish never calls this. Without it
	 * the mac80211 ROC entry is never destroyed and every subsequent
	 * CMD_FRAME takes the coalescing path (roc_list_empty=0).
	 */
	if (rtt_ar && rtt_ar->ah) {
		ath12k_dbg(rtt_ar->ab, ATH12K_DBG_RTT,
				"[radio_idx:%u] RTT: rtt_arvif_cleanup_extn signalling ROC expired\n",
				rtt_ar->radio_idx);
		ieee80211_remain_on_channel_expired(rtt_ar->ah->hw);
	}
}
EXPORT_SYMBOL(ath12k_rtt_arvif_cleanup_extn);


/*
 * get_ranging_pasn_auth_freq - return the PASN Auth target frequency
 * (MHz) that start_rtt_vif() tuned the dedicated rtt_vif to, or 0 if no
 * PASN session is active on this radio's rtt_vif.
 */
u32 get_ranging_pasn_auth_freq(struct ath12k *ar)
{
	u32 freq;

	if (!ar->ar_extn.rtt_vif)
		return 0;

	freq = ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.pasn_auth_freq;
	if (freq)
		ath12k_dbg(ar->ab, ATH12K_DBG_RTT,
			   "[radio_idx:%u] get_ranging_pasn_auth_freq: rtt_vif vdev=%u freq=%u\n",
			   ar->radio_idx, ar->ar_extn.rtt_vif->vdev_id, freq);

	return freq;
}
EXPORT_SYMBOL(get_ranging_pasn_auth_freq);

/*
 * ranging_redirect_mgmt_tx_wmi - redirect a queued PASN Auth mgmt skb to
 * the dedicated RTT/ranging vdev (rtt_vif) for WMI TX, instead of the
 * normal per-link arvif lookup in ath12k_mgmt_over_wmi_tx_work(), which
 * would fail for this skb since rtt_vif has no ahvif/link registration.
 *
 * Returns true if this skb was a PASN Auth frame for an active rtt_vif
 * session and was handled (sent or dropped) here; false otherwise, so
 * the caller falls through to its normal skb_cb->vif-based lookup.
 */
bool ranging_redirect_mgmt_tx_wmi(struct ath12k *ar, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	int ret;

	if (!ieee80211_is_auth(hdr->frame_control) ||
	    !ar->ar_extn.rtt_vif || !ar->ar_extn.rtt_vif->is_started ||
	    !ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.pasn_auth_freq)
		return false;

	ath12k_err(ar->ab,
		   "[radio_idx:%u] mgmt_tx_work: PASN Auth redirect to rtt_vif vdev=%u freq=%u\n",
		   ar->radio_idx, ar->ar_extn.rtt_vif->vdev_id,
		   ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.pasn_auth_freq);

	/* The frame was enqueued with TX_OFFCHAN set so num_pending_mgmt_tx was
	 * NOT incremented. We're sending via WMI_MGMT_TX_SEND_CMDID whose
	 * completion path unconditionally decrements it — increment here to
	 * balance that. Clear TX_OFFCHAN so ath12k_mac_mgmt_tx_wmi() takes the
	 * WMI_MGMT_TX_SEND_CMDID path instead of the offchan data path; restore
	 * it on failure so the drop path below does not decrement a second time.
	 */
	atomic_inc(&ar->num_pending_mgmt_tx);
	tx_info->flags &= ~IEEE80211_TX_CTL_TX_OFFCHAN;
	ret = ath12k_mac_mgmt_tx_wmi(ar, ar->ar_extn.rtt_vif, skb);
	if (ret) {
		tx_info->flags |= IEEE80211_TX_CTL_TX_OFFCHAN;
		ath12k_err(ar->ab,
			   "[radio_idx:%u] mgmt_tx_work: PASN Auth TX failed ret=%d\n",
			   ar->radio_idx, ret);
		atomic_dec(&ar->num_pending_mgmt_tx);
		ath12k_mgmt_over_wmi_tx_drop(ar, skb);
	} else {
		ath12k_dbg(ar->ab, ATH12K_DBG_RTT,
			   "[radio_idx:%u] mgmt_tx_work: PASN Auth TX success vdev=%u\n",
			   ar->radio_idx, ar->ar_extn.rtt_vif->vdev_id);
	}

	return true;
}
EXPORT_SYMBOL(ranging_redirect_mgmt_tx_wmi);

/*
 * ath12k_ranging_deliver_pasn_mgmt_rx_extn - deliver an Auth/Deauth frame
 * received on the dedicated RTT/ranging vdev (rtt_vif) directly to
 * wpa_supplicant via cfg80211_rx_mgmt() on current_ranging_wdev.
 * rtt_vif has no registered mac80211 vif, so the normal ieee80211_rx_ni()
 * path cannot deliver to it.
 *
 * Caller (ath12k_mgmt_rx_event(), right after
 * arvif = ath12k_mac_get_arvif(ar, vdev_id)) holds a nested
 * rcu_read_lock() and ar->data_lock across this call; both are dropped
 * here before calling cfg80211_rx_mgmt() (which can sleep) whenever this
 * returns true. The outer rcu_read_lock() taken at the top of
 * ath12k_mgmt_rx_event() is left untouched — the caller must still
 * `goto exit` (not `return`) so that outer lock gets its matching
 * rcu_read_unlock() there.
 *
 * Returns true if this was a PASN Auth/Deauth frame for an active
 * rtt_vif ranging session and was delivered (and the skb freed) here;
 * false otherwise, so the caller proceeds with normal RX processing.
 */
bool ath12k_ranging_deliver_pasn_mgmt_rx_extn(struct ath12k *ar, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);

	if (!ar->ar_extn.rtt_vif || !ar->ar_extn.rtt_vif->is_started ||
	    !ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev ||
	    !ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.pasn_auth_freq ||
	    !(ieee80211_is_auth(hdr->frame_control) ||
	      ieee80211_is_deauth(hdr->frame_control)))
		return false;

	ath12k_err(ar->ab,
		   "[radio_idx:%u] mgmt_rx: PASN Auth/Deauth on rtt_vif vdev=%u — delivering via cfg80211_rx_mgmt freq=%u\n",
		   ar->radio_idx, ar->ar_extn.rtt_vif->vdev_id,
		   ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.pasn_auth_freq);

	rcu_read_unlock();
	spin_unlock_bh(&ar->data_lock);
	cfg80211_rx_mgmt(ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev,
			 ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.pasn_auth_freq,
			 status->signal, skb->data, skb->len, 0);
	dev_kfree_skb(skb);

	return true;
}
EXPORT_SYMBOL(ath12k_ranging_deliver_pasn_mgmt_rx_extn);

/*
 * ath12k_rtt_hw_scan_blocked_extn - check whether background scans must be
 * blocked on this radio because RTT ranging is active.
 *
 * current_ranging_wdev is set at RTT_START and cleared when the measurement
 * result is returned to userspace. A scan during this window would retune
 * the radio, breaking the ongoing RTT session. rtt_vif alone is not
 * sufficient -- it exists from pre-association scan time and persists until
 * STA associates.
 *
 * Returns true if hw_scan must be blocked; false otherwise.
 */
bool ath12k_rtt_hw_scan_blocked_extn(struct ath12k *ar)
{
	if (!ar->ar_extn.rtt_vif ||
	    !ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev)
		return false;

	ath12k_err(ar->ab,
		   "[radio_idx:%u] hw_scan: blocked — RTT ranging active (rtt_vif vdev=%u wdev=%p)\n",
		   ar->radio_idx, ar->ar_extn.rtt_vif->vdev_id,
		   ar->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev);

	return true;
}
EXPORT_SYMBOL(ath12k_rtt_hw_scan_blocked_extn);

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

			ath12k_dbg(NULL, ATH12K_DBG_RTT, "LOWI RTT Role vdev=%d pdev=%u link_id=%d\n",
					vdev_id, pdev_id, link_id);

			ath12k_wmi_vdev_set_param_cmd(ar, vdev_id,
					WMI_VDEV_PARAM_ENABLE_DISABLE_RTT_INITIATOR_ROLE,
					1);
		} else {
			/* STA Un assoc case */
			ar = ath12k_ah_to_ar(ah, (u8)radio_idx);
			if (!ar) {
				ath12k_err(NULL, "LOWI: no ar for radio_idx=%d STA Un Assoc case\n",
						radio_idx);
				return -EINVAL;
			}
			ath12k_info(NULL, "LOWI: STA Un Assoc case ar resolved via radio_idx=%d wdev=%p netdev=%s\n",
					radio_idx, wdev,
					(wdev && wdev->netdev) ? wdev->netdev->name : "<none>");

			/* Cancel any ongoing scan and tear down its scan vdev
			 * on every radio (not just this ranging radio) before
			 * sending the ranging request -- a live scan vdev on
			 * any radio would otherwise contend with the
			 * dedicated rtt_vif for the off-channel ranging
			 * exchange. ath12k_mac_op_cancel_hw_scan() walks all
			 * of vif's scan links (one per radio) and is a no-op
			 * per link when that radio's scan is already idle.
			 */
			ath12k_mac_op_cancel_hw_scan(hw, vif);

			if (ar->ar_extn.rtt_vif) {
				arvif = ar->ar_extn.rtt_vif;
				vdev_id = arvif->vdev_id;
				ath12k_wmi_vdev_set_param_cmd(ar, vdev_id,
						WMI_VDEV_PARAM_ENABLE_DISABLE_RTT_INITIATOR_ROLE,
						1);
				arvif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev = wdev;
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

/**
 * ath12k_rtt_arvif_from_wdev_extn() - Find the rtt_vif whose current_ranging_wdev
 * matches @wdev.
 *
 * Used by ath12k_vendor_pasn_cmd() and ath12k_arvif_from_peer_addr_extn() as a
 * fallback when the normal arvif lookup cannot resolve the arvif via link_id /
 * peer addr (unassociated ranging — no started STA arvif exists yet).
 *
 * Returns the rtt_vif on success, NULL if not found.
 */
struct ath12k_link_vif *
ath12k_rtt_arvif_from_wdev_extn(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar_tmp;
	int i;

	if (!vif)
		return NULL;

	if (vif->cfg.assoc) {
		/* PASN should come for un assoc only return NULL */
		return NULL;
	}

	for_each_ar(ah, ar_tmp, i) {
		if (!ar_tmp->ar_extn.rtt_vif)
			continue;
		if (ar_tmp->ar_extn.rtt_vif->rtt_ctx.rtt_ctx_extn.current_ranging_wdev
		    == wdev) {
			ath12k_dbg(ar_tmp->ab, ATH12K_DBG_RTT,
				   "RTT: rtt_arvif_from_wdev matched wdev=%p vdev=%u\n",
				   wdev, ar_tmp->ar_extn.rtt_vif->vdev_id);
			return ar_tmp->ar_extn.rtt_vif;
		}
	}

	ath12k_dbg(NULL, ATH12K_DBG_RTT,
		   "RTT: rtt_arvif_from_wdev no rtt_vif matched wdev=%p\n", wdev);
	return NULL;
}
EXPORT_SYMBOL(ath12k_rtt_arvif_from_wdev_extn);

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

	ath12k_dbg(NULL, ATH12K_DBG_RTT,
		   "RTT: ltf_keyseed_all_radios enter wdev=%p peer=%pM vdev_id=%u rsn_authmode=%u keyseed_len=%u\n",
		   wdev, peer_addr, arg->vdev_id, arg->rsn_authmode, arg->keyseed_len);

	if (!vif) {
		ath12k_dbg(NULL, ATH12K_DBG_RTT,
			   "RTT: ltf_keyseed_all_radios no vif for wdev=%p\n", wdev);
		return -ENOLINK;
	}

	ahvif = ath12k_vif_to_ahvif(vif);

	/* STA not yet associated: there is no real per-link vdev to walk.
	 * Plumb the keyseed onto every radio's dedicated unassociated-RTT
	 * vdev (ar->ar_extn.rtt_vif) instead.
	 */
	if (vif->type == NL80211_IFTYPE_STATION && !vif->cfg.assoc) {
		struct ath12k_hw *ah = ahvif->ah;
		struct ath12k *ar;
		int i;

		for_each_ar(ah, ar, i) {
			lvif = ar->ar_extn.rtt_vif;
			ath12k_dbg(NULL, ATH12K_DBG_RTT,
				   "RTT: ltf_keyseed_all_radios unassoc radio_idx=%u rtt_vif=%p\n",
				   ar->radio_idx, lvif);
			if (!lvif)
				continue;

			ret = ath12k_wmi_send_ltf_key_seed_link_extn(lvif, arg, peer_addr);
			if (ret)
				return ret;
		}

		return 0;
	}

	/* SLO/non-MLO vifs never populate links_map or link[]; the single
	 * link vdev lives in the preallocated deflink instead.
	 */
	if (!ieee80211_vif_is_mld(vif)) {
		lvif = &ahvif->deflink;
		ath12k_dbg(NULL, ATH12K_DBG_RTT,
			   "RTT: ltf_keyseed_all_radios non-MLD deflink ar=%p is_started=%d vdev_id=%u\n",
			   lvif->ar, lvif->is_started, lvif->vdev_id);
		if (!lvif->ar || !lvif->is_started)
			return -ENOLINK;

		return ath12k_wmi_send_ltf_key_seed_link_extn(lvif, arg, peer_addr);
	}

	links_map = ahvif->links_map;
	ath12k_dbg(NULL, ATH12K_DBG_RTT,
		   "RTT: ltf_keyseed_all_radios MLD links_map=0x%lx\n", links_map);
	for_each_set_bit(link_id, &links_map, ATH12K_NUM_MAX_LINKS) {
		lvif = wiphy_dereference(ahvif->ah->hw->wiphy, ahvif->link[link_id]);
		ath12k_dbg(NULL, ATH12K_DBG_RTT,
			   "RTT: ltf_keyseed_all_radios link_id=%u lvif=%p ar=%p is_started=%d\n",
			   link_id, lvif, lvif ? lvif->ar : NULL,
			   lvif ? lvif->is_started : 0);
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

	spin_lock_bh(&ar->data_lock);
	if (ar->created_vdev_map & (1LL << vdev_id))
		ath12k_err(ab, "vdev_id %d already set in the created vdev map = 0x%llx\n",
			   vdev_id, ar->created_vdev_map);
	else
		ar->created_vdev_map |= 1LL << vdev_id;
	spin_unlock_bh(&ar->data_lock);

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

        if (arvif->is_created) {
                spin_lock_bh(&ar->data_lock);
                ar->created_vdev_map &= ~(1LL << arvif->vdev_id);
                spin_unlock_bh(&ar->data_lock);
        }

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
