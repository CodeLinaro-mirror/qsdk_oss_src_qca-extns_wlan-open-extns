// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hal_rx.h"
#include "hal_rx_desc.h"
#include "../../hal_mon_cmn.h"
#include "hal_mon.h"
#include "hal_qcn9074.h"
#include "hal_mon_qcn9074.h"
#include "dp_rx.h"

u32 ath12k_wifi6_hal_mon_rx_mpdu_start_wmask_get(void)
{
	return RX_MON_MPDU_START_WMASK;
}

u32 ath12k_wifi6_hal_mon_rx_mpdu_end_wmask_get(void)
{
	return RX_MON_MPDU_END_WMASK;
}

u32 ath12k_wifi6_hal_mon_rx_msdu_end_wmask_get(void)
{
	return RX_MON_MSDU_END_WMASK;
}

static __always_inline void
ath12k_wifi6_hal_mon_get_mac_addr(u32 addr_l32, u16 addr_h16, u8 *addr)
{
	memcpy(addr, &addr_l32, 4);
	memcpy(addr + 4, &addr_h16, ETH_ALEN - 4);
}

static __always_inline void
ath12k_wifi6_hal_mon_get_nrp_mac_addr(u16 addr_l16, u32 addr_h32, u8 *addr)
{
	memcpy(addr, &addr_l16, 2);
	memcpy(addr + 2, &addr_h32, ETH_ALEN - 2);
}

static __always_inline void
ath12k_wifi6_hal_mon_populate_mu_user_info(struct hal_rx_mon_ppdu_info *ppdu_info,
					   struct hal_rx_user_status *rx_user_status)
{
	rx_user_status->ast_index = ppdu_info->ast_index;
	rx_user_status->tid = ppdu_info->tid;
	rx_user_status->tcp_ack_msdu_count =
		ppdu_info->tcp_ack_msdu_count;
	rx_user_status->tcp_msdu_count =
		ppdu_info->tcp_msdu_count;
	rx_user_status->udp_msdu_count =
		ppdu_info->udp_msdu_count;
	rx_user_status->other_msdu_count =
		ppdu_info->other_msdu_count;
	rx_user_status->frame_control = ppdu_info->frame_control;
	rx_user_status->frame_control_info_valid =
		ppdu_info->frame_control_info_valid;
	rx_user_status->data_sequence_control_info_valid =
		ppdu_info->data_sequence_control_info_valid;
	rx_user_status->first_data_seq_ctrl =
		ppdu_info->first_data_seq_ctrl;
	rx_user_status->preamble_type = ppdu_info->preamble_type;
	rx_user_status->ht_flags = ppdu_info->ht_flags;
	rx_user_status->vht_flags = ppdu_info->vht_flags;
	rx_user_status->he_flags = ppdu_info->he_flags;
	rx_user_status->rs_flags = ppdu_info->rs_flags;

	rx_user_status->mpdu_cnt_fcs_ok =
		ppdu_info->num_mpdu_fcs_ok;
	rx_user_status->mpdu_cnt_fcs_err =
		ppdu_info->num_mpdu_fcs_err;
}

static __always_inline void
ath12k_wifi6_hal_mon_parse_vht_sig_a(const void *tlv_data,
				     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 value = 0;
	u8 group_id = 0;

	uint8_t *vht_sig_a_info = (uint8_t *)tlv_data +
			HAL_RX_OFFSET(UNIFIED_PHYRX_VHT_SIG_A_0,
				      VHT_SIG_A_INFO_PHYRX_VHT_SIG_A_INFO_DETAILS);

	value = HAL_RX_GET(vht_sig_a_info, VHT_SIG_A_INFO_1, SU_MU_CODING);
	ppdu_info->ldpc = (value == HAL_RX_SU_MU_CODING_LDPC) ? 1 : 0;
	group_id = HAL_RX_GET(vht_sig_a_info, VHT_SIG_A_INFO_0, GROUP_ID);
	ppdu_info->vht_flag_values5 = group_id;
	ppdu_info->mcs = HAL_RX_GET(vht_sig_a_info, VHT_SIG_A_INFO_1, MCS);
	ppdu_info->sgi = HAL_RX_GET(vht_sig_a_info,
				    VHT_SIG_A_INFO_1, GI_SETTING);
	ppdu_info->is_stbc = HAL_RX_GET(vht_sig_a_info, VHT_SIG_A_INFO_0, STBC);
	value =  HAL_RX_GET(vht_sig_a_info, VHT_SIG_A_INFO_0, N_STS);
	value = value & VHT_SIG_SU_NSS_MASK;
	if (ppdu_info->is_stbc && (value > 0))
		value = ((value + 1) >> 1) - 1;
	ppdu_info->nss = ((value & VHT_SIG_SU_NSS_MASK) + 1);

	ppdu_info->vht_flag_values3[0] = (((ppdu_info->mcs) << 4) | ppdu_info->nss);
	ppdu_info->bw = HAL_RX_GET(vht_sig_a_info, VHT_SIG_A_INFO_0, BANDWIDTH);
	ppdu_info->vht_flag_values2 = ppdu_info->bw;
	ppdu_info->vht_flag_values4 = HAL_RX_GET(vht_sig_a_info,
						 VHT_SIG_A_INFO_1, SU_MU_CODING);

	ppdu_info->beamformed = HAL_RX_GET(vht_sig_a_info, VHT_SIG_A_INFO_1, BEAMFORMED);
	if (group_id == 0 || group_id == 63)
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
	else
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_MIMO;
}

static __always_inline void
ath12k_wifi6_hal_mon_parse_ht_sig(const void *tlv_data,
				  struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 value = 0;
	uint8_t *ht_sig_info = (uint8_t *)tlv_data +
			HAL_RX_OFFSET(UNIFIED_PHYRX_HT_SIG_0,
				      HT_SIG_INFO_PHYRX_HT_SIG_INFO_DETAILS);

	value = HAL_RX_GET(ht_sig_info, HT_SIG_INFO_1, FEC_CODING);
	ppdu_info->ldpc = (value == HAL_RX_SU_MU_CODING_LDPC) ? 1 : 0;
	ppdu_info->mcs = HAL_RX_GET(ht_sig_info, HT_SIG_INFO_0, MCS);
	ppdu_info->bw = HAL_RX_GET(ht_sig_info, HT_SIG_INFO_0, CBW);
	ppdu_info->sgi = HAL_RX_GET(ht_sig_info, HT_SIG_INFO_1, SHORT_GI);
	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
	ppdu_info->nss = (ppdu_info->mcs >> 3) + 1;
	ppdu_info->is_stbc = HAL_RX_GET(ht_sig_info, HT_SIG_INFO_1, STBC);
}

static __always_inline u8
ath12k_wifi6_hal_mon_map_legacy_rate_to_hw_rate(u8 rate)
{
	u8 ath12k_rate;

	/* Map hal_rx_legacy_rate to ath12k_hw_rate_cck */
	switch (rate) {
	case HAL_RX_LEGACY_RATE_LP_1_MBPS:
		ath12k_rate = ATH12K_HW_RATE_CCK_LP_1M;
		break;
	case HAL_RX_LEGACY_RATE_LP_2_MBPS:
		ath12k_rate = ATH12K_HW_RATE_CCK_LP_2M;
		break;
	case HAL_RX_LEGACY_RATE_LP_5_5_MBPS:
		ath12k_rate = ATH12K_HW_RATE_CCK_LP_5_5M;
		break;
	case HAL_RX_LEGACY_RATE_LP_11_MBPS:
		ath12k_rate = ATH12K_HW_RATE_CCK_LP_11M;
		break;
	case HAL_RX_LEGACY_RATE_SP_2_MBPS:
		ath12k_rate = ATH12K_HW_RATE_CCK_SP_2M;
		break;
	case HAL_RX_LEGACY_RATE_SP_5_5_MBPS:
		ath12k_rate = ATH12K_HW_RATE_CCK_SP_5_5M;
		break;
	case HAL_RX_LEGACY_RATE_SP_11_MBPS:
		ath12k_rate = ATH12K_HW_RATE_CCK_SP_11M;
		break;
	default:
		ath12k_rate = rate;
		break;
	}

	return ath12k_rate;
}

static __always_inline void
ath12k_wifi6_hal_mon_parse_l_sig_b(const void *tlv_data,
				   struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 value = 0;
	u8 rate;
	uint8_t *l_sig_b_info = (uint8_t *)tlv_data +
			HAL_RX_OFFSET(UNIFIED_PHYRX_L_SIG_B_0,
				      L_SIG_B_INFO_PHYRX_L_SIG_B_INFO_DETAILS);

	value = HAL_RX_GET(l_sig_b_info, L_SIG_B_INFO_0, RATE);
	switch (value) {
	case 1:
		rate = HAL_RX_LEGACY_RATE_LP_1_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS3;
		break;
	case 2:
		rate = HAL_RX_LEGACY_RATE_LP_2_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS2;
		break;
	case 3:
		rate = HAL_RX_LEGACY_RATE_LP_5_5_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS1;
		break;
	case 4:
		rate = HAL_RX_LEGACY_RATE_LP_11_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS0;
		break;
	case 5:
		rate = HAL_RX_LEGACY_RATE_SP_2_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS6;
		break;
	case 6:
		rate = HAL_RX_LEGACY_RATE_SP_5_5_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS5;
		break;
	case 7:
		rate = HAL_RX_LEGACY_RATE_SP_11_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS4;
		break;
	default:
		rate = HAL_RX_LEGACY_RATE_INVALID;
		break;
	}

	ppdu_info->rate = ath12k_wifi6_hal_mon_map_legacy_rate_to_hw_rate(rate);
	ppdu_info->cck_flag = 1;
	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
}

static __always_inline void
ath12k_wifi6_hal_mon_parse_l_sig_a(const void *tlv_data,
				   struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 value = 0;
	u8 rate;
	uint8_t *l_sig_a_info = (uint8_t *)tlv_data +
			HAL_RX_OFFSET(UNIFIED_PHYRX_L_SIG_A_0,
				      L_SIG_A_INFO_PHYRX_L_SIG_A_INFO_DETAILS);

	value = HAL_RX_GET(l_sig_a_info, L_SIG_A_INFO_0, RATE);
	switch (value) {
	case 8:
		rate = HAL_RX_LEGACY_RATE_OFDM_48_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS0;
		break;
	case 9:
		rate = HAL_RX_LEGACY_RATE_OFDM_24_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS1;
		break;
	case 10:
		rate = HAL_RX_LEGACY_RATE_OFDM_12_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS2;
		break;
	case 11:
		rate = HAL_RX_LEGACY_RATE_OFDM_6_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS3;
		break;
	case 12:
		rate = HAL_RX_LEGACY_RATE_OFDM_54_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS4;
		break;
	case 13:
		rate = HAL_RX_LEGACY_RATE_OFDM_36_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS5;
		break;
	case 14:
		rate = HAL_RX_LEGACY_RATE_OFDM_18_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS6;
		break;
	case 15:
		rate = HAL_RX_LEGACY_RATE_OFDM_9_MBPS;
		ppdu_info->mcs = HAL_LEGACY_MCS7;
		break;
	default:
		rate = HAL_RX_LEGACY_RATE_OFDM_INVALID;
		break;
	}

	ppdu_info->rate = rate;
	ppdu_info->ofdm_flag = 1;
	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
}

static __always_inline void
ath12k_wifi6_hal_mon_parse_he_sig_b2_ofdma(const struct hal_rx_he_sig_b2_ofdma_info *ofdma,
					   struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 info0, value;

	info0 = __le32_to_cpu(ofdma->info0);

	ppdu_info->he_data1 |= HE_MCS_KNOWN | HE_DCM_KNOWN | HE_CODING_KNOWN;

	/* HE-data2 */
	ppdu_info->he_data2 |= HE_TXBF_KNOWN;

	ppdu_info->mcs = u32_get_bits(info0, HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_MCS);
	value = ppdu_info->mcs << HE_TRANSMIT_MCS_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_DCM);
	value = value << HE_DCM_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_CODING);
	ppdu_info->ldpc = value;
	value = value << HE_CODING_SHIFT;
	ppdu_info->he_data3 |= value;

	/* HE-data4 */
	value = u32_get_bits(info0, HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_ID);
	value = value << HE_STA_ID_SHIFT;
	ppdu_info->he_data4 |= value;

	ppdu_info->nss = u32_get_bits(info0,
				      HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_NSTS) + 1;
	ppdu_info->beamformed = u32_get_bits(info0,
					     HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_TXBF);
}

static __always_inline void
ath12k_wifi6_hal_mon_parse_he_sig_b2_mu(const struct hal_rx_he_sig_b2_mu_info *he_sig_b2_mu,
					struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 info0, value;

	info0 = __le32_to_cpu(he_sig_b2_mu->info0);

	ppdu_info->he_data1 |= HE_MCS_KNOWN | HE_CODING_KNOWN;

	ppdu_info->mcs = u32_get_bits(info0, HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_MCS);
	value = ppdu_info->mcs << HE_TRANSMIT_MCS_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_CODING);
	ppdu_info->ldpc = value;
	value = value << HE_CODING_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_ID);
	value = value << HE_STA_ID_SHIFT;
	ppdu_info->he_data4 |= value;

	ppdu_info->nss = u32_get_bits(info0, HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_NSTS) + 1;
}

static __always_inline void
ath12k_wifi6_hal_mon_parse_he_sig_b1_mu(const struct hal_rx_he_sig_b1_mu_info *he_sig_b1_mu,
					struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 info0 = __le32_to_cpu(he_sig_b1_mu->info0);
	u16 ru_tones;

	ru_tones = u32_get_bits(info0,
				HAL_RX_HE_SIG_B1_MU_INFO_INFO0_RU_ALLOCATION);
	ppdu_info->ru_alloc = ath12k_he_ru_tones_to_nl80211_he_ru_alloc(ru_tones);
	ppdu_info->he_RU[0] = ru_tones;
}

static __always_inline void
ath12k_wifi6_hal_mon_parse_he_sig_mu(const void *tlv_data,
				     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 value = 0;
	u8 he_stbc = 0;
	u16 he_gi = 0, he_ltf = 0;

	uint8_t *he_sig_a_mu_dl_info = (uint8_t *)tlv_data +
		HAL_RX_OFFSET(UNIFIED_PHYRX_HE_SIG_A_MU_DL_0,
			      HE_SIG_A_MU_DL_INFO_PHYRX_HE_SIG_A_MU_DL_INFO_DETAILS);

	ppdu_info->he_mu_flags = 1;

	/* HE Flags */
	/*data1*/
	ppdu_info->he_data1 = HE_MU_FORMAT_TYPE;
	ppdu_info->he_data1 |=
			HE_BSS_COLOR_KNOWN |
			HE_DL_UL_KNOWN |
			HE_LDPC_EXTRA_SYMBOL_KNOWN |
			HE_STBC_KNOWN |
			HE_DATA_BW_RU_KNOWN |
			HE_DOPPLER_KNOWN;

	/* data2 */
	ppdu_info->he_data2 =
			HE_GI_KNOWN |
			HE_LTF_SYMBOLS_KNOWN |
			HE_PRE_FEC_PADDING_KNOWN |
			HE_PE_DISAMBIGUITY_KNOWN |
			HE_TXOP_KNOWN |
			HE_MIDABLE_PERIODICITY_KNOWN;

	/* data3 */
	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_0, BSS_COLOR_ID);
	ppdu_info->he_data3 = value;

	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_0, DL_UL_FLAG);
	value = value << HE_DL_UL_SHIFT;
	ppdu_info->he_data3 |= value;

	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_1,
			   LDPC_EXTRA_SYMBOL);
	value = value << HE_LDPC_EXTRA_SYMBOL_SHIFT;
	ppdu_info->he_data3 |= value;

	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_1, STBC);
	he_stbc = value;
	value = value << HE_STBC_SHIFT;
	ppdu_info->he_data3 |= value;

	/* data4 */
	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_0,
			   SPATIAL_REUSE);
	ppdu_info->he_data4 = value;

	/* data5 */
	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_0, TRANSMIT_BW);
	ppdu_info->he_data5 = value;
	ppdu_info->bw = value;

	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_0, CP_LTF_SIZE);

	switch (value) {
	case 0:
		he_gi = HE_GI_0_8;
		he_ltf = HE_LTF_4_X;
		break;
	case 1:
		he_gi = HE_GI_0_8;
		he_ltf = HE_LTF_2_X;
		break;
	case 2:
		he_gi = HE_GI_1_6;
		he_ltf = HE_LTF_2_X;
		break;
	case 3:
		he_gi = HE_GI_3_2;
		he_ltf = HE_LTF_4_X;
		break;
	}

	ppdu_info->sgi = he_gi;
	ppdu_info->ltf_size = he_ltf;
	hal_get_radiotap_he_gi_ltf(&he_gi, &he_ltf);
	value = he_gi << HE_GI_SHIFT;
	ppdu_info->he_data5 |= value;

	value = he_ltf << HE_LTF_SIZE_SHIFT;
	ppdu_info->he_data5 |= value;

	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_1, NUM_LTF_SYMBOLS);
	value = (value << HE_LTF_SYM_SHIFT);
	ppdu_info->he_data5 |= value;

	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_1,
			   PACKET_EXTENSION_A_FACTOR);
	value = value << HE_PRE_FEC_PAD_SHIFT;
	ppdu_info->he_data5 |= value;

	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_1,
			   PACKET_EXTENSION_PE_DISAMBIGUITY);
	value = value << HE_PE_DISAMBIGUITY_SHIFT;
	ppdu_info->he_data5 |= value;

	/*data6*/
	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_0,
			   DOPPLER_INDICATION);
	value = value << HE_DOPPLER_SHIFT;
	ppdu_info->he_data6 |= value;

	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_1,
			   TXOP_DURATION);
	value = value << HE_TXOP_SHIFT;
	ppdu_info->he_data6 |= value;

	/* HE-MU Flags */
	/* HE-MU-flags1 */
	ppdu_info->he_flags1 =
		HE_SIG_B_MCS_KNOWN |
		HE_SIG_B_DCM_KNOWN |
		HE_SIG_B_COMPRESSION_FLAG_1_KNOWN |
		HE_SIG_B_SYM_NUM_KNOWN |
		HE_RU_0_KNOWN;

	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_0, MCS_OF_SIG_B);
	ppdu_info->he_flags1 |= value;
	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_0, DCM_OF_SIG_B);
	value = value << HE_DCM_FLAG_1_SHIFT;
	ppdu_info->he_flags1 |= value;

	/* HE-MU-flags2 */
	ppdu_info->he_flags2 = HE_BW_KNOWN;

	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_0, TRANSMIT_BW);
	ppdu_info->he_flags2 |= value;
	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_0, COMP_MODE_SIG_B);
	value = value << HE_SIG_B_COMPRESSION_FLAG_2_SHIFT;
	ppdu_info->he_flags2 |= value;
	value = HAL_RX_GET(he_sig_a_mu_dl_info, HE_SIG_A_MU_DL_INFO_0,
			   NUM_SIG_B_SYMBOLS);
	value = value - 1;
	value = value << HE_NUM_SIG_B_SYMBOLS_SHIFT;
	ppdu_info->he_flags2 |= value;

	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_MIMO;
}

static __always_inline void
ath12k_wifi6_hal_mon_parse_he_sig_su(const void *tlv_data,
				     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 value = 0;
	u8 he_dcm = 0, he_stbc = 0;
	u16 he_gi = 0, he_ltf = 0;
	uint8_t *he_sig_a_su_info = (uint8_t *)tlv_data +
			HAL_RX_OFFSET(UNIFIED_PHYRX_HE_SIG_A_SU_0,
				      HE_SIG_A_SU_INFO_PHYRX_HE_SIG_A_SU_INFO_DETAILS);

	ppdu_info->he_flags = 1;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_0,
			   FORMAT_INDICATION);
	if (value == 0)
		ppdu_info->he_data1 = HE_TRIG_FORMAT_TYPE;
	else
		ppdu_info->he_data1 = HE_SU_FORMAT_TYPE;

	ppdu_info->he_data1 |=
			HE_BSS_COLOR_KNOWN |
			HE_BEAM_CHANGE_KNOWN |
			HE_DL_UL_KNOWN |
			HE_MCS_KNOWN |
			HE_DCM_KNOWN |
			HE_CODING_KNOWN |
			HE_LDPC_EXTRA_SYMBOL_KNOWN |
			HE_STBC_KNOWN |
			HE_DATA_BW_RU_KNOWN |
			HE_DOPPLER_KNOWN;

	ppdu_info->he_data2 |=
			HE_GI_KNOWN |
			HE_TXBF_KNOWN |
			HE_PE_DISAMBIGUITY_KNOWN |
			HE_TXOP_KNOWN |
			HE_LTF_SYMBOLS_KNOWN |
			HE_PRE_FEC_PADDING_KNOWN |
			HE_MIDABLE_PERIODICITY_KNOWN;

	/* data3 */
	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_0, BSS_COLOR_ID);
	ppdu_info->he_data3 = value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_0, BEAM_CHANGE);
	value = value << HE_BEAM_CHANGE_SHIFT;
	ppdu_info->he_data3 |= value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_0, DL_UL_FLAG);
	value = value << HE_DL_UL_SHIFT;
	ppdu_info->he_data3 |= value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_0, TRANSMIT_MCS);
	ppdu_info->mcs = value;
	value = value << HE_TRANSMIT_MCS_SHIFT;
	ppdu_info->he_data3 |= value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_0, DCM);
	he_dcm = value;
	value = value << HE_DCM_SHIFT;
	ppdu_info->he_data3 |= value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_1, CODING);
	ppdu_info->ldpc = (value == HAL_RX_SU_MU_CODING_LDPC) ? 1 : 0;
	value = value << HE_CODING_SHIFT;
	ppdu_info->he_data3 |= value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_1, LDPC_EXTRA_SYMBOL);
	value = value << HE_LDPC_EXTRA_SYMBOL_SHIFT;
	ppdu_info->he_data3 |= value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_1, STBC);
	he_stbc = value;
	value = value << HE_STBC_SHIFT;
	ppdu_info->he_data3 |= value;

	/* data4 */
	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_0, SPATIAL_REUSE);
	ppdu_info->he_data4 = value;

	/* data5 */
	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_0, TRANSMIT_BW);
	ppdu_info->he_data5 = value;
	ppdu_info->bw = value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_0, CP_LTF_SIZE);
	switch (value) {
	case 0:
		he_gi = HE_GI_0_8;
		he_ltf = HE_LTF_1_X;
		break;
	case 1:
		he_gi = HE_GI_0_8;
		he_ltf = HE_LTF_2_X;
		break;
	case 2:
		he_gi = HE_GI_1_6;
		he_ltf = HE_LTF_2_X;
		break;
	case 3:
		if (he_dcm && he_stbc) {
			he_gi = HE_GI_0_8;
			he_ltf = HE_LTF_4_X;
		} else {
			he_gi = HE_GI_3_2;
			he_ltf = HE_LTF_4_X;
		}
		break;
	}
	ppdu_info->sgi = he_gi;
	hal_get_radiotap_he_gi_ltf(&he_gi, &he_ltf);
	value = he_gi << HE_GI_SHIFT;
	ppdu_info->he_data5 |= value;
	value = he_ltf << HE_LTF_SIZE_SHIFT;
	ppdu_info->ltf_size = he_ltf;
	ppdu_info->he_data5 |= value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_0, NSTS);
	value = (value << HE_LTF_SYM_SHIFT);
	ppdu_info->he_data5 |= value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_1,
			   PACKET_EXTENSION_A_FACTOR);
	value = value << HE_PRE_FEC_PAD_SHIFT;
	ppdu_info->he_data5 |= value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_1, TXBF);
	value = value << HE_TXBF_SHIFT;
	ppdu_info->he_data5 |= value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_1,
			   PACKET_EXTENSION_PE_DISAMBIGUITY);
	value = value << HE_PE_DISAMBIGUITY_SHIFT;
	ppdu_info->he_data5 |= value;

	/* data6 */
	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_0, NSTS);
	value++;
	ppdu_info->nss = value;
	ppdu_info->he_data6 = value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_1, DOPPLER_INDICATION);
	value = value << HE_DOPPLER_SHIFT;
	ppdu_info->he_data6 |= value;

	value = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_1, TXOP_DURATION);
	value = value << HE_TXOP_SHIFT;
	ppdu_info->he_data6 |= value;

	ppdu_info->beamformed = HAL_RX_GET(he_sig_a_su_info, HE_SIG_A_SU_INFO_1, TXBF);
	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
}

static void
ath12k_wifi6_hal_mon_rx_set_decap_type_raw_mode(struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u8 user_id = ppdu_info->user_id;

	/* set decap type to raw mode for management frames, control frames
	 * and NULL data frames
	 */
	if (ppdu_info->nrp_info.fc_valid && ppdu_info->mpdu_info[user_id].decap_type) {
		if (ieee80211_is_mgmt(ppdu_info->nrp_info.frame_control) ||
		    ieee80211_is_ctl(ppdu_info->nrp_info.frame_control) ||
		    (ppdu_info->grp_id == HAL_MPDU_START_SW_FRAME_GRP_NULL_DATA)) {
			ppdu_info->mpdu_info[user_id].decap_type = DP_RX_DECAP_TYPE_RAW;
		}
	}
}

static __always_inline void
ath12k_wifi6_hal_mon_rx_mpdu_start_info_get(const void *tlv_data, u32 userid,
					    struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_mpdu_start *mpdu_start =
		(struct hal_rx_mpdu_start *)tlv_data;
	u16 peer_id, addr_16;
	u32 info[9], addr_32;
	u8 user_id = ppdu_info->user_id;

	info[0] = __le32_to_cpu(mpdu_start->info0);
	info[1] = __le32_to_cpu(mpdu_start->info1);
	info[2] = __le32_to_cpu(mpdu_start->info2);
	info[3] = __le32_to_cpu(mpdu_start->info3);
	info[4] = __le32_to_cpu(mpdu_start->info4);
	info[5] = __le32_to_cpu(mpdu_start->info5);
	info[6] = __le32_to_cpu(mpdu_start->info6);
	info[7] = __le32_to_cpu(mpdu_start->info7);
	info[8] = __le32_to_cpu(mpdu_start->info8);

	ppdu_info->grp_id = u32_get_bits(info[1],
					 HAL_RX_MPDU_START_INFO1_SW_GRP_ID);

	peer_id = u32_get_bits(info[2], HAL_RX_MPDU_START_INFO2_PEERID);
	if (peer_id)
		ppdu_info->peer_id = peer_id;

	ppdu_info->mpdu_retry += u32_get_bits(info[3],
					      HAL_RX_MPDU_START_INFO3_MPDU_RETRY);
	ppdu_info->nrp_info.mac_addr2_valid =
		u32_get_bits(info[3], HAL_RX_MPDU_START_INFO3_ADDR2_VALID);
	ppdu_info->nrp_info.to_ds_flag =
		u32_get_bits(info[3],
			     HAL_RX_MPDU_START_INFO3_TO_DS);
	ppdu_info->nrp_info.fc_valid =
		u32_get_bits(info[3],
			     HAL_RX_MPDU_START_INFO3_FC_VALID);

	if (userid < HAL_MAX_UL_MU_USERS) {
		ppdu_info->mpdu_info[user_id].raw_mpdu =
			u32_get_bits(info[4], HAL_RX_MPDU_START_INFO4_RAW_MPDU);
		if (ppdu_info->mpdu_info[user_id].raw_mpdu)
			ppdu_info->mpdu_info[user_id].decap_type = DP_RX_DECAP_TYPE_RAW;
		else
			ppdu_info->mpdu_info[user_id].decap_type =
				u32_get_bits(info[4], HAL_RX_MPDU_START_INFO4_DECAP_TYPE);
	}

	ppdu_info->mpdu_len += u32_get_bits(info[5],
					    HAL_RX_MPDU_START_INFO5_MPDU_LEN);
	ppdu_info->nrp_info.mcast_bcast =
		u32_get_bits(info[5], HAL_RX_MPDU_START_INFO5_MCAST_BCAST);
	ppdu_info->nrp_info.frame_control =
		u32_get_bits(info[6],
			     HAL_RX_MPDU_START_INFO6_FC_FIELD);
	addr_16 = u32_get_bits(info[7],
			       HAL_RX_MPDU_START_INFO7_ADDR2_15_0);
	addr_32 = u32_get_bits(info[8],
			       HAL_RX_MPDU_START_INFO8_ADDR2_47_16);
	if (ppdu_info->nrp_info.fc_valid &&
	    ppdu_info->nrp_info.to_ds_flag &&
	    ppdu_info->nrp_info.mac_addr2_valid)
		ath12k_wifi6_hal_mon_get_nrp_mac_addr(addr_16, addr_32,
						      ppdu_info->nrp_info.mac_addr2);

	if (userid < HAL_MAX_UL_MU_USERS) {
		ppdu_info->userid = userid;
		ppdu_info->userstats[userid].sw_peer_id = peer_id;
		ppdu_info->userstats[userid].ampdu_id =
			u32_get_bits(info[1], HAL_RX_MPDU_START_INFO1_PPDU_ID);
		ppdu_info->userstats[userid].filter_category =
			u32_get_bits(info[1],
				     HAL_RX_MPDU_START_INFO1_FILTER_CAT);
		ppdu_info->userstats[userid].mpdu_retry +=
			u32_get_bits(info[3], HAL_RX_MPDU_START_INFO3_MPDU_RETRY);
		ppdu_info->userstats[userid].frame_control_info_valid =
				ppdu_info->nrp_info.fc_valid;
		ppdu_info->userstats[userid].frame_control =
				ppdu_info->nrp_info.frame_control;
	}

	ath12k_wifi6_hal_mon_rx_set_decap_type_raw_mode(ppdu_info);
}

static __always_inline void
ath12k_wifi6_hal_mon_rx_mpdu_start_info_get_compact(const void *tlv_data, u32 userid,
						    struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_mon_mpdu_start_compact *mpdu_start =
		(struct hal_rx_mon_mpdu_start_compact *)tlv_data;
	u16 peer_id, addr_16;
	u32 info[9], addr_32;
	u8 user_id = ppdu_info->user_id;

	info[0] = __le32_to_cpu(mpdu_start->info0);
	info[1] = __le32_to_cpu(mpdu_start->info1);
	info[2] = __le32_to_cpu(mpdu_start->info2);
	info[3] = __le32_to_cpu(mpdu_start->info3);
	info[4] = __le32_to_cpu(mpdu_start->info4);
	info[5] = __le32_to_cpu(mpdu_start->info5);
	info[6] = __le32_to_cpu(mpdu_start->info6);
	info[7] = __le32_to_cpu(mpdu_start->info7);
	info[8] = __le32_to_cpu(mpdu_start->info8);

	ppdu_info->grp_id = u32_get_bits(info[1],
					 HAL_RX_MPDU_START_INFO1_SW_GRP_ID_CMPCT);

	peer_id = u32_get_bits(info[2], HAL_RX_MPDU_START_INFO2_PEERID_CMPCT);
	if (peer_id)
		ppdu_info->peer_id = peer_id;

	ppdu_info->mpdu_retry += u32_get_bits(info[3],
					      HAL_RX_MPDU_START_INFO3_MPDU_RETRY_CMPCT);
	ppdu_info->nrp_info.mac_addr2_valid =
		u32_get_bits(info[3], HAL_RX_MPDU_START_INFO3_ADDR2_VALID_CMPCT);
	ppdu_info->nrp_info.to_ds_flag =
		u32_get_bits(info[3],
			     HAL_RX_MPDU_START_INFO3_TO_DS_CMPCT);
	ppdu_info->nrp_info.fc_valid =
		u32_get_bits(info[3],
			     HAL_RX_MPDU_START_INFO3_FC_VALID_CMPCT);
	ppdu_info->mpdu_info[user_id].raw_mpdu =
		u32_get_bits(info[4], HAL_RX_MPDU_START_INFO4_RAW_MPDU_CMPCT);
	if (ppdu_info->mpdu_info[user_id].raw_mpdu)
		ppdu_info->mpdu_info[user_id].decap_type = DP_RX_DECAP_TYPE_RAW;
	else
		ppdu_info->mpdu_info[user_id].decap_type =
			u32_get_bits(info[4], HAL_RX_MPDU_START_INFO4_DECAP_TYPE_CMPCT);

	ppdu_info->mpdu_len += u32_get_bits(info[5],
					    HAL_RX_MPDU_START_INFO5_MPDU_LEN_CMPCT);
	ppdu_info->nrp_info.mcast_bcast =
		u32_get_bits(info[5], HAL_RX_MPDU_START_INFO5_MCAST_BCAST_CMPCT);
	ppdu_info->nrp_info.frame_control =
		u32_get_bits(info[6],
			     HAL_RX_MPDU_START_INFO6_FC_FIELD_CMPCT);

	addr_16 = u32_get_bits(info[7],
			       HAL_RX_MPDU_START_INFO7_ADDR2_15_0_CMPCT);
	addr_32 = u32_get_bits(info[8],
			       HAL_RX_MPDU_START_INFO8_ADDR2_47_16_CMPCT);
	if (ppdu_info->nrp_info.fc_valid &&
	    ppdu_info->nrp_info.to_ds_flag &&
	    ppdu_info->nrp_info.mac_addr2_valid)
		ath12k_wifi6_hal_mon_get_nrp_mac_addr(addr_16, addr_32,
						      ppdu_info->nrp_info.mac_addr2);

	if (userid < HAL_MAX_UL_MU_USERS) {
		ppdu_info->userid = userid;
		ppdu_info->userstats[userid].sw_peer_id = peer_id;
		ppdu_info->userstats[userid].ampdu_id =
			u32_get_bits(info[1], HAL_RX_MPDU_START_INFO1_PPDU_ID_CMPCT);
		ppdu_info->userstats[userid].filter_category =
			u32_get_bits(info[1],
				     HAL_RX_MPDU_START_INFO1_FILTER_CAT_CMPCT);
		ppdu_info->userstats[userid].mpdu_retry +=
			u32_get_bits(info[3], HAL_RX_MPDU_START_INFO3_MPDU_RETRY_CMPCT);
		ppdu_info->userstats[userid].frame_control_info_valid =
				ppdu_info->nrp_info.fc_valid;
		ppdu_info->userstats[userid].frame_control =
				ppdu_info->nrp_info.frame_control;
	}
}

void
ath12k_wifi6_hal_mon_rx_mpdu_start_info_parse(const void *tlv_data, u32 userid,
					      struct hal_rx_mon_ppdu_info *ppdu_info,
					      u32 tlv_len)
{
	if (likely(tlv_len < HAL_MON_RX_MPDU_START_TLV_SIZE))
		ath12k_wifi6_hal_mon_rx_mpdu_start_info_get_compact(tlv_data, userid,
								   ppdu_info);
	else
		ath12k_wifi6_hal_mon_rx_mpdu_start_info_get(tlv_data, userid,
							    ppdu_info);
}

static __always_inline void
ath12k_wifi6_hal_mon_rx_msdu_end_info_get(const void *tlv_data, u32 userid,
					  struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_msdu_end *msdu_end =
		(struct hal_rx_msdu_end *)tlv_data;
	u32 info[3];

	info[0] = __le32_to_cpu(msdu_end->info0);
	info[1] = __le32_to_cpu(msdu_end->info1);
	info[2] = __le32_to_cpu(msdu_end->info2);

	ppdu_info->grp_id = u32_get_bits(info[0],
					 HAL_RX_MSDU_END_INFO0_SW_FRAME_GRP_ID);

	ppdu_info->decap_format = u32_get_bits(info[1],
					       HAL_RX_MSDU_END_INFO1_DECAP_FORMAT);
	ath12k_wifi6_hal_mon_parse_rx_msdu_end_err(info[2],
						   &ppdu_info->errmap);
}

static __always_inline void
ath12k_wifi6_hal_mon_rx_msdu_end_info_get_compact(const void *tlv_data, u32 userid,
						  struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_mon_msdu_end_compact *msdu_end =
		(struct hal_rx_mon_msdu_end_compact *)tlv_data;
	u32 info[3];

	info[0] = __le32_to_cpu(msdu_end->info0);
	info[1] = __le32_to_cpu(msdu_end->info1);
	info[2] = __le32_to_cpu(msdu_end->info2);

	ppdu_info->grp_id = u32_get_bits(info[0],
					 HAL_RX_MSDU_END_INFO0_SW_FRAME_GRP_ID_CMPCT);

	ppdu_info->decap_format = u32_get_bits(info[1],
					       HAL_RX_MSDU_END_INFO1_DECAP_FORMAT_CMPCT);
	ath12k_wifi6_hal_mon_parse_rx_msdu_end_err(info[2],
						   &ppdu_info->errmap);
}

void
ath12k_wifi6_hal_mon_rx_msdu_end_info_parse(const void *tlv_data, u32 userid,
					    struct hal_rx_mon_ppdu_info *ppdu_info,
					    u32 tlv_len)
{
	if (likely(tlv_len < HAL_MON_RX_MSDU_END_TLV_SIZE))
		ath12k_wifi6_hal_mon_rx_msdu_end_info_get_compact(tlv_data, userid,
								  ppdu_info);
	else
		ath12k_wifi6_hal_mon_rx_msdu_end_info_get(tlv_data, userid,
							  ppdu_info);
}

static __always_inline void
ath12k_wifi6_hal_mon_handle_ofdma_info(const void *tlv_data,
				       struct hal_rx_user_status *rx_user_status)
{
	rx_user_status->ul_ofdma_user_v0_word0 =
				HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_11,
					   SW_RESPONSE_REFERENCE_PTR);
	rx_user_status->ul_ofdma_user_v0_word1 =
				HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_22,
					   SW_RESPONSE_REFERENCE_PTR_EXT);
}
static __always_inline void
ath12k_wifi6_hal_mon_populate_byte_count(const void *tlv_data,
					 struct hal_rx_user_status *rx_user_status)
{
	rx_user_status->mpdu_ok_byte_count =
		HAL_RX_GET(tlv_data,
			   RX_PPDU_END_USER_STATS_17, MPDU_OK_BYTE_COUNT);
	rx_user_status->mpdu_err_byte_count =
		HAL_RX_GET(tlv_data,
			   RX_PPDU_END_USER_STATS_19, MPDU_ERR_BYTE_COUNT);
}

static __always_inline void
ath12k_wifi6_hal_mon_rx_parse_ppdu_end_usr_stats(const void *tlv_data,
						 u32 userid,
						 struct hal_rx_mon_ppdu_info *ppdu_info)
{
	unsigned long tid_bitmap = 0;
	uint16_t seq = 0;

	ppdu_info->nss = HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_1, NSS) + 1;
	ppdu_info->mcs = HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_1, MCS);

	ppdu_info->ast_index =
			HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_4,
				   AST_INDEX);
	tid_bitmap = HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_12,
				RECEIVED_QOS_DATA_TID_BITMAP);

	ppdu_info->tid = ffs(tid_bitmap) - 1;

	ppdu_info->tcp_msdu_count = HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_9,
					       TCP_MSDU_COUNT) +
				    HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_10,
					       TCP_ACK_MSDU_COUNT);
	ppdu_info->udp_msdu_count = HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_9,
					       UDP_MSDU_COUNT);
	ppdu_info->other_msdu_count = HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_10,
						 OTHER_MSDU_COUNT);

	if (ppdu_info->ppdu_info_extn.sw_frame_group_id !=
	    HAL_MPDU_SW_FRAME_GROUP_NULL_DATA) {
		ppdu_info->frame_control_info_valid =
			HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_3,
				   FRAME_CONTROL_INFO_VALID);

		if (ppdu_info->frame_control_info_valid)
			ppdu_info->frame_control = HAL_RX_GET(tlv_data,
							      RX_PPDU_END_USER_STATS_4,
							      FRAME_CONTROL_FIELD);
	}

	ppdu_info->data_sequence_control_info_valid =
		HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_3,
			   DATA_SEQUENCE_CONTROL_INFO_VALID);

	seq = HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_5,
			 FIRST_DATA_SEQ_CTRL);
	if (ppdu_info->data_sequence_control_info_valid)
		ppdu_info->first_data_seq_ctrl = seq;

	ppdu_info->preamble_type = HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_3,
					      HT_CONTROL_FIELD_PKT_TYPE);
	switch (ppdu_info->preamble_type) {
	case HAL_RX_PREAMBLE_11N:
		ppdu_info->ht_flags = 1;
		break;
	case HAL_RX_PREAMBLE_11AC:
		ppdu_info->vht_flags = 1;
		break;
	case HAL_RX_PREAMBLE_11AX:
		ppdu_info->he_flags = 1;
		break;
	default:
		break;
	}

	ppdu_info->num_mpdu_fcs_ok = HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_3,
						MPDU_CNT_FCS_OK);
	ppdu_info->num_mpdu_fcs_err = HAL_RX_GET(tlv_data, RX_PPDU_END_USER_STATS_2,
						 MPDU_CNT_FCS_ERR);

	if (userid < HAL_MAX_UL_MU_USERS) {
		struct hal_rx_user_status *rxuser_stats =
			&ppdu_info->userstats[userid];

		ath12k_wifi6_hal_mon_handle_ofdma_info(tlv_data,
						       rxuser_stats);
		ath12k_wifi6_hal_mon_populate_byte_count(tlv_data,
							 rxuser_stats);

		if (ppdu_info->num_mpdu_fcs_ok > 1 ||
		    ppdu_info->num_mpdu_fcs_err > 1)
			ppdu_info->userstats[userid].ampdu_present = true;

		ppdu_info->num_users += 1;

		ath12k_wifi6_hal_mon_populate_mu_user_info(ppdu_info,
							   rxuser_stats);
	}
}

enum hal_rx_mon_status
ath12k_wifi6_hal_mon_rx_parse_status_tlv(struct ath12k_hal *hal,
					 struct hal_rx_mon_ppdu_info *ppdu_info,
					 struct hal_tlv_parsed_hdr *tlv_parsed_hdr)
{
	const void *tlv_data = tlv_parsed_hdr->data;
	u32 userid;
	u16 tlv_tag, tlv_len;

	tlv_tag = tlv_parsed_hdr->tag;
	tlv_len = tlv_parsed_hdr->len;
	userid = tlv_parsed_hdr->userid;

	switch (tlv_tag) {
	case HAL_RX_PPDU_START: {
		/* Reset ppdu_info before processing the ppdu */
		memset(ppdu_info, 0, sizeof(struct hal_rx_mon_ppdu_info));

		ppdu_info->last_ppdu_id =
			ppdu_info->ppdu_id =
				HAL_RX_GET(tlv_data, RX_PPDU_START_0,
					   PHY_PPDU_ID);
		/* channel number is set in PHY meta data */
		ppdu_info->chan_num =
			(HAL_RX_GET(tlv_data, RX_PPDU_START_1,
				    SW_PHY_META_DATA) & 0x0000FFFF);
		ppdu_info->freq =
			(HAL_RX_GET(tlv_data, RX_PPDU_START_1,
				    SW_PHY_META_DATA) & 0xFFFF0000) >>
						HAL_PPDU_START_FREQ_MASK;

		ppdu_info->ppdu_ts =
			HAL_RX_GET(tlv_data, RX_PPDU_START_2,
				   PPDU_START_TIMESTAMP);
		ppdu_info->ppdu_info_extn.rx_state = HAL_RX_MON_PPDU_START;
		break;
	}
	case HAL_RX_PPDU_END_USER_STATS:
               ath12k_wifi6_hal_mon_rx_parse_ppdu_end_usr_stats(tlv_data,
								userid, ppdu_info);
		break;
	case HAL_PHYRX_HT_SIG:
		ath12k_wifi6_hal_mon_parse_ht_sig(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_L_SIG_B:
		ath12k_wifi6_hal_mon_parse_l_sig_b(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_L_SIG_A:
		ath12k_wifi6_hal_mon_parse_l_sig_a(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_VHT_SIG_A:
		ath12k_wifi6_hal_mon_parse_vht_sig_a(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_A_SU:
		ath12k_wifi6_hal_mon_parse_he_sig_su(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_A_MU_DL:
		ath12k_wifi6_hal_mon_parse_he_sig_mu(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_B1_MU:
		ath12k_wifi6_hal_mon_parse_he_sig_b1_mu(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_B2_MU:
		ath12k_wifi6_hal_mon_parse_he_sig_b2_mu(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_B2_OFDMA:
		ath12k_wifi6_hal_mon_parse_he_sig_b2_ofdma(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_RSSI_LEGACY: {
		const struct hal_rx_phyrx_rssi_legacy_info *rssi = tlv_data;
		u32 info[2];

		info[0] = __le32_to_cpu(rssi->info0);
		info[1] = __le32_to_cpu(rssi->info1);

		/* TODO: Please note that the combined rssi will not be accurate
		 * in MU case. Rssi in MU needs to be retrieved from
		 * PHYRX_OTHER_RECEIVE_INFO TLV.
		 */
		ppdu_info->rssi_comb =
			u32_get_bits(info[1],
				     HAL_RX_PHYRX_RSSI_LEGACY_INFO_INFO1_RSSI_COMB);

		ppdu_info->bw = u32_get_bits(info[0],
					     HAL_RX_PHYRX_RSSI_LEGACY_INFO_INFO0_RX_BW);
		break;
	}
	case HAL_PHYRX_OTHER_RECEIVE_INFO: {
		const struct hal_phyrx_common_user_info *cmn_usr_info = tlv_data;

		ppdu_info->gi = le32_get_bits(cmn_usr_info->info0,
					      HAL_RX_PHY_CMN_USER_INFO0_GI);
		break;
	}
	case HAL_RX_PPDU_START_USER_INFO:
		break;

	case HAL_RX_PPDU_END:
		ppdu_info->ppdu_info_extn.rx_state = HAL_RX_MON_PPDU_END;
		break;

	case HAL_RXPCU_PPDU_END_INFO: {
		ppdu_info->ppdu_info_extn.rx_antenna = HAL_RX_GET(tlv_data,
								  RXPCU_PPDU_END_INFO_2,
								  RX_ANTENNA);
		ppdu_info->tsft =
			HAL_RX_GET(tlv_data, RXPCU_PPDU_END_INFO_1,
				   WB_TIMESTAMP_UPPER_32);
		ppdu_info->tsft = (ppdu_info->tsft << 32) |
					HAL_RX_GET(tlv_data, RXPCU_PPDU_END_INFO_0,
						   WB_TIMESTAMP_LOWER_32);
		ppdu_info->rx_duration =
			HAL_RX_GET(tlv_data, RXPCU_PPDU_END_INFO_9,
				   RX_PPDU_DURATION);
		break;
	}
	case HAL_RX_MPDU_START: {

		ath12k_hal_mon_rx_mpdu_start_info_get(hal, tlv_data,
						      userid, ppdu_info,
						      tlv_len);

		return HAL_RX_MON_STATUS_MPDU_START;
	}
	case HAL_RX_MSDU_START:
		/* TODO: add msdu start parsing logic */
		break;
	case HAL_RX_MSDU_END:

		ath12k_hal_mon_rx_msdu_end_info_get(hal, tlv_data, userid, ppdu_info,
						    tlv_len);

		if (ppdu_info->grp_id == RX_MSDU_END_INFO0_SW_FRAMEGROUP_UCAST_DATA ||
		    ppdu_info->grp_id == RX_MSDU_END_INFO0_SW_FRAMEGROUP_MCAST_DATA) {
			if (userid < HAL_MAX_UL_MU_USERS) {
				ppdu_info->userstats[userid].errmap = ppdu_info->errmap;
			}
		}

		return HAL_RX_MON_STATUS_MSDU_END;
	case HAL_RX_MPDU_END:
		return HAL_RX_MON_STATUS_MPDU_END;
	case HAL_DUMMY:
		return HAL_RX_MON_STATUS_BUF_DONE;
	case HAL_RX_HEADER:
		return HAL_RX_MON_STATUS_RX_HDR;
	case HAL_RX_PPDU_END_STATUS_DONE:
	case 0:
		return HAL_RX_MON_STATUS_PPDU_DONE;
	default:
		break;
	}

	return HAL_RX_MON_STATUS_PPDU_NOT_DONE;
}

enum hal_tx_mon_status
ath12k_wifi6_hal_mon_tx_status_get_num_user(u16 tlv_tag,
					    const void *tx_tlv,
					    u8 *num_users)
{
	u32 tlv_status = HAL_TX_MON_STATUS_PPDU_NOT_DONE;
	u32 info0;

	switch (tlv_tag) {
	case HAL_TX_FES_SETUP: {
		struct hal_tx_fes_setup *tx_fes_setup =
				(struct hal_tx_fes_setup *)tx_tlv;

		info0 = __le32_to_cpu(tx_fes_setup->info0);

		*num_users = u32_get_bits(info0, HAL_TX_FES_SETUP_INFO0_NUM_OF_USERS);
		tlv_status = HAL_TX_MON_FES_SETUP;
		break;
	}

	case HAL_RX_RESPONSE_REQUIRED_INFO: {
		/* TODO: need to update *num_users */
		tlv_status = HAL_RX_MON_RESPONSE_REQUIRED_INFO;
		break;
	}
	}

	return tlv_status;
}

enum hal_tx_mon_status
ath12k_wifi6_hal_mon_tx_parse_status_tlv(struct hal_tx_mon_ppdu_info *tx_ppdu_info,
					 u16 tlv_tag, const void *tlv_data, u32 userid)
{
	enum hal_tx_mon_status status = HAL_TX_MON_STATUS_PPDU_NOT_DONE;
	u32 info[7] = {0};

	switch (tlv_tag) {
	case HAL_TX_FES_SETUP: {
		const struct hal_tx_fes_setup *tx_fes_setup = tlv_data;

		info[0] = __le32_to_cpu(tx_fes_setup->info0);
		tx_ppdu_info->ppdu_id = __le32_to_cpu(tx_fes_setup->schedule_id);
		tx_ppdu_info->num_users =
			u32_get_bits(info[0], HAL_TX_FES_SETUP_INFO0_NUM_OF_USERS);
		status = HAL_TX_MON_FES_SETUP;
		break;
	}

	case HAL_TX_FES_STATUS_END: {
		const struct hal_tx_fes_status_end *tx_fes_status_end = tlv_data;
		u32 tst_15_0, tst_31_16;

		info[0] = __le32_to_cpu(tx_fes_status_end->info0);
		tst_15_0 =
			u32_get_bits(info[0],
				     HAL_TX_FES_STATUS_END_INFO0_START_TIMESTAMP_15_0);
		tst_31_16 =
			u32_get_bits(info[0],
				     HAL_TX_FES_STATUS_END_INFO0_START_TIMESTAMP_31_16);

		tx_ppdu_info->rx_status.ppdu_ts = (tst_15_0 | (tst_31_16 << 16));
		status = HAL_TX_MON_FES_STATUS_END;
		break;
	}

	case HAL_RX_RESPONSE_REQUIRED_INFO: {
		const struct hal_rx_resp_req_info *rx_resp_req_info = tlv_data;
		u32 addr_32;
		u16 addr_16;

		info[0] = __le32_to_cpu(rx_resp_req_info->info0);
		info[1] = __le32_to_cpu(rx_resp_req_info->info1);
		info[2] = __le32_to_cpu(rx_resp_req_info->info2);
		info[3] = __le32_to_cpu(rx_resp_req_info->info3);
		info[4] = __le32_to_cpu(rx_resp_req_info->info4);
		info[5] = __le32_to_cpu(rx_resp_req_info->info5);

		tx_ppdu_info->rx_status.ppdu_id =
			u32_get_bits(info[0], HAL_RX_RESP_REQ_INFO0_PPDU_ID);
		tx_ppdu_info->rx_status.reception_type =
			u32_get_bits(info[0], HAL_RX_RESP_REQ_INFO0_RECEPTION_TYPE);
		tx_ppdu_info->rx_status.rx_duration =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_DURATION);
		tx_ppdu_info->rx_status.mcs =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_RATE_MCS);
		tx_ppdu_info->rx_status.sgi =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_SGI);
		tx_ppdu_info->rx_status.is_stbc =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_STBC);
		tx_ppdu_info->rx_status.ldpc =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_LDPC);
		tx_ppdu_info->rx_status.is_ampdu =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_IS_AMPDU);
		tx_ppdu_info->rx_status.num_users =
			u32_get_bits(info[2], HAL_RX_RESP_REQ_INFO2_NUM_USER);

		addr_32 = u32_get_bits(info[3], HAL_RX_RESP_REQ_INFO3_ADDR1_31_0);
		addr_16 = u32_get_bits(info[3], HAL_RX_RESP_REQ_INFO4_ADDR1_47_32);
		ath12k_wifi6_hal_mon_get_mac_addr(addr_32, addr_16,
						  tx_ppdu_info->rx_status.addr1);

		addr_16 = u32_get_bits(info[4], HAL_RX_RESP_REQ_INFO4_ADDR1_15_0);
		addr_32 = u32_get_bits(info[5], HAL_RX_RESP_REQ_INFO5_ADDR1_47_16);
		ath12k_wifi6_hal_mon_get_mac_addr(addr_32, addr_16,
						  tx_ppdu_info->rx_status.addr2);

		status = HAL_RX_MON_RESPONSE_REQUIRED_INFO;
		break;
	}

	case HAL_PCU_PPDU_SETUP_INIT: {
		const struct hal_tx_pcu_ppdu_setup_init *ppdu_setup = tlv_data;
		u32 addr_32;
		u16 addr_16;

		info[0] = __le32_to_cpu(ppdu_setup->info0);
		info[1] = __le32_to_cpu(ppdu_setup->info1);
		info[2] = __le32_to_cpu(ppdu_setup->info2);
		info[3] = __le32_to_cpu(ppdu_setup->info3);
		info[4] = __le32_to_cpu(ppdu_setup->info4);
		info[5] = __le32_to_cpu(ppdu_setup->info5);
		info[6] = __le32_to_cpu(ppdu_setup->info6);

		/* protection frame address 1 */
		addr_32 = u32_get_bits(info[1],
				       HAL_TX_PPDU_SETUP_INFO1_PROT_FRAME_ADDR1_31_0);
		addr_16 = u32_get_bits(info[2],
				       HAL_TX_PPDU_SETUP_INFO2_PROT_FRAME_ADDR1_47_32);
		ath12k_wifi6_hal_mon_get_mac_addr(addr_32, addr_16,
						  tx_ppdu_info->rx_status.addr1);

		/* protection frame address 2 */
		addr_16 = u32_get_bits(info[2],
				       HAL_TX_PPDU_SETUP_INFO2_PROT_FRAME_ADDR2_15_0);
		addr_32 = u32_get_bits(info[3],
				       HAL_TX_PPDU_SETUP_INFO3_PROT_FRAME_ADDR2_47_16);
		ath12k_wifi6_hal_mon_get_mac_addr(addr_32, addr_16,
						  tx_ppdu_info->rx_status.addr2);

		/* protection frame address 3 */
		addr_32 = u32_get_bits(info[4],
				       HAL_TX_PPDU_SETUP_INFO4_PROT_FRAME_ADDR3_31_0);
		addr_16 = u32_get_bits(info[5],
				       HAL_TX_PPDU_SETUP_INFO5_PROT_FRAME_ADDR3_47_32);
		ath12k_wifi6_hal_mon_get_mac_addr(addr_32, addr_16,
						  tx_ppdu_info->rx_status.addr3);

		/* protection frame address 4 */
		addr_16 = u32_get_bits(info[5],
				       HAL_TX_PPDU_SETUP_INFO5_PROT_FRAME_ADDR4_15_0);
		addr_32 = u32_get_bits(info[6],
				       HAL_TX_PPDU_SETUP_INFO6_PROT_FRAME_ADDR4_47_16);
		ath12k_wifi6_hal_mon_get_mac_addr(addr_32, addr_16,
						  tx_ppdu_info->rx_status.addr4);

		status = u32_get_bits(info[0],
				      HAL_TX_PPDU_SETUP_INFO0_MEDIUM_PROT_TYPE);
		break;
	}

	case HAL_TX_QUEUE_EXTENSION: {
		const struct hal_tx_queue_exten *tx_q_exten = tlv_data;

		info[0] = __le32_to_cpu(tx_q_exten->info0);

		tx_ppdu_info->rx_status.frame_control =
			u32_get_bits(info[0],
				     HAL_TX_Q_EXT_INFO0_FRAME_CTRL);
		tx_ppdu_info->rx_status.fc_valid = true;
		break;
	}

	case HAL_TX_FES_STATUS_START: {
		const struct hal_tx_fes_status_start *tx_fes_start = tlv_data;

		info[0] = __le32_to_cpu(tx_fes_start->info0);

		tx_ppdu_info->rx_status.medium_prot_type =
			u32_get_bits(info[0],
				     HAL_TX_FES_STATUS_START_INFO0_MEDIUM_PROT_TYPE);
		break;
	}

	case HAL_TX_FES_STATUS_PROT: {
		const struct hal_tx_fes_status_prot *tx_fes_status = tlv_data;
		u32 start_timestamp;
		u32 end_timestamp;

		info[0] = __le32_to_cpu(tx_fes_status->info0);
		info[1] = __le32_to_cpu(tx_fes_status->info1);

		start_timestamp =
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_PROT_INFO0_STRT_FRM_TS_15_0);
		start_timestamp |=
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_PROT_INFO0_STRT_FRM_TS_31_16) << 15;
		end_timestamp =
			u32_get_bits(info[1],
				     HAL_TX_FES_STAT_PROT_INFO1_END_FRM_TS_15_0);
		end_timestamp |=
			u32_get_bits(info[1],
				     HAL_TX_FES_STAT_PROT_INFO1_END_FRM_TS_31_16) << 15;
		tx_ppdu_info->rx_status.rx_duration = end_timestamp - start_timestamp;

		status = HAL_TX_MON_FES_STATUS_PROT;
		break;
	}

	case HAL_TX_FES_STATUS_START_PPDU:
	case HAL_TX_FES_STATUS_START_PROT: {
		const struct hal_tx_fes_status_start_prot *tx_fes_stat_start = tlv_data;
		u64 ppdu_ts;

		info[0] = __le32_to_cpu(tx_fes_stat_start->info0);
		info[1] = __le32_to_cpu(tx_fes_stat_start->info1);

		tx_ppdu_info->rx_status.ppdu_ts =
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_STRT_INFO0_PROT_TS_LOWER_32);
		ppdu_ts = (u32_get_bits(info[1],
					HAL_TX_FES_STAT_STRT_INFO1_PROT_TS_UPPER_32));
		tx_ppdu_info->rx_status.ppdu_ts |= ppdu_ts << 32;
		break;
	}

	case HAL_TX_FES_STATUS_USER_PPDU: {
		const struct hal_tx_fes_status_user_ppdu *tx_fes_usr_ppdu = tlv_data;

		info[0] = __le32_to_cpu(tx_fes_usr_ppdu->info0);

		tx_ppdu_info->rx_status.rx_duration =
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_USR_PPDU_INFO0_DURATION);
		break;
	}

	case HAL_MACTX_HE_SIG_A_SU:
		ath12k_wifi6_hal_mon_parse_he_sig_su(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_A_MU_DL:
		ath12k_wifi6_hal_mon_parse_he_sig_mu(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_B1_MU:
		ath12k_wifi6_hal_mon_parse_he_sig_b1_mu(tlv_data,
							&tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_B2_MU:
		ath12k_wifi6_hal_mon_parse_he_sig_b2_mu(tlv_data,
							&tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_B2_OFDMA:
		ath12k_wifi6_hal_mon_parse_he_sig_b2_ofdma(tlv_data,
							   &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_VHT_SIG_A:
		ath12k_wifi6_hal_mon_parse_vht_sig_a(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_L_SIG_A:
		ath12k_wifi6_hal_mon_parse_l_sig_a(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_L_SIG_B:
		ath12k_wifi6_hal_mon_parse_l_sig_b(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_RX_FRAME_BITMAP_ACK: {
		const struct hal_rx_frame_bitmap_ack *fbm_ack = tlv_data;
		u32 addr_32;
		u16 addr_16;

		info[0] = __le32_to_cpu(fbm_ack->info0);
		info[1] = __le32_to_cpu(fbm_ack->info1);

		addr_32 = u32_get_bits(info[0],
				       HAL_RX_FBM_ACK_INFO0_ADDR1_31_0);
		addr_16 = u32_get_bits(info[1],
				       HAL_RX_FBM_ACK_INFO1_ADDR1_47_32);
		ath12k_wifi6_hal_mon_get_mac_addr(addr_32, addr_16,
						  tx_ppdu_info->rx_status.addr1);

		status = HAL_TX_MON_FRAME_BITMAP_ACK;
		break;
	}

	case HAL_MACTX_PHY_DESC: {
		const struct hal_tx_phy_desc *tx_phy_desc = tlv_data;

		info[0] = __le32_to_cpu(tx_phy_desc->info0);
		info[1] = __le32_to_cpu(tx_phy_desc->info1);
		info[2] = __le32_to_cpu(tx_phy_desc->info2);
		info[3] = __le32_to_cpu(tx_phy_desc->info3);

		tx_ppdu_info->rx_status.beamformed =
			u32_get_bits(info[0],
				     HAL_TX_PHY_DESC_INFO0_BF_TYPE);
		tx_ppdu_info->rx_status.preamble_type =
			u32_get_bits(info[0],
				     HAL_TX_PHY_DESC_INFO0_PREAMBLE_11B);
		tx_ppdu_info->rx_status.mcs =
			u32_get_bits(info[1],
				     HAL_TX_PHY_DESC_INFO1_MCS);
		tx_ppdu_info->rx_status.ltf_size =
			u32_get_bits(info[3],
				     HAL_TX_PHY_DESC_INFO3_LTF_SIZE);
		tx_ppdu_info->rx_status.nss =
			u32_get_bits(info[2],
				     HAL_TX_PHY_DESC_INFO2_NSS);
		tx_ppdu_info->rx_status.chan_num =
			u32_get_bits(info[3],
				     HAL_TX_PHY_DESC_INFO3_ACTIVE_CHANNEL);
		tx_ppdu_info->rx_status.bw =
			u32_get_bits(info[0],
				     HAL_TX_PHY_DESC_INFO0_BANDWIDTH);
		break;
	}

	case HAL_TX_MPDU_START: {
		status = HAL_TX_MON_MPDU_START;
		break;
	}

	case HAL_TX_MPDU_END:
		status = HAL_TX_MON_MPDU_END;
		break;
	}

	return status;
}

u8 *ath12k_wifi6_hal_mon_rx_desc_get_msdu_payload(void *rx_desc)
{
	return ath12k_wifi6_hal_rx_desc_get_msdu_payload_qcn9074(rx_desc);
}

const struct hal_mon_ops hal_qcn9074_mon_ops = {
	.get_mon_mpdu_start_wmask = NULL,
	.get_mon_mpdu_end_wmask = NULL,
	.get_mon_msdu_end_wmask = NULL,
	.get_mon_ppdu_end_usr_stats_wmask = NULL,
};

void ath12k_wifi6_hal_mon_ops_init(struct ath12k_hal *hal,
				   u8 hw_version)
{
	switch (hw_version) {
	case ATH12K_HW_QCN9074_HW10:
		hal->hal_mon_ops = &hal_qcn9074_mon_ops;
		break;
	default:
		break;
	}
}
