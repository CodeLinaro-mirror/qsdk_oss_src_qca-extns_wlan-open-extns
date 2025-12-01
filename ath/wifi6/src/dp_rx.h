/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_RX_WIFI6_H
#define ATH12K_DP_RX_WIFI6_H

#include "../../dp_rx.h"

#define DP_BA_WIN_SZ_MAX_WIFI6 256

int ath12k_wifi6_dp_rx_process_wbm_err(struct ath12k_dp *dp,
				       struct napi_struct *napi, int budget);
int ath12k_wifi6_dp_rx_process_err(struct ath12k_dp *dp, struct napi_struct *napi,
				   int budget);
int ath12k_wifi6_dp_rx_process(struct ath12k_dp *dp, int mac_id,
			       struct napi_struct *napi,
			       int budget);
int ath12k_wifi6_dp_rx_link_desc_return(struct ath12k_dp *dp,
					struct ath12k_buffer_addr *buf_addr_info,
					enum hal_wbm_rel_bm_act action);
void ath12k_wifi6_dp_rx_process_reo_status(struct ath12k_dp *dp);
int ath12k_wifi6_dp_rxdma_ring_sel_config_qcn9274(struct ath12k_base *ab);

static inline
void ath12k_wifi6_dp_extract_rx_spd_data(struct ath12k_hal *hal,
					 struct hal_rx_spd_data *rx_info,
					 struct hal_rx_desc *rx_desc, int set)
{
	hal->hal_ops->extract_rx_spd_data(rx_info, rx_desc, set);
}

static inline
void ath12k_wifi6_dp_extract_rx_desc_data(struct ath12k_dp *dp,
					  struct hal_rx_desc_data *rx_desc_data,
					  struct hal_rx_desc *rx_desc,
					  struct hal_rx_desc *ldesc)
{
	dp->hw_params->hal_ops->extract_rx_desc_data(rx_desc_data, rx_desc, ldesc);
}

void ath12k_wifi6_dp_rx_ring_free(struct ath12k_base *ab);
int ath12k_wifi6_dp_rx_ring_setup(struct ath12k_base *ab);
#endif
