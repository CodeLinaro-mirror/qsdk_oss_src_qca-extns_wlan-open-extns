
/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <net/netlink.h>
#include <net/mac80211.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include <linux/byteorder/generic.h>
#include <linux/ctype.h>
#include <linux/etherdevice.h>
#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/timekeeping.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
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
#include "reg_extn.h"
#include "../net/mac80211/ieee80211_i.h"
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
#include "mesh_util.h"
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */

const struct nla_policy
ath12k_vendor_tdma_schedule_policy[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_RADIO_INDEX] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_TYPE] = { .type = NLA_U8 },
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_ID] = { .type = NLA_U8 },
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_BSSID] = NLA_POLICY_EXACT_LEN_WARN(ETH_ALEN),
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_START_TIME_TSF_LOW] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_START_TIME_TSF_HIGH] = { .type = NLA_U32 },
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_NUM_BUSY_SLOTS] = { .type = NLA_U16 },
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_BUSY_SLOTS_DUR] = { .type = NLA_U16 },
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_BUSY_SLOTS_INTVL] = { .type = NLA_U16 },
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_EDCA_PARAMS_VALID] = { .type = NLA_U8 },
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_AIFSN] = { .type = NLA_BINARY,
						      .len = sizeof(u16) * WLAN_MAX_AC },
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_CWMIN] = { .type = NLA_BINARY,
						      .len = sizeof(u16) * WLAN_MAX_AC },
	[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_CWMAX] = { .type = NLA_BINARY,
						      .len = sizeof(u16) * WLAN_MAX_AC },
};
#ifndef nla_nest_end_checked
#define nla_nest_end_checked(skb, nla)		\
	do {					\
		if ((skb) && (nla))		\
			nla_nest_end((skb), (nla)); \
	} while (0)
#endif

#define ATH12K_CONFIG_AGGR_MAX_AMPDU_SIZE 255
#define ATH12K_CONFIG_AGGR_MAX_AMSDU_SIZE 7
#define ATH12K_CONFIG_MAX_BA_BUFSIZE 6
#define ATH12K_CONFIG_MAX_TX_ENCAP_TYPE 2
#define ATH12K_CONFIG_MAX_RX_DECAP_TYPE 2
#define ATH12K_CHAN144_FREQ 5720

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
	[QCA_VENDOR_ATTR_WLAN_HOME_OFFCHAN_TX_RX_IS_MLD] = {.type = NLA_FLAG},
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
	[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MBSSID_ENABLED] = { .type = NLA_FLAG },
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

#define ATH12K_HWBL_NL_CHAN_WIDTH_INVALID ((u32)-1)

static struct ath12k_hw_blocklist_state *
ath12k_vendor_hwbl_get_state_extn(struct ath12k_base *ab, int pdev_idx)
{
	if (!ab || pdev_idx < 0 || pdev_idx >= ab->num_radios)
		return NULL;

	return ab->ath12k_base_extn.hw_blocklist[pdev_idx];
}

static u8 ath12k_vendor_hwbl_pwr_mode_to_nl_extn(u8 pwr_mode)
{
	switch (pwr_mode) {
	case WMI_REG_INDOOR_AP:
		return NL80211_REG_AP_LPI;
	case WMI_REG_STD_POWER_AP:
		return NL80211_REG_AP_SP;
	case WMI_REG_VLP_AP:
		return NL80211_REG_AP_VLP;
	default:
		return NL80211_REG_NUM_POWER_MODES;
	}
}

static u32 ath12k_vendor_hwbl_bw_to_nl_extn(u8 max_bw)
{
	switch (max_bw) {
	case WMI_HOST_CHAN_WIDTH_20:
		return NL80211_CHAN_WIDTH_20;
	case WMI_HOST_CHAN_WIDTH_40:
		return NL80211_CHAN_WIDTH_40;
	case WMI_HOST_CHAN_WIDTH_80:
		return NL80211_CHAN_WIDTH_80;
	case WMI_HOST_CHAN_WIDTH_160:
		return NL80211_CHAN_WIDTH_160;
	case WMI_HOST_CHAN_WIDTH_320:
		return NL80211_CHAN_WIDTH_320;
	default:
		return ATH12K_HWBL_NL_CHAN_WIDTH_INVALID;
	}
}

static const struct nla_policy
ath12k_hw_blocked_chans_query_policy_extn[QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_REQ_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_REQ_RADIO_INDEX] = { .type = NLA_S32 },
};

static int ath12k_hw_blocked_chans_event_update_or_get_len_extn(
	struct sk_buff *vendor_event,
	const struct ath12k_hw_blocklist_state *st,
	u8 radio_id)
{
	struct nlattr *band_list = NULL;
	struct nlattr *band_info = NULL;
	struct nlattr *pwr_mode_list = NULL;
	struct nlattr *pwr_mode_info = NULL;
	struct nlattr *fb_chan_list = NULL;
	struct nlattr *pc_chan_list = NULL;
	struct nlattr *chan_info = NULL;
	int len = 0;
	enum wmi_reg_6g_ap_type pwr_mode;
	int i;

	if (vendor_event &&
	    nla_put_s32(vendor_event,
			QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_RADIO_RADIO_INDEX,
			radio_id)) {
		goto fail;
	} else {
		len += nla_total_size(sizeof(u32));
	}

	if (vendor_event) {
		band_list = nla_nest_start(vendor_event,
					   QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_RADIO_BAND_LIST);
		if (!band_list)
			goto fail;
	} else {
		len += nla_total_size(0);
	}

	if (vendor_event) {
		band_info = nla_nest_start(vendor_event, NL80211_BAND_6GHZ);
		if (!band_info)
			goto fail;
	} else {
		len += nla_total_size(0);
	}

	if (vendor_event &&
	    nla_put_u32(vendor_event,
			QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_BAND_ID,
			NL80211_BAND_6GHZ))
		goto fail;
	else
		len += nla_total_size(sizeof(u32));

	if (vendor_event) {
		pwr_mode_list = nla_nest_start(vendor_event,
					       QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_BAND_PWR_MODE_LIST);
		if (!pwr_mode_list)
			goto fail;
	} else {
		len += nla_total_size(0);
	}

	for (pwr_mode = 0; pwr_mode < WMI_REG_CURRENT_MAX_AP_TYPE; pwr_mode++) {
		const struct ath12k_hw_blocklist_chans *chans = &st->pwr[pwr_mode];
		u8 nl_pwr_mode;

		nl_pwr_mode = ath12k_vendor_hwbl_pwr_mode_to_nl_extn(pwr_mode);
		if (nl_pwr_mode >= NL80211_REG_NUM_POWER_MODES)
			continue;

		if (vendor_event) {
			pwr_mode_info = nla_nest_start(vendor_event, pwr_mode);
			if (!pwr_mode_info)
				goto fail;
		} else {
			len += nla_total_size(0);
		}

		if (vendor_event &&
		    nla_put_u8(vendor_event,
			       QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_PWR_MODE_ID,
			       nl_pwr_mode))
			goto fail;
		else {
			len += nla_total_size(sizeof(u8));
		}

		if (vendor_event) {
			fb_chan_list = nla_nest_start(vendor_event,
						      QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_PWR_MODE_FB_CHAN_LIST);
			if (!fb_chan_list)
				goto fail;
		} else {
			len += nla_total_size(0);
		}

		for (i = 0; i < chans->nfbchans; i++) {
			const struct ath12k_hw_blocklist_fb_chan *fb_chan = &chans->fb_chan[i];
			u32 nl_bw = ath12k_vendor_hwbl_bw_to_nl_extn(fb_chan->max_bw);

			if (nl_bw == ATH12K_HWBL_NL_CHAN_WIDTH_INVALID)
				continue;

			if (vendor_event) {
				chan_info = nla_nest_start(vendor_event, i);
				if (!chan_info)
					goto fail;
			} else {
				len += nla_total_size(0);
			}

			if (vendor_event &&
			    (nla_put_u32(vendor_event,
					 QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_FB_CHAN_BW,
					 nl_bw) ||
			     nla_put_u16(vendor_event,
					 QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_FB_CHAN_CENTER_FREQ,
					 fb_chan->center_freq) ||
			     nla_put_u16(vendor_event,
					 QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_FB_CHAN_PRI20_BITMAP,
					 fb_chan->pri20_bitmap)))
				goto fail;
			else {
				len += nla_total_size(sizeof(u32));
				len += nla_total_size(sizeof(u16)) * 2;
			}

			nla_nest_end_checked(vendor_event, chan_info);
			chan_info = NULL;
		}
		nla_nest_end_checked(vendor_event, fb_chan_list);
		fb_chan_list = NULL;

		if (vendor_event) {
			pc_chan_list = nla_nest_start(vendor_event,
						      QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_PWR_MODE_PC_CHAN_LIST);
			if (!pc_chan_list)
				goto fail;
		} else {
			len += nla_total_size(0);
		}

		for (i = 0; i < chans->npcchans; i++) {
			const struct ath12k_hw_blocklist_pc_chan *pc_chan = &chans->pc_chan[i];
			u32 nl_bw = ath12k_vendor_hwbl_bw_to_nl_extn(pc_chan->max_bw);

			if (nl_bw == ATH12K_HWBL_NL_CHAN_WIDTH_INVALID)
				continue;

			if (vendor_event) {
				chan_info = nla_nest_start(vendor_event, i);
				if (!chan_info)
					goto fail;
			} else {
				len += nla_total_size(0);
			}

			if (vendor_event &&
			    (nla_put_u32(vendor_event,
					 QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_PC_CHAN_BW,
					 nl_bw) ||
			     nla_put_u16(vendor_event,
					 QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_PC_CHAN_CENTER_FREQ,
					 pc_chan->center_freq) ||
			     nla_put_u32(vendor_event,
					 QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_PC_CHAN_PUNC_PATTERN_BITMAP,
					 /* Bitmap of puncture patterns (BIT(idx)). */
					 pc_chan->punc_pat_bmap)))
				goto fail;
			else {
				len += nla_total_size(sizeof(u32));
				len += nla_total_size(sizeof(u16));
				len += nla_total_size(sizeof(u32));
			}

			nla_nest_end_checked(vendor_event, chan_info);
			chan_info = NULL;
		}
		nla_nest_end_checked(vendor_event, pc_chan_list);
		pc_chan_list = NULL;

		nla_nest_end_checked(vendor_event, pwr_mode_info);
		pwr_mode_info = NULL;
	}

	nla_nest_end_checked(vendor_event, pwr_mode_list);
	nla_nest_end_checked(vendor_event, band_info);
	nla_nest_end_checked(vendor_event, band_list);
	return vendor_event ? 0 : len;

fail:
	nla_nest_end_checked(vendor_event, chan_info);
	nla_nest_end_checked(vendor_event, fb_chan_list);
	nla_nest_end_checked(vendor_event, pc_chan_list);
	nla_nest_end_checked(vendor_event, pwr_mode_info);
	nla_nest_end_checked(vendor_event, pwr_mode_list);
	nla_nest_end_checked(vendor_event, band_info);
	nla_nest_end_checked(vendor_event, band_list);
	return -EINVAL;
}

static int ath12k_vendor_validate_hw_blocked_chans_state_extn(struct ath12k *ar, u8 radio_id)
{
	struct ath12k_hw_blocklist_state *st;
	int ret = 0;

	if (ar->pdev_idx >= ar->ab->num_radios)
		return -EINVAL;

	spin_lock_bh(&ar->ab->base_lock);
	st = ath12k_vendor_hwbl_get_state_extn(ar->ab, ar->pdev_idx);
	if (!st || !st->valid)
		goto unlock;

	if (!st->complete) {
		ath12k_dbg(ar->ab, ATH12K_DBG_REG,
			   "HW blocked channels query rejected: fragmented update in progress for radio %u",
			   radio_id);
		ret = -EAGAIN;
	}

unlock:
	spin_unlock_bh(&ar->ab->base_lock);
	return ret;
}

static int ath12k_vendor_send_hw_blocked_chans_reply_extn(struct wiphy *wiphy,
							  struct ath12k *ar,
							  u8 radio_id)
{
	struct ath12k_hw_blocklist_state snapshot;
	struct sk_buff *reply_skb;
	struct nlattr *radio_list = NULL;
	struct nlattr *radio_info = NULL;
	int reply_len, ret;

	ret = ath12k_vendor_validate_hw_blocked_chans_state_extn(ar, radio_id);
	if (ret)
		return ret;

	/* Take a deep copy of the blocklist state. */
	ret = ath12k_reg_hw_blocklist_snapshot_extn(ar->ab, ar->pdev_idx, &snapshot);
	if (ret)
		return ret;

	reply_len = ath12k_hw_blocked_chans_event_update_or_get_len_extn(NULL, &snapshot,
									 radio_id);
	if (reply_len <= 0) {
		ret = -EINVAL;
		goto free_snapshot;
	}

	reply_len += nla_total_size(0); /* radio_list nest header */
	reply_len += nla_total_size(0); /* radio_info nest header */

	reply_skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, NLMSG_HDRLEN + reply_len);
	if (!reply_skb) {
		ret = -ENOMEM;
		goto free_snapshot;
	}

	radio_list = nla_nest_start(reply_skb,
				    QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_RESP_RADIO_LIST);
	if (!radio_list) {
		ret = -EINVAL;
		goto fail;
	}

	radio_info = nla_nest_start(reply_skb, 0);
	if (!radio_info) {
		ret = -EINVAL;
		goto fail;
	}

	ret = ath12k_hw_blocked_chans_event_update_or_get_len_extn(reply_skb, &snapshot,
								   radio_id);
	if (ret)
		goto fail;

	nla_nest_end_checked(reply_skb, radio_info);
	radio_info = NULL;
	nla_nest_end_checked(reply_skb, radio_list);
	radio_list = NULL;

	ath12k_reg_hw_blocklist_free_snapshot_extn(&snapshot);
	return cfg80211_vendor_cmd_reply(reply_skb);

fail:
	nla_nest_end_checked(reply_skb, radio_info);
	nla_nest_end_checked(reply_skb, radio_list);
	kfree_skb(reply_skb);
free_snapshot:
	ath12k_reg_hw_blocklist_free_snapshot_extn(&snapshot);
	return ret;
}

static int ath12k_vendor_send_hw_blocked_chans_reply_all_extn(struct wiphy *wiphy)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath12k_hw *ah = hw->priv;
	struct sk_buff *reply_skb = NULL;
	struct nlattr *radio_list = NULL;
	struct nlattr *radio_info = NULL;
	struct ath12k *ar;
	int reply_len = 0;
	int radio_reply_len;
	u8 radio_id;
	bool has_6ghz_radio = false;
	int ret;

	for (radio_id = 0; radio_id < ah->num_radio; radio_id++) {
		struct ath12k_hw_blocklist_state snap = {};

		ar = &ah->radio[radio_id];
		if (!ar->supports_6ghz)
			continue;

		has_6ghz_radio = true;
		ret = ath12k_vendor_validate_hw_blocked_chans_state_extn(ar, radio_id);
		if (ret)
			return ret;

		ret = ath12k_reg_hw_blocklist_snapshot_extn(ar->ab, ar->pdev_idx, &snap);
		if (ret)
			memset(&snap, 0, sizeof(snap));

		radio_reply_len =
		    ath12k_hw_blocked_chans_event_update_or_get_len_extn(NULL, &snap,
									 radio_id);
		ath12k_reg_hw_blocklist_free_snapshot_extn(&snap);
		if (radio_reply_len <= 0)
			return -EINVAL;

		reply_len += nla_total_size(0); /* radio_info nest header */
		reply_len += radio_reply_len;
	}

	if (!has_6ghz_radio)
		return -EOPNOTSUPP;

	reply_len += nla_total_size(0); /* radio_list nest header */
	reply_skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, NLMSG_HDRLEN + reply_len);
	if (!reply_skb)
		return -ENOMEM;

	radio_list = nla_nest_start(reply_skb,
				    QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_RESP_RADIO_LIST);
	if (!radio_list) {
		ret = -EINVAL;
		goto fail;
	}

	for (radio_id = 0; radio_id < ah->num_radio; radio_id++) {
		struct ath12k_hw_blocklist_state snap = {};

		ar = &ah->radio[radio_id];
		if (!ar->supports_6ghz)
			continue;

		ret = ath12k_reg_hw_blocklist_snapshot_extn(ar->ab, ar->pdev_idx, &snap);
		if (ret)
			memset(&snap, 0, sizeof(snap));

		radio_info = nla_nest_start(reply_skb, radio_id);
		if (!radio_info) {
			ath12k_reg_hw_blocklist_free_snapshot_extn(&snap);
			ret = -EINVAL;
			goto fail;
		}

		ret = ath12k_hw_blocked_chans_event_update_or_get_len_extn(reply_skb,
									   &snap,
									   radio_id);
		ath12k_reg_hw_blocklist_free_snapshot_extn(&snap);
		if (ret)
			goto fail;

		nla_nest_end_checked(reply_skb, radio_info);
		radio_info = NULL;
	}

	nla_nest_end_checked(reply_skb, radio_list);
	radio_list = NULL;
	return cfg80211_vendor_cmd_reply(reply_skb);

fail:
	nla_nest_end_checked(reply_skb, radio_info);
	nla_nest_end_checked(reply_skb, radio_list);
	kfree_skb(reply_skb);
	return ret;
}

int ath12k_vendor_get_hw_blocked_chans_extn(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    const void *data,
					    int data_len)
{
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_REQ_MAX + 1] = {};
	struct ath12k *ar;
	int radio_id = -1;

	(void)wdev;

	if (data_len) {
		if (!data)
			return -EINVAL;

		if (nla_parse(tb, QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_REQ_MAX, data, data_len,
			      ath12k_hw_blocked_chans_query_policy_extn, NULL))
			return -EINVAL;
	}

	if (!tb[QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_REQ_RADIO_INDEX])
		return ath12k_vendor_send_hw_blocked_chans_reply_all_extn(wiphy);

	radio_id = nla_get_s32(tb[QCA_WLAN_VENDOR_ATTR_HW_BLOCKED_CHANS_REQ_RADIO_INDEX]);
	if (radio_id < -1 || radio_id > U8_MAX)
		return -EINVAL;
	if (radio_id < 0)
		return ath12k_vendor_send_hw_blocked_chans_reply_all_extn(wiphy);

	ar = ath12k_get_radio_by_id(wiphy, (u8)radio_id);
	if (!ar)
		return -ENODATA;

	if (!ar->supports_6ghz)
		return -EOPNOTSUPP;

	return ath12k_vendor_send_hw_blocked_chans_reply_extn(wiphy, ar, (u8)radio_id);
}

static int ath12k_vendor_resolve_nol_path_from_data_extn(const void *data,
							 u32 data_len,
							 u8 radio_idx,
							 char **path);
static int ath12k_vendor_reply_u32_extn(struct wiphy *wiphy, u32 value);

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

static int ath12k_wsi_bypass_event(struct ath12k_base *ab, int status)
{
	struct ath12k *ar;
	struct sk_buff *skb;

	if (ab->num_radios > 0 && ab->pdevs[0].ar)
		ar = ab->pdevs[0].ar;

	if (!ar || !ar->ah || !ar->ah->hw)
		return -EINVAL;

	skb = cfg80211_vendor_event_alloc(ar->ah->hw->wiphy,
					  NULL,
					  NLMSG_DEFAULT_SIZE,
					  QCA_NL80211_VENDOR_SUBCMD_SET_WIFI_INDEX,
					  GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	if (nla_put_u32(skb, QCA_WLAN_VENDOR_ATTR_SET_RADIO_PARAM,
	    QCA_WLAN_VENDOR_RADIO_PARAM_WSI_BYPASS)) {
		kfree_skb(skb);
		return -ENOBUFS;
	}

	if (nla_put_u32(skb, QCA_WLAN_VENDOR_ATTR_SET_RADIO_VALUE,
	    ab->wsi_remap_state)) {
		kfree_skb(skb);
		return -ENOBUFS;
	}

	if (nla_put_u32(skb, QCA_WLAN_VENDOR_ATTR_SET_RADIO_STATUS,
	    status)) {
		kfree_skb(skb);
		return -ENOBUFS;
	}

	cfg80211_vendor_event(skb, GFP_KERNEL);
	ath12k_info(ab, "wsi_bypass_event: cfg80211_vendor_event sent with state %d, status %d\n", ab->wsi_remap_state, status);
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

/**
 * ath12k_vendor_get_mintxpower - Get min tx power supported @ar
 * @ar: pointer to ath12k structure
 * @value: pointer to store the retrieved min tx power
 *
 * Returns: 0 on success, negative error code on failure
 */
static int ath12k_vendor_get_mintxpower(struct ath12k *ar, u64 *value)
{
	s32 svalue = (s32)ar->min_tx_power;

	if(svalue < S8_MIN || svalue > S8_MAX)
		return -ERANGE;

	*value = (s8)ar->min_tx_power;

	return 0;
}

/**
 * ath12k_vendor_get_maxtxpower - Get max tx power supported by @ar
 * @ar: pointer to ath12k structure
 * @value: pointer to store the retrieved max tx power
 *
 * Returns: 0 on success, negative error code on failure
 */
static int ath12k_vendor_get_maxtxpower(struct ath12k *ar, u64 *value)
{
	s32 svalue = (s32)ar->max_tx_power;

	if(svalue < S8_MIN || svalue > S8_MAX)
		return -ERANGE;

	*value = (s8)ar->max_tx_power;

	return 0;
}

/**
 * ath12k_vendor_get_regtxpower - Get max reg tx power supported by current operating channel
 * @arvif: pointer to ath12k_link_vif structure
 * @value: pointer to store the retrieved max tx power
 *
 * Returns: 0 on success, negative error code on failure
 */
static int ath12k_vendor_get_regtxpower(struct ath12k_link_vif *arvif, u64 *value)
{
	struct ieee80211_vif *vif;
	struct cfg80211_chan_def def;
	s32 svalue;

	vif = ath12k_ahvif_to_vif(arvif->ahvif);
	if (!vif)
		return -EINVAL;

	if (ath12k_mac_vif_link_chan(vif, arvif->link_id, &def))
		return -ENOTCONN;

	svalue = (s32)def.chan->max_reg_power;
	if(svalue < S8_MIN || svalue > S8_MAX)
		return -ERANGE;

	*value = (s8)def.chan->max_reg_power;
	return 0;
}

/**
 * ath12k_vendor_get_ctlpwrscale - Get current ctl power scale value
 * @ar: pointer to ath12k structure
 * @value: pointer to store the retrieved ctl power scale value
 *
 * Returns: 0 on success
 */
static int ath12k_vendor_get_ctlpwrscale(struct ath12k *ar, u32 *value)
{
	*value = ar->radio_cfg.ctlpwrscale;
	return 0;
}

/**
 * ath12k_vendor_set_ctlpwrscale - Set ctl power scale value
 * @ar: pointer to ath12k structure
 * @value: pointer to store the user input ctl power scale value
 * Returns: 0 on success, negative error code on failure.
 */
static int ath12k_vendor_set_ctlpwrscale(struct ath12k *ar, u32 value)
{
	int ret;

	if (value >= WMI_HOST_TP_SCALE_SIZE)
		return -EINVAL;

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_CUST_TXPOWER_SCALE,
						value, ar->pdev->pdev_id);
	if (ret) {
		ath12k_err(ar->ab,
			"Failed to send WMI_PDEV_PARAM_CUST_TXPOWER_SCALE to firmware: %d\n", ret);
		return ret;
	}

	ar->radio_cfg.ctlpwrscale = value;
	ath12k_info(ar->ab, "CTL Power scale set to %u\n", value);

	return 0;
}

/**
 * ath12k_vendor_get_txpower_resolution - Get effective per-link TX power cap
 * @arvif: pointer to ath12k_link_vif structure
 * @value: pointer to store the retrieved tx power in 0.5 dBm units
 *
 * Uses ieee80211_bss_conf.txpower which is the operating TX power
 * computed by mac80211 (minimum of regulatory, HW capability, and user-set
 * limits).
 *
 * Returns: 0 on success, negative error code on failure
 */
static int ath12k_vendor_get_txpower_resolution(struct ath12k_link_vif *arvif, u64 *value)
{
	struct ieee80211_bss_conf *bss_conf;
	s32 svalue;

	bss_conf = ath12k_mac_get_link_bss_conf(arvif);
	if (!bss_conf)
		return -EINVAL;

	if (bss_conf->txpower == INT_MIN)
		return -ENOTCONN;

	svalue = (s32)bss_conf->txpower * 2;
	if(svalue < S8_MIN || svalue > S8_MAX)
		return -ERANGE;

	*value = (s8)bss_conf->txpower * 2;

	ath12k_info(NULL, "txpower in 1 dBm scale %d.%02d dBm",
		    svalue / 2, abs(svalue % 2) * 50);
	return 0;
}

static struct ieee80211_channel *
ath12k_vendor_get_radio_chan144(struct wiphy *wiphy, struct ath12k *ar)
{
	struct ieee80211_channel *chan;
	u32 chan144_khz = MHZ_TO_KHZ(ATH12K_CHAN144_FREQ);

	if (ar->freq_range.start_freq && ar->freq_range.end_freq &&
	    (chan144_khz < ar->freq_range.start_freq ||
	     chan144_khz > ar->freq_range.end_freq)) {
		ath12k_err(NULL, "CHAN144: radio %u does not support center freq %d MHz\n",
			   ar->pdev_idx, ATH12K_CHAN144_FREQ);
		return NULL;
	}

	chan = ieee80211_get_channel(wiphy, ATH12K_CHAN144_FREQ);
	if (!chan) {
		ath12k_err(NULL, "CHAN144: freq %d MHz not found in wiphy\n",
			   ATH12K_CHAN144_FREQ);
		return NULL;
	}

	return chan;
}

static void ath12k_vendor_chan144_notify_reg_change(struct wiphy *wiphy)
{
	const struct ieee80211_regdomain *regd;
	int ret;

	regd = get_wiphy_regdom(wiphy);
	if (!regd)
		return;

	ret = regulatory_set_wiphy_regd(wiphy, (struct ieee80211_regdomain *)regd);
	if (ret)
		ath12k_err(NULL, "CHAN144: failed to notify userspace reg change, ret=%d\n",
			   ret);
}

static int ath12k_vendor_chan144_apply(struct ath12k *ar, u8 enable)
{
	struct wiphy *wiphy = ath12k_ar_to_hw(ar)->wiphy;
	struct ieee80211_channel *chan;
	const struct ieee80211_reg_rule *reg_rule;
	u32 old_flags;
	int ret = 0;

	if (enable > CHAN_144_EN) {
		ath12k_err(NULL, "CHAN144: invalid input %u\n", enable);
		return -EINVAL;
	}

	chan = ath12k_vendor_get_radio_chan144(wiphy, ar);
	if (!chan)
		return -ENODATA;

	old_flags = chan->flags;
	if (!enable) {
		chan->flags |= IEEE80211_CHAN_DISABLED;
		chan->orig_flags |= IEEE80211_CHAN_DISABLED;
	} else {
		chan->flags &= ~IEEE80211_CHAN_DISABLED;
		chan->orig_flags &= ~IEEE80211_CHAN_DISABLED;
	}

	if (enable) {
		reg_rule = freq_reg_info(wiphy, MHZ_TO_KHZ(ATH12K_CHAN144_FREQ));
		if (IS_ERR_OR_NULL(reg_rule)) {
			ath12k_err(NULL, "CHAN144: regulatory disallows enabling freq %d\n",
				   ATH12K_CHAN144_FREQ);
			return -EPERM;
		}
	}

	if (ar->ah->state == ATH12K_HW_STATE_ON) {
		ret = ath12k_reg_update_chan_list(ar, true);
		if (ret) {
			chan->flags = old_flags;
			chan->orig_flags = old_flags;
			ath12k_err(NULL, "CHAN144: failed to sync FW channel list, ret=%d\n",
				   ret);
			return ret;
		}
	}

	ar->radio_cfg.chan144_enabled = enable;
	ath12k_info(NULL, "CHAN144: policy %s applied on radio %u (flags 0x%x -> 0x%x)\n",
		    enable ? "enable" : "disable", ar->pdev_idx, old_flags, chan->flags);

	ath12k_vendor_chan144_notify_reg_change(wiphy);
	return 0;
}

int ath12k_vendor_get_en_chan144(struct ath12k *ar, u32 *value)
{
	*value = ar->radio_cfg.chan144_enabled;
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

/**
 * ath12k_chainmask_validate - Validate chainmask against capabilities
 * @ar: pointer to ath12k structure
 * @mask: chainmask value to validate
 * @is_tx: true for TX chainmask, false for RX chainmask
 *
 * Returns: 0 on success, negative error code on failure
 */
static int ath12k_chainmask_validate(struct ath12k *ar, u32 mask, bool is_tx)
{
	u32 max_mask;

    	if (!ar || !ar->pdev) {
        	ath12k_err(NULL, "Invalid ar or pdev pointer\n");
        	return -EINVAL;
    	}

	max_mask = is_tx ? ar->pdev->cap.tx_chain_mask :
			       ar->pdev->cap.rx_chain_mask;

	if (!max_mask) {
		ath12k_err(ar->ab, "Invalid %s chainmask capability (0x%x)\n",
			is_tx ? "TX" : "RX", max_mask);
		return -EINVAL;
	}

	if (!mask) {
		ath12k_err(ar->ab, "Mask should be non-zero\n");
		return -EINVAL;
	}

	if((mask & max_mask) != mask){
		ath12k_err(ar->ab, "Mask invalid value=%u\t max_mask=%u\n",
			   mask, max_mask);
		return -EINVAL;
	}

	return 0;
}

/**
 * ath12k_set_chainmask - Validate and set TX/RX chainmask
 * @ar: pointer to ath12k structure
 * @value: chainmask value to set
 * @is_tx: true for TX chainmask, false for RX chainmask
 *
 * Returns: 0 on success, negative error code on failure
 */
static int ath12k_set_chainmask(struct ath12k *ar, u32 value, bool is_tx)
{
	int ret;

	ret = ath12k_chainmask_validate(ar, value, is_tx);

	if (ret)
        	return ret;

    	if (is_tx)
        	return ath12k_mac_set_tx_antenna(ar, value);

    	return ath12k_mac_set_rx_antenna(ar, value);
}

static void ath12k_wsi_bypass_workfn(struct work_struct *work)
{
	struct ath12k_wsi_remap_work *w =
	    container_of(work, struct ath12k_wsi_remap_work, work);
	struct ath12k_base *ab = w->ab;
	int ret;

	ret = ath12k_mac_dynamic_wsi_remap(ab);
	if (ret) {
	        struct ath12k_hw_group *ag = ab->ag;
	        if (ag)
	                ag->wsi_remap_in_progress = false;
	        ab->wsi_remap_state = ATH12K_WSI_BYPASS_DEFAULT;
	        ab->is_bypassed = false;
	        ath12k_err(ab, "WSI bypass worker failed: %d\n", ret);
	}

	ath12k_wsi_bypass_event(ab, ret);
	kfree(w);
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
		ret = ath12k_set_chainmask(ar, value, true);
		break;
   	case ACFG_PARAM_RADIO_RXCHAINMASK:
		ret = ath12k_set_chainmask(ar, value, false);
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
	case QCA_WLAN_VENDOR_VDEV_PARAM_PROTECTION_MODE:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_PROTECTION_MODE, value);
		if (!ret) {
			arvif->vap_cfg.protection_mode = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_PROTECTION_MODE to firmware");
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
	case QCA_WLAN_VENDOR_VDEV_PARAM_BW_NSS_RATE:
		u_int32_t bw, nss, ratemask, max_bw;

		bw = (value & 0xff000000) >> 24;
		nss = (value & 0x00ff0000) >> 16;
		ratemask = (value & 0xffff);
		max_bw = 6;

		if (bw > max_bw) {
			ath12k_err(NULL, "Not yet supported value for Band Width: %d\n", bw);
			return -EINVAL;
		} else if ((bw <= max_bw) && (bw >= 3) && (ratemask > 0x3ff)) {
			ath12k_err(NULL, "Invalid ratemask for VHT Band: 0x%x\n", ratemask);
			return -EINVAL;
		} else if ((bw <= 2) && (bw >= 1) && (ratemask > 0xff)) {
			ath12k_err(NULL, "Invalid ratemask for HT Band: 0x%x\n", ratemask);
			return -EINVAL;
		} else if ((bw <= max_bw) && (bw >= 1) && ((nss > 4) || (nss == 0)))  {
			ath12k_err(NULL, "Invalid nss for VHT/HT Band: %d\n", nss);
			return -EINVAL;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_BW_NSS_RATEMASK, value);
		if (!ret) {
			arvif->vap_cfg.bw_nss_rate = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_BW_NSS_RATEMASK to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_LDPC:
		if (value < 0 || value > 1) {
			ath12k_err(NULL, "EHT UL LDPC setting should either 0 or 1.\n");
			return -EINVAL;
		}
		if (value == arvif->vap_cfg.eht_ul_ldpc ) {
			ath12k_err(NULL, "value %d is already set for iv_eht_ul_ldpc", value);
			return 0;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_UL_LDPC, value | WMI_HOST_VDEV_PARAM_EHT_MODE);
		if (!ret) {
			arvif->vap_cfg.eht_ul_ldpc = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_UL_LDPC to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_LTF:
		if (value > IEEE80211_EHT_LTF_4X) {
			ath12k_err(NULL, "LTF value should be less than or equal to 0x%x\n", IEEE80211_EHT_LTF_4X);
			return -EINVAL;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_UL_EHT_LTF, value);
		if (!ret) {
			arvif->vap_cfg.eht_ul_ltf = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_UL_EHT_LTF to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_NSS:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_UL_NSS, value | WMI_HOST_VDEV_PARAM_EHT_MODE);
		if (!ret) {
			arvif->vap_cfg.eht_ul_nss = value;
			if (!(arvif->vap_cfg.eht_ul_ldpc) && (arvif->vap_cfg.eht_ul_nss > 4)) {
				arvif->vap_cfg.eht_ul_ldpc = 1;
			}
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_UL_NSS to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_PPDU_BW:
		if (value > IEEE80211_EHT_BW_IDX_MAX) {
			ath12k_err(NULL, "Channel width value should be less than IEEE80211_EHT_BW_IDX_MAX %d", IEEE80211_EHT_BW_IDX_MAX);
			return -EINVAL;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_UL_PPDU_BW, value | WMI_HOST_VDEV_PARAM_EHT_MODE);
		if (!ret) {
			arvif->vap_cfg.eht_ul_ppdu_bw = value;
			if (!(arvif->vap_cfg.eht_ul_ldpc) && (arvif->vap_cfg.eht_ul_ppdu_bw > 0)) {
				arvif->vap_cfg.eht_ul_ldpc = 1;
			}
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_UL_PPDU_BW to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_SHORTGI:
		if ((value == IEEE80211_GI_0DOT4_US) || (value > IEEE80211_GI_3DOT2_US)) {
			ath12k_err(NULL, "%d not supported setting for EHT UL Shortgi\n", IEEE80211_GI_0DOT4_US);
			return -EINVAL;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_UL_GI, value | WMI_HOST_VDEV_PARAM_EHT_MODE);
		if (!ret) {
			arvif->vap_cfg.eht_ul_shortgi = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_UL_GI to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_STBC:
		if (value < 0 || value > 1) {
			ath12k_err(NULL, "EHT UL STBC setting should either 0 or 1.\n");
			return -EINVAL;
		}
		if (value == arvif->vap_cfg.eht_ul_stbc) {
			ath12k_err(NULL, "value %d is already set for iv_eht_ul_stbc", value);
			return 0;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_UL_STBC, value | WMI_HOST_VDEV_PARAM_EHT_MODE);
		if (!ret) {
			arvif->vap_cfg.eht_ul_stbc = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_UL_STBC to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_ENABLERTSCTS:
		if (((value & (~0xff)) != 0) ||
		    (((value & IEEE80211_RTS_CTS_MASK) == 0) &&
		     (((value & IEEE80211_RTS_CTS_PROFILE_MASK) >> IEEE80211_RTS_CTS_PROFILE_SHIFT) != 0)) ||
		    (((value & IEEE80211_RTS_CTS_MASK) != 0) &&
		     (((value & IEEE80211_RTS_CTS_PROFILE_MASK) >> IEEE80211_RTS_CTS_PROFILE_SHIFT) == 0))) {
			ath12k_err(NULL, "Invalid value for RTS-CTS: %x\n", value);
			return -EINVAL;
		}
		if (((value & IEEE80211_RTS_CTS_MASK) > IEEE80211_RTSCTS_FLAG_MAX_VALUE) ||
		    (((value & IEEE80211_RTS_CTS_PROFILE_MASK) >> IEEE80211_RTS_CTS_PROFILE_SHIFT) > IEEE80211_RTSCTS_PROFILE_MAX_VALUE)) {
			ath12k_err(NULL, "Not yet supported value for RTS-CTS: %x\n", value);
			return -EINVAL;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_ENABLE_RTSCTS, value);
		if (!ret) {
			arvif->vap_cfg.enablertscts = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_ENABLE_RTSCTS to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_DCM:
		if (value < 0 || value > 1) {
			ath12k_err(NULL, "HE DCM setting should be 0 or 1 ");
			return -EINVAL;
		}
		if (value == arvif->vap_cfg.he_dcm) {
			ath12k_err(NULL, "value %d is already set for he_dcm", value);
			return 0;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_HE_DCM, value);
		if (!ret) {
			arvif->vap_cfg.he_dcm = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_HE_DCM to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_EXTRANGE:
		if (value < 0 || value > 1) {
			ath12k_err(NULL, "HE Extended Range setting should be 0 or 1 ");
			return -EINVAL;
		}
		if (value == arvif->vap_cfg.he_extrange) {
			ath12k_err(NULL, "value %d is already set for he_extrange", value);
			return 0;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_HE_RANGE_EXT, value);
		if (!ret) {
			arvif->vap_cfg.he_extrange = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_HE_RANGE_EXT to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_UL_LDPC:
		if (value < 0 || value > 1) {
			ath12k_err(NULL, "UL LDPC setting should either 0 or 1.\n");
			return -EINVAL;
		}
		if (value == arvif->vap_cfg.he_ul_ldpc) {
			ath12k_err(NULL, "value %d is already set for he_ul_ldpc", value);
			return 0;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_UL_LDPC, value);
		if (!ret) {
			arvif->vap_cfg.he_ul_ldpc = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_UL_LDPC to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_UL_NSS:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_UL_NSS, value);
		if (!ret) {
			arvif->vap_cfg.he_ul_nss = value;
			if (!(arvif->vap_cfg.he_ul_ldpc) && (arvif->vap_cfg.he_ul_nss > 4)) {
				arvif->vap_cfg.he_ul_ldpc = 1;
			}
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_UL_NSS to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_UL_PPDU_BW:
		if (value >  IEEE80211_HE_BW_IDX_MAX) {
			ath12k_err(NULL, "Channel width value should be less than 3.\n");
			return -EINVAL;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_UL_PPDU_BW, value);
		if (!ret) {
			arvif->vap_cfg.he_ul_ppdu_bw = value;
			if (!(arvif->vap_cfg.he_ul_ldpc) && (arvif->vap_cfg.he_ul_ppdu_bw > 0)) {
				arvif->vap_cfg.he_ul_ldpc = 1;
			}
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_UL_PPDU_BW to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_UL_SHORTGI:
		if ((value == IEEE80211_GI_0DOT4_US) ||
		    (value > IEEE80211_GI_3DOT2_US)) {
			ath12k_err(NULL, "%d not supported setting for HE UL Shortgi\n", IEEE80211_GI_0DOT4_US);
			return -EINVAL;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_UL_GI, value);
		if (!ret) {
			arvif->vap_cfg.he_ul_shortgi = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_UL_GI to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_UL_STBC:
		if (value < 0 || value > 1) {
			ath12k_err(NULL, "UL STBC setting should either 0 or 1.\n");
			return -EINVAL;
		}
		if (value == arvif->vap_cfg.he_ul_stbc) {
			ath12k_err(NULL, "value %d is already set for he_ul_stbc", value);
			return 0;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_UL_STBC, value);
		if (!ret) {
			arvif->vap_cfg.he_ul_stbc = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_UL_STBC to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RTSCTS_RATE:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_RTSCTS_RATE, value);
		if (!ret) {
			arvif->vap_cfg.rtscts_rate = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_RTSCTS_RATE to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_VHT_SGIMASK:
		if (value > MAX_VHT_SGI_MASK) {
			ath12k_err(NULL, "value can not be more than %d", MAX_VHT_SGI_MASK);
			return -EINVAL;
		}
		if (value == arvif->vap_cfg.vht_sgimask) {
			ath12k_err(NULL, "vht_sgimask %d is already set", value);
			return 0;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_VHT_SGIMASK, value);
		if (!ret) {
			arvif->vap_cfg.vht_sgimask = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_VHT_SGIMASK to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_VHT80_RATE:
		if (value > MAX_VHT80_RATE_MASK) {
			ath12k_err(NULL, "value can not be more than %d", MAX_VHT80_RATE_MASK);
			return -EINVAL;
		}
		if ((value & MAX_VHT80_RATE_MASK) == arvif->vap_cfg.vht80_rate) {
			ath12k_err(NULL, "vht80_ratemask %d already set", value);
			return 0;
		}
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_VHT80_RATEMASK, value);
		if (!ret) {
			arvif->vap_cfg.vht80_rate = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_VHT80_RATEMASK to firmware");
		}
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_DIS_LPI_ANT_OPTIMIZE:
		ret = ath12k_wmi_vdev_set_param_cmd(ar, arvif->vdev_id,
				WMI_VDEV_PARAM_DISABLE_LPI_ANT_OPTIMIZATION, value);
		if (!ret) {
			arvif->vap_cfg.dis_lpi_ant_optimize = value;
		} else {
			ath12k_err(NULL, "Failed to send WMI_VDEV_PARAM_DISABLE_LPI_ANT_OPTIMIZATION to firmware");
		}
		break;
	default:
		ath12k_dbg(NULL, ATH12K_DBG_CFG,
				"Un-supported param: %d\n", param);
		break;
	}

	return ret;
}

static inline enum srtype ath12k_extact_sr_type(void *data, uint32_t data_len)
{
	char srtype[SR_TYPE_LENGTH];

	if (!data_len)
		goto err;

	if (data_len > SR_TYPE_LENGTH-1)
		data_len = SR_TYPE_LENGTH-1;

	memcpy(srtype, data, data_len);
	srtype[data_len] = '\0';

	if (strcmp(srtype, "non-srg") == 0)
		return SR_TYPE_NON_SRG_OBSS_PD;
	else if(strcmp(srtype, "srg") == 0)
		return SR_TYPE_SRG_OBSS_PD;
	else if(strcmp(srtype, "obss_pd") == 0)
		return SR_TYPE_OBSS_PD;
	else if(strcmp(srtype, "psr") == 0)
		return SR_TYPE_PSR;

err:
	ath12k_err(NULL, "Wrong Spatial reuse type");
	return SR_TYPE_MAX;
}

static inline int set_obss_pd_enable_bit(uint32_t *threshold,
					 enum srtype type,
					 uint8_t value)
{
	switch (type) {
	case SR_TYPE_NON_SRG_OBSS_PD:
		SET_SELF_OBSS_PD_ENABLE(NON_SRG, *threshold, value);
		break;

	case SR_TYPE_SRG_OBSS_PD:
		SET_SELF_OBSS_PD_ENABLE(SRG, *threshold, value);
		break;

	default:
		ath12k_err(NULL, "Invalid Spatial Reuse type");
		return -1;
	}

	return 0;
}

static inline int set_obss_pd_threshold(uint32_t *threshold,
					enum srtype type,
					uint8_t value)
{
	switch (type) {
	case SR_TYPE_NON_SRG_OBSS_PD:
		SET_SELF_OBSS_PD_THRESH(NON_SRG, *threshold, value);
		break;

	case SR_TYPE_SRG_OBSS_PD:
		SET_SELF_OBSS_PD_THRESH(SRG, *threshold, value);
		break;

	default:
		ath12k_err(NULL, "Invalid Spatial Reuse type");
		return -1;
	}

	return 0;
}

static inline int set_sr_per_ac(uint32_t *enable_bitmap,
				enum srtype type, uint8_t value)
{
	switch (type) {
	case SR_TYPE_OBSS_PD:
		SET_SELF_SR_PER_AC(OBSS_PD, *enable_bitmap, value);
		break;

	case SR_TYPE_PSR:
		SET_SELF_SR_PER_AC(PSR, *enable_bitmap, value);
	break;

	default:
		ath12k_err(NULL, "Invalid Spatial Reuse type");
		return -1;
	}

	return 0;
}

static inline void set_obss_pd_threshold_unit(uint32_t *threshold, uint8_t value)
{
	SR_SET_FIELD(*threshold, value, SELF_OBSS_PD_THRESH_UNITS);
}

static int ath12k_set_obss_pd_enable_bit(struct ath12k *ar, u32 enable,
					 enum srtype type)
{
	int ret;
	int temp_thresh;

	if (enable > 1) {
		ath12k_err(NULL, "OBSS_PD Threshold enable value should be either 0 or 1");
		return -EINVAL;
	}

	SR_SET_FIELD(temp_thresh,
		     IEEE80211_SELF_OBSS_PD_THRESHOLD_IN_DBM,
		     SELF_OBSS_PD_THRESH_UNITS); //check for global

	if (set_obss_pd_enable_bit(&temp_thresh, type, enable))
		return -EINVAL;

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_CMD_OBSS_PD_THRESHOLD,
					temp_thresh, ar->pdev->pdev_id);

	if (ret) {
		ath12k_err(NULL, "Could not set obss pd thresh enable val");
	} else {
		ar->sr_cfg.obss_pd_thresh = temp_thresh;
	}

	return ret;
}

static int ath12k_set_obss_pd_threshold(struct ath12k *ar, int32_t threshold,
					enum srtype type)
{
	int ret;
	int temp_thresh;

	/* OBSS Packet Detect threshold bounds for Spatial Reuse feature.
	 * The parameter value is programmed into the spatial reuse
	 * register, to specify how low the background signal strength
	 * from neighboring BSS cells must be, for this AP to
	 * employ spatial reuse.
	 */
	if (threshold > SELF_OBSS_PD_UPPER_THRESH ||
	    threshold < SELF_OBSS_PD_LOWER_THRESH) {
		ath12k_err(NULL, "Threshold must in the range [%d, %d] (both inclusive)",
			   SELF_OBSS_PD_LOWER_THRESH, SELF_OBSS_PD_UPPER_THRESH);
		return -EINVAL;
	}

	SR_SET_FIELD(temp_thresh,
		     IEEE80211_SELF_OBSS_PD_THRESHOLD_IN_DBM,
		     SELF_OBSS_PD_THRESH_UNITS); //check for gloabal

	if (set_obss_pd_threshold(&temp_thresh, type, threshold))
		return -EINVAL;

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_CMD_OBSS_PD_THRESHOLD,
					temp_thresh, ar->pdev->pdev_id);

	if (ret) {
		ath12k_err(NULL, "WMI send for set cmd obss pd threshold failed");
	} else {
		ar->sr_cfg.obss_pd_thresh = temp_thresh;
	}

	return ret;
}

static int ath12k_set_sr_per_ac(struct ath12k *ar, u32 value,
				enum srtype type)
{
	int ret;
	u32 temp_enable_sr;

	if (value > 15) {
		ath12k_err(NULL, "Value: %d is not a valid value for setting SR ACs",
			   value);
		return -EINVAL;
	}

	temp_enable_sr = IEEE80211_SR_ENABLE_PER_AC;
	// check for global
	if (set_sr_per_ac(&temp_enable_sr, type, value)) {
		ath12k_err(NULL, "Unable to set SR per AC");
		return -EINVAL;
	}

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SET_CMD_OBSS_PD_PER_AC,
					temp_enable_sr, ar->pdev->pdev_id);

	if (ret) {
		ath12k_err(NULL, "Error sending WMI for SR per AC");
	} else {
		ar->sr_cfg.enable_per_ac = temp_enable_sr;
	}

	return ret;
}

static int ath12k_set_self_hesiga_sr15_enable(struct ath12k *ar,
					      u8 enable)
{
	int ret;

	if (enable > 1) {
		ath12k_err(NULL, "HESIGA SR15 enable field can either be 0 or 1");
		return -EINVAL;
	}

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ENABLE_SR_PROHIBIT,
					enable, ar->pdev->pdev_id);

	if (ret) {
		ath12k_err(NULL, "WMI send to set HESIGA SR15 failed, discarding configuration");
	} else {
		ar->sr_cfg.hesiga_sr15_enable = enable;
	}

	return ret;
}

static int ath12k_set_self_psr_tx_enable(struct ath12k *ar,
					 u8 enable)
{
	int ret;

	if (enable > 1) {
		ath12k_err(NULL, "PSR Tx enable field can either be 0 or 1");
		return -EINVAL;
	}

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_ENABLE_SRP,
					enable, ar->pdev->pdev_id);

	if (ret) {
		ath12k_err(NULL, "WMI command failed, discarding the configuration");
	} else {
		ar->sr_cfg.psr_tx_enable = enable;
	}

	return ret;
}

static int ath12k_set_self_safety_margin_psr(struct ath12k *ar,
					     u8 margin)
{
	int ret;

	if (margin > 5) {
		ath12k_err(NULL, "Safety margin should be in the range 0-5 (both inclusive)");
		return -EINVAL;
	}

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_SR_TRIGGER_MARGIN,
					margin, ar->pdev->pdev_id);

	if (ret) {
		ath12k_err(NULL, "WMI command failed, discarding the configuration");
	} else {
		ar->sr_cfg.safety_margin_psr = margin;
	}

	return ret;
}

static int ath12k_configure_obss_pd_ctrl_frame(struct ath12k *ar,
					       u32 enable,
					       int32_t threshold,
					       enum srtype type)
{
	int ret;
	int temp_thresh;

	if (enable > 1) {
		ath12k_err(NULL, "OBSS_PD Threshold enable value should be either 0 or 1");
		return -EINVAL;
	}

	/* OBSS Packet Detect threshold bounds for Spatial Reuse feature.
	 * The parameter value is programmed into the spatial reuse
	 * register, to specify how low the background signal strength
	 * from neighboring BSS cells must be, for this AP to
	 * employ spatial reuse.
	 */
	if (threshold > SELF_OBSS_PD_UPPER_THRESH ||
	    threshold < SELF_OBSS_PD_LOWER_THRESH) {
		ath12k_err(NULL, "Threshold must in the range [%d, %d] (both inclusive)",
			   SELF_OBSS_PD_LOWER_THRESH, SELF_OBSS_PD_UPPER_THRESH);
        	return -EINVAL;
	}

	/* Read the existing configuration */
	temp_thresh  = ar->sr_cfg.obss_pd_thresh_ctrl_frame;

	/* Set the enable value */
	if (set_obss_pd_enable_bit(&temp_thresh, type, enable))
		return -EINVAL;

	/* Set the threshold */
	if (set_obss_pd_threshold(&temp_thresh, type, threshold))
		return -EINVAL;

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_CTRL_FRAME_OBSS_PD_THRESHOLD,
					temp_thresh, ar->pdev->pdev_id);

	if (ret) {
		ath12k_err(NULL, "WMI send for set cmd obss pd threshold ctrl frame failed");
	} else {
		ar->sr_cfg.obss_pd_thresh_ctrl_frame = temp_thresh;
	}

	return ret;
}

static int ath12k_set_self_sr_enable_truncation(struct ath12k *ar,
						u8 enable)
{
	int ret;

	if (enable > 1) {
		ath12k_err(NULL, "SR truncate enable field can either be 0 or 1");
		return -EINVAL;
	}

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_TRUNCATE_SR,
					enable, ar->pdev->pdev_id);

	if (ret) {
		ath12k_err(NULL, "WMI command failed, discarding the configuration");
	} else {
		ar->sr_cfg.enable_sr_truncate = enable;
	}

	return ret;
}

static int ath12k_set_self_sr_obss_ppdu_min_dur(struct ath12k *ar,
						u32 duration)
{
	int ret;

	if (duration > MAX_SELF_SR_MIN_TXOP ) {
		ath12k_err(NULL, "Enter a valid sr duration < Min: 0 -  Max: 65535 >");
		return -EINVAL;
	}

	ret = ath12k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_OBSS_MIN_DURATION_CHECK_FOR_SR,
					duration, ar->pdev->pdev_id);

	if (ret) {
		ath12k_err(NULL, "WMI command failed, discarding the configuration");
	} else {
		ar->sr_cfg.min_obss_ppdu_dur_sr = duration;
	}

	return ret;
}

static int ath12k_set_self_srg_bss_color_bitmap(struct ath12k *ar,
						u32 *bitmap)
{
	int ret;

	ret = ath12k_wmi_pdev_set_srg_bss_color_bitmap(ar, bitmap);
	if (ret) {
		ath12k_err(NULL, "WMI send for set self srg bitmap failed, discarding the configuration");
	} else {
		ar->sr_cfg.srg_bss_color_bitmap[0] = bitmap[0];
		ar->sr_cfg.srg_bss_color_bitmap[1] = bitmap[1];
	}

	return ret;
}

static int ath12k_set_self_srg_partial_bssid_bitmap(struct ath12k *ar,
						    u32 *bitmap)
{
	int ret;

	ret = ath12k_wmi_pdev_set_srg_patial_bssid_bitmap(ar, bitmap);
	if (ret) {
		ath12k_err(NULL, "WMI send for set self srg bitmap failed, discarding the configuration");
	} else {
		ar->sr_cfg.srg_partial_bssid_bitmap[0] = bitmap[0];
		ar->sr_cfg.srg_partial_bssid_bitmap[1] = bitmap[1];
	}

	return ret;
}

static int ath12k_set_self_srg_obss_color_enable_bitmap(struct ath12k *ar,
							u32 *bitmap)
{
	int ret;

	ret = ath12k_wmi_pdev_srg_obss_color_enable_bitmap(ar, bitmap);
	if (ret) {
		ath12k_err(NULL, "WMI send for set_self_srg_obss_color_enable_bitmap failed, discarding the configuration");
	} else {
		ar->sr_cfg.srg_obss_color_enable_bitmap[0] = bitmap[0];
		ar->sr_cfg.srg_obss_color_enable_bitmap[1] = bitmap[1];
	}

	return ret;
}

static int ath12k_set_self_srg_obss_bssid_enable_bitmap(struct ath12k *ar,
							u32 *bitmap)
{
	int ret;

	ret = ath12k_wmi_pdev_srg_obss_bssid_enable_bitmap(ar, bitmap);
	if (ret) {
		ath12k_err(NULL, "WMI send for set_self_srg_obss_bssid_enable_bitmap failed, discarding the configuration");
	} else {
		ar->sr_cfg.srg_obss_bssid_enable_bitmap[0] = bitmap[0];
		ar->sr_cfg.srg_obss_bssid_enable_bitmap[1] = bitmap[1];
	}

	return ret;
}

static int ath12k_set_self_non_srg_obss_color_enable_bitmap(struct ath12k *ar,
							    u32 *bitmap)
{
	int ret;

	ret = ath12k_wmi_pdev_non_srg_obss_color_enable_bitmap(ar, bitmap);
	if (ret) {
		ath12k_err(NULL, "WMI send for set_self_non_srg_obss_color_enable_bitmap failed, discarding the configuration");
	} else {
		ar->sr_cfg.non_srg_obss_color_enable_bitmap[0] = bitmap[0];
		ar->sr_cfg.non_srg_obss_color_enable_bitmap[1] = bitmap[1];
	}

	return ret;
}

static int ath12k_set_self_non_srg_obss_bssid_enable_bitmap(struct ath12k *ar,
							    u32 *bitmap)
{
	int ret;

	ret = ath12k_wmi_pdev_non_srg_obss_bssid_enable_bitmap(ar, bitmap);
	if (ret) {
		ath12k_err(NULL, "WMI send for set_self_non_srg_obss_bssid_enable_bitmap failed, discarding the configuration");
	} else {
		ar->sr_cfg.non_srg_obss_bssid_enable_bitmap[0] = bitmap[0];
		ar->sr_cfg.non_srg_obss_bssid_enable_bitmap[1] = bitmap[1];
	}

	return ret;
}

static int ath12k_vendor_set_sr_radio_params(struct ath12k *ar,
					     u32 param, void *data,
					     u32 data_len, u32 value,
					     u32 flags, bool *reload)
{
	int ret = -1;
	u32 bitmap[2];

	switch (param) {
	case SR_SELF_OBSS_PD_TX_ENABLE:
		ret = ath12k_set_obss_pd_enable_bit(ar, value,
						    ath12k_extact_sr_type(data, data_len));
		break;
	case SR_SELF_OBSS_PD_THRESHOLD_DB:
		ret = ath12k_set_obss_pd_threshold(ar, value,
						   ath12k_extact_sr_type(data, data_len));
		break;
	case SR_SELF_OBSS_PD_THRESHOLD_DBM:
		ret = ath12k_set_obss_pd_threshold(ar, value,
						   ath12k_extact_sr_type(data, data_len));
		break;
	case SR_SELF_ENABLE_PER_AC:
		ret = ath12k_set_sr_per_ac(ar, value,
					   ath12k_extact_sr_type(data, data_len));
		break;
	case SR_SELF_HESIGA_SR15_ENABLE:
		ret = ath12k_set_self_hesiga_sr15_enable(ar,
							 *(uint8_t*)data);
		break;
	case SR_SELF_PSR_TX_ENABLE:
		ret = ath12k_set_self_psr_tx_enable(ar,
						    *(uint8_t*)data);
		break;
	case SR_SELF_SAFETY_MARGIN_PSR:
		ret = ath12k_set_self_safety_margin_psr(ar,
							*(uint8_t*)data);
		break;
	case SR_SELF_OBSS_PD_CTRL_FRAME:
		ret = ath12k_configure_obss_pd_ctrl_frame(ar, value,
							  flags,
							  ath12k_extact_sr_type(data, data_len));
		break;
	case SR_SELF_ENABLE_TRUNCATION:
		ret = ath12k_set_self_sr_enable_truncation(ar,
							   *(uint8_t*)data);
		break;
	case SR_SELF_OBSS_PPDU_MIN_DUR:
		ret = ath12k_set_self_sr_obss_ppdu_min_dur(ar,
							   *(uint8_t*)data);
		break;
	case SR_SELF_SRG_BSS_COLOR_BITMAP:
		bitmap[0] = value;
		memcpy(&bitmap[1], data, sizeof(u32));
		ret = ath12k_set_self_srg_bss_color_bitmap(ar,
							   bitmap);
		break;
	case SR_SELF_SRG_PARTIAL_BSSID_BITMAP:
		bitmap[0] = value;
		memcpy(&bitmap[1], data, sizeof(u32));
		ret = ath12k_set_self_srg_partial_bssid_bitmap(ar,
							       bitmap);
		break;
	case SR_SELF_SRG_OBSS_COLOR_ENABLE_BITMAP:
		bitmap[0] = value;
		memcpy(&bitmap[1], data, sizeof(u32));
		ret = ath12k_set_self_srg_obss_color_enable_bitmap(ar,
								   bitmap);
		break;
	case SR_SELF_SRG_OBSS_BSSID_ENABLE_BITMAP:
		bitmap[0] = value;
		memcpy(&bitmap[1], data, sizeof(u32));
		ret = ath12k_set_self_srg_obss_bssid_enable_bitmap(ar,
								   bitmap);
		break;
	case SR_SELF_NON_SRG_OBSS_COLOR_ENABLE_BITMAP:
		bitmap[0] = value;
		memcpy(&bitmap[1], data, sizeof(u32));
		ret = ath12k_set_self_non_srg_obss_color_enable_bitmap(ar,
								       bitmap);
		break;
	case SR_SELF_NON_SRG_OBSS_BSSID_ENABLE_BITMAP:
		bitmap[0] = value;
		memcpy(&bitmap[1], data, sizeof(u32));
		ret = ath12k_set_self_non_srg_obss_bssid_enable_bitmap(ar,
								       bitmap);
		break;
	default:
		ath12k_info(NULL, "Unhandled SR config command");
		return -EINVAL;
	}

	return ret;
}

/* Set radio level parameters
 * set 'reload' to true to send reload event to userspace */
static int ath12k_vendor_set_radio_params(struct ath12k *ar,
					  u32 param, u32 value,
					  struct ath12k_wifi_generic_params *params,
					  bool *reload)
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
	case PARAM_RADIO_TXCHAINSOFT:
		ret = ath12k_set_chainmask(ar, value, true);
		break;
	case ACFG_PARAM_RADIO_RXCHAINMASK:
		ret = ath12k_set_chainmask(ar, value, false);
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
		 if (value < -128 && value>127) {
		     ath12k_err(NULL, "value should between -128 to 127");
		     return -EINVAL;
		 }
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
	case QCA_WLAN_VENDOR_RADIO_PARAM_CTLPWRSCALE:
		ret = ath12k_vendor_set_ctlpwrscale(ar, value);
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_EN_CHAN_144:
		ret = ath12k_vendor_chan144_apply(ar, value);
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_PDEV_TO_REO_DEST:
		if (value < ATH12K_REO2SW1_RING || value > ATH12K_REO2SW4_RING) {
			ath12k_err(NULL, "REO dest ring value should be between %d and %d\n",
					ATH12K_REO2SW1_RING, ATH12K_REO2SW4_RING);
                        ret = -EINVAL;
                } else {
                        ar->radio_cfg.pdev_to_reo_dest = value;
                        ret = 0;
                }
                break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_NOL_CHAN_LIST:
	{
		struct wiphy *wiphy;
		char *path;
		u32 updated = 0;

		if (!ar->ah || !ar->ah->hw || !ar->ah->hw->wiphy)
			return -EINVAL;

		wiphy = ar->ah->hw->wiphy;
		ret = ath12k_vendor_resolve_nol_path_from_data_extn(params->data,
								    params->data_len,
								    params->radio_idx,
								    &path);
		if (ret)
			return ret;

		ret = ath12k_vendor_set_nol_chan_list_extn(wiphy,
							   params->radio_idx,
							   path, &updated);
		kfree(path);
		if (ret)
			return ret;

		ret = ath12k_vendor_reply_u32_extn(wiphy, updated);
		break;
	}
	case QCA_WLAN_VENDOR_RADIO_PARAM_WSI_BYPASS:
	        int i, ret;
	        struct ath12k_hw_group *ag;
	        struct ath12k_base *ab;
	        struct ath12k_wsi_remap_work *w;

	        ath12k_err(ar->ab,
	                   "set_radio_params: WSI_BYPASS param=%u value=%u\n",
	                   param, value);

	        /* Expect val: 1 = bypass(remove) device, 2 = re-add device */
	        if (value != ATH12K_WSI_BYPASS_REMOVE_DEVICE &&
	            value != ATH12K_WSI_BYPASS_ADD_DEVICE) {
	                ath12k_err(ar->ab,
	                           "set_radio_params: invalid WSI_BYPASS value=%u\n",
	                           value);
	                return -EINVAL;
	        }

	        ab = ar->ab;
	        if (!ab) {
	                ath12k_err(NULL, "set_radio_params: ab is NULL\n");
	                return -EINVAL;
	        }

	        /* Reject if any device is statically bypassed */
	        ag = ab->ag;
	        if (!ag) {
	                ath12k_err(ab, "set_radio_params: ag is NULL\n");
	                return -EINVAL;
	        }

	        for (i = 0; i < ag->num_devices; i++) {
	                struct ath12k_base *partner_ab = ag->ab[i];
	                if (partner_ab && partner_ab->is_static_bypassed) {
	                        ath12k_err(ab,
	                                   "set_radio_params: reject, static bypassed dev idx=%d\n",
	                                   i);
	                        return -EINVAL;
	                }
	        }

	        mutex_lock(&ab->core_lock);
	        ret = ath12k_wsi_bypass_precheck(ab, value);
	        if (ret) {
	                ath12k_err(ab,
	                           "set_radio_params: wsi_bypass_precheck failed ret=%d value=%u state=%u is_bypassed=%d\n",
	                           ret, value, ab->wsi_remap_state, ab->is_bypassed);
	                mutex_unlock(&ab->core_lock);
	                return ret;
	        }

	        ab->wsi_remap_state = value;

	        /* Redundant safety: if already bypassed and request is remove */
	        if (ab->is_bypassed &&
	                ab->wsi_remap_state == ATH12K_WSI_BYPASS_REMOVE_DEVICE) {
	                ath12k_err(ab,
	                           "set_radio_params: reject redundant remove (is_bypassed=1)\n");
	                mutex_unlock(&ab->core_lock);
	                return -EINVAL;
	        }
	        mutex_unlock(&ab->core_lock);

	        /* Schedule bypass/remap asynchronously to avoid blocking cfg80211 */
	        w = kzalloc(sizeof(*w), GFP_KERNEL);
	        if (!w) {
	                ath12k_err(ab, "set_radio_params: kzalloc work failed\n");
	                return -ENOMEM;
	        }

	        INIT_WORK(&w->work, ath12k_wsi_bypass_workfn);
	        w->ab = ab;

	        queue_work(system_unbound_wq, &w->work);

	        return 0;
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

/*
 *  Enable per-peer sounding interval via WMI
 */
int ath12k_vendor_enable_sounding_extn(struct wiphy *wiphy, struct wireless_dev *wdev, struct ath12k_wifi_generic_params *params)
{
        struct ieee80211_vif *vif = NULL;
        struct ath12k_vif *ahvif;
        struct ath12k_link_vif *arvif = NULL;
        struct ath12k_hw *ah = NULL;
        struct ath12k *ar;
        int ret = -1;
        u8  mac_addr[ETH_ALEN];
        u32 enable;
        u32 vdev_id = 0;

        lockdep_assert_wiphy(wiphy);

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
        if (!arvif || !arvif->ar) {
                rcu_read_unlock();
                return -EINVAL;
        }

        ar = arvif->ar;
        vdev_id = arvif->vdev_id;
        rcu_read_unlock();

       if (params->data_len != ETH_ALEN) {
        ath12k_err(NULL, "Invalid radio MAC address len: %u\n", params->data_len);
        return -EINVAL;
       }
       if (!is_valid_ether_addr(params->data)) {
               ath12k_err(NULL, "Invalid radio MAC address\n");
               return -EINVAL;
       }
       ether_addr_copy(mac_addr, (u8 *)params->data);

       enable = !!params->value;
       ath12k_dbg(NULL, ATH12K_DBG_CFG,
                "hwaddr: %p link: %d, vdev-id : %d, enable: %d\n",
                 mac_addr, params->link_id, vdev_id, enable);

       ret = ath12k_wmi_set_peer_param(ar, mac_addr, vdev_id, WMI_PEER_PARAM_SOUNDING_ENABLE, enable);
       if (ret) {
            ath12k_err(NULL, "WMI FAILED\n");
       }
       else {
            ath12k_err(NULL,"WMI OK\n");
       }
       return ret;
}

/*
 * Set per-peer SU sounding interval via WMI
 */
int ath12k_vendor_set_sounding_params_extn(struct wiphy *wiphy, struct wireless_dev *wdev, struct ath12k_wifi_generic_params *params)
{
        struct ieee80211_vif *vif = NULL;
        struct ath12k_vif *ahvif;
        struct ath12k_link_vif *arvif = NULL;
        struct ath12k_hw *ah = NULL;
        struct ath12k *ar;
        int ret = -1;
        u8  mac_addr[ETH_ALEN];
        u32 interval_ms;
        u32 vdev_id = 0;

        lockdep_assert_wiphy(wiphy);
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
        if (!arvif || !arvif->ar) {
                rcu_read_unlock();
                return -EINVAL;
        }
        ar = arvif->ar;
        vdev_id = arvif->vdev_id;
        rcu_read_unlock();
       if (params->data_len != ETH_ALEN) {
        ath12k_err(NULL, "Invalid radio MAC address len: %u\n", params->data_len);
        return -EINVAL;
       }
       if (!is_valid_ether_addr(params->data)) {
               ath12k_err(NULL, "Invalid radio MAC address\n");
               return -EINVAL;
       }
       ether_addr_copy(mac_addr, (u8 *)params->data);
       interval_ms = params->value;
       ath12k_dbg(NULL, ATH12K_DBG_CFG,
                  "hwaddr: %p, link: %d, vdev-id : %d, interval_ms: %d\n",
                   mac_addr,  params->link_id, vdev_id, interval_ms);

       ret = ath12k_wmi_set_peer_param(ar, mac_addr, vdev_id, WMI_PEER_PARAM_SU_SET_SOUNDING_INTERVAL, interval_ms);
       if (ret) {
          ath12k_err(NULL, "WMI FAILED\n");
       }
       else {
       ath12k_err(NULL, "WMI OK\n");
       }
       return ret;
}

/*
 * Set per-peer MU sounding interval via WMI
 */
int ath12k_vendor_set_mu_sounding_params_extn(struct wiphy *wiphy, struct wireless_dev *wdev, struct ath12k_wifi_generic_params *params)
{
        struct ieee80211_vif *vif = NULL;
        struct ath12k_vif *ahvif;
        struct ath12k_link_vif *arvif = NULL;
        struct ath12k_hw *ah = NULL;
        struct ath12k *ar;
        int ret = -1;
        u8  mac_addr[ETH_ALEN];
        u32 interval_ms;
        u32 vdev_id = 0;

        lockdep_assert_wiphy(wiphy);

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
        if (!arvif || !arvif->ar) {
                rcu_read_unlock();
                return -EINVAL;
        }

        ar = arvif->ar;
        vdev_id = arvif->vdev_id;
        rcu_read_unlock();

       if (params->data_len != ETH_ALEN) {
        ath12k_err(NULL, "Invalid radio MAC address len: %u\n", params->data_len);
        return -EINVAL;
       }

       if (!is_valid_ether_addr(params->data)) {
               ath12k_err(NULL, "Invalid radio MAC address\n");
               return -EINVAL;
       }

       ether_addr_copy(mac_addr, (u8 *)params->data);
       interval_ms = params->value;

       ath12k_dbg(NULL, ATH12K_DBG_CFG,
                  "hwaddr: %p, link: %d, vdev-id : %d, interval_ms: %d\n",
                   mac_addr,  params->link_id, vdev_id, interval_ms);

       ret = ath12k_wmi_set_peer_param(ar, mac_addr, vdev_id, WMI_PEER_PARAM_MU_SET_SOUNDING_INTERVAL, interval_ms);

       if (ret) {
            ath12k_err(NULL,"WMI FAILED\n");
        }
       else {
           ath12k_err(NULL,"WMI OK\n");
       }
       return ret;
}

/*
 * Enable per-peer Sched mu via WMI
 */
int ath12k_vendor_sched_mu_enable(struct wiphy *wiphy,
                                  struct wireless_dev *wdev,
                                  struct ath12k_wifi_generic_params *params)
{
        struct ieee80211_vif *vif;
        struct ath12k_vif *ahvif;
        struct ath12k_link_vif *arvif = NULL;
        struct ath12k_hw *ah;
        struct ath12k *ar;
        u8  mac_addr[ETH_ALEN];
        u32 enable;
        u32 vdev_id;
        int ret;

        lockdep_assert_wiphy(wiphy);

        if (!wdev || !params) {
                ath12k_err(NULL, "Invalid args wdev=%p params=%p\n", wdev, params);
                return -EINVAL;
        }

        if (!params->data || params->data_len != ETH_ALEN) {
                ath12k_err(NULL, "Invalid MAC len=%u\n", params->data_len);
                return -EINVAL;
        }
        if (!is_valid_ether_addr(params->data)) {
            ath12k_err(NULL, "Invalid MAC %pM\n", (u8 *)params->data);
            return -EINVAL;
        }
        ether_addr_copy(mac_addr, (const u8 *)params->data);

        vif = wdev_to_ieee80211_vif(wdev);
        if (!vif) {
                ath12k_err(NULL, "vif is NULL\n");
                return -EINVAL;
        }

        ahvif = ath12k_vif_to_ahvif(vif);
        if (!ahvif) {
                ath12k_err(NULL, "ahvif is NULL\n");
                return -EINVAL;
        }

        ah = ahvif->ah;

        rcu_read_lock();
        if (ah && params->link_id == INVALID_LINK_ID) {
                arvif = &ahvif->deflink;
        } else if (params->link_id < ATH12K_NUM_MAX_LINKS) {
                arvif = rcu_dereference(ahvif->link[params->link_id]);
        }
        if (!arvif || !arvif->ar) {
                rcu_read_unlock();
                ath12k_err(NULL, "arvif/ar NULL (link=%d)\n", params->link_id);
                return -EINVAL;
        }

        ar = arvif->ar;
        vdev_id = arvif->vdev_id;
        rcu_read_unlock();

        enable = !!params->value;

        ath12k_dbg(NULL, ATH12K_DBG_CFG,
                 "hwaddr: %p link: %d, vdev-id : %d, enable: %d\n",
                 mac_addr, params->link_id, vdev_id, enable);

        ret = ath12k_wmi_set_peer_param(ar, mac_addr, vdev_id,
                                        WMI_PEER_PARAM_MU_SCHED_ENABLE,
                                        enable);
        if (ret) {
               ath12k_err(NULL,"WMI FAILED\n");
        }
        else {
            ath12k_err(NULL,"WMI OK\n");
        }
        return ret;
}

/*
 * Enable Sched ofdma enable via WMI
 */
int ath12k_vendor_sched_ofdma_enable(struct wiphy *wiphy,
                                  struct wireless_dev *wdev,
                                  struct ath12k_wifi_generic_params *params)
{
        struct ieee80211_vif *vif;
        struct ath12k_vif *ahvif;
        struct ath12k_link_vif *arvif = NULL;
        struct ath12k_hw *ah;
        struct ath12k *ar;
        u8  mac_addr[ETH_ALEN];
        u32 enable;
        u32 vdev_id;
        int ret;

        lockdep_assert_wiphy(wiphy);

        if (!wdev || !params) {
                ath12k_err(NULL, "Invalid args wdev=%p params=%p\n", wdev, params);
                return -EINVAL;
        }

        if (!params->data || params->data_len != ETH_ALEN) {
                ath12k_err(NULL, "Invalid MAC len=%u\n", params->data_len);
                return -EINVAL;
        }
        if (!is_valid_ether_addr(params->data)) {
            ath12k_err(NULL, "Invalid MAC %pM\n", (u8 *)params->data);
            return -EINVAL;
        }
        ether_addr_copy(mac_addr, (const u8 *)params->data);

        vif = wdev_to_ieee80211_vif(wdev);
        if (!vif) {
                ath12k_err(NULL, "vif is NULL\n");
                return -EINVAL;
        }

        ahvif = ath12k_vif_to_ahvif(vif);
        if (!ahvif) {
                ath12k_err(NULL, "ahvif is NULL\n");
                return -EINVAL;
        }

        ah = ahvif->ah;

        rcu_read_lock();
        if (ah && params->link_id == INVALID_LINK_ID) {
                arvif = &ahvif->deflink;
        } else if (params->link_id < ATH12K_NUM_MAX_LINKS) {
                arvif = rcu_dereference(ahvif->link[params->link_id]);
        }
        if (!arvif || !arvif->ar) {
                rcu_read_unlock();
                ath12k_err(NULL, "arvif/ar NULL (link=%d)\n", params->link_id);
                return -EINVAL;
        }

        ar = arvif->ar;
        vdev_id = arvif->vdev_id;
        rcu_read_unlock();

        enable = !!params->value;

        ath12k_dbg(NULL, ATH12K_DBG_CFG,
                   "MU-SCHED RESOLVED: ar=%p vdev_id=%u peer=%pM enable=%u ifidx=%d link=%d\n",
                   ar, vdev_id, mac_addr, enable, params->ifindex, params->link_id);

        /* Apply via WMI: MU scheduler enable/disable */
        ret = ath12k_wmi_set_peer_param(ar, mac_addr, vdev_id,
                                        WMI_PEER_PARAM_OFDMA_SCHED_ENABLE,
                                        enable);
        if (ret)
                ath12k_err(NULL,"WMI FAILED\n");
        else
                ath12k_err(NULL,"WMI OK\n");
        return ret;
}


/*
 *  Set per-peer Nav_Override_Config via WMI
 */
int ath12k_vendor_set_nav_override_config_extn(struct wiphy *wiphy,
                                               struct wireless_dev *wdev,
                                               struct ath12k_wifi_generic_params *params)
{
    struct ieee80211_vif *vif;
    struct ath12k_vif *ahvif;
    struct ath12k_link_vif *arvif = NULL;
    struct ath12k *ar;
    u32 status = 0, threshold = 0, cfg;
    int ret;


    if (!wdev || !params) {
        ath12k_err(NULL, "NAV-SET ERROR: wdev or params NULL\n");
        return -EINVAL;
    }

    /*
     * Correct decode for cfg80211tool mapping:
     *  - status is encoded in packed data (lower 2 bits)
     *  - threshold comes from params->value (CLI second argument)
     */
    if (params->data && params->data_len >= sizeof(u32)) {
        u32 v = *(const u32 *)params->data;
        status = v & 0x3;
        threshold = v >> 2;

        if (params->value)
            threshold = params->value;

    }
    else {
        ath12k_err(NULL, "NAV-SET ERROR: missing packed data\n");
        return -EINVAL;
    }

    if (status > 3) {
        ath12k_err(NULL, "NAV-SET ERROR: invalid status=%u\n", status);
        return -EINVAL;
    }

    cfg = (threshold << 2) | (status & 0x3);

    /* Resolve vif */
    vif = wdev_to_ieee80211_vif(wdev);
    if (!vif) {
        ath12k_err(NULL, "NAV-SET ERROR: vif NULL\n");
        return -EINVAL;
    }

    ahvif = (struct ath12k_vif *)vif->drv_priv;
    if (!ahvif) {
        ath12k_err(NULL, "NAV-SET ERROR: ahvif NULL\n");
        return -EINVAL;
    }

    /* Resolve MLO link */
    rcu_read_lock();
    if (params->link_id == INVALID_LINK_ID) {
        arvif = &ahvif->deflink;
    } else if (params->link_id < IEEE80211_MLD_MAX_NUM_LINKS) {
        arvif = rcu_dereference(ahvif->link[params->link_id]);
    }

    if (!arvif || !arvif->ar) {
        ath12k_err(NULL,
            "NAV-SET ERROR: arvif/ar NULL for link=%u\n",
            params->link_id);
        rcu_read_unlock();
        return -EINVAL;
    }

    ar = arvif->ar;
    rcu_read_unlock();

    /* Send WMI */
    ret = ath12k_wmi_pdev_set_param(ar,
                                    WMI_PDEV_PARAM_NAV_OVERRIDE_CONFIG,
                                    cfg,
                                    ar->pdev->pdev_id);
    if (ret) {
        ath12k_err(NULL,"WMI FAILED\n");
        return ret;
    }
    else {
        ath12k_err(NULL, "WMI OK\n");
    }

    /* Cache for GET */
    arvif->nav_status = status;
    arvif->nav_threshold = threshold;
    return ret;
}

int ath12k_vendor_get_nav_override_config_extn(struct wiphy *wiphy,
                                               struct wireless_dev *wdev,
                                               struct ath12k_wifi_generic_params *params)
{
    struct ieee80211_vif *vif;
    struct ath12k_vif *ahvif;
    struct ath12k_link_vif *arvif = NULL;
    struct sk_buff *skb;
    u32 combined;

    if (!wdev) {
        ath12k_err(NULL, "NAV-GET ERROR: wdev NULL\n");
        return -EINVAL;
    }

    /* Resolve vif */
    vif = wdev_to_ieee80211_vif(wdev);
    if (!vif) {
        ath12k_err(NULL, "vif NULL\n");
        return -EINVAL;
    }

    ahvif = (struct ath12k_vif *)vif->drv_priv;
    if (!ahvif) {
        ath12k_err(NULL, "ahvif NULL\n");
        return -EINVAL;
    }

    /* Resolve link */
    if (!params || params->link_id == INVALID_LINK_ID) {
        arvif = &ahvif->deflink;
        ath12k_err(NULL, "Using deflink\n");
    } else if (params->link_id < IEEE80211_MLD_MAX_NUM_LINKS) {
        rcu_read_lock();
        arvif = rcu_dereference(ahvif->link[params->link_id]);
        rcu_read_unlock();
        ath12k_err(NULL, "Using link=%u\n", params->link_id);
    }

    if (!arvif) {
        ath12k_err(NULL, "arvif NULL\n");
        return -EINVAL;
    }

    combined = (arvif->nav_status & 0x3) |
               (arvif->nav_threshold << 2);

    skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, NLMSG_DEFAULT_SIZE);
    if (!skb) {
        ath12k_err(NULL, "Skb alloc failed\n");
        return -ENOMEM;
    }

    /* Generic reply (kernel‑correct) */
    if (nla_put_u32(skb, QCA_WLAN_VENDOR_ATTR_PARAM_DATA, combined) ||
        nla_put_u32(skb, QCA_WLAN_VENDOR_ATTR_PARAM_LENGTH, sizeof(u32)) ||
        nla_put_u32(skb, QCA_WLAN_VENDOR_ATTR_PARAM_FLAGS, 0)) {
        kfree_skb(skb);
        ath12k_err(NULL, "Generic nla_put failed\n");
        return -EINVAL;
    }

    /* Tool‑specific attributes (cfg80211tool expects these for ID 74) */
    if (nla_put_u32(skb, 3 , combined) ||             /* dparam0 */
        nla_put_u32(skb, 1 , arvif->nav_status) ||    /* value0 */
        nla_put_u32(skb, 2 , arvif->nav_threshold)) { /* value1 */
        kfree_skb(skb);
        ath12k_err(NULL, "tool attr nla_put failed\n");
        return -EINVAL;
    }

    return cfg80211_vendor_cmd_reply(skb);
}

/*
 * Set Ulofdma_rtd via WMI
 */
int ath12k_vendor_peer_ulofdma_rtd_extn(struct wiphy *wiphy,
                                        struct wireless_dev *wdev,
                                        struct ath12k_wifi_generic_params *params)
{
        struct ieee80211_vif *vif = NULL;
        struct ath12k_vif *ahvif;
        struct ath12k_link_vif *arvif = NULL;
        struct ath12k_hw *ah = NULL;
        struct ath12k *ar;
        u8  mac_addr[ETH_ALEN];
        u32 rtd_ms;
        u32 vdev_id = 0;
        int ret = -1;

        lockdep_assert_wiphy(wiphy);

        if (!wdev || !params || !params->data) {
                ath12k_err(NULL, "Invalid input args\n");
                return -EINVAL;
        }

        /* Validate MAC */
        if (params->data_len != ETH_ALEN) {
                ath12k_err(NULL, "Invalid MAC address len: %u\n", params->data_len);
                return -EINVAL;
        }
        if (!is_valid_ether_addr(params->data)) {
                ath12k_err(NULL, "Invalid MAC address\n");
                return -EINVAL;
        }
        ether_addr_copy(mac_addr, (u8 *)params->data);

        /* Validate value (0 - 600) */
        rtd_ms = params->value;
        if (rtd_ms > 600) {
                ath12k_err(NULL, "Invalid value %u (range 0..600)\n", rtd_ms);
                return -EINVAL;
        }

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
        if (!arvif || !arvif->ar) {
                rcu_read_unlock();
                ath12k_err(NULL, "arvif/ar NULL (link=%d)\n", params->link_id);
                return -EINVAL;
        }

        ar = arvif->ar;
        vdev_id = arvif->vdev_id;
        rcu_read_unlock();

        ath12k_dbg(NULL, ATH12K_DBG_CFG,
                   "UL-RTD extn RESOLVED: ar=%p vdev_id=%u peer=%p rtd=%u ifidx=%d link=%d\n",
                   ar, vdev_id, mac_addr, rtd_ms, params->ifindex, params->link_id);

        /* Send via WMI */
        ret = ath12k_wmi_set_peer_param(ar, mac_addr, vdev_id,
                                        WMI_PDEV_PARAM_UL_OFDMA_RTD,
                                        rtd_ms);
        if (ret)
                ath12k_err(NULL,"WMI FAILED\n");
        else
                ath12k_err(NULL,"WMI OK\n");

        return ret;
}

/**
 * ath12k_vendor_get_he_mcs_12_13_cap - Read pdev HE MCS 12/13 capability.
 * @ah:        ath12k_hw instance
 * @tb:        parsed netlink attribute table
 * @skb:       reply skb to fill
 *
 * Reads the pdev HE MCS 12/13 hardware capability from
 * (U32 bits)band_cap->he_cap_info[1] bits[31:16] (the extended HE MAC cap
 * word populated by firmware.
 *
 * Format returned (2 bytes via GENERIC_DATA):
 *   byte[0] = bits[23:16] = L80 NSS bitmap (<=80 MHz)
 *   byte[1] = bits[31:24] = G80 NSS bitmap (>80 MHz)
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ath12k_vendor_get_he_mcs_12_13_cap(struct ath12k_hw *ah, struct nlattr **tb,
					      u64 *value)
{
	struct ath12k *ar;
	struct ath12k_band_cap *cap_band;
	enum nl80211_band band;
	u8 radio_idx;

	if (!tb[QCA_WLAN_VENDOR_ATTR_CONFIG_RADIO_INDEX]) {
		ath12k_err(NULL, "HE MCS 12/13: radio index not specified\n");
		return -EINVAL;
	}

	radio_idx = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_CONFIG_RADIO_INDEX]);
	if (radio_idx >= ah->num_radio) {
		ath12k_err(NULL, "HE MCS 12/13: invalid radio index %u\n", radio_idx);
		return -EINVAL;
	}

	ar = &ah->radio[radio_idx];
	if (ar->pdev->cap.supported_bands & BIT(NL80211_BAND_5GHZ))
		band = NL80211_BAND_5GHZ;
	else if (ar->pdev->cap.supported_bands & BIT(NL80211_BAND_6GHZ))
		band = NL80211_BAND_6GHZ;
	else
		band = NL80211_BAND_2GHZ;

	cap_band = &ar->pdev->cap.band[band];
	ar->ar_extn.he_mcs_12_13_radio_cap = (u16)(cap_band->he_cap_info[1] >> 16);
	*value = ar->ar_extn.he_mcs_12_13_radio_cap;
	ath12k_dbg(ar->ab, ATH12K_DBG_CFG,
		   "HE MCS 12/13 capability = 0x%04x radio_idx = %u band = %d\n",
		   ar->ar_extn.he_mcs_12_13_radio_cap, radio_idx, band);

	return 0;
}

/**
 * ath12k_vendor_set_he_mcs_12_13_peer_cap - Store intersected peer HE MCS 12/13
 * NSS capability, split into TX and RX fields.
 *
 * @wiphy:       wiphy pointer
 * @wifi_params: parsed generic command parameters
 *
 * Stores the intersected peer capability with the radio's own hardware capab
 * for use during the WMI peer-assoc command.
 *
 * Results are stored as:
 *   he_mcs_12_13_peer_rx_cap = radio_cap & peer_cap (all platforms)
 *   he_mcs_12_13_peer_tx_cap = radio_cap & peer_cap (platforms that can RX MCS 12/13)
 *                            = 0 (platforms that cannot RX MCS 12/13, eg: IPQ5332)
 *
 * Returns 0 on success, negative error code on failure.
 */
static int ath12k_vendor_set_he_mcs_12_13_peer_cap(struct wiphy *wiphy,
						   struct ath12k_wifi_generic_params *wifi_params)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath12k_hw *ah = hw->priv;
	struct ath12k *ar;
	u8 radio_idx = wifi_params->radio_idx;
	__le16 peer_cap_le;
	u16 peer_cap_supp;

	if (radio_idx >= ah->num_radio) {
		ath12k_err(NULL, "HE MCS 12/13 SET: invalid radio index %u\n", radio_idx);
		return -EINVAL;
	}

	ar = &ah->radio[radio_idx];
	if (!wifi_params->data || wifi_params->data_len < sizeof(peer_cap_le)) {
		ath12k_err(ar->ab, "HE MCS 12/13 SET: missing or short data\n");
		return -EINVAL;
	}
	memcpy(&peer_cap_le, wifi_params->data, sizeof(peer_cap_le));
	peer_cap_supp = ar->ar_extn.he_mcs_12_13_radio_cap & le16_to_cpu(peer_cap_le);
	ar->ar_extn.he_mcs_12_13_peer_rx_cap = peer_cap_supp;
	if (ar->ab->hw_rev == ATH12K_HW_IPQ5332_HW10) {
		ar->ar_extn.he_mcs_12_13_peer_tx_cap = 0;
	} else {
		ar->ar_extn.he_mcs_12_13_peer_tx_cap = peer_cap_supp;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_CFG,
		   "HE MCS 12/13: peer tx capability = 0x%04x peer rx capability = 0x%04x radio_idx=%u\n",
		   ar->ar_extn.he_mcs_12_13_peer_tx_cap,
		   ar->ar_extn.he_mcs_12_13_peer_rx_cap,
		   radio_idx);

	return 0;
}

void ath12k_peer_assoc_h_he_mcs_12_13_extn(struct ath12k *ar,
					   struct ath12k_wmi_peer_assoc_arg *arg)
{
	u16 rx_cap = ar->ar_extn.he_mcs_12_13_peer_rx_cap;
	u16 tx_cap = ar->ar_extn.he_mcs_12_13_peer_tx_cap;
	u32 rx_packed = WMI_HE_EXTRA_MCS_PACK(rx_cap);
	u32 tx_packed = WMI_HE_EXTRA_MCS_PACK(tx_cap);
	int i;

	for (i = 0; i < arg->peer_he_mcs_count; i++) {
		arg->peer_he_rx_mcs_set[i] &=
			~(WMI_HE_EXTRA_MCS_L80_MASK | WMI_HE_EXTRA_MCS_G80_MASK);
		arg->peer_he_rx_mcs_set[i] |= rx_packed;
		arg->peer_he_tx_mcs_set[i] &=
			~(WMI_HE_EXTRA_MCS_L80_MASK | WMI_HE_EXTRA_MCS_G80_MASK);
		arg->peer_he_tx_mcs_set[i] |= tx_packed;
	}
}

int ath12k_vendor_wifi_config_handler_extn(struct wiphy *wiphy, struct wireless_dev *wdev,
					   struct ath12k_wifi_generic_params *wifi_params)
{
	int ret = 0;

	if (!wiphy || !wdev || !wifi_params) {
		ath12k_err(NULL, "NULL input params received\n");
		return -EINVAL;
	}

	switch (wifi_params->command) {
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
	case QCA_NL80211_VENDOR_WIFI_GENERIC_SUBCMD_MESH_PARAMS:
		if (!wifi_params->data) {
			ath12k_err(NULL, "Invalid param command received\n");
			return -EINVAL;
		}
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
        case QCA_WLAN_VENDOR_WIFI_PARAM_ENABLE_SOUNDING:
		if (!wifi_params->data) {
			ath12k_err(NULL,"Invalid param command received\n");
			return -EINVAL;
		}
		ret = ath12k_vendor_enable_sounding_extn(wiphy, wdev, wifi_params);
		if (ret) {
			ath12k_err(NULL,"Failed to set wifi params \n");
			return -EINVAL;
		}
		break;
        case QCA_WLAN_VENDOR_WIFI_PARAM_SU_SND_INTERVAL:
		if (!wifi_params->data) {
			ath12k_err(NULL,"Invalid param command received\n");
			return -EINVAL;
		}
		ret = ath12k_vendor_set_sounding_params_extn(wiphy, wdev, wifi_params);
		if (ret) {
			ath12k_err(NULL,"Failed to set wifi params \n");
			return -EINVAL;
		}
		break;
	case QCA_WLAN_VENDOR_WIFI_PARAM_MU_SND_INTERVAL:
		if (!wifi_params->data) {
			ath12k_err(NULL,"Invalid param command received\n");
			return -EINVAL;
		}
		ret = ath12k_vendor_set_mu_sounding_params_extn(wiphy, wdev, wifi_params);
		if (ret) {
			ath12k_err(NULL,"Failed to set wifi params \n");
			return -EINVAL;
		}
		break;
	case QCA_WLAN_VENDOR_WIFI_PARAM_SCHED_MU_ENABLE:
		if (!wifi_params->data) {
			ath12k_err(NULL,"Invalid param command received\n");
			return -EINVAL;
		}
		ret = ath12k_vendor_sched_mu_enable(wiphy, wdev, wifi_params);
		if (ret) {
			ath12k_err(NULL,"Failed to set wifi params \n");
			return -EINVAL;
		}
		break;
	case QCA_WLAN_VENDOR_WIFI_PARAM_SCHED_OFDMA_ENABLE:
		if (!wifi_params->data) {
			ath12k_err(NULL,"Invalid param command received\n");
			return -EINVAL;
		}
		ret = ath12k_vendor_sched_ofdma_enable(wiphy, wdev, wifi_params);
		if (ret) {
			ath12k_err(NULL,"Failed to set wifi params \n");
			return -EINVAL;
		}
		break;
	case QCA_WLAN_VENDOR_WIFI_PARAM_SET_NAV_OVERRIDE_CONFIG:
		if (!wifi_params->data) {
			ath12k_err(NULL,"Invalid param command received\n");
			return -EINVAL;
		}
		ret = ath12k_vendor_set_nav_override_config_extn(wiphy, wdev, wifi_params);
		if (ret) {
			ath12k_err(NULL,"Failed to set wifi params \n");
			return -EINVAL;
		}
		break;
         case QCA_WLAN_VENDOR_WIFI_PARAM_GET_NAV_OVERRIDE_CONFIG:
		struct ath12k_wifi_generic_params dummy = {0};
		dummy.link_id = INVALID_LINK_ID;
		ret = ath12k_vendor_get_nav_override_config_extn(wiphy, wdev, &dummy);
		if (ret) {
			ath12k_err(NULL,"Failed to set wifi params \n");
			return -EINVAL;
		}
		break;
         case QCA_WLAN_VENDOR_WIFI_PARAM_UL_OFDMA_RTD:
		if (!wifi_params->data) {
			ath12k_err(NULL,"Invalid param command received\n");
			return -EINVAL;
		}
		ret = ath12k_vendor_peer_ulofdma_rtd_extn(wiphy, wdev, wifi_params);
		if (ret) {
			ath12k_err(NULL,"Failed to ufodma rtd params \n");
			return ret;
		}
		break;
#endif
	case QCA_NL80211_VENDOR_SUBCMD_HE_MCS_12_13_SUPP:
		return ath12k_vendor_set_he_mcs_12_13_peer_cap(wiphy, wifi_params);
	default:
		ath12k_dbg(NULL, ATH12K_DBG_CFG, "Un-supported generic command\n");
		return -EOPNOTSUPP;
	}

	return ret;
}

#define ATH12K_VENDOR_NOL_LOG_LIMIT         64

/**
 * ath12k_vendor_nol_chan_matches_radio() - Check whether a channel
 * matches a requested radio
 * @wiphy: Wiphy that owns the channel table
 * @chan: Channel to evaluate
 * @radio_idx: Target radio index, or negative to match all radios
 *
 * Return: %true if @chan belongs to @radio_idx or no radio filter is set.
 */
bool
ath12k_vendor_nol_chan_matches_radio(struct wiphy *wiphy,
				     struct ieee80211_channel *chan,
				     int radio_idx)
{
	if (radio_idx < 0)
		return true;

	return cfg80211_get_hw_idx_by_chan(wiphy, chan) == radio_idx;
}

/**
 * ath12k_vendor_build_default_nol_path_extn() - Build the default NOL dump
 * path
 * @fallback: %true to build the fallback path, %false for the primary path
 * @radio_idx: Radio index encoded in the file name
 *
 * Return: Allocated path string on success, or %NULL on allocation failure.
 */
static char *ath12k_vendor_build_default_nol_path_extn(bool fallback, u8 radio_idx)
{
	return kasprintf(GFP_KERNEL,
			 fallback ? ATH12K_VENDOR_NOL_PERSIST_PATH_FALLBACK_FMT :
				    ATH12K_VENDOR_NOL_PERSIST_PATH_PRIMARY_FMT,
			 radio_idx);
}

/**
 * ath12k_vendor_validate_dump_path_extn() - Validate a NOL dump file path
 * @path: Absolute path provided for dump persistence
 *
 * Return: 0 if @path is acceptable for kernel file access, else -EINVAL.
 */
static int ath12k_vendor_validate_dump_path_extn(const char *path)
{
	if (!path || !path[0] || path[0] != '/')
		return -EINVAL;

	if (strstr(path, "/../") || strstr(path, "/..") == path ||
	    (strlen(path) >= 3 && !strcmp(path + strlen(path) - 3, "/..")))
		return -EINVAL;

	return 0;
}

/**
 * ath12k_vendor_alloc_nol_paths_extn() - Allocate default primary and
 * fallback paths
 * @radio_idx: Radio index encoded in the file names
 * @primary_path: Returned primary path string
 * @fallback_path: Returned fallback path string
 *
 * Return: 0 if both paths were allocated, or -ENOMEM on failure.
 */
static int ath12k_vendor_alloc_nol_paths_extn(u8 radio_idx,
					      char **primary_path,
					      char **fallback_path)
{
	*primary_path = ath12k_vendor_build_default_nol_path_extn(false,
								 radio_idx);
	*fallback_path = ath12k_vendor_build_default_nol_path_extn(true,
								  radio_idx);
	if (*primary_path && *fallback_path)
		return 0;

	kfree(*primary_path);
	kfree(*fallback_path);
	return -ENOMEM;
}

/**
 * ath12k_vendor_open_dump_write_file_extn() - Open a NOL dump file for
 * writing
 * @path: Requested destination path
 * @primary_path: Default primary path for this radio
 * @fallback_path: Default fallback path for this radio
 * @filp: Returned open file pointer
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int ath12k_vendor_open_dump_write_file_extn(const char *path,
						   const char *primary_path,
						   const char *fallback_path,
						   struct file **filp)
{
	int ret;

	ret = ath12k_vendor_validate_dump_path_extn(path);
	if (ret)
		return ret;

	*filp = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(*filp) && PTR_ERR(*filp) == -ENOENT &&
	    !strcmp(path, primary_path)) {
		ret = ath12k_vendor_validate_dump_path_extn(fallback_path);
		if (ret)
			return ret;
		*filp = filp_open(fallback_path, O_WRONLY | O_CREAT | O_TRUNC,
				  0644);
	}

	return IS_ERR(*filp) ? PTR_ERR(*filp) : 0;
}

/**
 * ath12k_vendor_write_file_contents_extn() - Write a full buffer to an open
 * file
 * @filp: File to write and close
 * @buf: Buffer containing serialized NOL data
 * @len: Number of bytes to write from @buf
 *
 * Return: 0 if all bytes were written, otherwise a negative error code.
 */
static int ath12k_vendor_write_file_contents_extn(struct file *filp,
						  const char *buf, size_t len)
{
	loff_t pos = 0;
	ssize_t wrote;

	wrote = kernel_write(filp, buf, len, &pos);
	filp_close(filp, NULL);
	if (wrote < 0)
		return (int)wrote;

	return (size_t)wrote == len ? 0 : -ENOSPC;
}

static int ath12k_vendor_write_buf_to_file_extn(const char *path,
						u8 radio_idx,
						const char *buf, size_t len);

/**
 * ath12k_vendor_mirror_tmp_nol_dump_extn() - Mirror a /tmp dump to persistent
 * storage
 * @path: Path used for the original dump
 * @radio_idx: Radio index used to derive the persistent path
 * @buf: Serialized NOL data to mirror
 * @len: Number of bytes in @buf
 *
 * Return: 0 if mirroring is skipped or attempted, or -ENOMEM on failure.
 */
static int ath12k_vendor_mirror_tmp_nol_dump_extn(const char *path,
						  u8 radio_idx,
						  const char *buf, size_t len)
{
	char *persist_path;
	int ret;

	if (strncmp(path, "/tmp/", 5))
		return 0;

	persist_path = ath12k_vendor_build_default_nol_path_extn(false,
								 radio_idx);
	if (!persist_path)
		return -ENOMEM;

	ret = ath12k_vendor_write_buf_to_file_extn(persist_path, radio_idx, buf,
						       len);
	kfree(persist_path);
	if (ret)
		ath12k_err(NULL,
			   "NOL dump under /tmp is volatile; persistent mirror failed: %d\n",
			   ret);

	return 0;
}

/**
 * ath12k_vendor_write_buf_to_file_extn() - Persist a serialized NOL buffer to
 * disk
 * @path: Destination path requested by userspace
 * @radio_idx: Radio index used to resolve default paths
 * @buf: Buffer to persist
 * @len: Number of bytes in @buf
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int ath12k_vendor_write_buf_to_file_extn(const char *path,
						u8 radio_idx,
						const char *buf, size_t len)
{
	struct file *filp;
	char *primary_path;
	char *fallback_path;
	int ret;

	ret = ath12k_vendor_alloc_nol_paths_extn(radio_idx, &primary_path,
						 &fallback_path);
	if (ret)
		return ret;

	ret = ath12k_vendor_open_dump_write_file_extn(path, primary_path,
						      fallback_path, &filp);
	if (!ret)
		ret = ath12k_vendor_write_file_contents_extn(filp, buf, len);

	kfree(primary_path);
	kfree(fallback_path);
	if (ret)
		return ret;

	return ath12k_vendor_mirror_tmp_nol_dump_extn(path, radio_idx, buf, len);
}

/**
 * ath12k_vendor_try_open_dump_file_extn() - Open a validated dump file
 * @path: Absolute path to open for reading
 *
 * Return: Open file pointer on success or an error pointer on failure.
 */
static struct file *ath12k_vendor_try_open_dump_file_extn(const char *path)
{
	int ret;

	ret = ath12k_vendor_validate_dump_path_extn(path);
	if (ret)
		return ERR_PTR(ret);

	return filp_open(path, O_RDONLY, 0);
}

/**
 * ath12k_vendor_open_dump_read_file_extn() - Open a dump file with fallbacks
 * @path: Requested source path
 * @primary_path: Default primary path for this radio
 * @fallback_path: Default fallback path for this radio
 *
 * Return: Open file pointer on success or an error pointer on failure.
 */
static struct file *
ath12k_vendor_open_dump_read_file_extn(const char *path,
				       const char *primary_path,
				       const char *fallback_path)
{
	struct file *filp;

	filp = ath12k_vendor_try_open_dump_file_extn(path);
	if (!IS_ERR(filp) || PTR_ERR(filp) != -ENOENT)
		return filp;
	if (!strcmp(path, primary_path))
		return ath12k_vendor_try_open_dump_file_extn(fallback_path);
	if (strncmp(path, "/tmp/", 5))
		return filp;

	filp = ath12k_vendor_try_open_dump_file_extn(primary_path);
	if (!IS_ERR(filp) || PTR_ERR(filp) != -ENOENT)
		return filp;

	return ath12k_vendor_try_open_dump_file_extn(fallback_path);
}

/**
 * ath12k_vendor_read_filp_to_buf_extn() - Read a dump file into memory
 * @filp: Open file to read from
 * @out_buf: Returned buffer holding the file contents
 * @out_len: Returned number of bytes stored in @out_buf
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int ath12k_vendor_read_filp_to_buf_extn(struct file *filp,
					       char **out_buf, size_t *out_len)
{
	loff_t pos = 0;
	ssize_t read_len;
	size_t size;
	char *buf;

	size = i_size_read(file_inode(filp));
	if (!size)
		return -EINVAL;

	size = min_t(size_t, size, 128 * 1024);
	buf = vzalloc(size + 1);
	if (!buf)
		return -ENOMEM;

	read_len = kernel_read(filp, buf, size, &pos);
	if (read_len < 0) {
		vfree(buf);
		return (int)read_len;
	}

	buf[read_len] = '\0';
	*out_buf = buf;
	*out_len = (size_t)read_len;
	return 0;
}

/**
 * ath12k_vendor_read_file_to_buf_extn() - Read a dump file with fallbacks
 * @path: Requested source path
 * @radio_idx: Radio index used to resolve default paths
 * @out_buf: Returned buffer holding the file contents
 * @out_len: Returned number of bytes stored in @out_buf
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int ath12k_vendor_read_file_to_buf_extn(const char *path,
					       u8 radio_idx,
					       char **out_buf, size_t *out_len)
{
	struct file *filp;
	char *primary_path;
	char *fallback_path;
	int ret;

	if (!out_buf || !out_len)
		return -EINVAL;

	ret = ath12k_vendor_validate_dump_path_extn(path);
	if (ret)
		return ret;

	ret = ath12k_vendor_alloc_nol_paths_extn(radio_idx, &primary_path,
						 &fallback_path);
	if (ret)
		return ret;

	filp = ath12k_vendor_open_dump_read_file_extn(path, primary_path,
							 fallback_path);
	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		goto out_free;
	}

	ret = ath12k_vendor_read_filp_to_buf_extn(filp, out_buf, out_len);
	filp_close(filp, NULL);
out_free:
	kfree(primary_path);
	kfree(fallback_path);
	return ret;
}

/**
 * ath12k_vendor_get_nol_left_ms_extn() - Compute remaining NOP time for a
 * channel
 * @chan: Channel whose DFS state is inspected
 * @nop_ms: Configured NOP timeout in milliseconds
 *
 * Return: Remaining unavailable time in milliseconds, or 0 if expired.
 */
static u32 ath12k_vendor_get_nol_left_ms_extn(const struct ieee80211_channel *chan,
					      u32 nop_ms)
{
	u32 elapsed_ms;

	if (chan->dfs_state != NL80211_DFS_UNAVAILABLE)
		return 0;

	elapsed_ms = jiffies_to_msecs(jiffies - chan->dfs_state_entered);
	return (elapsed_ms >= nop_ms) ? 0 : (nop_ms - elapsed_ms);
}

/**
 * ath12k_vendor_count_nol_entries_extn() - Count NOL channels for one radio
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to filter, or negative to count all radios
 * @nop_ms: NOP timeout used to age DFS state
 *
 * Return: Number of channels that are still in NOL.
 */
static u32 ath12k_vendor_count_nol_entries_extn(struct wiphy *wiphy,
						int radio_idx, u32 nop_ms)
{
	u32 entry_count = 0;
	int band, chan_idx;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		struct ieee80211_supported_band *sband = wiphy->bands[band];

		if (!sband || !sband->channels)
			continue;

		for (chan_idx = 0; chan_idx < sband->n_channels; chan_idx++) {
			struct ieee80211_channel *chan = &sband->channels[chan_idx];

			if (!ath12k_vendor_nol_chan_matches_radio(wiphy, chan,
								  radio_idx))
				continue;
			if (!ath12k_vendor_get_nol_left_ms_extn(chan, nop_ms))
				continue;
			entry_count++;
		}
	}

	return entry_count;
}

/**
 * ath12k_vendor_init_nol_bin_hdr_extn() - Initialize a serialized NOL dump
 * header
 * @hdr: Header buffer to initialize
 * @nop_ms: NOP timeout recorded in the dump
 * @entry_count: Number of NOL entries stored after the header
 *
 * Return: None.
 */
static void ath12k_vendor_init_nol_bin_hdr_extn(
	struct ath12k_vendor_nol_bin_hdr *hdr, u32 nop_ms, u32 entry_count)
{
	memset(hdr, 0, sizeof(*hdr));
	memcpy(hdr->magic, ATH12K_VENDOR_NOL_BIN_MAGIC, sizeof(hdr->magic));
	hdr->version = cpu_to_le16(1);
	hdr->hdr_len = cpu_to_le16(sizeof(*hdr));
	hdr->nop_ms = cpu_to_le32(nop_ms);
	hdr->dump_time_real_ns = cpu_to_le64(ktime_get_real_ns());
	hdr->entry_count = cpu_to_le32(entry_count);
}

/**
 * ath12k_vendor_fill_nol_entries_extn() - Fill serialized NOL entries for a
 * radio
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to filter, or negative to include all radios
 * @nop_ms: NOP timeout used to age DFS state
 * @entries: Output array for serialized NOL entries
 * @entry_count: Capacity of @entries in elements
 *
 * Return: Number of entries written to @entries.
 */
static u32 ath12k_vendor_fill_nol_entries_extn(
	struct wiphy *wiphy, int radio_idx, u32 nop_ms,
	struct ath12k_vendor_nol_bin_entry *entries, u32 entry_count)
{
	u32 entry_idx = 0;
	int band, chan_idx;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		struct ieee80211_supported_band *sband = wiphy->bands[band];

		if (!sband || !sband->channels)
			continue;

		for (chan_idx = 0; chan_idx < sband->n_channels; chan_idx++) {
			struct ieee80211_channel *chan = &sband->channels[chan_idx];
			u32 left_ms;

			if (!ath12k_vendor_nol_chan_matches_radio(wiphy, chan,
								  radio_idx))
				continue;

			left_ms = ath12k_vendor_get_nol_left_ms_extn(chan, nop_ms);
			if (!left_ms || entry_idx >= entry_count)
				continue;

			entries[entry_idx].freq_mhz =
				cpu_to_le32((u32)chan->center_freq);
			entries[entry_idx].left_ms = cpu_to_le32(left_ms);
			entries[entry_idx].bw_mhz = cpu_to_le32(20);
			entry_idx++;
		}
	}

	return entry_idx;
}

/**
 * ath12k_vendor_build_nol_chan_list_extn() - Build a serialized NOL snapshot
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to serialize
 * @out_buf: Returned serialized buffer
 * @out_len: Returned length of @out_buf in bytes
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int ath12k_vendor_build_nol_chan_list_extn(struct wiphy *wiphy,
						  int radio_idx,
						  char **out_buf,
						  size_t *out_len)
{
	struct ath12k_vendor_nol_bin_hdr hdr;
	struct ath12k_vendor_nol_bin_entry *entries;
	char *buf;
	u32 entry_count;
	u32 nop_ms = IEEE80211_DFS_MIN_NOP_TIME_MS;
	u32 entry_idx;

	if (!out_buf || !out_len)
		return -EINVAL;

	lockdep_assert_wiphy(wiphy);

	entry_count = ath12k_vendor_count_nol_entries_extn(wiphy, radio_idx,
							    nop_ms);
	buf = kzalloc(sizeof(hdr) + ((size_t)entry_count * sizeof(*entries)),
		      GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ath12k_vendor_init_nol_bin_hdr_extn(&hdr, nop_ms, entry_count);
	memcpy(buf, &hdr, sizeof(hdr));
	entries = (void *)(buf + sizeof(hdr));
	entry_idx = ath12k_vendor_fill_nol_entries_extn(wiphy, radio_idx,
							 nop_ms, entries,
							 entry_count);
	*out_buf = buf;
	*out_len = sizeof(hdr) + ((size_t)entry_idx * sizeof(*entries));
	return 0;
}

/**
 * ath12k_vendor_log_nol_band_extn() - Log active NOL entries for one band
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to filter, or negative to include all radios
 * @tag: Prefix string used in log messages
 * @nop_ms: NOP timeout used to age DFS state
 * @band: nl80211 band index to scan
 * @total: Running count of matching NOL entries
 * @printed: Running count of entries already logged
 *
 * Return: None.
 */
static void ath12k_vendor_log_nol_band_extn(struct wiphy *wiphy, int radio_idx,
					    const char *tag, u32 nop_ms,
					    int band, int *total, int *printed)
{
	struct ieee80211_supported_band *sband = wiphy->bands[band];
	int chan_idx;

	if (!sband || !sband->channels)
		return;

	for (chan_idx = 0; chan_idx < sband->n_channels; chan_idx++) {
		struct ieee80211_channel *chan = &sband->channels[chan_idx];
		u32 left_ms;
		int ch;

		if (!ath12k_vendor_nol_chan_matches_radio(wiphy, chan,
							 radio_idx))
			continue;

		left_ms = ath12k_vendor_get_nol_left_ms_extn(chan, nop_ms);
		if (!left_ms)
			continue;

		(*total)++;
		if (*printed >= ATH12K_VENDOR_NOL_LOG_LIMIT)
			continue;

		ch = ieee80211_frequency_to_channel(chan->center_freq);
		wiphy_info(wiphy, "%s: freq=%d MHz chan=%d left=%u ms\n",
			   tag, chan->center_freq, ch, left_ms);
		(*printed)++;
	}
}

/**
 * ath12k_vendor_log_nol_chan_list_extn() - Log the current NOL state
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to filter, or negative to include all radios
 * @tag: Prefix string used in log messages
 *
 * Return: None.
 */
static void ath12k_vendor_log_nol_chan_list_extn(struct wiphy *wiphy,
						 int radio_idx, const char *tag)
{
	u32 nop_ms = IEEE80211_DFS_MIN_NOP_TIME_MS;
	int band;
	int total = 0;
	int printed = 0;

	if (!wiphy)
		return;

	if (!tag)
		tag = "nol";

	lockdep_assert_wiphy(wiphy);

	wiphy_info(wiphy, "%s: current NOL channels (NOP=%u ms)\n", tag,
		   nop_ms);
	for (band = 0; band < NUM_NL80211_BANDS; band++)
		ath12k_vendor_log_nol_band_extn(wiphy, radio_idx, tag, nop_ms,
						band, &total, &printed);

	if (!total) {
		wiphy_info(wiphy, "%s: NOL empty\n", tag);
		return;
	}

	if (total > printed)
		wiphy_info(wiphy, "%s: total=%d (truncated after %d)\n",
			   tag, total, printed);
	else
		wiphy_info(wiphy, "%s: total=%d\n", tag, total);
}

/**
 * ath12k_vendor_skip_spaces_extn() - Skip leading whitespace in a string
 * @str: String to advance in place
 *
 * Return: Pointer to the first non-space character in @str.
 */
static char *ath12k_vendor_skip_spaces_extn(char *str)
{
	while (*str && isspace(*str))
		str++;

	return str;
}

/**
 * ath12k_vendor_get_nol_dump_delta_ms_extn() - Age a dump timestamp in
 * milliseconds
 * @dump_ns: Real-time dump timestamp in nanoseconds
 *
 * Return: Elapsed milliseconds since @dump_ns, or 0 if unavailable.
 */
static u64 ath12k_vendor_get_nol_dump_delta_ms_extn(u64 dump_ns)
{
	u64 now_ns;

	if (!dump_ns)
		return 0;

	now_ns = ktime_get_real_ns();
	return div_u64((now_ns > dump_ns) ? (now_ns - dump_ns) : 0, 1000000ULL);
}

/**
 * ath12k_vendor_parse_nol_text_header_extn() - Parse metadata from a text
 * dump header
 * @line: Header line starting with '#'
 * @dump_ns: Returned dump timestamp in nanoseconds
 * @nop_ms: Returned NOP timeout in milliseconds
 *
 * Return: None.
 */
static void ath12k_vendor_parse_nol_text_header_extn(char *line, u64 *dump_ns,
						     u32 *nop_ms)
{
	char *tok;
	char *p = ath12k_vendor_skip_spaces_extn(line + 1);

	while ((tok = strsep(&p, " \t")) != NULL) {
		u64 tmp_u64;
		u32 tmp_u32;

		if (!*tok)
			continue;
		if (!strncmp(tok, "dump_time_real_ns=", 18)) {
			if (!kstrtou64(tok + 18, 0, &tmp_u64))
				*dump_ns = tmp_u64;
			continue;
		}
		if (strncmp(tok, "nop_ms=", 7) || kstrtou32(tok + 7, 0, &tmp_u32))
			continue;

		tmp_u32 = min_t(u32, tmp_u32,
				(u32)IEEE80211_DFS_MIN_NOP_TIME_MS);
		if (tmp_u32)
			*nop_ms = tmp_u32;
	}
}

/**
 * ath12k_vendor_nol_left_ms_token_extn() - Match a token carrying remaining
 * NOL time
 * @tok: Token from a text dump line
 *
 * Return: Pointer to the numeric substring if @tok matches, else %NULL.
 */
static const char *ath12k_vendor_nol_left_ms_token_extn(const char *tok)
{
	if (!strncmp(tok, "nol_left_ms=", 12))
		return tok + 12;
	if (!strncmp(tok, "nol_timeout_ms=", 15))
		return tok + 15;
	if (!strncmp(tok, "left_ms=", 8))
		return tok + 8;

	return NULL;
}

/**
 * ath12k_vendor_parse_nol_text_entry_extn() - Parse one text NOL entry
 * @line: Entry line to parse in place
 * @nop_ms: Default NOP timeout in milliseconds
 * @freq_mhz: Returned center frequency in MHz
 * @left_ms: Returned remaining NOL time in milliseconds
 *
 * Return: 0 if an entry was parsed, or -ENOENT if the line has no usable
 * entry.
 */
static int ath12k_vendor_parse_nol_text_entry_extn(char *line, u32 nop_ms,
						   int *freq_mhz, u32 *left_ms)
{
	char *tok;
	bool has_left = false;

	*freq_mhz = -1;
	*left_ms = nop_ms;

	while ((tok = strsep(&line, " \t")) != NULL) {
		const char *left_ms_ptr;
		u32 tmp_u32;

		if (!*tok)
			continue;
		if (!strncmp(tok, "freq=", 5)) {
			int tmp_freq;

			if (!kstrtoint(tok + 5, 0, &tmp_freq))
				*freq_mhz = tmp_freq;
			continue;
		}

		left_ms_ptr = ath12k_vendor_nol_left_ms_token_extn(tok);
		if (!left_ms_ptr || kstrtou32(left_ms_ptr, 0, &tmp_u32))
			continue;

		*left_ms = tmp_u32;
		has_left = true;
	}

	if (*freq_mhz <= 0)
		return -ENOENT;
	if (!has_left)
		*left_ms = nop_ms;

	*left_ms = min_t(u32, *left_ms, nop_ms);
	return *left_ms ? 0 : -ENOENT;
}

/**
 * ath12k_vendor_adjust_nol_left_ms_extn() - Age a saved NOL timeout
 * @left_ms: Saved remaining NOL time in milliseconds
 * @nop_ms: Maximum NOP timeout in milliseconds
 * @delta_ms: Elapsed wall-clock time since the dump
 * @remaining_ms: Returned remaining NOL time after aging
 *
 * Return: %true if the entry still has time left, otherwise %false.
 */
static bool ath12k_vendor_adjust_nol_left_ms_extn(u32 left_ms, u32 nop_ms,
						  u64 delta_ms, u32 *remaining_ms)
{
	left_ms = min_t(u32, left_ms, nop_ms);
	if (!left_ms || delta_ms >= left_ms)
		return false;

	*remaining_ms = left_ms - (u32)delta_ms;
	return true;
}

/**
 * ath12k_vendor_apply_nol_freq_extn() - Restore one channel into NOL state
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to filter, or negative to include all radios
 * @freq_mhz: Channel center frequency to restore
 * @left_ms: Saved remaining NOL time in milliseconds
 * @nop_ms: Maximum NOP timeout in milliseconds
 * @delta_ms: Elapsed wall-clock time since the dump
 *
 * Return: %true if a channel was updated, otherwise %false.
 */
static bool ath12k_vendor_apply_nol_freq_extn(struct wiphy *wiphy, int radio_idx,
					      u32 freq_mhz, u32 left_ms,
					      u32 nop_ms, u64 delta_ms)
{
	struct ieee80211_channel *chan;
	unsigned long delta_j;
	u32 remaining_ms;

	if (!freq_mhz || !ath12k_vendor_adjust_nol_left_ms_extn(left_ms, nop_ms,
								delta_ms,
								&remaining_ms))
		return false;

	chan = ieee80211_get_channel(wiphy, (int)freq_mhz);
	if (!chan || !ath12k_vendor_nol_chan_matches_radio(wiphy, chan,
							  radio_idx))
		return false;

	chan->dfs_state = NL80211_DFS_UNAVAILABLE;
	delta_j = msecs_to_jiffies(nop_ms - remaining_ms);
	chan->dfs_state_entered = jiffies - delta_j;
	return true;
}

/**
 * ath12k_vendor_apply_nol_from_text_buf_extn() - Restore NOL entries from a
 * text dump
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to restore
 * @buf: Writable text buffer containing dump contents
 *
 * Return: Number of channels restored on success or a negative error code.
 */
static int ath12k_vendor_apply_nol_from_text_buf_extn(struct wiphy *wiphy,
						      int radio_idx, char *buf)
{
	char *line;
	u64 dump_ns = 0;
	u64 delta_ms = 0;
	u32 nop_ms = IEEE80211_DFS_MIN_NOP_TIME_MS;
	int updated = 0;

	if (!wiphy || !buf)
		return -EINVAL;

	lockdep_assert_wiphy(wiphy);

	while ((line = strsep(&buf, "\n")) != NULL) {
		int freq_mhz;
		u32 left_ms;

		line = ath12k_vendor_skip_spaces_extn(line);
		if (!*line)
			continue;

		if (*line == '#') {
			ath12k_vendor_parse_nol_text_header_extn(line, &dump_ns,
								&nop_ms);
			delta_ms = ath12k_vendor_get_nol_dump_delta_ms_extn(dump_ns);
			continue;
		}

		if (ath12k_vendor_parse_nol_text_entry_extn(line, nop_ms, &freq_mhz,
							    &left_ms))
			continue;
		if (ath12k_vendor_apply_nol_freq_extn(wiphy, radio_idx, freq_mhz,
						      left_ms, nop_ms,
						      delta_ms))
			updated++;
	}

	cfg80211_schedule_dfs_chan_update(wiphy);

	return updated;
}

struct ath12k_vendor_nol_bin_info_extn {
	const struct ath12k_vendor_nol_bin_entry *entries;
	u32 entry_count;
	u32 nop_ms;
	u64 delta_ms;
};

/**
 * ath12k_vendor_parse_nol_bin_hdr_extn() - Validate a binary NOL dump header
 * @buf: Binary dump buffer
 * @len: Number of bytes available in @buf
 * @info: Returned parsed header metadata
 *
 * Return: 0 if the header is valid or a negative error code otherwise.
 */
static int ath12k_vendor_parse_nol_bin_hdr_extn(
	const u8 *buf, size_t len, struct ath12k_vendor_nol_bin_info_extn *info)
{
	const struct ath12k_vendor_nol_bin_hdr *hdr = (const void *)buf;
	u16 hdr_len;
	u16 version;
	size_t need_len;

	if (len < sizeof(*hdr))
		return -EINVAL;
	if (memcmp(hdr->magic, ATH12K_VENDOR_NOL_BIN_MAGIC, sizeof(hdr->magic)))
		return -EINVAL;

	version = le16_to_cpu(hdr->version);
	hdr_len = le16_to_cpu(hdr->hdr_len);
	if (version != 1 || hdr_len < sizeof(*hdr) || hdr_len > len)
		return -EINVAL;

	info->nop_ms = le32_to_cpu(hdr->nop_ms);
	if (!info->nop_ms)
		info->nop_ms = IEEE80211_DFS_MIN_NOP_TIME_MS;
	info->nop_ms = min_t(u32, info->nop_ms,
			     (u32)IEEE80211_DFS_MIN_NOP_TIME_MS);
	info->entry_count = le32_to_cpu(hdr->entry_count);
	need_len = (size_t)hdr_len + (size_t)info->entry_count *
		   sizeof(*info->entries);
	if (need_len > len)
		return -EINVAL;

	info->delta_ms = ath12k_vendor_get_nol_dump_delta_ms_extn(
		le64_to_cpu(hdr->dump_time_real_ns));
	info->entries = (const void *)(buf + hdr_len);
	return 0;
}

/**
 * ath12k_vendor_apply_nol_from_bin_buf_extn() - Restore NOL entries from a
 * binary dump
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to restore
 * @buf: Binary dump buffer
 * @len: Number of bytes available in @buf
 *
 * Return: Number of channels restored on success or a negative error code.
 */
static int ath12k_vendor_apply_nol_from_bin_buf_extn(struct wiphy *wiphy,
						     int radio_idx,
						     const u8 *buf, size_t len)
{
	struct ath12k_vendor_nol_bin_info_extn info;
	u32 i;
	int updated = 0;
	int ret;

	if (!wiphy || !buf)
		return -EINVAL;

	lockdep_assert_wiphy(wiphy);

	ret = ath12k_vendor_parse_nol_bin_hdr_extn(buf, len, &info);
	if (ret)
		return ret;

	for (i = 0; i < info.entry_count; i++) {
		if (ath12k_vendor_apply_nol_freq_extn(
			    wiphy, radio_idx,
			    le32_to_cpu(info.entries[i].freq_mhz),
			    le32_to_cpu(info.entries[i].left_ms), info.nop_ms,
			    info.delta_ms))
			updated++;
	}

	cfg80211_schedule_dfs_chan_update(wiphy);

	return updated;
}

/**
 * ath12k_vendor_apply_nol_from_buf_extn() - Restore NOL entries from a dump
 * buffer
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to restore
 * @buf: Dump buffer, either text or binary
 * @len: Number of bytes available in @buf
 *
 * Return: Number of channels restored on success or a negative error code.
 */
static int ath12k_vendor_apply_nol_from_buf_extn(struct wiphy *wiphy,
						 int radio_idx, char *buf,
						 size_t len)
{
	if (wiphy && buf && len >= sizeof(struct ath12k_vendor_nol_bin_hdr) &&
	    !memcmp(buf, ATH12K_VENDOR_NOL_BIN_MAGIC,
		    sizeof(ATH12K_VENDOR_NOL_BIN_MAGIC)))
		return ath12k_vendor_apply_nol_from_bin_buf_extn(wiphy, radio_idx,
								 (const u8 *)buf,
								 len);

	return ath12k_vendor_apply_nol_from_text_buf_extn(wiphy, radio_idx, buf);
}

/**
 * ath12k_vendor_restore_nol_file_extn() - Restore NOL entries from a file
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to restore
 * @path: Dump file path to read
 *
 * Return: Number of channels restored on success or a negative error code.
 */
static int ath12k_vendor_restore_nol_file_extn(struct wiphy *wiphy,
					       u8 radio_idx, const char *path)
{
	char *file_buf = NULL;
	size_t file_len = 0;
	int ret;

	ret = ath12k_vendor_read_file_to_buf_extn(path, radio_idx, &file_buf,
						  &file_len);
	if (ret)
		return ret;

	ret = ath12k_vendor_apply_nol_from_buf_extn(wiphy, radio_idx, file_buf,
						    file_len);
	vfree(file_buf);
	return ret;
}

/**
 * ath12k_vendor_validate_radio_idx() - Validate a radio index for setnol
 * @wiphy: Wiphy exposing the radio count
 * @radio_idx: Radio index from userspace
 *
 * Return: 0 if @radio_idx is valid, otherwise -EINVAL.
 */
int ath12k_vendor_validate_radio_idx(struct wiphy *wiphy, u8 radio_idx)
{
	if (!wiphy || radio_idx == INVALID_RADIO_INDEX ||
	    radio_idx >= wiphy->n_radio)
		return -EINVAL;

	return 0;
}

/**
 * ath12k_vendor_resolve_nol_path_extn() - Resolve the dump path for setnol
 * @path: Optional userspace path
 * @radio_idx: Radio index used for the default path
 * @resolved_path: Returned resolved path string
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int ath12k_vendor_resolve_nol_path_extn(const char *path, u8 radio_idx,
					       char **resolved_path)
{
	if (path && *path)
		*resolved_path = kstrdup(path, GFP_KERNEL);
	else
		*resolved_path = ath12k_vendor_build_default_nol_path_extn(false,
									  radio_idx);

	return *resolved_path ? 0 : -ENOMEM;
}

/**
 * ath12k_vendor_resolve_nol_path_from_data_extn() - Resolve the dump path for
 * getnol/setnol command payload
 * @data: Optional userspace path buffer
 * @data_len: Number of bytes available in @data
 * @radio_idx: Radio index used for the default path
 * @path: Returned resolved path string
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int ath12k_vendor_resolve_nol_path_from_data_extn(const void *data,
							 u32 data_len,
							 u8 radio_idx,
							 char **path)
{
	if (!path)
		return -EINVAL;

	if (!data || !data_len)
		*path = ath12k_vendor_build_default_nol_path_extn(false,
								  radio_idx);
	else
		*path = kstrndup(data, min_t(u32, data_len, PATH_MAX - 1),
				 GFP_KERNEL);

	return *path ? 0 : -ENOMEM;
}

/**
 * ath12k_vendor_reply_u32_extn() - Send a 32-bit vendor command reply
 * @wiphy: Wiphy used to allocate the reply skb
 * @value: Reply value to return through generic params
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int ath12k_vendor_reply_u32_extn(struct wiphy *wiphy, u32 value)
{
	struct sk_buff *reply;
	size_t reply_len = nla_total_size(sizeof(u32)) * 3;

	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, reply_len);
	if (!reply)
		return -ENOMEM;

	if (nla_put_u32(reply, QCA_WLAN_VENDOR_ATTR_PARAM_DATA, value) ||
	    nla_put_u32(reply, QCA_WLAN_VENDOR_ATTR_PARAM_LENGTH,
			sizeof(value)) ||
	    nla_put_u32(reply, QCA_WLAN_VENDOR_ATTR_PARAM_FLAGS, 0)) {
		kfree_skb(reply);
		return -EINVAL;
	}

	return cfg80211_vendor_cmd_reply(reply);
}

/**
 * ath12k_vendor_dump_nol_to_file_extn() - Dump the current NOL snapshot to
 * disk
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to serialize
 * @path: Destination file path
 * @dump_len: Returned serialized dump length in bytes
 *
 * Return: 0 if the NOL snapshot was persisted, else a negative error code.
 */
static int ath12k_vendor_dump_nol_to_file_extn(struct wiphy *wiphy,
					       u8 radio_idx, const char *path,
					       u32 *dump_len)
{
	char *out = NULL;
	size_t out_len = 0;
	int ret;

	ret = ath12k_vendor_build_nol_chan_list_extn(wiphy, radio_idx, &out,
						     &out_len);
	if (!ret)
		ret = ath12k_vendor_write_buf_to_file_extn(path, radio_idx, out,
							   out_len);
	if (!ret)
		ath12k_vendor_log_nol_chan_list_extn(wiphy, radio_idx, "getnol");
	if (!ret && dump_len)
		*dump_len = (u32)min_t(size_t, out_len, (size_t)~0U);

	kfree(out);
	return ret;
}

/**
 * ath12k_vendor_set_nol_chan_list_extn() - Restore saved NOL state for a
 * radio
 * @wiphy: Wiphy that owns the DFS channel tables
 * @radio_idx: Radio index to restore
 * @path: Optional dump file path
 * @num_updated: Returned number of channels restored
 *
 * Return: 0 if the dump was applied successfully, else a negative error code.
 */
int ath12k_vendor_set_nol_chan_list_extn(struct wiphy *wiphy, u8 radio_idx,
					 const char *path, u32 *num_updated)
{
	char *resolved_path = NULL;
	int updated;
	int ret;

	if (!wiphy)
		return -EINVAL;

	lockdep_assert_wiphy(wiphy);

	ret = ath12k_vendor_validate_radio_idx(wiphy, radio_idx);
	if (ret)
		return ret;

	ret = ath12k_vendor_resolve_nol_path_extn(path, radio_idx,
						 &resolved_path);
	if (ret)
		return ret;

	updated = ath12k_vendor_restore_nol_file_extn(wiphy, radio_idx,
						      resolved_path);
	if (updated < 0) {
		kfree(resolved_path);
		return updated;
	}

	if (num_updated)
		*num_updated = updated;

	wiphy_info(wiphy, "setnol: radio_idx=%u applied %d entries from %s\n",
		   radio_idx, updated, resolved_path);
	ath12k_vendor_log_nol_chan_list_extn(wiphy, radio_idx, "setnol");
	kfree(resolved_path);

	return 0;
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

        ret = ath12k_vendor_set_radio_params(ar, param, value, params, &reload);

        if (!ret && reload)
                ath12k_vendor_event_iface_reload(wiphy, params->radio_idx);

        return ret;
}

int ath12k_vendor_set_wiphy_sr_params_extn(struct wiphy *wiphy,
					   struct ath12k_wifi_generic_params *params)
{
	struct ieee80211_hw *hw = NULL;
	struct ath12k_hw *ah = NULL;
	struct ath12k *ar = NULL;
	u32 param = params->value;
	void *data = params->data;
	u32 data_len = params->data_len;
	u32 value = params->length;
	u32 flags = params->flags;
	bool reload = false;
	int ret;

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

	ret = ath12k_vendor_set_sr_radio_params(ar, param, data, data_len,
						value, flags, &reload);

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
       case PARAM_RADIO_TXCHAINSOFT:
                *value = ar->cfg_tx_chainmask;
                ret = 0;
                break;
        case ACFG_PARAM_RADIO_RXCHAINMASK:
                *value = ar->cfg_rx_chainmask;
                ret = 0;
                break;
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
	case QCA_WLAN_VENDOR_VDEV_PARAM_GET_MINTXPOWER:
		ret = ath12k_vendor_get_mintxpower(ar, value);
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_GET_MAXTXPOWER:
		ret = ath12k_vendor_get_maxtxpower(ar, value);
                break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_REGTXPOWER:
		ret = ath12k_vendor_get_regtxpower(arvif, value);
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
	case QCA_WLAN_VENDOR_VDEV_PARAM_PROTECTION_MODE:
		*value = arvif->vap_cfg.protection_mode;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_MCAST_RATE:
		ret = ath12k_get_vdev_mcast_rate(arvif, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_BCAST_RATE:
		ret = ath12k_get_vdev_bcast_rate(arvif, value);
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_GET_TXPOWER_RESOLUTION:
		ret = ath12k_vendor_get_txpower_resolution(arvif, value);
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
	case QCA_WLAN_VENDOR_VDEV_PARAM_BW_NSS_RATE:
		*value = arvif->vap_cfg.bw_nss_rate;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_LDPC:
		*value = arvif->vap_cfg.eht_ul_ldpc;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_LTF:
		*value = arvif->vap_cfg.eht_ul_ltf;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_NSS:
		*value = arvif->vap_cfg.eht_ul_nss;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_PPDU_BW:
		*value = arvif->vap_cfg.eht_ul_ppdu_bw;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_SHORTGI:
		*value = arvif->vap_cfg.eht_ul_shortgi;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_EHT_UL_STBC:
		*value = arvif->vap_cfg.eht_ul_stbc;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_ENABLERTSCTS:
		*value = arvif->vap_cfg.enablertscts;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_DCM:
		*value = arvif->vap_cfg.he_dcm;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_EXTRANGE:
		*value = arvif->vap_cfg.he_extrange;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_UL_LDPC:
		*value = arvif->vap_cfg.he_ul_ldpc;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_UL_NSS:
		*value = arvif->vap_cfg.he_ul_nss;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_UL_PPDU_BW:
		*value = arvif->vap_cfg.he_ul_ppdu_bw;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_UL_SHORTGI:
		*value = arvif->vap_cfg.he_ul_shortgi;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_HE_UL_STBC:
		*value = arvif->vap_cfg.he_ul_stbc;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_RTSCTS_RATE:
		*value = arvif->vap_cfg.rtscts_rate;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_VHT_SGIMASK:
		*value = arvif->vap_cfg.vht_sgimask;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_VHT80_RATE:
		*value = arvif->vap_cfg.vht80_rate;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_VDEV_PARAM_DIS_LPI_ANT_OPTIMIZE:
		*value = arvif->vap_cfg.dis_lpi_ant_optimize;
		ret = 0;
		break;
       default:
                ath12k_dbg(NULL, ATH12K_DBG_CFG,
                           "Un-supported param: %d\n", param);
                break;
        }

        return ret;
}

static inline int get_obss_pd_enable_bit(uint32_t threshold,
					 enum srtype type,
					 uint8_t *value)
{
	switch (type) {
	case SR_TYPE_NON_SRG_OBSS_PD:
		*value = GET_SELF_OBSS_PD_ENABLE(NON_SRG, threshold);
	break;

	case SR_TYPE_SRG_OBSS_PD:
		*value = GET_SELF_OBSS_PD_ENABLE(SRG, threshold);
	break;

	default:
		ath12k_err(NULL, "Invalid Spatial Reuse type");
		return -1;
	}

	return 0;
}

static inline int get_obss_pd_threshold(uint32_t threshold,
					enum srtype type,
					uint8_t *value)
{
	switch (type) {
	case SR_TYPE_NON_SRG_OBSS_PD:
		*value = GET_SELF_OBSS_PD_THRESH(NON_SRG, threshold);
	break;

	case SR_TYPE_SRG_OBSS_PD:
		*value = GET_SELF_OBSS_PD_THRESH(SRG, threshold);
	break;

	default:
		ath12k_err(NULL, "Invalid Spatial Reuse type");
		return -1;
	}

	return 0;
}

static inline int get_sr_per_ac(uint32_t enable_bitmap,
				enum srtype type,
				uint8_t *value)
{
	switch (type) {
	case SR_TYPE_OBSS_PD:
		*value = GET_SELF_SR_PER_AC(OBSS_PD, enable_bitmap);
	break;

	case SR_TYPE_PSR:
		*value = GET_SELF_SR_PER_AC(PSR, enable_bitmap);
	break;

	default:
		ath12k_err(NULL, "Invalid Spatial Reuse type");
	return -1;
	}

	return 0;
}

static int ath12k_vendor_get_sr_radio_params(struct ath12k *ar,
					     u32 param,
					     char value[], size_t length)
{
	uint8_t srg_val = 0, non_srg_val = 0;
	uint8_t srg_enable = 0, non_srg_enable = 0;
	uint8_t enable_per_ac_obss_pd = 0, enable_per_ac_psr = 0;

	switch (param) {
	case SR_SELF_OBSS_PD_TX_ENABLE:
		if (get_obss_pd_enable_bit(ar->sr_cfg.obss_pd_thresh,
					   SR_TYPE_NON_SRG_OBSS_PD,
					   &non_srg_enable))
			return -EINVAL;

		if (get_obss_pd_enable_bit(ar->sr_cfg.obss_pd_thresh,
					   SR_TYPE_SRG_OBSS_PD,
					   &srg_enable))
			return -EINVAL;

		snprintf(value, length, " non-srg: %d, srg: %d",
			 non_srg_enable, srg_enable);
		break;

	case SR_SELF_OBSS_PD_THRESHOLD_DB:
		if (get_obss_pd_threshold(ar->sr_cfg.obss_pd_thresh,
					  SR_TYPE_NON_SRG_OBSS_PD,
					  &non_srg_val))
			return -EINVAL;

		if (get_obss_pd_threshold(ar->sr_cfg.obss_pd_thresh,
					  SR_TYPE_SRG_OBSS_PD,
					  &srg_val))
			return -EINVAL;

		snprintf(value, length, " non-srg: %d, srg: %d",
			 (int8_t)non_srg_val, (int8_t)srg_val);
		break;

	case SR_SELF_OBSS_PD_THRESHOLD_DBM:
		if (get_obss_pd_threshold(ar->sr_cfg.obss_pd_thresh,
					  SR_TYPE_NON_SRG_OBSS_PD,
					  &non_srg_val))
			return -EINVAL;

		if (get_obss_pd_threshold(ar->sr_cfg.obss_pd_thresh,
					  SR_TYPE_SRG_OBSS_PD, &srg_val))
			return -EINVAL;

		snprintf(value, length, " non-srg: %d, srg: %d",
			 (int8_t)non_srg_val, (int8_t)srg_val);
		break;

	case SR_SELF_ENABLE_PER_AC:
		if (get_sr_per_ac(ar->sr_cfg.enable_per_ac, SR_TYPE_OBSS_PD,
				  &enable_per_ac_obss_pd))
			return -EINVAL;

		if (get_sr_per_ac(ar->sr_cfg.enable_per_ac, SR_TYPE_PSR,
				  &enable_per_ac_psr))
			return -EINVAL;

		snprintf(value, length, " obss_pd: 0x%X psr: 0x%X",
			 enable_per_ac_obss_pd, enable_per_ac_psr);
		break;

	case SR_SELF_HESIGA_SR15_ENABLE:
		snprintf(value, length, "%d", ar->sr_cfg.hesiga_sr15_enable);
		break;

	case SR_SELF_PSR_TX_ENABLE:
		snprintf(value, length, "%d", ar->sr_cfg.psr_tx_enable);
		break;

	case SR_SELF_SAFETY_MARGIN_PSR:
		snprintf(value, length, "%d", ar->sr_cfg.safety_margin_psr);
		break;

	case SR_SELF_ENABLE_TRUNCATION:
		snprintf(value, length, "%d", ar->sr_cfg.enable_sr_truncate);
		break;

	case SR_SELF_OBSS_PPDU_MIN_DUR:
		snprintf(value, length, "%u", ar->sr_cfg.min_obss_ppdu_dur_sr);
		break;

	case SR_SELF_OBSS_PD_CTRL_FRAME:
		if (get_obss_pd_enable_bit(ar->sr_cfg.obss_pd_thresh_ctrl_frame,
					   SR_TYPE_NON_SRG_OBSS_PD, &non_srg_enable))
			return -EINVAL;

		if (get_obss_pd_threshold(ar->sr_cfg.obss_pd_thresh_ctrl_frame,
					  SR_TYPE_NON_SRG_OBSS_PD, &non_srg_val))
			return -EINVAL;

		if (get_obss_pd_enable_bit(ar->sr_cfg.obss_pd_thresh_ctrl_frame,
					   SR_TYPE_SRG_OBSS_PD, &srg_enable))
			return -EINVAL;

		if (get_obss_pd_threshold(ar->sr_cfg.obss_pd_thresh_ctrl_frame,
					  SR_TYPE_SRG_OBSS_PD, &srg_val))
			return -EINVAL;

		snprintf(value, length,
			 " non-srg:{enable:%d threshold:%d},"
			 " srg:{enable:%d threshold:%d}",
			 non_srg_enable, (int8_t)non_srg_val,
			 srg_enable, (int8_t)srg_val);
		break;
	case SR_SELF_SRG_BSS_COLOR_BITMAP:
		snprintf(value, length, "0x%08X %08X",
			 ar->sr_cfg.srg_bss_color_bitmap[1],
			 ar->sr_cfg.srg_bss_color_bitmap[0]);
		break;
	 case SR_SELF_SRG_PARTIAL_BSSID_BITMAP:
		snprintf(value, length, "0x%08X %08X",
			 ar->sr_cfg.srg_partial_bssid_bitmap[1],
			 ar->sr_cfg.srg_partial_bssid_bitmap[0]);
		break;
	case SR_SELF_SRG_OBSS_COLOR_ENABLE_BITMAP:
		snprintf(value, length, "0x%08X %08X",
			 ar->sr_cfg.srg_obss_color_enable_bitmap[1],
			 ar->sr_cfg.srg_obss_color_enable_bitmap[0]);
		break;
	case SR_SELF_SRG_OBSS_BSSID_ENABLE_BITMAP:
		snprintf(value, length, "0x%08X %08X",
			 ar->sr_cfg.srg_obss_bssid_enable_bitmap[1],
			 ar->sr_cfg.srg_obss_bssid_enable_bitmap[0]);
		break;
	case SR_SELF_NON_SRG_OBSS_COLOR_ENABLE_BITMAP:
		snprintf(value, length, "0x%08X %08X",
			 ar->sr_cfg.non_srg_obss_color_enable_bitmap[1],
			 ar->sr_cfg.non_srg_obss_color_enable_bitmap[0]);
		break;
	case SR_SELF_NON_SRG_OBSS_BSSID_ENABLE_BITMAP:
		snprintf(value, length, "0x%08X %08X",
			 ar->sr_cfg.non_srg_obss_bssid_enable_bitmap[1],
			 ar->sr_cfg.non_srg_obss_bssid_enable_bitmap[0]);
		break;
	default:
		ath12k_err(NULL, "Unhandled SR config command");
		return -EINVAL;
	}
	return 0;
}

/* Get radio level parameters */
static int ath12k_vendor_get_radio_params(struct ath12k *ar,
					  u32 param,
					  struct ath12k_wifi_generic_params *params,
					  u32 *value)
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
	case PARAM_RADIO_TXCHAINSOFT:
		*value = ar->cfg_tx_chainmask;
		ret = 0;
		break;
	case ACFG_PARAM_RADIO_RXCHAINMASK:
		*value = ar->cfg_rx_chainmask;
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
	case QCA_WLAN_VENDOR_RADIO_PARAM_RADAR_DETECT_COUNT:
		*value = ar->radar_detect_count;
		ret = 0;
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_CTLPWRSCALE:
		ret = ath12k_vendor_get_ctlpwrscale(ar, value);
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_EN_CHAN_144:
		ret = ath12k_vendor_get_en_chan144(ar, value);
		break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_PDEV_TO_REO_DEST:
                *value = ar->radio_cfg.pdev_to_reo_dest;
                ret = 0;
                break;
	case QCA_WLAN_VENDOR_RADIO_PARAM_NOL_CHAN_LIST:
	{
		struct wiphy *wiphy;
		char *path;
		u32 dump_len = 0;

		if (!ar->ah || !ar->ah->hw || !ar->ah->hw->wiphy)
			return -EINVAL;

		wiphy = ar->ah->hw->wiphy;
		ret = ath12k_vendor_resolve_nol_path_from_data_extn(params->data,
								    params->data_len,
								    params->radio_idx,
								    &path);
		if (ret)
			return ret;

		ret = ath12k_vendor_dump_nol_to_file_extn(wiphy,
							  params->radio_idx,
							  path, &dump_len);
		kfree(path);
		if (ret)
			return ret;

		*value = dump_len;
		ret = 0;
		break;
	}
	case QCA_WLAN_VENDOR_RADIO_PARAM_WSI_BYPASS:
		/* Report 1 if device is currently bypassed, else 0 */
		*value = ar->ab->is_bypassed ? 1 : 0;
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
        ret = ath12k_vendor_get_radio_params(ar, param, params, value);

        return ret;
}

int ath12k_vendor_get_wiphy_sr_params_extn(struct wiphy *wiphy,
					   struct ath12k_wifi_generic_params *params,
					   char value[], size_t length)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath12k_hw *ah = hw->priv;
	struct ath12k *ar = NULL;
	uint8_t param = params->value;
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
	ret = ath12k_vendor_get_sr_radio_params(ar, param, value, length);

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

int ath12k_vendor_get_wifi_config_handler_extn(struct wiphy *wiphy,
					       struct nlattr **tb,
					       u64 *value)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath12k_hw *ah = hw->priv;
	u32 generic_cmd;

	if (!tb[QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_COMMAND])
		return -EOPNOTSUPP;

	generic_cmd = nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_CONFIG_GENERIC_COMMAND]);
	switch (generic_cmd) {
	case QCA_NL80211_VENDOR_SUBCMD_HE_MCS_12_13_SUPP:
		return ath12k_vendor_get_he_mcs_12_13_cap(ah, tb, value);
	default:
		ath12k_dbg(NULL, ATH12K_DBG_CFG,
			   "GET_WIFI_CONFIG: unsupported generic_cmd %u\n",
			   generic_cmd);
		return -EOPNOTSUPP;
	}
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
 * Only the 5th octet may vary when ATH12K_MBSSID_MAX_INTERFACES == 16.
 * Preserve the upper nibble and modify the lower nibble for each vif.
 */
void ath12k_compute_mbssid_mac(u8 mac[ETH_ALEN], u8 vif_id)
{
	if (vif_id >= ATH12K_MBSSID_MAX_INTERFACES) {
		ath12k_err(NULL, "Invalid BSS ID for multiple BSSID group\n");
		return;
	}

	/* Ensure locally administered and unicast */
	mac[0] &= ~0x01;
	/* Currently vendor MAC address with MBSSID is supported only for 6 GHz
	 * hence the 0th byte has lower octet set to 0x02.*/
	mac[0] |= 0x02;

	mac[5] = (mac[5] & 0xF0) | (((mac[5] & 0x0F) + vif_id) & 0x0F);
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
	u32 flags = 0;
	enum nl80211_iftype iftype = NL80211_IFTYPE_UNSPECIFIED;
	u8 pdev_bitmap;
	bool byte0 = true, mbssid_enabled = false;
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
	if (tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MBSSID_ENABLED]) {
		mbssid_enabled = nla_get_flag(tb[QCA_WLAN_VENDOR_ATTR_MAC_CONFIG_MBSSID_ENABLED]);
		if (vif_id >= ATH12K_MBSSID_MAX_INTERFACES) {
			ath12k_err(NULL, "Invalid BSS ID for multiple BSSID group\n");
			return -EINVAL;
		}
	}

	/* Get hw/ah to know which byte index to use for VIF ID */
	hw = wiphy_to_ieee80211_hw(wiphy);
	ah = hw ? hw->priv : NULL;
	ar = ah ? &ah->radio[radio_idx] : NULL;

	if (!ar)
		return -EINVAL;

	base_addr = wiphy->addresses[radio_idx].addr;
	if (iftype == NL80211_IFTYPE_AP && mbssid_enabled) {
		if (!ar->supports_6ghz) {
			ath12k_err(NULL,
				   "Static MAC address with MBSSID enabled is allowed only in 6 GHz\n");
			return -EINVAL;
		}
		ether_addr_copy(mac_addr, base_addr);
		ath12k_compute_mbssid_mac(mac_addr, vif_id);

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
	struct ath12k_mac_get_any_chanctx_conf_arg arg;
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

	if (intf_type_bitmap == WMI_DCS_AWGN_INTF) {
		struct cfg80211_chan_def *chandef;

		arg.ar = ar;
		arg.chanctx_conf = NULL;
		ieee80211_iter_chan_contexts_atomic(ath12k_ar_to_hw(ar),
						    ath12k_mac_get_any_chanctx_conf_iter,
						    &arg);
		if (!arg.chanctx_conf) {
			ath12k_err(NULL,
				   "failed to get chanctx for dcs sim awgn\n");
			goto exit;
		}

		chandef = &arg.chanctx_conf->def;
		if (!chandef->chan ||
		    chandef->chan->band != NL80211_BAND_6GHZ) {
			ath12k_err(ar->ab,
				   "DCS SIM AWGN is supported only on 6 GHz (freq=%d band=%d)\n",
				   chandef->chan ? chandef->chan->center_freq : 0,
				   chandef->chan ? (int) chandef->chan->band : -1);
			goto exit;
		}

		spin_lock_bh(&ar->data_lock);
		ar->awgn_chandef = *chandef;
		ar->awgn_intf_handling_in_prog = true;
		ar->chan_bw_interference_bitmap = intf_bitmap;
		spin_unlock_bh(&ar->data_lock);
		ath12k_info(ar->ab,
			    "DCS SIM AWGN: pdev=%d freq=%d width=%d cf1=%d cf2=%d bitmap=0x%x\n",
			    ar->pdev->pdev_id,
			    chandef->chan ? chandef->chan->center_freq : 0,
			    chandef->width,
			    chandef->center_freq1,
			    chandef->center_freq2,
			    intf_bitmap);
		cfg80211_awgn_event(wihpy, chandef, GFP_ATOMIC, intf_bitmap);
		rcu_read_unlock();
		return 0;
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

/**
 * ath12k_vendor_tdma_schedule_config() - Configure TDMA schedule parameters
 * @wiphy: wiphy device pointer
 * @wdev: wireless device pointer
 * @data: vendor command data
 * @data_len: vendor command data length
 *
 * This vendor command configures TDMA (Time Division Multiple Access)
 * scheduling parameters for a given radio. It parses the schedule type,
 * slot timing, BSSID, TSF start time, and optional EDCA parameters.
 *
 * Return: 0 on success, negative error code on failure
 */
int ath12k_vendor_tdma_schedule_config(struct wiphy *wiphy,
					      struct wireless_dev *wdev,
					      const void *data, int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath12k_hw *ah = hw->priv;
	struct ath12k *ar;
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_MAX + 1];
	struct ath12k_tdma_sched_info sched = {0};
	u32 radio_index = 0;
	int ret;

	ret = nla_parse(tb, QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_MAX, data, data_len,
			ath12k_vendor_tdma_schedule_policy, NULL);
	if (ret) {
		ath12k_err(NULL, "Failed to parse TDMA schedule attributes: %d\n", ret);
		return ret;
	}

	if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_RADIO_INDEX])
		radio_index = nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_RADIO_INDEX]);

	if (radio_index >= ah->num_radio) {
		ath12k_err(NULL, "Invalid radio index %u for TDMA schedule config\n",
			   radio_index);
		return -EINVAL;
	}

	ar = &ah->radio[radio_index];
	if (!ar) {
		ath12k_err(NULL, "ar is NULL for radio index %u\n", radio_index);
		return -EINVAL;
	}

	sched.pdev_id = ar->pdev->pdev_id;

	if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_TYPE])
		sched.sched_type =
			nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_TYPE]);

	if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_ID])
		sched.sched_id =
			nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_ID]);

	if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_BSSID] &&
	    nla_len(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_BSSID]) == ETH_ALEN)
		memcpy(sched.bssid,
		       nla_data(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_BSSID]),
		       ETH_ALEN);

	if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_START_TIME_TSF_LOW])
		sched.start_time_tsf_low =
			nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_START_TIME_TSF_LOW]);

	if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_START_TIME_TSF_HIGH])
		sched.start_time_tsf_high =
			nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_START_TIME_TSF_HIGH]);

	if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_NUM_BUSY_SLOTS])
		sched.num_busy_slots =
			nla_get_u16(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_NUM_BUSY_SLOTS]);

	if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_BUSY_SLOTS_DUR])
		sched.busy_slot_dur_ms =
			nla_get_u16(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_BUSY_SLOTS_DUR]);

	if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_BUSY_SLOTS_INTVL])
		sched.busy_slot_intvl_ms =
			nla_get_u16(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_BUSY_SLOTS_INTVL]);

	if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_EDCA_PARAMS_VALID])
		sched.edca_params_valid =
			nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_EDCA_PARAMS_VALID]);

	if (sched.edca_params_valid) {
		if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_AIFSN] &&
		    nla_len(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_AIFSN]) ==
		    sizeof(u16) * WLAN_MAX_AC)
			memcpy(sched.aifsn,
			       nla_data(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_AIFSN]),
			       sizeof(u16) * WLAN_MAX_AC);

		if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_CWMIN] &&
		    nla_len(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_CWMIN]) ==
		    sizeof(u16) * WLAN_MAX_AC)
			memcpy(sched.cwmin,
			       nla_data(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_CWMIN]),
			       sizeof(u16) * WLAN_MAX_AC);

		if (tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_CWMAX] &&
		    nla_len(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_CWMAX]) ==
		    sizeof(u16) * WLAN_MAX_AC)
			memcpy(sched.cwmax,
			       nla_data(tb[QCA_WLAN_VENDOR_ATTR_TDMA_SCHEDULE_CWMAX]),
			       sizeof(u16) * WLAN_MAX_AC);
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
		   "TDMA sched : pdev %u type %u id %u bssid %pM tsf %08x:%08x slots %u dur %u intvl %u edca %d\n",
		   sched.pdev_id, sched.sched_type, sched.sched_id, sched.bssid,
		   sched.start_time_tsf_high, sched.start_time_tsf_low,
		   sched.num_busy_slots, sched.busy_slot_dur_ms,
		   sched.busy_slot_intvl_ms, sched.edca_params_valid);

	return ath12k_wmi_send_tdma_schedule_request(ar, &sched);;
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
