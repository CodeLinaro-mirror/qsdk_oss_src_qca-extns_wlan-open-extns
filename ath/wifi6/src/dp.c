// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include "../../core.h"
#include "../../debug.h"
#include "../../dp_rx.h"
#include "../../hif.h"
#include "../../dp_cmn.h"
#include "dp_rx.h"
#include "dp.h"
#include "hal.h"

static int ath12k_wifi6_dp_service_srng(struct ath12k_dp *dp,
					struct ath12k_ext_irq_grp *irq_grp,
					int budget)
{
	struct napi_struct *napi = &irq_grp->napi;
	int grp_id = irq_grp->grp_id;
	int work_done = 0;
	int i = 0;
	int tot_work_done = 0;
	u8 rx_mask;

	rx_mask = dp->hw_params->ring_mask->rx[grp_id];

	if (dp->hw_params->ring_mask->rx_err[grp_id]) {
		work_done = ath12k_wifi6_dp_rx_process_err(dp, napi, budget);
		budget -= work_done;
		tot_work_done += work_done;
		if (budget <= 0)
			goto done;
	}

	if (dp->hw_params->ring_mask->rx_wbm_rel[grp_id]) {
		work_done = ath12k_wifi6_dp_rx_process_wbm_err(dp, napi, budget);
		budget -= work_done;
		tot_work_done += work_done;

		if (budget <= 0)
			goto done;
	}

	while (rx_mask) {
		i = fls(rx_mask) - 1;
		rx_mask ^= 1 << i;
		work_done = ath12k_wifi6_dp_rx_process(dp, i, napi, budget);
		budget -= work_done;
		tot_work_done += work_done;
		if (budget <= 0)
			goto done;
	}
/*
 *	if (dp->hw_params->ring_mask->rx_mon_status[grp_id]) {
		ring_mask = dp->hw_params->ring_mask->rx_mon_status[grp_id];
		for (i = 0; i < dp->num_radios; i++) {
			for (j = 0; j < dp->hw_params->num_rxdma_per_pdev; j++) {
				int id = i * dp->hw_params->num_rxdma_per_pdev + j;

				if (ring_mask & BIT(id)) {
					work_done =
					ath12k_dp_rx_mon_process_ring(dp, id,
								      napi, budget);
					budget -= work_done;
					tot_work_done += work_done;
					if (budget <= 0)
						goto done;
				}
			}
		}
	}
	 */

/*
 *	if (dp->hw_params->ring_mask->rx_mon_dest[grp_id]) {
		ring_mask = dp->hw_params->ring_mask->rx_mon_dest[grp_id];
		for (i = 0; i < dp->num_radios; i++) {
			for (j = 0; j < dp->hw_params->num_rxdma_per_pdev; j++) {
				int id = i * dp->hw_params->num_rxdma_per_pdev + j;

				if (ring_mask & BIT(id)) {
					work_done =
					ath12k_dp_rx_mon_process_ring(dp, id,
								      napi, budget);
					budget -= work_done;
					tot_work_done += work_done;

					if (budget <= 0)
						goto done;
				}
			}
		}
	}
	 */

/*
 *	if (dp->hw_params->ring_mask->tx_mon_dest[grp_id]) {
		ring_mask = dp->hw_params->ring_mask->tx_mon_dest[grp_id];
		for (i = 0; i < dp->num_radios; i++) {
			for (j = 0; j < dp->hw_params->num_rxdma_per_pdev; j++) {
				int id = i * dp->hw_params->num_rxdma_per_pdev + j;

				if (ring_mask & BIT(id)) {
					work_done =
					ath12k_dp_tx_mon_process_ring(dp, id, napi,
								      budget);
					budget -= work_done;
					tot_work_done += work_done;

					if (budget <= 0)
						goto done;
				}
			}
		}
	}
	 */
	if (dp->hw_params->ring_mask->reo_status[grp_id]) {
		ath12k_wifi6_dp_rx_process_reo_status(dp);
	}

	if (dp->hw_params->ring_mask->host2rxdma[grp_id]) {
		struct dp_rxdma_ring *rx_ring = &dp->rx_refill_buf_ring;
		LIST_HEAD(list);
		size_t req_entries;

		req_entries = ath12k_dp_get_req_entries_from_buf_ring(dp->ab, rx_ring, &list);
		if (req_entries)
			ath12k_dp_rx_bufs_replenish(dp, rx_ring, &list);
	}

/*
 *	if (dp->hw_params->ring_mask->host2rxmon[grp_id])
		ath12k_dp_mon_rx_process_low_thres(dp);

*/
	// TODO: Implement handler for other interrupts

done:
	return tot_work_done;
}

static int ath12k_wifi6_dp_op_device_init(struct ath12k_dp *dp)
{
	int ret;
	struct ath12k_base *ab = dp->ab;
	struct hal_srng *srng = NULL;
	u32 n_link_desc = 0;

	ret = ath12k_hif_ext_irq_setup(dp->ab, ath12k_wifi6_dp_service_srng, dp);
	if (ret)
		return ret;

	dp->idle_link_rbm =
			ath12k_hal_get_idle_link_rbm(&ab->hal, ab->device_id);

	ret = ath12k_wbm_idle_ring_setup(ab, &n_link_desc);
	if (ret) {
		ath12k_warn(ab, "failed to setup wbm_idle_ring: %d\n", ret);
		goto fail_irq_cleanup;
	}

	srng = &ab->hal.srng_list[dp->wbm_idle_ring.ring_id];

	ret = ath12k_dp_link_desc_setup(ab, dp->link_desc_banks,
					HAL_WBM_IDLE_LINK, srng, n_link_desc);
	if (ret) {
		ath12k_warn(ab, "failed to setup link desc: %d\n", ret);
		goto fail_irq_cleanup;
	}

	ret = ath12k_dp_srng_setup(ab, &dp->wbm_desc_rel_ring,
				   HAL_SW2WBM_RELEASE, 0, 0,
				   DP_WBM_RELEASE_RING_SIZE);
	if (ret) {
		ath12k_warn(ab, "failed to set up wbm2sw_release ring :%d\n",
			    ret);
		goto fail_link_desc_cleanup;
	}

	ret = ath12k_wifi6_dp_rx_ring_setup(ab);
	if (ret) {
		ath12k_warn(ab, "rx allod failed ret = %d\n", ret);
		goto fail_dp_rx_free;
	}

/*
	ret = ath12k_dp_mon_rx_alloc(dp);
	if (ret) {
		ath12k_warn(ab, "failed to setup rxdma rings ret = %d\n", ret);
		goto fail_dp_mon_rx_free;
	}
*/
	return 0;

/*
 * fail_dp_mon_rx_free:
	ath12k_dp_mon_rx_free(dp);
*/

fail_dp_rx_free:
	ath12k_wifi6_dp_rx_ring_free(ab);
	ath12k_dp_srng_cleanup(ab, &dp->wbm_desc_rel_ring);

fail_link_desc_cleanup:
	ath12k_dp_link_desc_cleanup(ab, dp->link_desc_banks,
				    HAL_WBM_IDLE_LINK, &dp->wbm_idle_ring);
fail_irq_cleanup:
	ath12k_hif_ext_irq_cleanup(dp->ab);

	return ret;

}

static void ath12k_wifi6_dp_op_device_deinit(struct ath12k_dp *dp)
{
	struct ath12k_base *ab = dp->ab;

	if (!dp->ab)
		return;

	ath12k_dp_link_desc_cleanup(ab, dp->link_desc_banks,
				    HAL_WBM_IDLE_LINK, &dp->wbm_idle_ring);
	ath12k_dp_srng_cleanup(ab, &dp->wbm_desc_rel_ring);
/*
	ath12k_dp_mon_rx_free(dp);
*/
	ath12k_wifi6_dp_rx_ring_free(ab);

	ath12k_hif_ext_irq_cleanup(dp->ab);
}

static struct ath12k_dp_hw_group *ath12k_wifi6_dp_hw_group_alloc(void)
{
	struct ath12k_dp_hw_group *dp_hw_grp;

	dp_hw_grp = kzalloc(sizeof(*dp_hw_grp), GFP_KERNEL);
	if (!dp_hw_grp)
		return NULL;
	return dp_hw_grp;
}

static struct ath12k_dp_arch_ops ath12k_wifi6_dp_arch_ops = {
	.dp_op_device_init = ath12k_wifi6_dp_op_device_init,
	.dp_op_device_deinit = ath12k_wifi6_dp_op_device_deinit,
	.rx_link_desc_return = ath12k_wifi6_dp_rx_link_desc_return,
	.dp_hw_group_alloc = ath12k_wifi6_dp_hw_group_alloc,
	.dp_pdev_alloc = ath12k_wifi6_dp_pdev_alloc,
	.dp_pdev_free = ath12k_wifi6_dp_pdev_free,
};

/* TODO: remove export once this file is built with wifi6 ko */
struct ath12k_dp *ath12k_wifi6_dp_init(struct ath12k_base *ab)
{
	struct ath12k_dp *dp;
	int ret;

	dp = kzalloc(sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return NULL;

	dp->arch_ops = &ath12k_wifi6_dp_arch_ops;

	dp->ab = ab;
	dp->dev = ab->dev;
	dp->hw_params = ab->hw_params;
	dp->hal = &ab->hal;

	ret = ath12k_dp_mon_init(dp);
	if (ret) {
		ath12k_warn(dp, "dp_mon_init failed %d\n", ret);
		goto dp_err;
	}

	return dp;
dp_err:
	ath12k_wifi6_dp_deinit(dp);
	return NULL;
}

void ath12k_wifi6_dp_deinit(struct ath12k_dp *dp)
{
	ath12k_dp_mon_deinit(dp);
	kfree(dp);
}
