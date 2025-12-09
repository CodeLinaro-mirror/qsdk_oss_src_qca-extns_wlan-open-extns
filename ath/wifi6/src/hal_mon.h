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

#define HAL_RX_FRAMECTRL_TYPE_MASK 0x0C
#define HAL_RX_GET_FRAME_CTRL_TYPE(fc) \
	(((fc) & HAL_RX_FRAMECTRL_TYPE_MASK) >> 2)
#define HAL_RX_FRAME_CTRL_TYPE_MGMT 0x0
#define HAL_RX_FRAME_CTRL_TYPE_CTRL 0x1
#define HAL_RX_FRAME_CTRL_TYPE_DATA 0x2

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
u8 *ath12k_wifi6_hal_mon_rx_desc_get_msdu_payload(void *rx_desc);
#endif
