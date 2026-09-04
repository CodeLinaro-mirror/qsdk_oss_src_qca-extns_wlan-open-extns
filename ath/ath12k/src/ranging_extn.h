/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef RANGING_EXTN_H
#define RANGING_EXTN_H

/* LOWI channel info entry used by the channel-info response builder */
struct ath12k_lowi_wmi_channel {
	__le32 mhz;
	__le32 band_center_freq1;
	__le32 band_center_freq2;
	__le32 info;
	__le32 reg_info_1;
	__le32 reg_info_2;
} __packed;

struct ath12k_lowi_channel_info_entry {
	__le32 chan_id;
	__le32 reserved0;
	struct ath12k_lowi_wmi_channel channel_info;
} __packed;

struct ath12k_wmi_lowi_oem_resp {
	const u8 *data1;
	const u8 *data2;
	u32 len1;
	u32 len2;
	bool indirect;
};

#define ATH12K_LOWI_OEM_TIMEOUT_HZ (3 * HZ)
/* reserved0[7:0] = radio_idx */
#define ATH12K_LOWI_RESERVED0_RADIO_IDX_MASK		0xff
#define ATH12K_LOWI_RESERVED0_RADIO_IDX_SHIFT		0
#define ATH12K_LOWI_RESERVED0_SET_RADIO_IDX(v)		\
	(((v) & ATH12K_LOWI_RESERVED0_RADIO_IDX_MASK) << ATH12K_LOWI_RESERVED0_RADIO_IDX_SHIFT)

/* Bit 10 of ch_info.info indicates DFS — mirrors WIFI_POS_FLAG_DFS */
#define ATH12K_LOWI_CH_INFO_FLAG_DFS			BIT(10)
/* info[5:0] encodes phy_mode (channel mode bits) */
#define ATH12K_LOWI_CH_INFO_MODE_MASK			0x3f
/* reg_info_1[23:16] = reg power (dBm); reg_info_2[15:8] = max tx power */
#define ATH12K_LOWI_REG_INFO1_SET_PWR(v)		(((v) & 0xff) << 16)
#define ATH12K_LOWI_REG_INFO2_SET_PWR(v)		(((v) & 0xff) << 8)

/**
 * ath12k_vendor_send_lowi_oem_data_extn() - Send a LOWI OEM data event to userspace
 * @hw: ieee80211_hw instance whose wiphy the vendor event is sent on
 * @ar: ath12k radio the data was received on, used to fill the LINK_ID
 *      attribute; may be NULL if the data is not associated with a radio
 * @data: OEM response payload to attach as vendor event data, may be NULL
 *        if @len is 0
 * @len: length of @data in bytes
 *
 * Wraps the payload in a QCA_NL80211_VENDOR_SUBCMD_OEM_DATA_INDEX vendor
 * event of type QCA_LOWI_EVT_MEAS_RESULT and delivers it to the LOWI
 * userspace daemon.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ath12k_vendor_send_lowi_oem_data_extn(struct ieee80211_hw *hw,
					  struct ath12k *ar,
					  const u8 *data, u32 len);

/**
 * ath12k_wmi_lowi_oem_response_event_extn() - Handle WMI_OEM_RESPONSE_EVENTID
 * @ab: ath12k_base the event was received on
 * @skb: WMI event skb containing the TLV-encoded OEM response
 *
 * Parses the direct-data TLVs out of the firmware's OEM response, stitching
 * together up to two byte arrays into a single buffer when the response is
 * split across TLVs, then forwards the result to userspace via
 * ath12k_vendor_send_lowi_oem_data_extn(). Indirect DMA data is not
 * supported and is logged as a warning. Completes ar_extn.lowi_oem_done to
 * unblock any caller waiting in ath12k_wmi_send_lowi_oem_data_extn().
 */
void ath12k_wmi_lowi_oem_response_event_extn(struct ath12k_base *ab,
					     struct sk_buff *skb);

/**
 * ath12k_rtt_arvif_from_wdev_extn() - Find the rtt_vif whose current_ranging_wdev
 * matches @wdev
 * @wiphy: wiphy the vendor command was issued on, used to reach ath12k_hw
 * @wdev: wireless_dev to match against each radio's rtt_vif
 *
 * Used as a fallback when the normal STA/AP arvif lookup cannot resolve an
 * arvif (unassociated ranging — no started STA arvif exists yet).
 *
 * Return: the matching rtt_vif, or NULL if not found.
 */
struct ath12k_link_vif *ath12k_rtt_arvif_from_wdev_extn(struct wiphy *wiphy,
							struct wireless_dev *wdev);

/**
 * ath12k_wmi_send_ltf_key_seed_all_radios_extn() - Push LTF keyseed to every
 * associated link vdev of a STA vif, MLO or SLO
 * @wdev: wireless_dev the vendor command was issued on (STA mode)
 * @arg: WMI LTF keyseed argument; vdev_id is overwritten per link
 * @peer_addr: PASN peer link MAC address the keyseed belongs to
 *
 * For a non-MLO (SLO) vif, sends on the single deflink vdev directly, since
 * such vifs never populate links_map/link[]. For an MLO vif, walks all set
 * bits in links_map, skipping any link that is not currently started. Each
 * active link vdev gets ath12k_wmi_send_ltf_key_seed() followed by
 * ath12k_pasn_peer_set_ltf_keyseed().
 *
 * Return: 0 on success, -ENOLINK if the vif or its active link cannot be
 * resolved, or a negative error code if any link's WMI send fails.
 */
int ath12k_wmi_send_ltf_key_seed_all_radios_extn(struct wireless_dev *wdev,
						 struct ath12k_wmi_ltf_keyseed_arg *arg,
						 const u8 *peer_addr);
/* RTT vdev lifecycle — unassociated STA LOWI path */
int ath12k_create_rtt_vdev_extn(struct ath12k *ar, const u8 *sta_mac);
void ath12k_add_rtt_interface_extn(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif);
void ath12k_destroy_rtt_vdev_extn(struct ath12k *ar);
struct ath12k_link_vif *
ath12k_get_rtt_vif_by_vdev_id_extn(struct ath12k_base *ab, u32 vdev_id);
struct wireless_dev *get_pasn_auth_wdev_extn(struct ath12k_link_vif *arvif);
void ath12k_pasn_vendor_event_sent_extn(struct ath12k_link_vif *arvif);
void ath12k_rtt_arvif_cleanup_extn(struct ath12k_link_vif *arvif);

struct wmi_vdev_add_mac_addr_to_rx_filter_cmd {
	__le32 tlv_header;
	__le32 vdev_id;
	__le32 freq;
	u8 mac_addr[ETH_ALEN];
	u8 _pad[2];
	__le32 enable;
} __packed;

int ath12k_wmi_vdev_add_mac_addr_rx_filter_extn(struct ath12k *ar, u32 vdev_id,
						const u8 *mac, u32 freq,
						bool enable);
int start_rtt_vif(struct ath12k *ar, struct ieee80211_hw *hw,
		  struct cfg80211_chan_def *chandef, int duration,
		  enum ieee80211_roc_type type);
struct ath12k_link_vif *get_ranging_mgmt_arvif(struct ieee80211_hw *hw,
					       struct ieee80211_vif *vif,
					       struct sk_buff *skb);
u32 get_ranging_pasn_auth_freq(struct ath12k *ar);
bool ranging_redirect_mgmt_tx_wmi(struct ath12k *ar, struct sk_buff *skb);
bool ath12k_ranging_deliver_pasn_mgmt_rx_extn(struct ath12k *ar, struct sk_buff *skb);

#endif /* RANGING_EXTN_H */
