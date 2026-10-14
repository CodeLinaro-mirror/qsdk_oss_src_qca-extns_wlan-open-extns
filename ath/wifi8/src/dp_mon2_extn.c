/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_mon2_extn.h"
#include "../../dp_mon_filter.h"

void ath12k_wifi8_hal_rx_get_phyrx_abort(struct hal_rx_mon_ppdu_info *ppdu_info,
					 const void *tlv_data)
{
	const struct ath12k_wifi8_rxpcu_ppdu_end_info *info = tlv_data;
	struct ath12k_phyrx_abort_info *abort_info =
		ppdu_info->ppdu_info_extn.phyrx_abort_info;

	if (!abort_info)
		return;

	if (le32_get_bits(info->info2,
			  ATH12K_WIFI8_RXPCU_PPDU_END_INFO2_PHYRX_ABORT_VALID)) {
		abort_info->phyrx_abort = 1;
		abort_info->phyrx_abort_reason =
			le32_get_bits(info->info11,
				      ATH12K_WIFI8_RXPCU_PPDU_END_INFO11_PHYRX_ABORT_REASON);
	}
}

void ath12k_wifi8_hal_mon_parse_l_sig_a_extn(const struct hal_rx_lsig_a_info *lsiga,
					     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_phyrx_abort_info *abort_info =
		ppdu_info->ppdu_info_extn.phyrx_abort_info;
	u32 info0 = __le32_to_cpu(lsiga->info0);

	if (!abort_info)
		return;

	abort_info->l_sig_length   = u32_get_bits(info0, HAL_RX_LSIG_A_INFO_INFO0_LEN);
	abort_info->l_sig_a_parity = u32_get_bits(info0, ATH12K_WIFI8_L_SIG_A_INFO0_PARITY);
	abort_info->l_sig_a_pkt_type = u32_get_bits(info0, HAL_RX_LSIG_A_INFO_INFO0_PKT_TYPE);
}

void ath12k_wifi8_hal_mon_parse_ht_sig_extn(const struct hal_rx_ht_sig_info *ht_sig,
					    struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_phyrx_abort_info *abort_info =
		ppdu_info->ppdu_info_extn.phyrx_abort_info;
	u32 info0 = __le32_to_cpu(ht_sig->info0);
	u32 info1 = __le32_to_cpu(ht_sig->info1);

	if (!abort_info)
		return;

	abort_info->ht_length	 = u32_get_bits(info0, ATH12K_WIFI8_HT_SIG_INFO0_LENGTH);
	abort_info->smoothing	 = u32_get_bits(info1, ATH12K_WIFI8_HT_SIG_INFO1_SMOOTHING);
	abort_info->not_sounding = u32_get_bits(info1, ATH12K_WIFI8_HT_SIG_INFO1_NOT_SOUNDING);
	abort_info->aggregation	 = u32_get_bits(info1, ATH12K_WIFI8_HT_SIG_INFO1_AGGREGATION);
	abort_info->ht_stbc	 = u32_get_bits(info1, HAL_RX_HT_SIG_INFO_INFO1_STBC);
	abort_info->ht_crc	 = u32_get_bits(info1, ATH12K_WIFI8_HT_SIG_INFO1_CRC);
}

void ath12k_wifi8_hal_mon_parse_vht_sig_a_extn(const struct hal_rx_vht_sig_a_info *vht_sig,
					       struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_phyrx_abort_info *abort_info =
		ppdu_info->ppdu_info_extn.phyrx_abort_info;
	u32 info0 = __le32_to_cpu(vht_sig->info0);
	u32 info1 = __le32_to_cpu(vht_sig->info1);

	if (!abort_info)
		return;

	abort_info->vht_no_txop_ps =
		u32_get_bits(info0, ATH12K_WIFI8_VHT_SIG_A_INFO0_TXOP_PS_NOT_ALLOWED);
	abort_info->vht_crc = u32_get_bits(info1, ATH12K_WIFI8_VHT_SIG_A_INFO1_CRC);
}

void ath12k_wifi8_hal_mon_parse_he_sig_su_extn(const struct hal_rx_he_sig_a_su_info *he_sig_a,
					       struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_phyrx_abort_info *abort_info =
		ppdu_info->ppdu_info_extn.phyrx_abort_info;
	u32 info1 = __le32_to_cpu(he_sig_a->info1);

	if (!abort_info)
		return;

	abort_info->he_sig_a_su_crc =
		u32_get_bits(info1, ATH12K_WIFI8_HE_SIG_A_SU_INFO1_CRC);
}

void ath12k_wifi8_hal_mon_parse_he_sig_mu_extn(const struct hal_rx_he_sig_a_mu_dl_info *he_sig_a_mu_dl,
					       struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_phyrx_abort_info *abort_info =
		ppdu_info->ppdu_info_extn.phyrx_abort_info;
	u32 info1 = __le32_to_cpu(he_sig_a_mu_dl->info1);

	if (!abort_info)
		return;

	abort_info->he_sig_a_mu_dl_crc =
		u32_get_bits(info1, ATH12K_WIFI8_HE_SIG_A_MU_DL_INFO1_CRC);
}

void ath12k_wifi8_hal_mon_parse_eht_non_mumimo_user_extn(const struct hal_eht_sig_non_mu_mimo *user,
							 struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_phyrx_abort_info *abort_info =
		ppdu_info->ppdu_info_extn.phyrx_abort_info;
	u32 info0 = __le32_to_cpu(user->info0);

	if (!abort_info)
		return;

	abort_info->eht_sig_a_su_crc =
		u32_get_bits(info0, HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_CRC);
}

void ath12k_wifi8_hal_mon_parse_eht_mumimo_user_extn(const struct hal_eht_sig_mu_mimo *user,
						     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_phyrx_abort_info *abort_info =
		ppdu_info->ppdu_info_extn.phyrx_abort_info;
	u32 info0 = __le32_to_cpu(user->info0);

	if (!abort_info)
		return;

	abort_info->eht_sig_a_mu_dl_crc =
		u32_get_bits(info0, HAL_RX_EHT_SIG_MUMIMO_USER_INFO0_CRC);
}

void ath12k_wifi8_hal_mon_parse_uhr_non_mumimo_user_extn(const struct hal_uhr_sig_non_mu_mimo *user,
							 struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_phyrx_abort_info *abort_info =
		ppdu_info->ppdu_info_extn.phyrx_abort_info;
	u32 info0 = __le32_to_cpu(user->info0);

	if (!abort_info)
		return;

	abort_info->uhr_sig_a_su_crc =
		u32_get_bits(info0, HAL_RX_UHR_SIG_NON_MUMIMO_USER_INFO0_CRC);
}

void ath12k_wifi8_hal_mon_parse_uhr_mumimo_user_extn(const struct hal_uhr_sig_mu_mimo *user,
						     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_phyrx_abort_info *abort_info =
		ppdu_info->ppdu_info_extn.phyrx_abort_info;
	u32 info0 = __le32_to_cpu(user->info0);

	if (!abort_info)
		return;

	abort_info->uhr_sig_a_mu_dl_crc =
		u32_get_bits(info0, HAL_RX_UHR_SIG_MUMIMO_USER_INFO0_CRC);
}

static int
ath12k_wifi8_dp_mon_undecoded_alloc(struct ath12k_pdev_mon_dp_extn *extn)
{
	if (!extn->undecoded_meta) {
		extn->undecoded_meta =
			kzalloc(sizeof(*extn->undecoded_meta), GFP_KERNEL);
		if (!extn->undecoded_meta) {
			ath12k_err(NULL, "undecoded: failed to allocate undecoded_meta\n");
			return -ENOMEM;
		}
	}
	if (!extn->phyrx_abort_info) {
		extn->phyrx_abort_info =
			kzalloc(sizeof(*extn->phyrx_abort_info), GFP_KERNEL);
		if (!extn->phyrx_abort_info) {
			ath12k_err(NULL, "undecoded: failed to allocate phyrx_abort_info\n");
			kfree(extn->undecoded_meta);
			extn->undecoded_meta = NULL;
			return -ENOMEM;
		}
	}
	return 0;
}

static void
ath12k_wifi8_dp_mon_undecoded_free(struct ath12k_pdev_mon_dp_extn *extn)
{
	kfree(extn->phyrx_abort_info);
	extn->phyrx_abort_info = NULL;
	kfree(extn->undecoded_meta);
	extn->undecoded_meta = NULL;
}

void
ath12k_wifi8_dp_mon_rx_undecoded_metadata_config_filter(struct ath12k_pdev_dp *dp_pdev)
{
	struct ath12k_pdev_mon_dp *dp_mon_pdev = dp_pdev->dp_mon_pdev;
	struct ath12k_pdev_mon_dp_extn *mon_dp_extn = &dp_mon_pdev->pdev_mon_dp_extn;
	struct ath12k_dp *dp = dp_pdev->dp;
	struct dp_mon_rx_filter rx_filter = {0};
	struct htt_rx_ring_tlv_filter *rx_tlv_filter;
	enum dp_mon_filter_mode mode = DP_MON_FILTER_UNDECODED_METADATA_CAPTURE_MODE;
	enum dp_mon_filter_srng_type srng_type =
				DP_MON_FILTER_SRNG_TYPE_RXMON_DEST;

	/* The merger supplies monitor TLVs from the monitor-mode slot. */
	rx_filter.valid = true;
	rx_tlv_filter = &rx_filter.rx_tlv_filter;

	rx_tlv_filter->fp_phy_err = 1;
	rx_tlv_filter->fp_phy_err_buf_src = ATH12K_WIFI8_FP_PHY_ERR_BUF_SRC_SW2RXDMA;
	rx_tlv_filter->fp_phy_err_buf_dest = ATH12K_WIFI8_FP_PHY_ERR_BUF_DEST_RXDMA2SW;
	rx_tlv_filter->phy_err_mask = mon_dp_extn->undecoded_meta->phyrx_error_mask;
	rx_tlv_filter->phy_err_mask_cont = mon_dp_extn->undecoded_meta->phyrx_error_mask_cont;
	rx_tlv_filter->phy_err_filter_valid = true;

	ath12k_dp_mon_rx_display_filters(dp, mode, &rx_filter);
	dp_mon_pdev->rx_filter[mode][srng_type] = rx_filter;
}
EXPORT_SYMBOL(ath12k_wifi8_dp_mon_rx_undecoded_metadata_config_filter);

void
ath12k_wifi8_dp_mon_rx_undecoded_metadata_reset_filter(struct ath12k_pdev_dp *dp_pdev)
{
	struct ath12k_pdev_mon_dp *dp_mon_pdev = dp_pdev->dp_mon_pdev;
	struct ath12k_pdev_mon_dp_extn *mon_dp_extn = &dp_mon_pdev->pdev_mon_dp_extn;
	struct ath12k_dp *dp = dp_pdev->dp;
	struct dp_mon_rx_filter rx_filter = {0};
	struct htt_rx_ring_tlv_filter *rx_tlv_filter;
	enum dp_mon_filter_mode mode = DP_MON_FILTER_UNDECODED_METADATA_CAPTURE_MODE;
	enum dp_mon_filter_srng_type srng_type =
				DP_MON_FILTER_SRNG_TYPE_RXMON_DEST;

	rx_tlv_filter = &rx_filter.rx_tlv_filter;

	rx_tlv_filter->fp_phy_err = 0;
	rx_tlv_filter->fp_phy_err_buf_src = ATH12K_WIFI8_FP_PHY_ERR_BUF_SRC_NONE;
	rx_tlv_filter->fp_phy_err_buf_dest = ATH12K_WIFI8_FP_PHY_ERR_BUF_DEST_RELEASING;
	rx_tlv_filter->phy_err_mask = 0;
	rx_tlv_filter->phy_err_mask_cont = 0;
	rx_tlv_filter->phy_err_filter_valid = true;

	if (mon_dp_extn->undecoded_meta) {
		mon_dp_extn->undecoded_meta->phyrx_error_mask = 0;
		mon_dp_extn->undecoded_meta->phyrx_error_mask_cont = 0;
	}

	ath12k_dp_mon_rx_display_filters(dp, mode, &rx_filter);
	dp_mon_pdev->rx_filter[mode][srng_type] = rx_filter;
}
EXPORT_SYMBOL(ath12k_wifi8_dp_mon_rx_undecoded_metadata_reset_filter);

int
ath12k_wifi8_dp_mon_rx_undecoded_metadata_capture_set(struct ath12k_pdev_dp *dp_pdev,
						      u32 value)
{
	struct ath12k_pdev_mon_dp_extn *mon_dp_extn;
	const struct ath12k_dp_arch_mon_ops *mon_ops;
	int ret;

	if (!dp_pdev->dp_mon_pdev)
		return -EINVAL;

	mon_dp_extn = &dp_pdev->dp_mon_pdev->pdev_mon_dp_extn;
	mon_ops = ath12k_dp_mon_ops_get(dp_pdev->dp);

	if (value) {
		ret = ath12k_wifi8_dp_mon_undecoded_alloc(mon_dp_extn);
		if (ret)
			return ret;
		mon_dp_extn->undecoded_meta->undecoded_metadata_capture = true;
		if (mon_ops && mon_ops->rx_undecoded_metadata_config_filter)
			mon_ops->rx_undecoded_metadata_config_filter(dp_pdev);
	} else {
		if (mon_dp_extn->undecoded_meta)
			mon_dp_extn->undecoded_meta->undecoded_metadata_capture = false;
		if (mon_ops && mon_ops->rx_undecoded_metadata_reset_filter)
			mon_ops->rx_undecoded_metadata_reset_filter(dp_pdev);
		ath12k_wifi8_dp_mon_undecoded_free(mon_dp_extn);
	}
	return 0;
}
EXPORT_SYMBOL(ath12k_wifi8_dp_mon_rx_undecoded_metadata_capture_set);

int
ath12k_wifi8_dp_mon_rx_undecoded_phy_err_mask_set(struct ath12k_pdev_dp *dp_pdev,
						  u32 mask, u32 mask_cont)
{
	struct ath12k_pdev_mon_dp_extn *mon_dp_extn;
	const struct ath12k_dp_arch_mon_ops *mon_ops;
	int ret;

	if (!dp_pdev->dp_mon_pdev)
		return -EINVAL;

	mon_dp_extn = &dp_pdev->dp_mon_pdev->pdev_mon_dp_extn;

	ret = ath12k_wifi8_dp_mon_undecoded_alloc(mon_dp_extn);
	if (ret)
		return ret;

	mon_dp_extn->undecoded_meta->phyrx_error_mask = mask;
	mon_dp_extn->undecoded_meta->phyrx_error_mask_cont = mask_cont;

	if (!mon_dp_extn->undecoded_meta->undecoded_metadata_capture)
		return 0;

	mon_ops = ath12k_dp_mon_ops_get(dp_pdev->dp);
	if (mon_ops && mon_ops->rx_undecoded_metadata_config_filter)
		mon_ops->rx_undecoded_metadata_config_filter(dp_pdev);
	return 0;
}
EXPORT_SYMBOL(ath12k_wifi8_dp_mon_rx_undecoded_phy_err_mask_set);

void
ath12k_wifi8_dp_mon_rx_htt_phy_err_filter_cfg(void *ptr,
					      struct htt_rx_ring_tlv_filter *tlv_filter)
{
	struct htt_rx_ring_selection_cfg_cmd *cmd = ptr;

	if (!tlv_filter->phy_err_filter_valid)
		return;

	/* On wifi8 only the routing bits (info2) are honored by FW; the
	 * phy_err_mask/_cont words are inert (reason filtering is host-side).
	 * They are still written for parity with wifi6 / other chips.
	 */
	cmd->info2 |= le32_encode_bits(tlv_filter->fp_phy_err,
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO2_FP_PHY_ERR);
	cmd->info2 |= le32_encode_bits(tlv_filter->fp_phy_err_buf_src,
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO2_FP_PHY_ERR_BUF_SRC);
	cmd->info2 |= le32_encode_bits(tlv_filter->fp_phy_err_buf_dest,
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO2_FP_PHY_ERR_BUF_DEST);
	cmd->phy_err_mask = le32_encode_bits(tlv_filter->phy_err_mask,
					     HTT_RX_RING_SELECTION_CFG_CMD_PHY_ERR_MASK);
	cmd->phy_err_mask_cont = le32_encode_bits(tlv_filter->phy_err_mask_cont,
						  HTT_RX_RING_SELECTION_CFG_CMD_PHY_ERR_MASK_CONT);
}
EXPORT_SYMBOL(ath12k_wifi8_dp_mon_rx_htt_phy_err_filter_cfg);

void
ath12k_wifi8_dp_mon_rx_prepare_ppdu_info(struct ath12k_pdev_dp *pdev_dp,
					 struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_pdev_mon_dp *dp_mon_pdev = pdev_dp->dp_mon_pdev;
	struct ath12k_phyrx_abort_info *abort_info;

	if (!dp_mon_pdev)
		return;

	abort_info = dp_mon_pdev->pdev_mon_dp_extn.phyrx_abort_info;
	ppdu_info->ppdu_info_extn.phyrx_abort_info = abort_info;

	if (abort_info)
		memset(abort_info, 0, sizeof(*abort_info));
}
EXPORT_SYMBOL(ath12k_wifi8_dp_mon_rx_prepare_ppdu_info);

static inline bool
ath12k_wifi8_dp_rx_is_valid_undecoded_frame(u64 err_mask, u8 err_code)
{
	if (err_code < ATH12K_WIFI6_PHYRX_ERR_MAX &&
	    (err_mask & (1ULL << err_code)))
		return true;
	return false;
}

bool
ath12k_wifi8_dp_rx_mon_check_phyrx_abort(struct ath12k_pdev_dp *pdev_dp,
					 struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_pdev_mon_dp *dp_mon_pdev = pdev_dp->dp_mon_pdev;
	struct ath12k_pdev_mon_dp_extn *mon_dp_extn;

	if (!dp_mon_pdev)
		return false;

	mon_dp_extn = &dp_mon_pdev->pdev_mon_dp_extn;

	if (!mon_dp_extn->undecoded_meta ||
	    !ppdu_info->ppdu_info_extn.phyrx_abort_info)
		return false;

	return (mon_dp_extn->undecoded_meta->undecoded_metadata_capture &&
		ppdu_info->ppdu_info_extn.phyrx_abort_info->phyrx_abort);
}
EXPORT_SYMBOL(ath12k_wifi8_dp_rx_mon_check_phyrx_abort);

void
ath12k_wifi8_dp_rx_ppdu_undecoded_metadata(struct ath12k_pdev_dp *pdev_dp,
					   struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_pdev_mon_dp *dp_mon_pdev = pdev_dp->dp_mon_pdev;
	struct ath12k_pdev_mon_dp_extn *mon_dp_extn;
	u64 mask64;
	u8 abort_reason;

	if (!dp_mon_pdev)
		return;

	mon_dp_extn = &dp_mon_pdev->pdev_mon_dp_extn;

	mask64 = ((u64)mon_dp_extn->undecoded_meta->phyrx_error_mask_cont <<
		  ATH12K_WIFI6_PHYRX_ERR_MASK_CONT_SHIFT) |
		 mon_dp_extn->undecoded_meta->phyrx_error_mask;
	abort_reason = ppdu_info->ppdu_info_extn.phyrx_abort_info->phyrx_abort_reason;

	/* wifi8: FW does not gate reasons (phy_err_mask inert) - filter here. */
	if (!ath12k_wifi8_dp_rx_is_valid_undecoded_frame(mask64, abort_reason))
		return;

	mon_dp_extn->undecoded_meta->rx_undecoded_count++;
	if (abort_reason < ATH12K_WIFI6_PHYRX_ERR_MAX)
		mon_dp_extn->undecoded_meta->rx_undecoded_error[abort_reason]++;

	/* TODO: Report undecoded frame metadata to userspace via cfg80211 vendor
	 * event or Linux notifier chain. This should include:
	 * - PHY abort reason (ppdu_info->ppdu_info_extn.phyrx_abort_reason)
	 * - SIG/CRC fields from ppdu_info->ppdu_info_extn
	 * - Other relevant PPDU metadata (channel, timestamp, RSSI, etc.)
	 */
}
EXPORT_SYMBOL(ath12k_wifi8_dp_rx_ppdu_undecoded_metadata);
