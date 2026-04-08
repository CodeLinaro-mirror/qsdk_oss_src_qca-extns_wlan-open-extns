/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/skbuff.h>
#include <linux/ctype.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/completion.h>
#include <linux/if_ether.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/uuid.h>
#include <linux/time.h>
#include <linux/of.h>
#include "../core.h"
#include "../ce.h"
#include "../hw.h"
#include "../debug.h"
#include "../peer.h"
#include "ath12k_cmn_extn.h"
#include "../net/mac80211/ieee80211_i.h"
#include "esp_extn.h"
#include "vendor_extn.h"
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include "../wmi.h"
#include "ini.h"
#include "rropinfo.h"
#include "qcn_extns/if_meta_hdr.h"

struct ath12k_vif_sysfs_entry {
	struct kobject kobj;
	struct ath12k *ar;
	u8 radio_idx;
	u8 vif_id;
};

static int ath12k_mlo_set_3_link_forced_primary_umac(struct ath12k_hw *ah,
					      struct ath12k_sta *ahsta,
					      unsigned long valid_links,
					      int *psoc_id);
static int ath12k_get_total_stations_ab(struct ath12k_base *ab);
static int ath12k_get_total_mlo_peers_ab(struct ath12k_base *ab);

void ath12k_mgmt_rx_event_extn(struct ath12k_base *ab, struct ieee80211_hdr *hdr,
		struct ath12k_wmi_mgmt_rx_arg *rx_ev)
{
	struct ath12k_link_sta *arsta;
	if (ieee80211_is_assoc_req(hdr->frame_control)) {
		spin_lock_bh(&ab->base_lock);
		arsta = ath12k_link_sta_find_by_addr(ab, hdr->addr2);
		if (!arsta || arsta->is_bridge_peer) {
			ath12k_warn(ab, "arsta not found %pM\n",
					hdr->addr2);
		} else {
			arsta->arsta_extn.rssi_assoc = rx_ev->rssi;
		}
		spin_unlock_bh(&ab->base_lock);
	}
}

int ath12k_reg_chan_list_cc_ext_parse_extn(struct ath12k_base *ab,
					   const void *next_tlv,
					   struct ath12k_reg_info_extn *reg_info_extn)
{
	const struct wmi_tlv *prio_tlv;
	const struct ath12k_wmi_reg_chan_priority *prio;
	u16 wrapper_len;
	u16 prio_tag;

	if (!reg_info_extn)
		return -EINVAL;

	reg_info_extn->reg_6ghz_thresh_priority_freq = 0;

	if (!next_tlv)
		return -EINVAL;

	prio_tlv = (const struct wmi_tlv *)next_tlv;
	if (le32_get_bits(prio_tlv->header, WMI_TLV_TAG) != WMI_TAG_ARRAY_STRUCT ||
	    !le32_get_bits(prio_tlv->header, WMI_TLV_LEN))
		return -EPROTO;

	wrapper_len = le32_get_bits(prio_tlv->header, WMI_TLV_LEN);
	if (wrapper_len < sizeof(*prio))
		return -EINVAL;

	prio = (const struct ath12k_wmi_reg_chan_priority *)(prio_tlv + 1);
	prio_tag = le32_get_bits(prio->tlv_header, WMI_TLV_TAG);
	if (prio_tag != WMI_TAG_REG_CHAN_PRIORITY)
		return -EINVAL;

	reg_info_extn->reg_6ghz_thresh_priority_freq =
		le32_to_cpu(prio->freq_info) & 0xFFFF;

	ath12k_dbg(ab, ATH12K_DBG_REG,
		   "6 GHz VLP priority threshold freq %u",
		   reg_info_extn->reg_6ghz_thresh_priority_freq);

	return 0;
}

void ath12k_reg_handle_chan_list_extn(struct ath12k_base *ab,
				      int pdev_idx,
				      const struct ath12k_reg_info_extn *reg_info_extn)
{
	if (!ab || !reg_info_extn)
		return;

	if (pdev_idx < 0 || pdev_idx >= MAX_RADIOS)
		return;

	ab->ath12k_base_extn.reg_6ghz_thresh_priority_freq[pdev_idx] =
		reg_info_extn->reg_6ghz_thresh_priority_freq;

	ath12k_dbg(ab, ATH12K_DBG_REG,
		   "pdev %d 6 GHz priority threshold set to %u\n",
		   pdev_idx,
		   ab->ath12k_base_extn.reg_6ghz_thresh_priority_freq[pdev_idx]);
}

static void ath12k_mac_accumulate_avg_link_rssi(void *data,
		struct ieee80211_sta *sta)
{
	s32 *rssi_total_array = data;
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);

	rssi_total_array[ahsta->ahsta_extn.primary_soc_id] += ahsta->ahsta_extn.avg_link_rssi;
}

static int ath12k_get_total_mlo_peers_ab(struct ath12k_base *ab)
{
	int i;
	int total_ml_peers = 0;
	struct ath12k_pdev *pdev;
	struct ath12k *ar;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		if (pdev) {
			ar = pdev->ar;
			total_ml_peers += ar->num_ml_peers;
		}
	}
	return total_ml_peers;
}

static int ath12k_get_total_stations_ab(struct ath12k_base *ab)
{
	int i;
	int total_stations = 0;
	struct ath12k_pdev *pdev;
	struct ath12k *ar;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		if (pdev) {
			ar = pdev->ar;
			total_stations += ar->num_stations;
		}
	}

	if (total_stations) {
		/* Subtract 1 to exclude the station being added/evaluated from the count.
		 * This ensures we only count existing stations when making primary UMAC
		 * selection decisions for a new MLO association.
		 */
		return total_stations - 1;
	}

	return total_stations;
}

int ath12k_mlo_set_3_link_forced_primary_umac(struct ath12k_hw *ah,
				       struct ath12k_sta *ahsta,
				       unsigned long valid_links,
				       int *psoc_id)
{
	u32 target_type;
	struct ath12k *ar;
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(ahsta);
	bool found_qca5332 = false;
	bool found_qcn6432 = false;
	bool found_qcn9224 = false;
	bool found_qca5424 = false;
	u8 forced_psoc_id = 0;
	int num_links;
	int i = 0;

	num_links = hweight32(valid_links);
	ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
			 "mlo primary selection: forced check sta %pM valid_links 0x%lx links %d\n",
			 sta->addr, valid_links, num_links);

	if (num_links != 3) {
		ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2, "not a 3 link association\n");
		return -1;
	}

	/* loop through all ab in ah*/
	for (i = 0; i < ah->num_radio; i++) {
		ar = &ah->radio[i];
		if (!ar) {
			ath12k_err(NULL, "ar is NULL\n");
			return -1;
		}

		target_type = ar->ab->hw_rev;
		switch (target_type) {
		case ATH12K_HW_IPQ5332_HW10:
			ath12k_dbg_level(ar->ab, ATH12K_DBG_MAC, ATH12K_DBG_L2, "target: IPQ5332\n");
			found_qca5332 = true;
			break;

		case ATH12K_HW_QCN9274_HW10:
		case ATH12K_HW_QCN9274_HW20:
			found_qcn9224 = true;
			ath12k_dbg_level(ar->ab, ATH12K_DBG_MAC, ATH12K_DBG_L2, "target: QCN9274\n");
			break;

		case ATH12K_HW_QCN6432_HW10:
			found_qcn6432 = true;
			ath12k_dbg_level(ar->ab, ATH12K_DBG_MAC, ATH12K_DBG_L2, "target:QCN6432\n");
			forced_psoc_id = ath12k_get_ab_device_id(ar->ab);
			break;

		case ATH12K_HW_IPQ5424_HW10:
			found_qca5424 = true;
			ath12k_dbg_level(ar->ab, ATH12K_DBG_MAC, ATH12K_DBG_L2, "target:IPQ5424\n");
			forced_psoc_id = ath12k_get_ab_device_id(ar->ab);
			break;

		default:
		ath12k_err(NULL,
			   "Target not valid for pumac tgt_type %d",
			   target_type);
			return -EINVAL;
		}
	}

	if (found_qca5332 && found_qcn6432 && found_qcn9224) {
		*psoc_id = forced_psoc_id;
		ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
				 "mlo primary selection: forcing psoc %u as primary (3-link combo) for sta %pM\n",
				 forced_psoc_id, sta->addr);
		return 0;
	}

	/* In Marina-Waikiki platform, Marina will be chosen as
	 * the primary-umac
	 */
	if (found_qca5424) {
		*psoc_id = forced_psoc_id;
		ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
				 "mlo primary selection: forcing psoc %u as primary (Marina-Waikiki) for sta %pM\n",
				 forced_psoc_id, sta->addr);
		return 0;
	}

	ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
			 "mlo primary selection: no forced primary UMAC match for sta %pM\n",
			 sta->addr);

	return  -1;
}

int ath12k_derive_link_rssi(struct ath12k_hw *ah,
			       struct ath12k_link_vif *arvif,
			       struct ath12k_link_vif *assoc_arvif, u8 rssi)
{
	/* get assoc link channel, radio , ab etcc */
	struct cfg80211_chan_def *channel = &arvif->chanctx.def;
	struct cfg80211_chan_def *assoc_channel = &assoc_arvif->chanctx.def;
	u16 ch_freq, assoc_freq;
	u8 tx_pow, assoc_tx_pow;
	s8 diff_txpow;
	u8 log10_freq;
	u8 derived_rssi;
	s16 ten_derived_rssi;
	s8 ten_diff_pl = 0;

	if (channel)
		ch_freq = channel->chan->center_freq;
	else
		ch_freq = 1;

	if (assoc_channel)
		assoc_freq = assoc_channel->chan->center_freq;
	else
		assoc_freq = 1;

	/*
	 *  diff of path loss (of two links) = log10(freq1) - log10(freq2)
	 *                       (since distance is constant)
	 *  since log10 is not available, we cameup with approximate ranges
	 */
	log10_freq = (ch_freq * 10) / assoc_freq;
	if (log10_freq >= 20 && log10_freq < 30)
		ten_diff_pl = 4;  /* 0.4 *10 */
	else if (log10_freq >= 11 && log10_freq < 20)
		ten_diff_pl = 1;  /* 0.1 *10 */
	else if (log10_freq >= 8 && log10_freq < 11)
		ten_diff_pl = 0; /* 0 *10 */
	else if (log10_freq >= 4 && log10_freq < 8)
		ten_diff_pl = -1; /* -0.1 * 10 */
	else if (log10_freq >= 1 && log10_freq < 4)
		ten_diff_pl = -4;  /* -0.4 * 10 */

	assoc_tx_pow = assoc_arvif->txpower;
	tx_pow = arvif->txpower;
	diff_txpow = tx_pow - assoc_tx_pow;

	ten_derived_rssi = (diff_txpow * 10) - ten_diff_pl + (rssi * 10);
	derived_rssi = ten_derived_rssi / 10;

	ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
			 "mlo rssi derive: ch_freq %u assoc_freq %u tx_pow %u assoc_tx_pow %u ten_diff_pl %d diff_txpow %d input_rssi %u derived_rssi %u\n",
			 ch_freq, assoc_freq, tx_pow, assoc_tx_pow,
			 ten_diff_pl, diff_txpow, rssi, derived_rssi);

	return derived_rssi;
}

static void ath12k_peer_calculate_avg_rssi(struct ath12k_hw *ah,
					struct ath12k_vif *ahvif,
					struct ath12k_sta *ahsta,
					unsigned long valid_links)
{
	struct ath12k_link_vif *arvif = NULL;
	struct ath12k_link_vif *assoc_arvif = NULL;
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(ahsta);
	int total_rssi = 0;
	u8 num_psocs = 0;
	struct ath12k_link_sta *arsta;
	u8 link_id;
	int link_rssi;

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = ath12k_get_arvif_from_link_id(ahvif, link_id);
		if (!arvif || !arvif->ar)
			continue;
		if (ahsta->assoc_link_id == link_id) {
			assoc_arvif = arvif;
			break;
		}
	}

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = ath12k_get_arvif_from_link_id(ahvif, link_id);
		if (!arvif || !arvif->ar)
			continue;
		arsta = ahsta->link[link_id];
		if (!arsta)
			continue;
		num_psocs++;
		if (ahsta->assoc_link_id == link_id) {
			link_rssi = arsta->arsta_extn.rssi_assoc;
		} else {
			link_rssi = ath12k_derive_link_rssi(ah, arvif,
							    assoc_arvif,
							    arsta->arsta_extn.rssi_assoc);
		}

		total_rssi += link_rssi;
		ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
				 "mlo avg rssi: sta %pM link %u assoc %d rssi %d running_total %d\n",
				 sta->addr, link_id,
				 ahsta->assoc_link_id == link_id,
				 link_rssi, total_rssi);
	}

	if (!num_psocs)
		return;

	ahsta->ahsta_extn.avg_link_rssi = total_rssi / num_psocs;
	ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
			 "mlo avg rssi: sta %pM valid_links 0x%lx links %u avg %d assoc_link %u\n",
			 sta->addr, valid_links, num_psocs,
			 ahsta->ahsta_extn.avg_link_rssi, ahsta->assoc_link_id);
}

int get_linkid_from_psoc_id(struct ath12k_vif *ahvif,
			    unsigned long valid_links,
			    int soc_id,
			    u8 *pri_link_id)
{
	u8 link_id;
	struct ath12k_link_vif *arvif = NULL;
	bool is_link_found = false;

	/* Get link id from psoc_id */
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = ath12k_get_arvif_from_link_id(ahvif, link_id);
		if (!arvif || !arvif->ar)
			continue;

		if (ath12k_get_ab_device_id(arvif->ar->ab) == soc_id) {
			*pri_link_id = link_id;
			is_link_found = true;
			break;
		}
	}

	if (is_link_found)
		return 0;

	return -1;
}

#define ML_PRIMARY_LINK_CONGESTION 30
int ath12k_get_best_primary_umac_w_rssi(struct ath12k_hw *ah,
					  struct ath12k_vif *ahvif,
					  struct ath12k_sta *ahsta,
					  unsigned long valid_links,
					  u8 *primary_link_id)
{
	struct ath12k_link_vif *arvif = NULL;
	struct ath12k_link_vif *assoc_arvif = NULL;
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(ahsta);
	u16 total_ml_sta_count = 0;
	u32 total_cap, cap;
	u16 bw;
	u8 link_id;
	u8 num_psocs = 0;
	u8 psoc_id = 0;
	u8 psoc_w_nosta = 0;
	bool plink_preferable[ATH12K_MAX_SOCS] = {0};
	bool ml_no_sta[ATH12K_MAX_SOCS] = {0};
	u16 ml_sta_count[ATH12K_MAX_SOCS] = {0};
	u16 ml_ch_width[ATH12K_MAX_SOCS];
	bool group_full[ATH12K_MAX_SOCS] = {0};
	u16 group_size[ATH12K_MAX_SOCS] = {0};
	u16 grp_size = 0;
	u16 group_full_count = 0;
	u8 cong = ML_PRIMARY_LINK_CONGESTION;
	u16 chan_width = 0;
	s32 sum_link_rssi[ATH12K_MAX_SOCS] = {0};
	s32 avg_rssi[ATH12K_MAX_SOCS] = {0};
	s32 diff_rssi[ATH12K_MAX_SOCS] = {0};
	s32 diff_low;
	int primary_psoc = -1, i;
	int hi_bw = 0;
	int sec_hi_bw = 0;
	int temp_bw = 0;
	int psoc_hi_idx = -1;
	int psoc_second_hi_idx = -1;
	int ret = -1;

	ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
			 "mlo primary selection: sta %pM avg_link_rssi %d valid_links 0x%lx\n",
			 sta->addr, ahsta->ahsta_extn.avg_link_rssi, valid_links);

	if (ath12k_mlo_set_3_link_forced_primary_umac(ah, ahsta, valid_links,
				&primary_psoc) == 0) {
		goto exit_pri_link_selection;
	}

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = ath12k_get_arvif_from_link_id(ahvif, link_id);
		if (!arvif || !arvif->ar)
			continue;
		if (ahsta->assoc_link_id == link_id) {
			assoc_arvif = arvif;
			break;
		}
	}

	if (!assoc_arvif)
		goto exit_pri_link_selection;

	/* Derive other Link RSSI and have average avg_link_rssi for ahsta */
	ath12k_peer_calculate_avg_rssi(ah, ahvif, ahsta, valid_links);

	/* calculate avg_link_rssi per ab(primary link soc), for all MLO STAs in ah. */
	ieee80211_iterate_stations_atomic(ah->hw,
					  ath12k_mac_accumulate_avg_link_rssi,
					  &sum_link_rssi);


	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = ath12k_get_arvif_from_link_id(ahvif, link_id);
		if (!arvif || !arvif->ar)
			continue;

		if (!arvif->ar->ab->hw_params->is_plink_preferable) {
			ath12k_err(NULL, "Skip UMAC selection ...\n");
			psoc_id = ath12k_get_ab_device_id(arvif->ar->ab);
			plink_preferable[psoc_id] = false;
			continue;
		}

		psoc_id = ath12k_get_ab_device_id(arvif->ar->ab);
		plink_preferable[psoc_id] = true;
		chan_width = arvif->chanctx.def.width;
		ml_ch_width[psoc_id] = ath12k_mac_get_chan_width(chan_width);
		if (!ath12k_get_total_stations_ab(arvif->ar->ab)) {
			/* If this PSOC has no stations */
			ml_no_sta[psoc_id] = true;
			psoc_w_nosta++;
		}

		ml_sta_count[psoc_id] =
			ath12k_get_total_mlo_peers_ab(arvif->ar->ab);
		total_ml_sta_count += ml_sta_count[psoc_id];
		num_psocs++;
		if (ml_sta_count[psoc_id])
			avg_rssi[psoc_id] = sum_link_rssi[psoc_id] /
					    ml_sta_count[psoc_id];

		if (!avg_rssi[psoc_id]) {
			diff_rssi[psoc_id] = psoc_id * 20;
			continue;
		}

		diff_rssi[psoc_id] = (ahsta->ahsta_extn.avg_link_rssi >= avg_rssi[psoc_id]) ?
			(ahsta->ahsta_extn.avg_link_rssi - avg_rssi[psoc_id]) :
			(avg_rssi[psoc_id] - ahsta->ahsta_extn.avg_link_rssi);
	}

	ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
			 "%s: psoc_w_nosta: %d valid_links 0x%lx\n",
			 __func__, psoc_w_nosta, valid_links);

	for (i = 0; i < ATH12K_MAX_SOCS; i++) {

		ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
				 "mlo primary selection: psoc %d prefer %d width %u mlo_sta %u avg_rssi %d diff_rssi %d no_sta %d\n",
				 i, plink_preferable[i], ml_ch_width[i],
				 ml_sta_count[i], avg_rssi[i], diff_rssi[i],
				 ml_no_sta[i]);
	}

	primary_psoc = -1;
	if (psoc_w_nosta == 1) {
		for (i = 0; i < ATH12K_MAX_SOCS; i++) {
			if (ml_no_sta[i]) {
				primary_psoc = i;
				break;
			}
		}
		ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
				 "mlo primary selection: choosing empty psoc %d as primary for sta %pM\n",
				 primary_psoc, sta->addr);
	} else if (psoc_w_nosta > 1) {
		/* If more the 2 socs have no sta then choose 2nd highest BW link */
		ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
				 "mlo primary selection: choosing between psocs %d as primary for sta %pM\n",
				 psoc_w_nosta, sta->addr);
		hi_bw = 0;
		sec_hi_bw = 0;
		temp_bw = 0;
		psoc_hi_idx = -1;
		psoc_second_hi_idx = -1;
		for (i = 0; i < ATH12K_MAX_SOCS; i++) {
			if (!plink_preferable[i])
				continue;

			temp_bw = ml_ch_width[i];

			if (temp_bw > hi_bw) {
				sec_hi_bw = hi_bw;
				psoc_second_hi_idx = psoc_hi_idx;
				hi_bw = temp_bw;
				psoc_hi_idx = i;
			} else if (temp_bw < hi_bw && temp_bw > sec_hi_bw) {
				sec_hi_bw = temp_bw;
				psoc_second_hi_idx = i;
			}
		}
		primary_psoc = psoc_second_hi_idx;
		ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
				 "mlo primary selection: multiple empty psocs hi_bw %d(psoc %d) second_bw %d(psoc %d) primary %d for sta %pM\n",
				 hi_bw, psoc_hi_idx, sec_hi_bw, psoc_second_hi_idx,
				 primary_psoc, sta->addr);
	} else  {
		/* no empty psoc */
		/* Capacity based grouping algorithm */
		total_cap = 0;
		for (i = 0; i < ATH12K_MAX_SOCS; i++) {
			bw = ml_ch_width[i];
			total_cap += bw * (100 - cong);
		}

		ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
				"mlo primary selection: no empty socs  capicity based algo as primary for sta %pM\n",
				sta->addr);

		group_full_count = 0;
		for (i = 0; i < ATH12K_MAX_SOCS; i++) {
			if (!plink_preferable[i])
				continue;

			bw = ml_ch_width[i];
			cap = bw * (100 - cong);
			total_ml_sta_count++;
			grp_size = total_ml_sta_count * ((cap * 100) / total_cap);
			group_size[i] = grp_size / 100;
			if (grp_size % 100)
				group_size[i]++;

			if (group_size[i] == 0)
				group_size[i] = 1;
			/* Mark link full if its target allocation is already reached */
			if (group_size[i] <= ml_sta_count[i]) {
				group_full[i] = true;
				group_full_count++;
			}
		}

		if ((num_psocs - group_full_count) == 1) {
			for (i = 0; i < ATH12K_MAX_SOCS; i++) {
				if (!plink_preferable[i])
					continue;
				if (group_full[i])
					continue;
				primary_psoc = i;
				break;
			}
		} else {
			diff_low = 0;

			ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
				"mlo primary selection: no empty socs RSSI based algo as primary for sta %pM\n",
				sta->addr);
			/* find min diff of RSSI, based on it, allocate primary umac */
			for (i = 0; i < ATH12K_MAX_SOCS; i++) {
				if (!plink_preferable[i])
					continue;

				if (!diff_low || diff_low > diff_rssi[i]) {
					diff_low = diff_rssi[i];
					primary_psoc = i;
				}
			}
		}
	}

exit_pri_link_selection:

	if (primary_psoc >= 0) {
		if (!get_linkid_from_psoc_id(ahvif, valid_links, primary_psoc, primary_link_id)) {
			ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
					 "mlo primary selection: computed primary psoc %d linkid %u for sta %pM\n",
					 primary_psoc, *primary_link_id, sta->addr);
			ret = 0;
		} else {
			ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
					 "mlo primary selection: failed to map primary psoc %d to link for sta %pM\n",
					 primary_psoc, sta->addr);
		}
	} else if (primary_psoc < 0) {
		ath12k_dbg_level(NULL, ATH12K_DBG_MAC, ATH12K_DBG_L2,
				 "mlo primary selection: failed to compute primary psoc for sta %pM\n",
				 sta->addr);
	}

	return ret;

}

static ssize_t macaddr_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buf)
{
	struct ath12k_vif_sysfs_entry *entry;
	struct ath12k *ar;
	struct wiphy *wiphy;
	u8 mac_addr[ETH_ALEN];
	bool byte0 = true;
	u8 radio_idx;

	entry =	container_of(kobj, struct ath12k_vif_sysfs_entry, kobj);
	ar = entry->ar;
	wiphy = ar->ah->hw->wiphy;
	radio_idx = entry->radio_idx;

	if (ath12k_cfg_get(ar->ab, ATH12K_CFG_COHOSTED_BSS_IND_ENABLE) & (1 << ar->pdev_idx))
		byte0 = false;

	ath12k_compute_link_vif_mac(mac_addr,
				    wiphy->addresses[radio_idx].addr,
				    entry->vif_id,
				    byte0);

	return scnprintf(buf, PAGE_SIZE, "%pM\n", mac_addr);
}

static ssize_t flags_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct ath12k_vif_sysfs_entry *entry;
	u32 used;

	entry = container_of(kobj, struct ath12k_vif_sysfs_entry, kobj);
	used = (entry->ar->vendor_mac_used_bitmap & (1 << entry->vif_id)) ? 1 : 0;

	return scnprintf(buf, PAGE_SIZE, "%u\n", used);
}

static ssize_t flags_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	struct ath12k_vif_sysfs_entry *entry;
	struct ath12k *ar;
	unsigned long val;

	entry = container_of(kobj, struct ath12k_vif_sysfs_entry, kobj);
	ar = entry->ar;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val == 1)
		ar->vendor_mac_used_bitmap |= (1 << entry->vif_id);
	else if (val == 0)
		ar->vendor_mac_used_bitmap &= ~(1 << entry->vif_id);
	else
		return -EINVAL;

	return count;
}

static struct kobj_attribute macaddr_attr = __ATTR_RO(macaddr);
static struct kobj_attribute flags_attr = __ATTR(flags, 0644, flags_show, flags_store);

static struct attribute *ath12k_vif_attrs[] = {
	&macaddr_attr.attr,
	&flags_attr.attr,
	NULL,
};

static const struct attribute_group ath12k_vif_attr_group = {
	.attrs = ath12k_vif_attrs,
};

static const struct attribute_group *ath12k_vif_default_groups[] = {
	&ath12k_vif_attr_group,
	NULL,
};

extern const struct sysfs_ops kobj_sysfs_ops;

static void ath12k_vif_kobj_release(struct kobject *kobj)
{
	/* nothing */
}

static struct kobj_type ath12k_vif_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = ath12k_vif_default_groups,
	.release = ath12k_vif_kobj_release,
};

static void ath12k_sysfs_setup_vif_entry(struct ath12k *ar,
					 struct ath12k_vif_sysfs_entry *entry,
					 u8 vif_id,
					 u8 radio_idx)
{
	entry->vif_id = vif_id;
	entry->ar = ar;
	entry->radio_idx = radio_idx;
}

void ath12k_sysfs_init_extn(struct ath12k *ar)
{
	struct wiphy *wiphy = ar->ah->hw->wiphy;
	struct device *dev = wiphy_dev(wiphy);
	struct kobject *parent;
	struct ath12k_base *ab = ar->ab;
	char rname[16];
	int i, r;

	if (!dev)
		return;

	/* Place under the wiphy (class ieee80211 -> phyX) */
	parent = &dev->kobj;

	/*
	 * Create radio directory under phyX: radio<radio_idx>,
	 * determine radio_idx within ah->radio[]
	 */
	if (ab->ag->mlo_capable) {
		for (r = 0; r < ar->ah->num_radio; r++) {
			if (&ar->ah->radio[r] == ar) {
				snprintf(rname, sizeof(rname), "radio%d", r);
				break;
			}
		}
	} else {
		snprintf(rname, sizeof(rname), "radio%d", ar->pdev_idx);
	}

	ar->ar_extn.radio_kobj = kobject_create_and_add(rname, parent);
	if (!ar->ar_extn.radio_kobj)
		return;

	ar->ar_extn.vifs_kobj = kobject_create_and_add("vifs", ar->ar_extn.radio_kobj);
	if (!ar->ar_extn.vifs_kobj)
		return;

	for (i = 0; i <= 16; i++) {
		struct ath12k_vif_sysfs_entry *entry;
		char name[8];

		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry)
			break;

		snprintf(name, sizeof(name), "vif%d", i);
		kobject_init(&entry->kobj, &ath12k_vif_ktype);
		if (kobject_add(&entry->kobj, ar->ar_extn.vifs_kobj, "%s", name)) {
			kobject_put(&entry->kobj);
			kfree(entry);
			break;
		}
		for (r = 0; r < ar->ah->num_radio; r++) {
			if (&ar->ah->radio[r] == ar)
				break;
		}
		ath12k_sysfs_setup_vif_entry(ar, entry, i, r);
		ar->ar_extn.vif_entries[i] = entry;
	}
}

void ath12k_sysfs_cleanup_extn(struct ath12k *ar)
{
	int i;
	struct ath12k_vif_sysfs_entry *entry;

	if (ar->ar_extn.vifs_kobj) {
		for (i = 0; i <= 16; i++) {
			entry = ar->ar_extn.vif_entries[i];
			if (!entry)
				continue;
			kobject_put(&entry->kobj);
			kfree(entry);
			ar->ar_extn.vif_entries[i] = NULL;
		}

		kobject_put(ar->ar_extn.vifs_kobj);
		ar->ar_extn.vifs_kobj = NULL;

		if (ar->ar_extn.radio_kobj) {
			kobject_put(ar->ar_extn.radio_kobj);
			ar->ar_extn.radio_kobj = NULL;
		}
	}
}


void ath12k_wmi_peer_migration_event_extn(struct ath12k_vif *ahvif)
{
	struct sta_240mhz_info *sta, *tmp;

	spin_lock_bh(&ahvif->ath12k_vif_extn.data_lock);
	list_for_each_entry_safe(sta, tmp, &ahvif->ath12k_vif_extn.peer_240mhz_list_extn,
				 list) {
		list_del(&sta->list);
		kfree(sta);
	}

	spin_unlock_bh(&ahvif->ath12k_vif_extn.data_lock);
}

void ath12k_mac_hw_allocate_extn(struct ieee80211_hw *hw,
			  const struct ieee80211_ops_extn *ops_extn)
{
	struct ieee80211_local *local = wiphy_priv(hw->wiphy);
	local->ops_extn = ops_extn;
}

int
ath12k_add_sta_240mhz_info_extn(struct ath12k_vif *ahvif,
				u8 *mac,
				struct ieee80211_240mhz_vendor_oper_extn
				*params)
{
	struct sta_240mhz_info *sta;

	sta = kmalloc(sizeof(*sta), GFP_KERNEL);
	if (!sta)
		return -ENOMEM;

	ether_addr_copy(sta->mac_addr, mac);
	memcpy(&sta->params_240MHz_extn, params,
	       sizeof(sta->params_240MHz_extn));
	INIT_LIST_HEAD(&sta->list);
	spin_lock_bh(&ahvif->ath12k_vif_extn.data_lock);
	list_add_tail(&sta->list, &ahvif->ath12k_vif_extn.peer_240mhz_list_extn);
	spin_unlock_bh(&ahvif->ath12k_vif_extn.data_lock);

	return 0;
}

void ath12k_mac_init_arvif_extn(struct ath12k_vif *ahvif)
{
	/* Protects the extensions data update for an interface*/
	spin_lock_init(&ahvif->ath12k_vif_extn.data_lock);

	INIT_LIST_HEAD(&ahvif->ath12k_vif_extn.peer_240mhz_list_extn);

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
	ahvif->dp_vif.dp_extn.mhdr_len = sizeof(struct meta_hdr_s);
	ahvif->dp_vif.dp_extn.mhdr = 0;
	ahvif->dp_vif.dp_extn.mdbg = 0;
	ahvif->dp_vif.dp_extn.mesh_tx = 0;
#endif
}

void ath12k_mac_setup_radio_iface_comb_extn(
		struct ieee80211_iface_combination *comb)
{
	/* Add 320MHz support for 240MHz */
	comb->radar_detect_widths |= BIT(NL80211_CHAN_WIDTH_320);
}

int ath12k_wmi_op_rx_extn(enum wmi_tlv_event_id id, struct ath12k_base *ab,
			  struct sk_buff *skb)
{
	switch (id) {
	case WMI_ESP_ESTIMATE_EVENTID:
		ath12k_wmi_esp_estimate_event_extn(ab, skb);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void ath12k_mac_setup_extn(struct ath12k *ar)
{
	ath12k_esp_init_extn(ar);
	ath12k_custom_txrx_init(ar);

	/* Initialize WLAN interference stats cache */
	ar->ar_extn.wlan_intf.prev_valid = false;
	ar->ar_extn.wlan_intf.curr_valid = false;
	memset(&ar->ar_extn.wlan_intf.prev_stats, 0,
	       sizeof(ar->ar_extn.wlan_intf.prev_stats));
	memset(&ar->ar_extn.wlan_intf.curr_stats, 0,
	       sizeof(ar->ar_extn.wlan_intf.curr_stats));
}

void ath12k_mac_cleanup_unregister_extn(struct ath12k *ar)
{
	ath12k_esp_cleanup_extn(ar);
	ath12k_custom_txrx_deinit(ar);
}

int ath12k_wmi_tlv_scan_radio_caps_ext2(struct ath12k_base *ab, u16 tag,
					       u16 len, const void *ptr,
					       void *data)
{
	const struct wmi_scan_radio_capabilities_ext2 *cap = ptr;
	struct ath12k_pdev *pdev;
	u32 phy_id, flags, caps;
	int i;
	if (tag != WMI_TAG_SCAN_RADIO_CAPABILITIES_EXT2) {
		ath12k_err(ab,
			   "Unexpected Tag 0x%x (Expected 0x%x)\n",
			   tag, WMI_TAG_SCAN_RADIO_CAPABILITIES_EXT2);
		return -EPROTO;
	}
	phy_id = le32_to_cpu(cap->phy_id);
	flags  = le32_to_cpu(cap->flags);
	for (i = 0; i < ab->num_radios; i++) {
		ath12k_dbg(ab, ATH12K_DBG_WMI,
			   "Checking fw_pdev[%d].phy_id=%u\n",
			   i, ab->fw_pdev[i].phy_id);
		if (ab->fw_pdev[i].phy_id == phy_id) {
			pdev = &ab->pdevs[i];
			caps = 0;
			if (flags & WMI_SCAN_RADIO_CAP_SCAN_RADIO)
				caps |= ATH12K_SCAN_RADIO_CAP_SUPPORTED;
			if (flags & WMI_SCAN_RADIO_CAP_DFS_ENABLED)
				caps |= ATH12K_SCAN_RADIO_CAP_DFS_ENABLED;
			if (flags & WMI_SCAN_RADIO_CAP_BLANKING_SUPPORTED)
				caps |= ATH12K_SCAN_RADIO_CAP_BLANKING;

			pdev->cap.scan_radio_caps = caps;
			ath12k_info(ab,
				    "pdev %d scan-radio caps: 0x%x (supported=%d dfs=%d blanking=%d)\n",
				    pdev->pdev_id,
				    pdev->cap.scan_radio_caps,
				    !!(caps & ATH12K_SCAN_RADIO_CAP_SUPPORTED),
				    !!(caps & ATH12K_SCAN_RADIO_CAP_DFS_ENABLED),
				    !!(caps & ATH12K_SCAN_RADIO_CAP_BLANKING));
			break;
		}
	}
	return 0;
}

int ath12k_mac_setup_vdev_create_arg_scan_radio_extn(struct ath12k_link_vif *arvif,
						     struct ath12k_wmi_vdev_create_arg *arg)
{
	struct ath12k *ar = arvif->ar;
	struct ath12k_vif *ahvif = arvif->ahvif;

	/* If the interface is marked as a special vdev, set the corresponding
	 * VDEV flag that informs firmware to treat this interface as a scan
	 * radio. Firmware will not expect a VDEV_UP for beaconing
	 */
	if (ahvif->vdev_type != WMI_VDEV_TYPE_AP || arvif != &ahvif->deflink)
		return 0;

	/* Has user requested to configure scan radio? */
	if (ahvif->vap_submode == QCA_WLAN_VENDOR_VAP_SUBMODE_SCAN) {
		/* Reject request if firmware does not support scan radio */
		if (!ath12k_scan_radio_supported(ar->pdev)) {
			ath12k_err(ar->ab,
				   "Scan Radio not supported on PDEV %d (caps=0x%x)\n",
				   ar->pdev->pdev_id,
				   ar->pdev->cap.scan_radio_caps);
			return -EOPNOTSUPP;
		}

		arg->mbssid_flags |= VDEV_FLAGS_SCAN_MODE_VAP;
		ath12k_info(ar->ab, "vdev %d: Scan Radio ENABLED on pdev %d (caps=0x%x)\n",
			    arvif->vdev_id, ar->pdev->pdev_id,
			    ar->pdev->cap.scan_radio_caps);

	} else if (ath12k_scan_radio_supported(ar->pdev)) {
		/* Firmware supports scan radio but user didn't configure it as such.
		 * Current policy: pdev with scan radio capability should be dedicated
		 * to scan radio operation. This may change in the future if scan radio
		 * is extended to support other interface types (STA, monitor, etc.).
		 */
		ath12k_err(ar->ab,
			   "pdev %d supports scan radio (caps=0x%x) but vdev %d not configured as scan radio\n",
			   ar->pdev->pdev_id, ar->pdev->cap.scan_radio_caps,
			   arvif->vdev_id);
		return -EINVAL;
	} else {
		/* Normal AP operation: pdev does not have scan radio capability,
		 * proceed with standard VAP configuration.
		 */
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
			   "pdev %d: Normal VAP Operation (caps=0x%x)\n",
			   ar->pdev->pdev_id,
			   ar->pdev->cap.scan_radio_caps);
	}

	return 0;
}

void ath12k_extn_reconfig_extn_params(struct ath12k *ar)
{
	ath12k_reconfig_esp_params_extn(ar);
}

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
void ath12k_peer_assoc_h_mesh_extn(struct ath12k_link_vif *arvif,
				   struct ath12k_link_sta *arsta,
				   struct ieee80211_link_sta *link_sta,
				   struct ath12k_wmi_peer_assoc_arg *arg)
{
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k *ar = arvif->ar;

	if (!arsta || !link_sta || !arg) {
		ath12k_err(ar->ab, "Link STA/Peer assoc args is NULL\n");
		return;
	}

	/* For statically added mesh peers, keys will be programmed by user.
	 * Reset the need_ptk_4_way/need_gtk_2_way flag when sending peer assoc.
	 *
	 * Save calculated phymode and nss for peer lookup.
	 */
	if (ahvif->vap_submode == QCA_WLAN_VENDOR_VAP_SUBMODE_MESH) {
		arg->need_ptk_4_way = false;
		arg->need_gtk_2_way = false;
		link_sta->sta->sta_extn.peer_info.mesh_peer_phymode = arg->peer_phymode;
		link_sta->sta->sta_extn.peer_info.mesh_peer_nss = arg->peer_nss;
	}

	return;
}
#endif
