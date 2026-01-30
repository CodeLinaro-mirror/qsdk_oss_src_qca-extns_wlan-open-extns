/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "../../dp_mon.h"

extern const struct ath12k_dp_arch_mon_ops ath12k_wifi6_dp_arch_mon_quad_ring_ops;
extern const struct ath12k_dp_arch_mon_ops ath12k_wifi6_dp_arch_mon_dual_ring_ops;

static inline
void ath12k_wifi6_dp_mon_ops_register(struct ath12k_dp *dp)
{
	struct ath12k_dp_mon *dp_mon = dp->dp_mon;

	dp_mon->mon_ops = &ath12k_wifi6_dp_arch_mon_quad_ring_ops;
}

static inline
int ath12k_dp_rx_mon_process_ring(struct ath12k_dp *dp, int mac_id,
				  struct napi_struct *napi, int budget)
{
	struct ath12k_pdev_dp *dp_pdev;
	const struct ath12k_dp_arch_mon_ops *mon_ops;
	u8 pdev_id = ath12k_hw_mac_id_to_pdev_id(dp->hw_params, mac_id);
	int num_buffs_reaped = 0;

	mon_ops = ath12k_dp_mon_ops_get(dp);
	rcu_read_lock();

	dp_pdev = ath12k_dp_to_dp_pdev(dp, pdev_id);
	if (unlikely(!dp_pdev || !dp_pdev->dp_mon_pdev)) {
		rcu_read_unlock();
		return 0;
	}

	if (mon_ops && mon_ops->mon_rx_srng_process) {
		num_buffs_reaped =
			mon_ops->mon_rx_srng_process(dp_pdev, mac_id,
							napi, &budget);
	}

	rcu_read_unlock();

	return num_buffs_reaped;
}

int ath12k_wifi6_dp_mon_rx_srng_setup(struct ath12k_dp *dp);
void ath12k_wifi6_dp_mon_rx_srng_cleanup(struct ath12k_dp *dp);
int ath12k_wifi6_dp_mon_rx_buf_setup(struct ath12k_dp *dp);
void ath12k_wifi6_dp_mon_rx_buf_free(struct ath12k_dp *dp);
int ath12k_wifi6_dp_mon_rx_htt_srng_setup(struct ath12k_dp *dp);
int ath12k_wifi6_dp_mon_pdev_rx_srng_setup(struct ath12k_pdev_dp *dp_pdev, u32 mac_id);
void ath12k_wifi6_dp_mon_pdev_rx_srng_cleanup(struct ath12k_pdev_dp *dp_pdev);
int ath12k_wifi6_dp_mon_pdev_rx_htt_srng_setup(struct ath12k_pdev_dp *dp_pdev, u32 mac_id);
int ath12k_wifi6_mon_setup_mon_link_desc(struct ath12k_pdev_dp *dp_pdev);
int ath12k_wifi6_dp_mon_rx_quad_ring_process(struct ath12k_pdev_dp *pdev_dp,
					     int mac_id, struct napi_struct *napi,
					     int *budget);
void ath12k_wifi6_dp_mon_rx_monitor_mode_set(struct ath12k_pdev_dp *dp_pdev);
void ath12k_wifi6_dp_mon_rx_monitor_mode_reset(struct ath12k_pdev_dp *dp_pdev);
int ath12k_wifi6_dp_mon_rx_update_ring_filter(struct ath12k_pdev_dp *pdev_dp);
void ath12k_wifi6_dp_mon_rx_mon_mode_config_filter(struct ath12k_pdev_dp *dp_pdev);
void ath12k_wifi6_dp_mon_rx_mon_mode_reset_filter(struct ath12k_pdev_dp *dp_pdev);

