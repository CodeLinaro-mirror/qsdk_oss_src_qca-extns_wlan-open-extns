/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/bitfield.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/netlink.h>
#include "../core.h"
#include "../debug.h"
#include "../reg.h"
#include "../vendor.h"
#include "../wmi.h"
#include "../mac.h"
#include "../net/mac80211/ieee80211_i.h"
#include "reg_extn.h"

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

	if (pdev_idx < 0 || pdev_idx >= ab->num_radios)
		return;

	ab->ath12k_base_extn.reg_6ghz_thresh_priority_freq[pdev_idx] =
		reg_info_extn->reg_6ghz_thresh_priority_freq;

	ath12k_dbg(ab, ATH12K_DBG_REG,
		   "pdev %d 6 GHz priority threshold set to %u\n",
		   pdev_idx,
		   ab->ath12k_base_extn.reg_6ghz_thresh_priority_freq[pdev_idx]);
}

void ath12k_wmi_set_hw_blocklist_host_service_flag_extn(
		struct ath12k_wmi_resource_config_params *wmi_cfg,
		struct ath12k_wmi_resource_config_arg *tg_cfg)
{
	wmi_cfg->host_service_flags |=
		cpu_to_le32(tg_cfg->hw_blocklist_chans_support <<
			    WMI_RSRC_CFG_HOST_SVC_FLAG_SUPP_HW_BLACKLIST_CHANS);
}

void ath12k_wmi_set_hw_blocklist_service_support_extn(
		struct ath12k_base *ab,
		struct ath12k_wmi_resource_config_arg *res_cfg)
{
	if (test_bit(WMI_SERVICE_HW_BLACKLIST_CHAN_SUPPORT, ab->wmi_ab.svc_map))
		res_cfg->hw_blocklist_chans_support = true;
}

static void
ath12k_reg_hw_blocklist_free_chans_extn(struct ath12k_hw_blocklist_chans *chans)
{
	kfree(chans->fb_chan);
	kfree(chans->pc_chan);
	chans->fb_chan = NULL;
	chans->pc_chan = NULL;
	chans->nfbchans = 0;
	chans->npcchans = 0;
}

static void ath12k_reg_info_free_extn(struct ath12k_reg_info *reg_info)
{
	if (!reg_info)
		return;

	kfree(reg_info->reg_rules_2g_ptr);
	kfree(reg_info->reg_rules_5g_ptr);
	if (reg_info->is_ext_reg_event) {
		int i;

		for (i = 0; i < WMI_REG_CURRENT_MAX_AP_TYPE; i++) {
			int j;

			kfree(reg_info->reg_rules_6g_ap_ptr[i]);
			for (j = 0; j < WMI_REG_MAX_CLIENT_TYPE; j++)
				kfree(reg_info->reg_rules_6g_client_ptr[i][j]);
		}
	}

	ath12k_wmi_free_reg_hw_blocklist_extn(&reg_info->reg_info_extn);
	kfree(reg_info);
}

static struct ath12k_hw_blocklist_state *
ath12k_reg_hw_blocklist_get_state_extn(struct ath12k_base *ab, int pdev_idx)
{
	if (!ab || pdev_idx < 0 || pdev_idx >= ab->num_radios)
		return NULL;

	return ab->ath12k_base_extn.hw_blocklist[pdev_idx];
}

/**
 * ath12k_reg_hw_blocklist_ensure_state_extn - Return or allocate per-pdev blocklist state
 * @ab: Pointer to the ath12k base structure
 * @pdev_idx: pdev identifier
 *
 * If a state struct already exists for @pdev_idx return it.
 * Otherwise allocate and initialise a new one and store it.
 * The "ensure" name signals that callers get a valid pointer or NULL
 * without needing to check whether the struct was already present.
 *
 * Return: pointer to the state struct, or NULL on allocation failure
 */
static struct ath12k_hw_blocklist_state *
ath12k_reg_hw_blocklist_ensure_state_extn(struct ath12k_base *ab, int pdev_idx)
{
	struct ath12k_hw_blocklist_state *st;

	st = ath12k_reg_hw_blocklist_get_state_extn(ab, pdev_idx);
	if (st)
		return st;

	st = kzalloc(sizeof(*st), GFP_ATOMIC);
	if (!st)
		return NULL;

	st->last_cmd = ATH12K_HWBL_CMD_INVALID;
	ab->ath12k_base_extn.hw_blocklist[pdev_idx] = st;
	return st;
}

/* Lockless variant of 'ath12k_reg_hw_blocklist_free_for_mode_extn' */
static void
__ath12k_reg_hw_blocklist_free_for_mode_extn(struct ath12k_base *ab,
					     int pdev_idx,
					     enum wmi_reg_6g_ap_type pwr_type)
{
	struct ath12k_hw_blocklist_state *st;

	st = ath12k_reg_hw_blocklist_get_state_extn(ab, pdev_idx);
	if (!st || pwr_type >= WMI_REG_CURRENT_MAX_AP_TYPE)
		return;

	ath12k_reg_hw_blocklist_free_chans_extn(&st->pwr[pwr_type]);
}

void ath12k_reg_hw_blocklist_free_for_mode_extn(struct ath12k_base *ab,
						int pdev_idx,
						enum wmi_reg_6g_ap_type pwr_type)
{
	spin_lock_bh(&ab->base_lock);
	__ath12k_reg_hw_blocklist_free_for_mode_extn(ab, pdev_idx, pwr_type);
	spin_unlock_bh(&ab->base_lock);
}

static void
ath12k_reg_free_hw_blocklist_afc_and_reg_info(struct ath12k_base *ab, int pdev_idx)
{
	struct ath12k_reg_info **reg_info = ab->ath12k_base_extn.hw_bl_reg_info;
	struct ath12k_afc_info **afc_info = ab->ath12k_base_extn.hw_bl_afc_info;

	if (reg_info[pdev_idx]) {
		ath12k_reg_info_free_extn(reg_info[pdev_idx]);
		reg_info[pdev_idx] = NULL;
	}

	if (afc_info[pdev_idx]) {
		ath12k_free_afc_power_event_info(afc_info[pdev_idx]);
		kfree(afc_info[pdev_idx]);
		afc_info[pdev_idx] = NULL;
	}
}

/* Lockless variant — caller must hold ab->base_lock */
static void
__ath12k_reg_hw_blocklist_free_for_pdev_extn(struct ath12k_base *ab, int pdev_idx)
{
	int pwr;
	struct ath12k_hw_blocklist_state *st;

	if (!ab || pdev_idx < 0 || pdev_idx >= ab->num_radios)
		return;

	st = ath12k_reg_hw_blocklist_get_state_extn(ab, pdev_idx);
	if (st) {
		for (pwr = 0; pwr < WMI_REG_CURRENT_MAX_AP_TYPE; pwr++)
			ath12k_reg_hw_blocklist_free_chans_extn(&st->pwr[pwr]);

		st->valid = false;
		st->complete = false;
		st->last_cmd = ATH12K_HWBL_CMD_INVALID;
		st->last_seq = 0;
		st->expected_seq = 0;
	}

	ath12k_reg_free_hw_blocklist_afc_and_reg_info(ab, pdev_idx);
}

static void
ath12k_reg_hw_blocklist_free_for_pdev_extn(struct ath12k_base *ab, int pdev_idx)
{
	spin_lock_bh(&ab->base_lock);
	__ath12k_reg_hw_blocklist_free_for_pdev_extn(ab, pdev_idx);
	spin_unlock_bh(&ab->base_lock);
}

void ath12k_reg_hw_blocklist_free_extn(struct ath12k_base *ab)
{
	int pdev_idx;

	spin_lock_bh(&ab->base_lock);
	for (pdev_idx = 0; pdev_idx < ab->num_radios; pdev_idx++) {
		struct ath12k_hw_blocklist_state **hw_blocklist = ab->ath12k_base_extn.hw_blocklist;

		__ath12k_reg_hw_blocklist_free_for_pdev_extn(ab, pdev_idx);
		kfree(hw_blocklist[pdev_idx]);
		hw_blocklist[pdev_idx] = NULL;
	}
	spin_unlock_bh(&ab->base_lock);
}

static bool ath12k_hw_blocklist_cmd_matches_pwr_mode_extn(u8 cmd, u8 pwr)
{
	switch (cmd) {
	case ATH12K_HWBL_CMD_SP_UPDATE:
	case ATH12K_HWBL_CMD_SP_CLEAR:
		return pwr == WMI_REG_STD_POWER_AP;
	case ATH12K_HWBL_CMD_VLP_UPDATE:
	case ATH12K_HWBL_CMD_VLP_CLEAR:
		return pwr == WMI_REG_VLP_AP;
	case ATH12K_HWBL_CMD_UPDATE_ALL:
	case ATH12K_HWBL_CMD_CLEAR_ALL:
		return true;
	default:
		return false;
	}
}

static void
ath12k_reg_hw_blocklist_clear_cmd_extn(struct ath12k_hw_blocklist_state *st, u8 cmd)
{
	int i;

	for (i = 0; i < WMI_REG_CURRENT_MAX_AP_TYPE; i++) {
		if (!ath12k_hw_blocklist_cmd_matches_pwr_mode_extn(cmd, i))
			continue;

		ath12k_reg_hw_blocklist_free_chans_extn(&st->pwr[i]);
	}
}

static void
ath12k_reg_hw_blocklist_update_punc_chan_extn(struct ath12k_hw_blocklist_chans *dst,
					      const struct ath12k_hw_blocklist_chan_info *chan)
{
	struct ath12k_hw_blocklist_pc_chan *new_pc;
	u32 pc_idx = dst->npcchans;

	new_pc = krealloc(dst->pc_chan, (pc_idx + 1) * sizeof(*dst->pc_chan), GFP_ATOMIC);
	if (!new_pc)
		return;

	new_pc[pc_idx].center_freq = chan->center_freq;
	new_pc[pc_idx].max_bw = chan->max_bw;
	new_pc[pc_idx].punc_pat_bmap = chan->punc_pat_bmap;
	dst->pc_chan = new_pc;
	dst->npcchans++;
}

static void
ath12k_reg_hw_blocklist_update_fb_chan_extn(struct ath12k_hw_blocklist_chans *dst,
					    const struct ath12k_hw_blocklist_chan_info *chan)
{
	struct ath12k_hw_blocklist_fb_chan *new_fb;
	u32 fb_idx = dst->nfbchans;

	new_fb = krealloc(dst->fb_chan, (fb_idx + 1) * sizeof(*dst->fb_chan), GFP_ATOMIC);
	if (!new_fb)
		return;

	new_fb[fb_idx].pri20_bitmap = chan->pri20_bitmap;
	new_fb[fb_idx].center_freq = chan->center_freq;
	new_fb[fb_idx].max_bw = chan->max_bw;
	dst->fb_chan = new_fb;
	dst->nfbchans++;
}

static void
ath12k_reg_hw_blocklist_update_channels_extn(struct ath12k_hw_blocklist_state *st,
					     const struct ath12k_hw_blocklist_update *upd)
{
	u32 i;

	if (upd->is_first)
		ath12k_reg_hw_blocklist_clear_cmd_extn(st, upd->cmd);

	for (i = 0; i < upd->num_chans; i++) {
		const struct ath12k_hw_blocklist_chan_info *chan = &upd->chans[i];
		struct ath12k_hw_blocklist_chans *dst;

		if (chan->power_mode >= WMI_REG_CURRENT_MAX_AP_TYPE)
			continue;

		if (!ath12k_hw_blocklist_cmd_matches_pwr_mode_extn(upd->cmd, chan->power_mode))
			continue;

		dst = &st->pwr[chan->power_mode];
		if (chan->punc_pat_bmap)
			ath12k_reg_hw_blocklist_update_punc_chan_extn(dst, chan);
		else
			ath12k_reg_hw_blocklist_update_fb_chan_extn(dst, chan);
	}
}

static void
ath12k_reg_clear_hw_blocklist_ext(struct ath12k_hw_blocklist_state *st, u8 cmd)
{
	st->valid = true;
	st->last_cmd = cmd;
	ath12k_reg_hw_blocklist_clear_cmd_extn(st, cmd);
	st->complete = true;
	st->expected_seq = 0;
}

static int
ath12k_reg_update_hw_blocklist_ext(struct ath12k_base *ab, u8 phy_id,
				   struct ath12k_hw_blocklist_state *st,
				   const struct ath12k_hw_blocklist_update *upd)
{
	st->valid = true;
	st->last_cmd = upd->cmd;
	st->last_seq = upd->seq_num;

	if (upd->is_first) {
		st->complete = false;
		st->expected_seq = 1;
	}

	if (!st->expected_seq || upd->seq_num != st->expected_seq) {
		ath12k_warn(ab,
			    "hw blocklist: phy %u cmd %u seq mismatch got %u exp %u first %d more %d total %u\n",
			    phy_id, upd->cmd, upd->seq_num,
			    st->expected_seq, upd->is_first, upd->more,
			    upd->total_num);
		st->valid = false;
		st->complete = false;
		st->expected_seq = 0;
		return -EILSEQ;
	}

	ath12k_reg_hw_blocklist_update_channels_extn(st, upd);
	if (upd->more) {
		st->expected_seq++;
		return 0;
	}

	st->complete = !upd->total_num || upd->seq_num == upd->total_num;
	st->expected_seq = 0;
	if (!st->complete)
		ath12k_warn(ab,
			    "hw blocklist: phy %u cmd %u total mismatch seq %u total %u\n",
			    phy_id, upd->cmd, upd->seq_num, upd->total_num);
	return 0;
}

static void
ath12k_reg_handle_default_hw_blocklist_cmd_ext(struct ath12k_hw_blocklist_state *st)
{
	st->valid = false;
	st->complete = false;
	st->expected_seq = 0;
}

/**
 * ath12k_reg_update_hw_blocklist_extn - Update per-pdev HW blocklist state
 * @ab: Pointer to the ath12k base structure
 * @phy_id: pdev identifier (0-based radio index)
 * @upd: Parsed HW blocklist update from the WMI event
 *
 * Applies a HW blocklist update fragment to the per-pdev state machine.
 * Handles clear commands (immediate completion), update commands (may span
 * multiple fragments keyed by seq_num), and validates sequence ordering.
 *
 * Must be called with ab->base_lock held by the caller (or it acquires it
 * internally). Currently acquires base_lock itself.
 *
 * Return: 0 on success, negative error code on failure
 */
int ath12k_reg_update_hw_blocklist_extn(struct ath12k_base *ab, u8 phy_id,
					const struct ath12k_hw_blocklist_update *upd)
{
	struct ath12k_hw_blocklist_state *st;

	if (!upd->valid)
		return 0;

	if (phy_id >= ab->num_radios)
		return -EINVAL;

	spin_lock_bh(&ab->base_lock);
	st = ath12k_reg_hw_blocklist_ensure_state_extn(ab, phy_id);
	if (!st) {
		spin_unlock_bh(&ab->base_lock);
		return -ENOMEM;
	}

	switch (upd->cmd) {
	case ATH12K_HWBL_CMD_SP_CLEAR:
	case ATH12K_HWBL_CMD_VLP_CLEAR:
	case ATH12K_HWBL_CMD_CLEAR_ALL:
		ath12k_reg_clear_hw_blocklist_ext(st, upd->cmd);
		break;
	case ATH12K_HWBL_CMD_SP_UPDATE:
	case ATH12K_HWBL_CMD_VLP_UPDATE:
	case ATH12K_HWBL_CMD_UPDATE_ALL: {
		int ret = ath12k_reg_update_hw_blocklist_ext(ab, phy_id, st, upd);

		if (ret) {
			spin_unlock_bh(&ab->base_lock);
			return ret;
		}
		break;
	}
	default:
		ath12k_reg_handle_default_hw_blocklist_cmd_ext(st);
		break;
	}

	ath12k_dbg(ab, ATH12K_DBG_REG,
		   "hw blocklist: phy %u cmd %u seq %u first %d more %d elems %u complete %d exp %u\n",
		   phy_id, upd->cmd, upd->seq_num, upd->is_first, upd->more,
		   upd->num_chans, st->complete, st->expected_seq);
	spin_unlock_bh(&ab->base_lock);

	return 0;
}

/* HW blocklist WMI message field macros */
#define ATH12K_HWBL_MSG_INFO_SEQ_NUM		GENMASK(7, 0)
#define ATH12K_HWBL_MSG_INFO_TOTAL_NUM		GENMASK(15, 8)
#define ATH12K_HWBL_MSG_INFO_RESP_CODE		GENMASK(19, 16)
#define ATH12K_HWBL_MSG_INFO_EVT_FLAG		BIT(20)

#define ATH12K_HWBL_NUM_TOTAL_CHAN		GENMASK(15, 0)
#define ATH12K_HWBL_NUM_CUR_CHAN		GENMASK(31, 16)

#define ATH12K_HWBL_FREQ_INFO_PRI20_BITMAP	GENMASK(15, 0)
#define ATH12K_HWBL_FREQ_INFO_CENTER_MHZ	GENMASK(31, 16)

#define ATH12K_HWBL_META_PWR_MODE		GENMASK(3, 0)
#define ATH12K_HWBL_META_MAX_BW			GENMASK(11, 4)

#define ATH12K_HWBL_PUNC_BITMAP			GENMASK(23, 0)

static void
ath12k_wmi_parse_hw_blocklist_chan_fixed_param_extn(struct ath12k_base *ab,
						    const void *ptr, u16 len,
						    struct ath12k_hw_blocklist_update *upd)
{
	const struct wmi_hw_blacklist_chan_fixed_param *fp = ptr;
	u32 msg_info, num_info;

	if (len < sizeof(*fp)) {
		ath12k_warn(ab, "HBL Fixed length Absent");
		return;
	}

	msg_info = le32_to_cpu(fp->blacklist_msg_info);
	num_info = le32_to_cpu(fp->num_hw_blocklist_channels);

	upd->total_num_chan = FIELD_GET(ATH12K_HWBL_NUM_TOTAL_CHAN, num_info);
	upd->current_num_chan = FIELD_GET(ATH12K_HWBL_NUM_CUR_CHAN, num_info);
	upd->valid = true;
	upd->cmd = FIELD_GET(ATH12K_HWBL_MSG_INFO_RESP_CODE, msg_info);
	upd->seq_num = FIELD_GET(ATH12K_HWBL_MSG_INFO_SEQ_NUM, msg_info);
	upd->total_num = FIELD_GET(ATH12K_HWBL_MSG_INFO_TOTAL_NUM, msg_info);
	upd->more = !!FIELD_GET(ATH12K_HWBL_MSG_INFO_EVT_FLAG, msg_info);
	upd->is_first = upd->seq_num == 1;
	upd->phy_id = le32_to_cpu(fp->phy_id);
}

static int
ath12k_wmi_parse_hw_blocklist_chan_data_extn(struct ath12k_base *ab,
					     const void *ptr, u16 len,
					     struct ath12k_hw_blocklist_update *upd)
{
	const struct wmi_hw_blacklist_chan_data *hw = ptr;
	struct ath12k_hw_blocklist_chan_info *new;
	u32 freq_info, meta, punc_info, punc;
	u16 pri20, center;
	u8 pwr, bw;
	u32 idx;

	if (len < sizeof(*hw)) {
		ath12k_warn(ab, "HBL Chan Data Length Absent");
		return 0;
	}

	if (!upd->valid) {
		ath12k_warn(ab, "HBL Not Valid Ignore Chan Data");
		return 0;
	}

	if (!upd->total_num_chan || !upd->current_num_chan) {
		ath12k_warn(ab, "HBL channel data not present total %u current %u",
			    upd->total_num_chan, upd->current_num_chan);
		return 0;
	}

	freq_info = le32_to_cpu(hw->freq_info);
	pri20  = FIELD_GET(ATH12K_HWBL_FREQ_INFO_PRI20_BITMAP, freq_info);
	center = FIELD_GET(ATH12K_HWBL_FREQ_INFO_CENTER_MHZ, freq_info);

	meta = le32_to_cpu(hw->chan_list_meta_data);
	pwr = FIELD_GET(ATH12K_HWBL_META_PWR_MODE, meta);
	bw  = FIELD_GET(ATH12K_HWBL_META_MAX_BW, meta);

	punc_info = le32_to_cpu(hw->puncture_pattern_bitmap_info);
	punc = FIELD_GET(ATH12K_HWBL_PUNC_BITMAP, punc_info);

	idx = upd->num_chans;
	new = krealloc(upd->chans, (idx + 1) * sizeof(*upd->chans), GFP_ATOMIC);
	if (!new) {
		kfree(upd->chans);
		upd->chans = NULL;
		return -ENOMEM;
	}

	new[idx].pri20_bitmap = pri20;
	new[idx].center_freq = center;
	new[idx].power_mode = pwr;
	new[idx].max_bw = bw;
	new[idx].punc_pat_bmap = punc;
	upd->chans = new;
	upd->num_chans++;

	return 0;
}

static int ath12k_wmi_hw_blocklist_elem_parse_extn(struct ath12k_base *ab, u16 tag,
						   u16 len, const void *ptr,
						   void *data)
{
	struct ath12k_hw_blocklist_update *upd = data;

	switch (tag) {
	case WMI_TAG_HW_BLACKLIST_CHAN_FIXED_PARAM:
		ath12k_wmi_parse_hw_blocklist_chan_fixed_param_extn(ab, ptr, len, upd);
		break;
	case WMI_TAG_HW_BLACKLIST_CHAN_DATA:
		return ath12k_wmi_parse_hw_blocklist_chan_data_extn(ab, ptr, len, upd);
	default:
		break;
	}

	return 0;
}

static int ath12k_wmi_hw_blocklist_tlv_parse_extn(struct ath12k_base *ab, u16 tag,
						   u16 len, const void *ptr,
						   void *data)
{
	const struct wmi_tlv *tlv;
	u16 inner_tag;

	if (tag != WMI_TAG_ARRAY_STRUCT)
		return 0;

	if (len < sizeof(*tlv))
		return 0;

	tlv = ptr;
	inner_tag = le32_get_bits(tlv->header, WMI_TLV_TAG);
	if (inner_tag != WMI_TAG_HW_BLACKLIST_CHAN_FIXED_PARAM &&
	    inner_tag != WMI_TAG_HW_BLACKLIST_CHAN_DATA)
		return 0;

	return ath12k_wmi_tlv_iter(ab, ptr, len,
				   ath12k_wmi_hw_blocklist_elem_parse_extn,
				   data);
}

void ath12k_wmi_pull_hw_blocklist_update_extn(struct ath12k_base *ab,
					      struct sk_buff *skb,
					      struct ath12k_hw_blocklist_update *upd)
{
	memset(upd, 0, sizeof(*upd));
	ath12k_wmi_tlv_iter(ab, skb->data, skb->len,
			    ath12k_wmi_hw_blocklist_tlv_parse_extn, upd);
}

int ath12k_wmi_pull_reg_hw_blocklist_extn(
		struct ath12k_base *ab, struct sk_buff *skb,
		struct ath12k_reg_info_extn *reg_info_extn)
{
	if (!reg_info_extn)
		return -EINVAL;

	reg_info_extn->hw_blocklist = kzalloc(sizeof(*reg_info_extn->hw_blocklist),
					      GFP_ATOMIC);
	if (!reg_info_extn->hw_blocklist)
		return -ENOMEM;

	ath12k_wmi_pull_hw_blocklist_update_extn(ab, skb,
						 reg_info_extn->hw_blocklist);
	return 0;
}

bool ath12k_wmi_is_reg_hw_blocklist_fragment_only_extn(
		struct ath12k_base *ab,
		struct ath12k_reg_info *reg_info)
{
	struct ath12k_hw_blocklist_update *hw_bl = reg_info->reg_info_extn.hw_blocklist;

	if (hw_bl && hw_bl->valid && !hw_bl->is_first) {
		ath12k_warn(ab,
			    "hw blocklist fragment-only update (phy_id %u cmd %u seq %u elems %u)\n",
			    reg_info->phy_id,
			    hw_bl->cmd,
			    hw_bl->seq_num,
			    hw_bl->num_chans);
		return true;
	}

	return false;
}

void ath12k_wmi_free_reg_hw_blocklist_extn(
		struct ath12k_reg_info_extn *reg_info_extn)
{
	if (!reg_info_extn || !reg_info_extn->hw_blocklist)
		return;

	kfree(reg_info_extn->hw_blocklist->chans);
	kfree(reg_info_extn->hw_blocklist);
	reg_info_extn->hw_blocklist = NULL;
}

static bool
ath12k_wmi_handle_afc_hwbl_no_data_extn(struct ath12k_base *ab,
					struct ath12k_afc_info *afc_info,
					struct ath12k_hw_blocklist_update *upd)
{
	u8 phy_id = afc_info->phy_id;

	if (phy_id < ab->num_radios) {
		struct ath12k_afc_info **hw_bl_afc_info = ab->ath12k_base_extn.hw_bl_afc_info;

		spin_lock_bh(&ab->base_lock);
		__ath12k_reg_hw_blocklist_free_for_mode_extn(ab, phy_id, WMI_REG_STD_POWER_AP);
		if (hw_bl_afc_info[phy_id]) {
			ath12k_free_afc_power_event_info(hw_bl_afc_info[phy_id]);
			kfree(hw_bl_afc_info[phy_id]);
			hw_bl_afc_info[phy_id] = NULL;
		}
		spin_unlock_bh(&ab->base_lock);
	}

	kfree(upd->chans);
	return false;
}

static bool
ath12k_wmi_handle_afc_hwbl_more_frags_extn(struct ath12k_base *ab, u8 phy_id,
					   bool is_first,
					   struct ath12k_afc_info *afc_info)
{
	struct ath12k_afc_info *cached;

	if (!is_first)
		goto free_afc;


	spin_lock_bh(&ab->base_lock);
	cached = ab->ath12k_base_extn.hw_bl_afc_info[phy_id];
	if (!cached) {
		/* GFP_ATOMIC is safe under spinlock — no need to release */
		cached = kzalloc(sizeof(*cached), GFP_ATOMIC);
		if (!cached)
			goto unlock_and_free;
		ab->ath12k_base_extn.hw_bl_afc_info[phy_id] = cached;
	}
	ath12k_free_afc_power_event_info(cached);
	*cached = *afc_info;
	spin_unlock_bh(&ab->base_lock);
	return true;

unlock_and_free:
	spin_unlock_bh(&ab->base_lock);
free_afc:
	ath12k_free_afc_power_event_info(afc_info);
	return true;
}

static bool
ath12k_wmi_handle_afc_hwbl_final_frag_extn(struct ath12k *ar, u8 phy_id,
					   struct ath12k_afc_info *afc_info)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_afc_info *process_info;
	int ret;

	spin_lock_bh(&ab->base_lock);
	process_info = ab->ath12k_base_extn.hw_bl_afc_info[phy_id];
	ab->ath12k_base_extn.hw_bl_afc_info[phy_id] = NULL;
	spin_unlock_bh(&ab->base_lock);
	ath12k_free_afc_power_event_info(afc_info);

	if (!process_info || !process_info->afc_reg_info) {
		ath12k_warn(ab,
			    "hw blocklist: no cached AFC info for final fragment phy_id %u\n",
			    phy_id);
		kfree(process_info);
		return true;
	}

	ret = ath12k_reg_process_afc_power_event(ar, process_info);
	if (ret)
		ath12k_warn(ab, "AFC reg rule update failed ret: %d\n", ret);

	ath12k_vendor_send_power_update_complete(ar, process_info);
	if (process_info->afc_reg_info->fw_status_code != REG_FW_AFC_POWER_EVENT_SUCCESS)
		ath12k_free_afc_power_event_info(process_info);

	kfree(process_info);
	return true;
}

/**
 * ath12k_wmi_handle_afc_power_info_hwbl_extn - Intercept AFC power event for HW blocklist
 * @ab: Pointer to the ath12k base structure
 * @skb: Socket buffer carrying the AFC WMI event
 * @afc_info: Parsed AFC info from the current event
 *
 * Extracts any HWBL update fragment embedded in the AFC event.
 *
 * - If no valid HWBL data: clears SP-mode blocklist for this pdev and returns
 *   false (caller should proceed with normal AFC processing).
 * - If more=true (multi-fragment): caches the first-fragment afc_info and
 *   returns true (caller should suppress normal processing).
 * - On the final fragment: swaps in the cached first-fragment afc_info and
 *   calls ath12k_reg_process_afc_power_event(), then returns true.
 *
 * Return: false if normal AFC source processing should continue;
 *         true if the event was consumed by this function.
 */
bool ath12k_wmi_handle_afc_power_info_hwbl_extn(
		struct ath12k_base *ab, struct sk_buff *skb,
		struct ath12k_afc_info *afc_info)
{
	struct ath12k_hw_blocklist_update upd = {};
	struct ath12k *ar;
	u8 phy_id;
	int ret;

	ath12k_wmi_pull_hw_blocklist_update_extn(ab, skb, &upd);
	if (!upd.valid)
		return ath12k_wmi_handle_afc_hwbl_no_data_extn(ab, afc_info, &upd);

	phy_id = upd.is_first ? afc_info->phy_id : upd.phy_id;
	if (phy_id >= ab->num_radios) {
		ath12k_warn(ab, "hw blocklist: invalid phy_id %u in AFC event\n", phy_id);
		goto free_and_return;
	}

	ar = ab->pdevs[phy_id].ar;
	if (!ar) {
		ath12k_warn(ab, "hw blocklist: no ar for phy_id %u in AFC event\n", phy_id);
		goto free_and_return;
	}

	if (upd.is_first)
		ath12k_reg_hw_blocklist_free_for_mode_extn(ab, phy_id, WMI_REG_STD_POWER_AP);

	ret = ath12k_reg_update_hw_blocklist_extn(ab, phy_id, &upd);
	kfree(upd.chans);
	if (ret) {
		ath12k_warn(ab, "hw blocklist: AFC update failed phy_id %u ret %d\n",
			    phy_id, ret);
		ath12k_free_afc_power_event_info(afc_info);
		return true;
	}

	if (upd.more)
		return ath12k_wmi_handle_afc_hwbl_more_frags_extn(ab, phy_id,
								  upd.is_first,
								  afc_info);

	if (upd.is_first) {
		struct ath12k_afc_info **hw_bl_afc_info = ab->ath12k_base_extn.hw_bl_afc_info;

		spin_lock_bh(&ab->base_lock);
		if (hw_bl_afc_info[phy_id]) {
			ath12k_free_afc_power_event_info(hw_bl_afc_info[phy_id]);
			kfree(hw_bl_afc_info[phy_id]);
			hw_bl_afc_info[phy_id] = NULL;
		}
		spin_unlock_bh(&ab->base_lock);
		return false;
	}

	return ath12k_wmi_handle_afc_hwbl_final_frag_extn(ar, phy_id, afc_info);

free_and_return:
	ath12k_free_afc_power_event_info(afc_info);
	kfree(upd.chans);
	return true;
}

static bool
ath12k_wmi_handle_reg_chan_list_hwbl_more_extn(struct ath12k_base *ab, u8 phy_id,
					       bool is_first,
					       struct ath12k_reg_info **reg_info_ptr)
{
	if (!is_first)
		return true;

	spin_lock_bh(&ab->base_lock);
	if (ab->ath12k_base_extn.hw_bl_reg_info[phy_id])
		ath12k_reg_info_free_extn(ab->ath12k_base_extn.hw_bl_reg_info[phy_id]);

	ab->ath12k_base_extn.hw_bl_reg_info[phy_id] = *reg_info_ptr;
	*reg_info_ptr = NULL;
	spin_unlock_bh(&ab->base_lock);
	return true;
}

static bool
ath12k_wmi_handle_reg_chan_list_hwbl_final_extn(struct ath12k_base *ab, u8 phy_id,
						struct ath12k_reg_info **reg_info_ptr)
{
	struct ath12k_reg_info *cached;

	spin_lock_bh(&ab->base_lock);
	cached = ab->ath12k_base_extn.hw_bl_reg_info[phy_id];
	ab->ath12k_base_extn.hw_bl_reg_info[phy_id] = NULL;
	spin_unlock_bh(&ab->base_lock);
	if (!cached) {
		ath12k_warn(ab,
			    "hw blocklist: no cached reg_info for final fragment phy_id %u\n",
			    phy_id);
		return true;
	}

	ath12k_reg_info_free_extn(*reg_info_ptr);
	*reg_info_ptr = cached;
	return false;
}

/**
 * ath12k_wmi_handle_reg_chan_list_hwbl_extn - Handle HW blocklist in reg chan list event
 * @ab: Pointer to the ath12k base structure
 * @reg_info_ptr: Double pointer to the reg_info struct for this event.
 *
 * Three code paths:
 * (1) No valid HWBL data: clear per-pdev blocklist state and return false
 *     (caller should call ath12k_reg_handle_chan_list normally).
 * (2) First fragment with more=true: steal *reg_info_ptr (set to NULL),
 *     cache it, return true (caller should NOT call ath12k_reg_handle_chan_list).
 * (3) Final fragment: swap *reg_info_ptr to the cached first-fragment reg_info
 *     (fragment-only event's reg_info is freed), return false (caller should
 *     call ath12k_reg_handle_chan_list with the swapped pointer).
 *
 * Return: false if normal source processing should continue;
 *         true if the event was fully consumed or deferred.
 */
bool ath12k_wmi_handle_reg_chan_list_hwbl_extn(struct ath12k_base *ab,
					       struct ath12k_reg_info **reg_info_ptr)
{
	struct ath12k_reg_info *info = *reg_info_ptr;
	struct ath12k_hw_blocklist_update *upd;
	u8 phy_id;
	int ret;

	upd = info->reg_info_extn.hw_blocklist;
	if (!upd || !upd->valid) {
		phy_id = info->phy_id;
		if (phy_id < ab->num_radios)
			ath12k_reg_hw_blocklist_free_for_pdev_extn(ab, phy_id);
		return false;
	}

	phy_id = upd->is_first ? info->phy_id : upd->phy_id;
	if (phy_id >= ab->num_radios) {
		ath12k_warn(ab, "hw blocklist: invalid phy_id %u in reg event\n", phy_id);
		return true;
	}

	if (upd->is_first)
		ath12k_reg_hw_blocklist_free_for_pdev_extn(ab, phy_id);

	ret = ath12k_reg_update_hw_blocklist_extn(ab, phy_id, upd);
	if (ret) {
		ath12k_warn(ab, "hw blocklist: update failed phy_id %u ret %d\n",
			    phy_id, ret);
		return true;
	}

	if (upd->more)
		return ath12k_wmi_handle_reg_chan_list_hwbl_more_extn(ab, phy_id,
								      upd->is_first,
								      reg_info_ptr);

	if (upd->is_first)
		return false;

	return ath12k_wmi_handle_reg_chan_list_hwbl_final_extn(ab, phy_id, reg_info_ptr);
}

int ath12k_wmi_hw_blocklist_chan_event_extn(struct ath12k_base *ab,
					    struct sk_buff *skb)
{
	struct ath12k_hw_blocklist_update hw_blocklist;
	int ret;
	u8 phy_id;

	ath12k_wmi_pull_hw_blocklist_update_extn(ab, skb, &hw_blocklist);
	if (!hw_blocklist.valid) {
		ath12k_warn(ab, "Invalid HW blocklist");
		return -EINVAL;
	}

	phy_id = hw_blocklist.phy_id;
	if (phy_id >= ab->num_radios) {
		ath12k_warn(ab, "Received AFC Event for Invalid phy id %d\n", phy_id);
		kfree(hw_blocklist.chans);
		return -EINVAL;
	}

	ret = ath12k_reg_update_hw_blocklist_extn(ab, phy_id, &hw_blocklist);
	if (ret) {
		ath12k_warn(ab, "Failed to update HW blocklist\n");
		kfree(hw_blocklist.chans);
		return ret;
	}

	kfree(hw_blocklist.chans);
	return 0;
}
