// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include "../../debug.h"
#include "../../dp_mon.h"
#include "dp_rx.h"
#include "hal_qcn9074.h"

int ath12k_wifi6_dp_rx_link_desc_return(struct ath12k_dp *dp,
					struct ath12k_buffer_addr *buf_addr_info,
					enum hal_wbm_rel_bm_act action)
{
	struct hal_wbm_release_ring *desc;
	struct ath12k_base *ab = dp->ab;
	struct hal_srng *srng;
	int ret = 0;

	srng = &ab->hal.srng_list[dp->wbm_desc_rel_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	desc = ath12k_hal_srng_src_get_next_entry(ab, srng);
	if (!desc) {
		ret = -ENOBUFS;
		goto exit;
	}

	ath12k_wifi6_hal_rx_msdu_link_desc_set(ab, desc, buf_addr_info, action);

exit:
	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return ret;
}

int ath12k_wifi6_dp_rx_process(struct ath12k_dp *dp, int ring_id,
			       struct napi_struct *napi, int budget)
{
	return 0;
}

int ath12k_wifi6_dp_rx_process_err(struct ath12k_dp *dp, struct napi_struct *napi,
				   int budget)
{
	return 0;
}

int ath12k_wifi6_dp_rx_process_wbm_err(struct ath12k_dp *dp,
				       struct napi_struct *napi, int budget)
{
	return 0;
}

int ath12k_wifi6_dp_rxdma_ring_sel_config_qcn9274(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct htt_rx_ring_tlv_filter tlv_filter = {0};
	u32 ring_id;
	int ret;
	u32 hal_rx_desc_sz = ab->hal.hal_desc_sz;

	ring_id = dp->rx_refill_buf_ring.refill_buf_ring.ring_id;

	tlv_filter.rx_filter = HTT_RX_TLV_FLAGS_RXDMA_RING;
	tlv_filter.rxmon_disable = true;
	tlv_filter.enable_fp = 1;
	tlv_filter.fp_ctrl_filter = FILTER_CTRL_BA_REQ;
	tlv_filter.fp_data_filter = FILTER_DATA_UCAST | FILTER_DATA_MCAST |
				    FILTER_DATA_NULL;
	tlv_filter.offset_valid = true;
	tlv_filter.rx_packet_offset = hal_rx_desc_sz;

	tlv_filter.rx_mpdu_start_offset =
		ath12k_wifi6_hal_rx_desc_get_mpdu_start_offset_qcn9274();
	tlv_filter.rx_msdu_end_offset =
		ath12k_wifi6_hal_rx_desc_get_msdu_end_offset_qcn9274();

	tlv_filter.rx_mpdu_start_wmask =
			ath12k_wifi6_hal_rx_mpdu_start_wmask_get_qcn9274();
	tlv_filter.rx_msdu_end_wmask =
			ath12k_wifi6_hal_rx_msdu_end_wmask_get_qcn9274();

	ath12k_dbg(ab, ATH12K_DBG_DATA,
		   "Configuring compact tlv masks rx_mpdu_start_wmask 0x%x rx_msdu_end_wmask 0x%x\n",
		   tlv_filter.rx_mpdu_start_wmask, tlv_filter.rx_msdu_end_wmask);

	ret = ath12k_dp_tx_htt_rx_filter_setup(ab, ring_id, 0,
					       HAL_RXDMA_BUF,
					       DP_RX_BUFFER_SIZE,
					       &tlv_filter);

	return ret;
}

void ath12k_wifi6_dp_rx_process_reo_status(struct ath12k_dp *dp)
{
}

int ath12k_wifi6_dp_rx_htt_setup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	u32 ring_id;
	int i, ret;

	/* TODO: Need to verify the HTT setup for QCN9224 */
	ring_id = dp->rx_refill_buf_ring.refill_buf_ring.ring_id;
	ret = ath12k_dp_tx_htt_srng_setup(ab, ring_id, 0, HAL_RXDMA_BUF);
	if (ret) {
		ath12k_warn(ab, "failed to configure rx_refill_buf_ring %d\n",
			    ret);
		return ret;
	}

	if (ab->hw_params->rx_mac_buf_ring) {
		for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
			ring_id = dp->rx_mac_buf_ring[i].ring_id;
			ret = ath12k_dp_tx_htt_srng_setup(ab, ring_id,
							  i, HAL_RXDMA_BUF);
			if (ret) {
				ath12k_warn(ab, "failed to configure rx_mac_buf_ring%d %d\n",
					    i, ret);
				return ret;
			}
		}
	}

	for (i = 0; i < ab->hw_params->num_rxdma_dst_ring; i++) {
		ring_id = dp->rxdma_err_dst_ring[i].ring_id;
		ret = ath12k_dp_tx_htt_srng_setup(ab, ring_id,
						  i, HAL_RXDMA_DST);
		if (ret) {
			ath12k_warn(ab, "failed to configure rxdma_err_dest_ring%d %d\n",
				    i, ret);
			return ret;
		}
	}

	ret = ath12k_dp_mon_rx_htt_setup(dp);
	if (ret) {
		ath12k_warn(ab, "Failed to setup rxdma monitor rings\n");
		return ret;
	}

	ret = ab->hw_params->hw_ops->rxdma_ring_sel_config(ab);
	if (ret) {
		ath12k_warn(ab, "failed to setup rxdma ring selection config\n");
		return ret;
	}

	return 0;
}

void ath12k_wifi6_dp_pdev_free(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k *ar;
	int i;

	spin_lock_bh(&dp->dp_lock);
	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		rcu_assign_pointer(dp->dp_pdevs[ar->pdev_idx], NULL);
	}
	spin_unlock_bh(&dp->dp_lock);

	synchronize_rcu();

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		ath12k_fw_stats_free(&ar->fw_stats);

		if (ar->dp.dp_mon_pdev_configured) {
			ath12k_dp_mon_pdev_rx_free(&ar->dp);
			ath12k_dp_mon_pdev_deinit(&ar->dp);

			ar->dp.dp_mon_pdev_configured = false;
		}
	}

	ath12k_dp_ppeds_stop(ab);
}

int ath12k_wifi6_dp_pdev_alloc(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_pdev_dp *dp_pdev;
	struct ath12k *ar;
	int ret;
	int i;

#ifdef CPTCFG_ATH12K_PPE_DS_SUPPORT
	ret = ath12k_dp_ppe_rxole_rxdma_cfg(ab);
	if (ret) {
		ath12k_err(ab, "Failed to send htt RxOLE and RxDMA messages to target :%d\n",
			   ret);
		goto out;
	}
#endif

	ret = ath12k_wifi6_dp_rx_htt_setup(ab);
	if (ret)
		goto out;

	/* TODO: Per-pdev rx ring unlike tx ring which is mapped to different AC's */
	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;

		memset(&ar->stats, 0, sizeof(struct ath12k_pdev_ctrl_path_stats));
		dp_pdev = &ar->dp;

		dp_pdev->hw = ar->ah->hw;
		dp_pdev->dp = dp;
		/* Below linking is a temporary linking to handle few cases like cac
		 * timeout, active pdev etc in dp rx. Some flags/fileds can be added
		 * in dp_pdev to remove ar dependencies in the performance critical
		 * path.
		 *
		 * TODO: remove this once those dependencies are resolved.
		 */
		dp_pdev->ar = ar;
		dp_pdev->dp_hw = &ar->ah->dp_hw;
		dp_pdev->hw_link_id = ar->hw_link_id;

		if (!dp_pdev->dp_mon_pdev_configured) {
			ret = ath12k_dp_mon_pdev_init(dp_pdev);
			if (ret) {
				ath12k_warn(ab, "failed to initialize mon pdev %d\n", i);
				goto err;
			}

			ret = ath12k_dp_mon_pdev_rx_alloc(dp_pdev, i);
			if (ret)
				goto err;

			ret = ath12k_dp_mon_pdev_rx_htt_setup(dp_pdev, i);
			if (ret)
				goto err;

			dp_pdev->dp_mon_pdev_configured = true;
		}
	}

	ret = ath12k_dp_ppeds_start(ab);
	if (ret) {
		ath12k_err(ab, "failed to start DP PPEDS\n");
		goto err;
	}

	spin_lock_bh(&dp->dp_lock);
	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		rcu_assign_pointer(dp->dp_pdevs[ar->pdev_idx], &ar->dp);
	}
	spin_unlock_bh(&dp->dp_lock);

	dp->num_radios = ab->num_radios;

	return ret;
err:
	ath12k_wifi6_dp_pdev_free(ab);
	ath12k_dp_ppeds_stop(ab);
out:
	return ret;
}

void ath12k_wifi6_dp_rx_ring_free(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int i;

	ath12k_dp_srng_cleanup(ab, &dp->rx_refill_buf_ring.refill_buf_ring);

	for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
		if (ab->hw_params->rx_mac_buf_ring)
			ath12k_dp_srng_cleanup(ab, &dp->rx_mac_buf_ring[i]);
	}

	for (i = 0; i < ab->hw_params->num_rxdma_dst_ring; i++)
		ath12k_dp_srng_cleanup(ab, &dp->rxdma_err_dst_ring[i]);

	ath12k_dp_srng_cleanup(ab, &dp->rx_rel_ring);
	ath12k_dp_rx_reo_cleanup(ab);
}

int ath12k_wifi6_dp_rx_ring_setup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int i, ret;

	ret = ath12k_dp_rx_reo_setup(ab);
	if (ret) {
		ath12k_err(ab, "failed to initialize reo destination rings: %d\n", ret);
		return ret;
	}

	ret = ath12k_dp_srng_setup(ab, &dp->rx_rel_ring, HAL_WBM2SW_RELEASE,
				   HAL_WBM2SW_REL_ERR_RING_NUM, 0,
				   DP_RX_RELEASE_RING_SIZE);
	if (ret) {
		ath12k_warn(ab, "failed to set up rx_rel ring :%d\n", ret);
		return ret;
	}

	ret = ath12k_dp_srng_setup(ab,
				   &dp->rx_refill_buf_ring.refill_buf_ring,
				   HAL_RXDMA_BUF, 0, 0,
				   DP_RXDMA_BUF_RING_SIZE);
	if (ret) {
		ath12k_warn(ab, "failed to setup rx_refill_buf_ring\n");
		return ret;
	}

	if (ab->hw_params->rx_mac_buf_ring) {
		for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
			ret = ath12k_dp_srng_setup(ab,
						   &dp->rx_mac_buf_ring[i],
						   HAL_RXDMA_BUF, 1,
						   i, DP_RX_MAC_BUF_RING_SIZE);
			if (ret) {
				ath12k_warn(ab, "failed to setup rx_mac_buf_ring %d\n",
					    i);
				return ret;
			}
		}
	}

	for (i = 0; i < ab->hw_params->num_rxdma_dst_ring; i++) {
		ret = ath12k_dp_srng_setup(ab, &dp->rxdma_err_dst_ring[i],
					   HAL_RXDMA_DST, 0, i,
					   DP_RXDMA_ERR_DST_RING_SIZE);
		if (ret) {
			ath12k_warn(ab, "failed to setup rxdma_err_dst_ring %d\n", i);
			return ret;
		}
	}

	ret = ath12k_dp_rxdma_buf_setup(ab);
	if (ret) {
		ath12k_warn(ab, "failed to setup rxdma ring\n");
		return ret;
	}

	return 0;
}
