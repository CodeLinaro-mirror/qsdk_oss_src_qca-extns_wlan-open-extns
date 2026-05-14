/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef DP_TX_EXTN_H
#define DP_TX_EXTN_H

#include "../core.h"
#include "../dp.h"
#include "../wifi7/dp_tx.h"

struct ath12k_dp_tx_msdu_info;
struct ath12k_dp_skb_ctrl;
struct ath12k_tx_desc_info;

bool ath12k_dp_tx_dma_map(struct ath12k_dp *dp, struct sk_buff *skb, u32 len,
			  struct ath12k_tx_desc_info *tx_desc,
			  struct ath12k_dp_tx_msdu_info *msdu_info,
			  struct ath12k_dp_skb_ctrl *skb_ctrl);

void ath12k_dp_tx_buffer_unmap(struct device *dev, dma_addr_t dma_handle, size_t size,
			       enum dma_data_direction direction);

static inline void ath12k_dmb(void)
{
	dmb(oshst);
}

#endif // DP_TX_EXTN_H
