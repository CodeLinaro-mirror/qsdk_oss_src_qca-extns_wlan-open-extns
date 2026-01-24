/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hal_rx.h"
#include "hal_desc.h"

#ifndef HAL_MON_H
#define HAL_MON_H

#define HAL_PPDU_START_GET_CHAN			0x0000FFFF
#define HAL_PPDU_START_GET_FREQ			0xFFFF0000
#define HAL_PPDU_START_FREQ_MASK		16

#define HAL_MPDU_START_SW_FRAME_GRP_NULL_DATA	0x3

#define RX_MON_MPDU_END_WMASK	0xff
#define HAL_MON_RX_MPDU_START_TLV_SIZE		\
	sizeof(struct hal_rx_mpdu_start)
#define HAL_MON_RX_MSDU_END_TLV_SIZE		\
	sizeof(struct hal_rx_msdu_end)
/**
 * @HAL_RX_MON_PPDU_START: PPDU start TLV is decoded in HAL
 * @HAL_RX_MON_PPDU_END: PPDU end TLV is decoded in HAL
 * @HAL_RX_MON_PPDU_RESET: Not PPDU start and end TLV
 */
enum hal_ppdu_status {
	HAL_RX_MON_PPDU_START = 0,
	HAL_RX_MON_PPDU_END,
	HAL_RX_MON_PPDU_RESET,
};

#define HAL_RX_OFFSET(block, field) block##_##field##_OFFSET
#define HAL_RX_LSB(block, field) block##_##field##_LSB
#define HAL_RX_MASK(block, field) block##_##field##_MASK

#define HAL_RX_GET(_ptr, block, field) \
	(((*((volatile uint32_t *)_ptr + (HAL_RX_OFFSET(block, field)>>2))) & \
	HAL_RX_MASK(block, field)) >> \
	HAL_RX_LSB(block, field))

enum hal_rx_mpdu_info_sw_frame_group_id_type {
	HAL_MPDU_SW_FRAME_GROUP_NDP_FRAME = 0,
	HAL_MPDU_SW_FRAME_GROUP_MULTICAST_DATA,
	HAL_MPDU_SW_FRAME_GROUP_UNICAST_DATA,
	HAL_MPDU_SW_FRAME_GROUP_NULL_DATA,
	HAL_MPDU_SW_FRAME_GROUP_MGMT,
	HAL_MPDU_SW_FRAME_GROUP_MGMT_PROBE_REQ = 8,
	HAL_MPDU_SW_FRAME_GROUP_MGMT_BEACON = 12,
	HAL_MPDU_SW_FRAME_GROUP_CTRL = 20,
	HAL_MPDU_SW_FRAME_GROUP_CTRL_NDPA = 25,
	HAL_MPDU_SW_FRAME_GROUP_CTRL_BAR = 28,
	HAL_MPDU_SW_FRAME_GROUP_CTRL_RTS = 31,
	HAL_MPDU_SW_FRAME_GROUP_UNSUPPORTED = 36,
	HAL_MPDU_SW_FRAME_GROUP_MAX = 37,
};

#define HAL_LEGACY_MCS0  0
#define HAL_LEGACY_MCS1  1
#define HAL_LEGACY_MCS2  2
#define HAL_LEGACY_MCS3  3
#define HAL_LEGACY_MCS4  4
#define HAL_LEGACY_MCS5  5
#define HAL_LEGACY_MCS6  6
#define HAL_LEGACY_MCS7  7

#define HAL_RX_UPDATE_RSSI_PER_CHAIN_BW(chain, word_1, word_2, \
					ppdu_info, rssi_info_tlv) \
	{						\
	ppdu_info->rssi_chain[chain][0] = \
			HAL_RX_GET(rssi_info_tlv, RECEIVE_RSSI_INFO_##word_1,\
				   RSSI_PRI20_CHAIN##chain); \
	ppdu_info->rssi_chain[chain][1] = \
			HAL_RX_GET(rssi_info_tlv, RECEIVE_RSSI_INFO_##word_1,\
				   RSSI_EXT20_CHAIN##chain); \
	ppdu_info->rssi_chain[chain][2] = \
			HAL_RX_GET(rssi_info_tlv, RECEIVE_RSSI_INFO_##word_1,\
				   RSSI_EXT40_LOW20_CHAIN##chain); \
	ppdu_info->rssi_chain[chain][3] = \
			HAL_RX_GET(rssi_info_tlv, RECEIVE_RSSI_INFO_##word_1,\
				   RSSI_EXT40_HIGH20_CHAIN##chain); \
	ppdu_info->rssi_chain[chain][4] = \
			HAL_RX_GET(rssi_info_tlv, RECEIVE_RSSI_INFO_##word_2,\
				   RSSI_EXT80_LOW20_CHAIN##chain); \
	ppdu_info->rssi_chain[chain][5] = \
			HAL_RX_GET(rssi_info_tlv, RECEIVE_RSSI_INFO_##word_2,\
				   RSSI_EXT80_LOW_HIGH20_CHAIN##chain); \
	ppdu_info->rssi_chain[chain][6] = \
			HAL_RX_GET(rssi_info_tlv, RECEIVE_RSSI_INFO_##word_2,\
				   RSSI_EXT80_HIGH_LOW20_CHAIN##chain); \
	ppdu_info->rssi_chain[chain][7] = \
			HAL_RX_GET(rssi_info_tlv, RECEIVE_RSSI_INFO_##word_2,\
				   RSSI_EXT80_HIGH20_CHAIN##chain); \
	}						\

#define HAL_RX_PPDU_UPDATE_RSSI(ppdu_info, rssi_info_tlv) \
	{HAL_RX_UPDATE_RSSI_PER_CHAIN_BW(0, 0, 1, ppdu_info, rssi_info_tlv) \
	 HAL_RX_UPDATE_RSSI_PER_CHAIN_BW(1, 2, 3, ppdu_info, rssi_info_tlv) \
	 HAL_RX_UPDATE_RSSI_PER_CHAIN_BW(2, 4, 5, ppdu_info, rssi_info_tlv) \
	 HAL_RX_UPDATE_RSSI_PER_CHAIN_BW(3, 6, 7, ppdu_info, rssi_info_tlv) \
	 HAL_RX_UPDATE_RSSI_PER_CHAIN_BW(4, 8, 9, ppdu_info, rssi_info_tlv) \
	 HAL_RX_UPDATE_RSSI_PER_CHAIN_BW(5, 10, 11, ppdu_info, rssi_info_tlv) \
	 HAL_RX_UPDATE_RSSI_PER_CHAIN_BW(6, 12, 13, ppdu_info, rssi_info_tlv) \
	 HAL_RX_UPDATE_RSSI_PER_CHAIN_BW(7, 14, 15, ppdu_info, rssi_info_tlv)} \

#define HAL_RX_PHY_CMN_USER_INFO0_GI		GENMASK(17, 16)

struct hal_phyrx_common_user_info {
	__le32 rsvd0[2];
	__le32 info0;
	__le32 rsvd1;
} __packed;

#define HAL_RX_MPDU_START_INFO0_ENCYRPT_TYP		GENMASK(5, 2)
#define HAL_RX_MPDU_START_INFO1_FILTER_CAT		GENMASK(1, 0)
#define HAL_RX_MPDU_START_INFO1_SW_GRP_ID		GENMASK(8, 2)
#define HAL_RX_MPDU_START_INFO1_PPDU_ID			GENMASK(31, 16)
#define HAL_RX_MPDU_START_INFO2_PEERID			GENMASK(29, 16)
#define HAL_RX_MPDU_START_INFO2_DEVICE_ID		GENMASK(31, 30)
#define HAL_RX_MPDU_START_INFO3_FC_VALID		BIT(0)
#define HAL_RX_MPDU_START_INFO3_ADDR2_VALID		BIT(3)
#define HAL_RX_MPDU_START_INFO3_TO_DS			BIT(17)
#define HAL_RX_MPDU_START_INFO3_MPDU_RETRY		BIT(19)
#define HAL_RX_MPDU_START_INFO4_DECAP_TYPE		GENMASK(11, 10)
#define HAL_RX_MPDU_START_INFO4_RAW_MPDU		BIT(30)
#define HAL_RX_MPDU_START_INFO5_MPDU_LEN		GENMASK(13, 0)
#define HAL_RX_MPDU_START_INFO5_MCAST_BCAST		BIT(15)
#define HAL_RX_MPDU_START_INFO6_FC_FIELD		GENMASK(15, 0)
#define HAL_RX_MPDU_START_INFO7_ADDR2_15_0		GENMASK(31, 16)
#define HAL_RX_MPDU_START_INFO8_ADDR2_47_16		GENMASK(31, 0)

/* The below hal_rx_mpdu_start structure is non-compact
 * structure for MPDU START TLV.
 * Only WCN chipset is using non-compact tlv structures.
 */
struct hal_rx_mpdu_start {
	__le32 rsvd0[7];
	__le32 info0;
	__le32 rsvd1;
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le32 info4;
	__le32 info5;
	__le32 info6;
	__le32 rsvd2;
	__le32 info7;
	__le32 info8;
	__le32 info9;
	__le32 info10;
	__le32 info11;
	__le32 info12;
	__le32 rsvd3[8];
} __packed;

#define HAL_RX_MSDU_END_INFO0_SW_FRAME_GRP_ID			GENMASK(8, 2)
#define HAL_RX_MSDU_END_INFO1_DECAP_FORMAT			GENMASK(9, 8)

/* The below hal_rx_msdu_end structure is non-compact
 * structure for MSDU END TLV.
 * Only WCN chipset is using non-compact tlv structures.
 */
struct hal_rx_msdu_end {
	__le32 info0;
	__le32 rsvd0[18];
	__le32 info1;
	__le32 rsvd1[10];
	__le32 info2;
	__le32 rsvd2;
} __packed;

#define HAL_TX_FES_STATUS_END_INFO0_START_TIMESTAMP_15_0	GENMASK(15, 0)
#define HAL_TX_FES_STATUS_END_INFO0_START_TIMESTAMP_31_16	GENMASK(31, 16)

struct hal_tx_fes_status_end {
	__le32 rsvd0[2];
	__le32 info0;
	__le32 reserved1[19];
} __packed;

#define HAL_TX_FES_SETUP_INFO0_NUM_OF_USERS	GENMASK(28, 23)

struct hal_tx_fes_setup {
	__le32 schedule_id;
	__le32 info0;
	__le64 rsvd0;
} __packed;

#define HAL_RX_RESP_REQ_INFO0_PPDU_ID		GENMASK(15, 0)
#define HAL_RX_RESP_REQ_INFO0_RECEPTION_TYPE	BIT(16)
#define HAL_RX_RESP_REQ_INFO1_DURATION		GENMASK(15, 0)
#define HAL_RX_RESP_REQ_INFO1_RATE_MCS		GENMASK(24, 21)
#define HAL_RX_RESP_REQ_INFO1_SGI		GENMASK(26, 25)
#define HAL_RX_RESP_REQ_INFO1_STBC		BIT(27)
#define HAL_RX_RESP_REQ_INFO1_LDPC		BIT(28)
#define HAL_RX_RESP_REQ_INFO1_IS_AMPDU		BIT(29)
#define HAL_RX_RESP_REQ_INFO2_NUM_USER		GENMASK(6, 0)
#define HAL_RX_RESP_REQ_INFO3_ADDR1_31_0	GENMASK(31, 0)
#define HAL_RX_RESP_REQ_INFO4_ADDR1_47_32	GENMASK(15, 0)
#define HAL_RX_RESP_REQ_INFO4_ADDR1_15_0	GENMASK(31, 16)
#define HAL_RX_RESP_REQ_INFO5_ADDR1_47_16	GENMASK(31, 0)

struct hal_rx_resp_req_info {
	__le32 info0;
	__le32 rsvd0[1];
	__le32 info1;
	__le32 info2;
	__le32 rsvd1[2];
	__le32 info3;
	__le32 info4;
	__le32 info5;
	__le32 rsvd2[5];
} __packed;

#define HAL_TX_PPDU_SETUP_INFO0_MEDIUM_PROT_TYPE	GENMASK(2, 0)
#define HAL_TX_PPDU_SETUP_INFO1_PROT_FRAME_ADDR1_31_0	GENMASK(31, 0)
#define HAL_TX_PPDU_SETUP_INFO2_PROT_FRAME_ADDR1_47_32	GENMASK(15, 0)
#define HAL_TX_PPDU_SETUP_INFO2_PROT_FRAME_ADDR2_15_0	GENMASK(31, 16)
#define HAL_TX_PPDU_SETUP_INFO3_PROT_FRAME_ADDR2_47_16	GENMASK(31, 0)
#define HAL_TX_PPDU_SETUP_INFO4_PROT_FRAME_ADDR3_31_0	GENMASK(31, 0)
#define HAL_TX_PPDU_SETUP_INFO5_PROT_FRAME_ADDR3_47_32	GENMASK(15, 0)
#define HAL_TX_PPDU_SETUP_INFO5_PROT_FRAME_ADDR4_15_0	GENMASK(31, 16)
#define HAL_TX_PPDU_SETUP_INFO6_PROT_FRAME_ADDR4_47_16	GENMASK(31, 0)

struct hal_tx_pcu_ppdu_setup_init {
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le32 rsvd0;
	__le32 info4;
	__le32 info5;
	__le32 info6;
} __packed;

#define HAL_TX_FES_STAT_PROT_INFO0_STRT_FRM_TS_15_0	GENMASK(15, 0)
#define HAL_TX_FES_STAT_PROT_INFO0_STRT_FRM_TS_31_16	GENMASK(31, 16)
#define HAL_TX_FES_STAT_PROT_INFO1_END_FRM_TS_15_0	GENMASK(15, 0)
#define HAL_TX_FES_STAT_PROT_INFO1_END_FRM_TS_31_16	GENMASK(31, 16)

struct hal_tx_fes_status_prot {
	__le64 rsvd0;
	__le32 info0;
	__le32 info1;
	__le32 rsvd1[11];
} __packed;

#define HAL_TX_FES_STAT_USR_PPDU_INFO0_DURATION		GENMASK(15, 0)

struct hal_tx_fes_status_user_ppdu {
	__le64 rsvd0;
	__le32 info0;
	__le32 rsvd1[3];
} __packed;

#define HAL_TX_FES_STAT_STRT_INFO0_PROT_TS_LOWER_32	GENMASK(31, 0)
#define HAL_TX_FES_STAT_STRT_INFO1_PROT_TS_UPPER_32	GENMASK(31, 0)

struct hal_tx_fes_status_start_prot {
	__le32 info0;
	__le32 info1;
	__le64 rsvd0;
} __packed;

#define HAL_TX_FES_STATUS_START_INFO0_MEDIUM_PROT_TYPE	GENMASK(29, 27)

struct hal_tx_fes_status_start {
	__le32 rsvd0;
	__le32 info0;
	__le64 rsvd1;
} __packed;

#define HAL_TX_Q_EXT_INFO0_FRAME_CTRL		GENMASK(15, 0)
#define HAL_TX_Q_EXT_INFO0_QOS_CTRL		GENMASK(31, 16)
#define HAL_TX_Q_EXT_INFO1_AMPDU_FLAG		BIT(0)

struct hal_tx_queue_exten {
	__le32 info0;
	__le32 info1;
} __packed;

#define HAL_RX_FBM_ACK_INFO0_ADDR1_31_0		GENMASK(31, 0)
#define HAL_RX_FBM_ACK_INFO1_ADDR1_47_32	GENMASK(15, 0)
#define HAL_RX_FBM_ACK_INFO1_ADDR2_15_0		GENMASK(31, 16)
#define HAL_RX_FBM_ACK_INFO2_ADDR2_47_16	GENMASK(31, 0)

struct hal_rx_frame_bitmap_ack {
	__le32 rsvd0;
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 rsvd1[10];
} __packed;

#define HAL_TX_PHY_DESC_INFO0_BF_TYPE		GENMASK(17, 16)
#define HAL_TX_PHY_DESC_INFO0_PREAMBLE_11B	BIT(20)
#define HAL_TX_PHY_DESC_INFO0_PKT_TYPE		GENMASK(24, 21)
#define HAL_TX_PHY_DESC_INFO0_BANDWIDTH		GENMASK(30, 28)
#define HAL_TX_PHY_DESC_INFO1_MCS		GENMASK(3, 0)
#define HAL_TX_PHY_DESC_INFO1_STBC		BIT(6)
#define HAL_TX_PHY_DESC_INFO2_NSS		GENMASK(23, 21)
#define HAL_TX_PHY_DESC_INFO3_AP_PKT_BW		GENMASK(6, 4)
#define HAL_TX_PHY_DESC_INFO3_LTF_SIZE		GENMASK(20, 19)
#define HAL_TX_PHY_DESC_INFO3_ACTIVE_CHANNEL	GENMASK(17, 15)

struct hal_tx_phy_desc {
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 info3;
} __packed;

struct hal_tlv_parsed_hdr {
	u16 tag;
	u16 len;
	u16 userid;
	u8 *data;
};

#define MPDU_START_SELECT_INFO0_ENCYRPT_TYP                      BIT(3)
#define MPDU_START_SELECT_INFO1_FILTER_GRPID_PPDUID              BIT(4)
#define MPDU_START_SELECT_INFO2_INFO3                            BIT(5)
#define MPDU_START_SELECT_INFO4_INFO5                            BIT(6)
#define MPDU_START_SELECT_INFO6_FC                               BIT(7)
#define MPDU_START_SELECT_INFO7_INFO8                            BIT(8)
#define MPDU_START_SELECT_INFO9_INF010                           BIT(9)
#define MPDU_START_SELECT_INF011_INFO12                          BIT(10)

#define RX_MON_MPDU_START_WMASK \
		(MPDU_START_SELECT_INFO0_ENCYRPT_TYP |                \
		 MPDU_START_SELECT_INFO1_FILTER_GRPID_PPDUID |        \
		 MPDU_START_SELECT_INFO2_INFO3 |                      \
		 MPDU_START_SELECT_INFO4_INFO5 |                      \
		 MPDU_START_SELECT_INFO6_FC |                         \
		 MPDU_START_SELECT_INFO7_INFO8 |                      \
		 MPDU_START_SELECT_INFO9_INF010 |                     \
		 MPDU_START_SELECT_INF011_INFO12)

#define HAL_RX_MPDU_START_INFO0_ENCYRPT_TYP_CMPCT		GENMASK(5, 2)
#define HAL_RX_MPDU_START_INFO1_FILTER_CAT_CMPCT		GENMASK(1, 0)
#define HAL_RX_MPDU_START_INFO1_SW_GRP_ID_CMPCT			GENMASK(8, 2)
#define HAL_RX_MPDU_START_INFO1_PPDU_ID_CMPCT			GENMASK(31, 16)
#define HAL_RX_MPDU_START_INFO2_PEERID_CMPCT			GENMASK(29, 16)
#define HAL_RX_MPDU_START_INFO2_DEVICE_ID_CMPCT			GENMASK(31, 30)
#define HAL_RX_MPDU_START_INFO3_FC_VALID_CMPCT			BIT(0)
#define HAL_RX_MPDU_START_INFO3_ADDR2_VALID_CMPCT		BIT(3)
#define HAL_RX_MPDU_START_INFO3_TO_DS_CMPCT			BIT(17)
#define HAL_RX_MPDU_START_INFO3_MPDU_RETRY_CMPCT		BIT(19)
#define HAL_RX_MPDU_START_INFO4_DECAP_TYPE_CMPCT		GENMASK(11, 10)
#define HAL_RX_MPDU_START_INFO4_RAW_MPDU_CMPCT			BIT(30)
#define HAL_RX_MPDU_START_INFO5_MPDU_LEN_CMPCT			GENMASK(13, 0)
#define HAL_RX_MPDU_START_INFO5_MCAST_BCAST_CMPCT		BIT(15)
#define HAL_RX_MPDU_START_INFO6_FC_FIELD_CMPCT			GENMASK(15, 0)
#define HAL_RX_MPDU_START_INFO7_ADDR2_15_0_CMPCT		GENMASK(31, 16)
#define HAL_RX_MPDU_START_INFO8_ADDR2_47_16_CMPCT		GENMASK(31, 0)

/* The below hal_rx_mon_mpdu_start_compact structure is tied with the mask value
 * RX_MON_MPDU_START_WMASK. If the mask value changes the structure will also
 * change.
 * ipq5424/5332, qcn6432/9274 uses common compact staructure as they share same
 * mpdu start wmask.
 */
struct hal_rx_mon_mpdu_start_compact {
	__le32 rsvd0;
	__le32 info0;
	__le32 rsvd1;
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le32 info4;
	__le32 info5;
	__le32 info6;
	__le32 rsvd2;
	__le32 info7;
	__le32 info8;
	__le32 info9;
	__le32 info10;
	__le32 info11;
	__le32 info12;
} __packed;

#define MSDU_END_SELECT_INFO0_SW_GRPID                         BIT(0)
#define MSDU_END_SELECT_INFO1_DECAP_FORMAT                     BIT(9)
#define MSDU_END_SELECT_INFO2_ERR_MAP                          BIT(15)

#define RX_MON_MSDU_END_WMASK \
		(MSDU_END_SELECT_INFO0_SW_GRPID |              \
		 MSDU_END_SELECT_INFO1_DECAP_FORMAT |          \
		 MSDU_END_SELECT_INFO2_ERR_MAP)

#define HAL_RX_MSDU_END_INFO0_SW_FRAME_GRP_ID_CMPCT		GENMASK(8, 2)
#define HAL_RX_MSDU_END_INFO1_DECAP_FORMAT_CMPCT		GENMASK(9, 8)

/* The below hal_rx_mon_msdu_end_compact structure is tied with the mask value
 * RX_MON_MSDU_END_WMASK. If the mask value changes the structure will also
 * change.
 * ipq5424/5332, qcn6432/9274 uses common compact staructure as they share same
 * msdu end wmask.
 */
struct hal_rx_mon_msdu_end_compact {
	__le32 info0;
	__le32 rsvd0[2];
	__le32 info1;
	__le32 info2;
	__le32 rsvd1;
} __packed;

static __always_inline void
ath12k_wifi6_hal_mon_parse_rx_msdu_end_err(u32 info, u32 *errmap)
{
	if (info & RX_MSDU_END_INFO13_FCS_ERR)
		*errmap |= HAL_RX_MON_MPDU_ERR_FCS;

	if (info & RX_MSDU_END_INFO13_DECRYPT_ERR)
		*errmap |= HAL_RX_MON_MPDU_ERR_DECRYPT;

	if (info & RX_MSDU_END_INFO13_TKIP_MIC_ERR)
		*errmap |= HAL_RX_MON_MPDU_ERR_TKIP_MIC;

	if (info & RX_MSDU_END_INFO13_A_MSDU_ERROR)
		*errmap |= HAL_RX_MON_MPDU_ERR_AMSDU_ERR;

	if (info & RX_MSDU_END_INFO13_OVERFLOW_ERR)
		*errmap |= HAL_RX_MON_MPDU_ERR_OVERFLOW;

	if (info & RX_MSDU_END_INFO13_MSDU_LEN_ERR)
		*errmap |= HAL_RX_MON_MPDU_ERR_MSDU_LEN;

	if (info & RX_MSDU_END_INFO13_MPDU_LEN_ERR)
		*errmap |= HAL_RX_MON_MPDU_ERR_MPDU_LEN;
}

enum hal_rx_mon_status
ath12k_wifi6_hal_mon_rx_parse_status_tlv(struct ath12k_hal *hal,
                                        struct hal_rx_mon_ppdu_info *ppdu_info,
                                        struct hal_tlv_parsed_hdr *tlv_parsed_hdr);
enum hal_tx_mon_status
ath12k_wifi6_hal_mon_tx_parse_status_tlv(struct hal_tx_mon_ppdu_info *tx_ppdu_info,
					 u16 tlv_tag, const void *tlv_data, u32 userid);
enum hal_tx_mon_status
ath12k_wifi6_hal_mon_tx_status_get_num_user(u16 tlv_tag,
					    const void *tx_tlv,
					    u8 *num_users);
u32 ath12k_wifi6_hal_mon_rx_mpdu_start_wmask_get(void);
u32 ath12k_wifi6_hal_mon_rx_mpdu_end_wmask_get(void);
u32 ath12k_wifi6_hal_mon_rx_msdu_end_wmask_get(void);
void
ath12k_wifi6_hal_mon_rx_mpdu_start_info_parse(const void *tlv_data, u32 userid,
					      struct hal_rx_mon_ppdu_info *ppdu_info,
					      u32 tlv_len);
void
ath12k_wifi6_hal_mon_rx_msdu_end_info_parse(const void *tlv_data, u32 userid,
					    struct hal_rx_mon_ppdu_info *ppdu_info,
					    u32 tlv_len);
u8 *ath12k_wifi6_hal_mon_rx_desc_get_msdu_payload(void *rx_desc);
#endif
