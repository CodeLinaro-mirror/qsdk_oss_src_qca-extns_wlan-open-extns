/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef DP_MON2_EXTN_H
#define DP_MON2_EXTN_H

#include "../../core.h"
#include "../hal_mon.h"
#include "../../hal_mon_cmn.h"
#include "../../dp_mon.h"
#include "../../dp_htt.h"
#include "../../qcn_extns/ath12k_cmn_extn.h"

/* FP PHY-error frame-group routing values programmed into the HTT
 * RX_RING_SELECTION_CFG.
 */
#define ATH12K_WIFI8_FP_PHY_ERR_BUF_SRC_SW2RXDMA	2
#define ATH12K_WIFI8_FP_PHY_ERR_BUF_SRC_NONE		3
#define ATH12K_WIFI8_FP_PHY_ERR_BUF_DEST_RXDMA2SW	2
#define ATH12K_WIFI8_FP_PHY_ERR_BUF_DEST_RELEASING	0

/* RXPCU_PPDU_END_INFO word 2 - phyrx_abort_request_info_valid at bit [28] */
#define ATH12K_WIFI8_RXPCU_PPDU_END_INFO2_PHYRX_ABORT_VALID	BIT(28)

/* RXPCU_PPDU_END_INFO word 11 - phyrx_abort_request_info_details.phyrx_abort_reason
 * at bits [7:0].
 */
#define ATH12K_WIFI8_RXPCU_PPDU_END_INFO11_PHYRX_ABORT_REASON	GENMASK(7, 0)

struct ath12k_wifi8_rxpcu_ppdu_end_info {
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 reserved[8];
	__le32 info11;
} __packed;

/* SIG-field bit positions for the abort-metadata capture that are not already
 * defined in wifi8 hal_mon.h. These are 802.11-standard bit positions within
 * the decoded SIG info words (same as the wifi6 driver documents).
 */
/* L_SIG_A info0 - parity at bit [17] (length/pkt_type use hal_mon.h masks) */
#define ATH12K_WIFI8_L_SIG_A_INFO0_PARITY	BIT(17)
/* HT_SIG info0 - HT length at bits [23:8] */
#define ATH12K_WIFI8_HT_SIG_INFO0_LENGTH	GENMASK(23, 8)
/* HT_SIG info1 - smoothing[0], not_sounding[1], aggregation[3], crc[17:10] */
#define ATH12K_WIFI8_HT_SIG_INFO1_SMOOTHING	BIT(0)
#define ATH12K_WIFI8_HT_SIG_INFO1_NOT_SOUNDING	BIT(1)
#define ATH12K_WIFI8_HT_SIG_INFO1_AGGREGATION	BIT(3)
#define ATH12K_WIFI8_HT_SIG_INFO1_CRC		GENMASK(17, 10)
/* VHT_SIG_A info0 - TXOP_PS_NOT_ALLOWED at bit [22] */
#define ATH12K_WIFI8_VHT_SIG_A_INFO0_TXOP_PS_NOT_ALLOWED	BIT(22)
/* VHT_SIG_A info1 - CRC at bits [17:10] */
#define ATH12K_WIFI8_VHT_SIG_A_INFO1_CRC	GENMASK(17, 10)
/* HE_SIG_A_SU info1 - CRC at bits [19:16] */
#define ATH12K_WIFI8_HE_SIG_A_SU_INFO1_CRC	GENMASK(19, 16)
/* HE_SIG_A_MU_DL info1 - CRC at bits [19:16] */
#define ATH12K_WIFI8_HE_SIG_A_MU_DL_INFO1_CRC	GENMASK(19, 16)

/* HAL parse: RXPCU abort valid/reason (byte-offset based off the TLV data) */
void ath12k_wifi8_hal_rx_get_phyrx_abort(struct hal_rx_mon_ppdu_info *ppdu_info,
					 const void *tlv_data);

/* HAL parse: SIG-field capture into phyrx_abort_info from decoded info words */
void ath12k_wifi8_hal_mon_parse_l_sig_a_extn(const struct hal_rx_lsig_a_info *lsiga,
					     struct hal_rx_mon_ppdu_info *ppdu_info);
void ath12k_wifi8_hal_mon_parse_ht_sig_extn(const struct hal_rx_ht_sig_info *ht_sig,
					    struct hal_rx_mon_ppdu_info *ppdu_info);
void ath12k_wifi8_hal_mon_parse_vht_sig_a_extn(const struct hal_rx_vht_sig_a_info *vht_sig,
					       struct hal_rx_mon_ppdu_info *ppdu_info);
void ath12k_wifi8_hal_mon_parse_he_sig_su_extn(const struct hal_rx_he_sig_a_su_info *he_sig_a,
					       struct hal_rx_mon_ppdu_info *ppdu_info);
void ath12k_wifi8_hal_mon_parse_he_sig_mu_extn(const struct hal_rx_he_sig_a_mu_dl_info *he_sig_a_mu_dl,
					       struct hal_rx_mon_ppdu_info *ppdu_info);
void ath12k_wifi8_hal_mon_parse_eht_non_mumimo_user_extn(const struct hal_eht_sig_non_mu_mimo *user,
							 struct hal_rx_mon_ppdu_info *ppdu_info);
void ath12k_wifi8_hal_mon_parse_eht_mumimo_user_extn(const struct hal_eht_sig_mu_mimo *user,
						     struct hal_rx_mon_ppdu_info *ppdu_info);
void ath12k_wifi8_hal_mon_parse_uhr_non_mumimo_user_extn(const struct hal_uhr_sig_non_mu_mimo *user,
							 struct hal_rx_mon_ppdu_info *ppdu_info);
void ath12k_wifi8_hal_mon_parse_uhr_mumimo_user_extn(const struct hal_uhr_sig_mu_mimo *user,
						     struct hal_rx_mon_ppdu_info *ppdu_info);

/* Ops-table entry points */
void ath12k_wifi8_dp_mon_rx_undecoded_metadata_config_filter(struct ath12k_pdev_dp *dp_pdev);
void ath12k_wifi8_dp_mon_rx_undecoded_metadata_reset_filter(struct ath12k_pdev_dp *dp_pdev);
int  ath12k_wifi8_dp_mon_rx_undecoded_metadata_capture_set(struct ath12k_pdev_dp *dp_pdev,
							   u32 value);
int  ath12k_wifi8_dp_mon_rx_undecoded_phy_err_mask_set(struct ath12k_pdev_dp *dp_pdev,
						       u32 mask, u32 mask_cont,
						       u32 mask_cont2, u32 mask_cont3);
void ath12k_wifi8_dp_mon_rx_htt_phy_err_filter_cfg(void *ptr,
						   struct htt_rx_ring_tlv_filter *tlv_filter);
void ath12k_wifi8_dp_mon_rx_prepare_ppdu_info(struct ath12k_pdev_dp *pdev_dp,
					      struct hal_rx_mon_ppdu_info *ppdu_info);

/* Reap-path check + handle */
bool ath12k_wifi8_dp_rx_mon_check_phyrx_abort(struct ath12k_pdev_dp *pdev_dp,
					      struct hal_rx_mon_ppdu_info *ppdu_info);
void ath12k_wifi8_dp_rx_ppdu_undecoded_metadata(struct ath12k_pdev_dp *pdev_dp,
						struct hal_rx_mon_ppdu_info *ppdu_info);

#endif /* DP_MON2_EXTN_H */
