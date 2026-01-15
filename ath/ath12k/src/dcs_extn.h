/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DCS_EXTN_H
#define ATH12K_DCS_EXTN_H

/* Valid DCS interference bitmap: CW, WLAN */
#define ATH12K_DCS_VALID_INTF_BITMAP (WMI_DCS_CW_INTF | \
                                     WMI_DCS_WLAN_INTF)

#ifndef CPTCFG_QCN_EXTN
static inline void ath12k_mac_set_intf_detect(struct ath12k *ar,
					      u16 intf_detect_bitmap)
{
}

static inline void cfg80211_intf_notify_extn(struct wiphy *wiphy,
					     struct ath12k *ar)
{
}

static inline void ath12k_vendor_send_intf_notify_extn(struct ath12k *ar)
{
}

static inline void ath12k_wmi_dcs_cw_interference_event_extn(struct ath12k_base *ab,
							     struct sk_buff *skb,
							     u32 pdev_id)
{
}
#else
void ath12k_mac_set_intf_detect(struct ath12k *ar, u16 intf_detect_bitmap);
void cfg80211_intf_notify_extn(struct wiphy *wiphy, struct ath12k *ar);
void ath12k_vendor_send_intf_notify_extn(struct ath12k *ar);
void ath12k_wmi_dcs_cw_interference_event_extn(struct ath12k_base *ab,
					       struct sk_buff *skb,
					       u32 pdev_id);
#endif
#endif
