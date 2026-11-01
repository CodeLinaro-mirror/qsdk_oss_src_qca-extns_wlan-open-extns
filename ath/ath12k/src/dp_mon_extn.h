/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef DP_MON_EXTN_H
#define DP_MON_EXTN_H
#include "../core.h"
#include "../wmi.h"
#include "../mac.h"
#include "../debug.h"
#include "../vendor.h"

#define DP_TLV_LOGGER_MAX_TLV_LOGGING_SIZE		1024	/* Total buffer size */
#define DP_TLV_LOGGER_MAX_NUM_PPDU_RECORD		4	/* Number of PPDU slots */
#define DP_TLV_LOGGER_MAX_TLVS_PER_PPDU			255	/* TLVs per PPDU slot */
#define DP_TLV_LOGGER_MAX_PPDU_START_TLV_NUM		38	/* PPDU start TLVs */
#define DP_TLV_LOGGER_MAX_MPDU_TLV_NUM			160	/* MPDU-level TLVs */
#define DP_TLV_LOGGER_MAX_PPDU_END_TLV_NUM		57	/* PPDU end TLVs */


struct ath12k_dp_mon_tlv_info {
	u32 tlv_tag:10;
	u32 data:22;
};

struct ath12k_dp_mon_tlv_logger {
	void *buff;
	u16 curr_ppdu_pos;
	u16 ppdu_start_idx;
	u16 mpdu_idx;
	u16 ppdu_end_idx;
	u16 max_ppdu_start_idx;
	u16 max_mpdu_idx;
	u16 max_ppdu_end_idx;
	u8 wrap_flag;
	u8 tlv_logging_enable;
};

enum ath12k_dp_mon_tlv_logger_category {
	PPDU_START,
	MPDU,
	PPDU_END,
	NOT_SUPPORTED,
};

enum tlv_logger_mode {
	TLV_LOGGER_RX_MODE,
	TLV_LOGGER_TX_MODE,
};

int ath12k_dp_mon_pdev_tlv_logger_init(struct ath12k_pdev_dp *dp_pdev);
void ath12k_dp_mon_pdev_tlv_logger_deinit(struct ath12k_pdev_dp *dp_pdev);
void ath12k_dp_mon_record_index_update(struct ath12k_pdev_mon_dp *mon_pdev, int mode);
void ath12k_dp_mon_tlv_logger_clear_buf(struct ath12k_pdev_mon_dp *mon_pdev, int mode);

struct ath12k_undecoded_metadata_event {
	u8  phyrx_abort_reason;
	u16 l_sig_length;
	u8  l_sig_a_parity;
	u8  l_sig_a_pkt_type;
	u16 ht_length;
	u8  ht_stbc;
	u8  ht_crc;
	u8  vht_crc;
	u8  vht_no_txop_ps;
	u8  he_sig_a_su_crc;
	u8  he_sig_a_mu_dl_crc;
	u8  rssi_comb;
	s8  rssi_chain[8];
	u64 tsft;
	u16 chan_num;
	u8  preamble_type;
	u8  mcs;
	u8  nss;
	u8  bw;
	u16 ppdu_id;
	u32 phyrx_error_mask;
	u32 phyrx_error_mask_cont;
};

/**
 * struct ath12k_undecoded_metadata_notif_event - event wrapper for the notifier
 * @skb: SKB containing ath12k_undecoded_metadata_event payload.
 *       Producer-owned — listeners must not hold or free beyond callback.
 *       Use skb_copy() if a separate buffer is needed.
 */
struct ath12k_undecoded_metadata_notif_event {
	struct sk_buff *skb;
};

int  ath12k_undecoded_metadata_notif_call_chain(
		struct ath12k_undecoded_metadata_notif_event *event);

#endif /* DP_MON_EXTN_H */
