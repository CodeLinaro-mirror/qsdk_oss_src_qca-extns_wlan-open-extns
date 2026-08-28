/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_CMN_EXTN_H
#define ATH12K_CMN_EXTN_H

#include <linux/mhi.h>
#include <linux/uuid.h>
#include <linux/workqueue.h>
#include "../../net/mac80211/qcn_extns/cmn_extn.h"


struct ath12k_base;
struct sk_buff;
struct ieee80211_240mhz_vendor_oper_extn;
struct ieee80211_ops_extn;
struct ieee80211_iface_combination;
struct ath12k_vif;

struct ath12k_vif_extn {
	struct list_head peer_240mhz_list_extn;
	spinlock_t data_lock;
};

struct ath12k_link_vif_extn {
#ifdef CPTCFG_EXT_IPA_STATS
	struct ath12k_dp_ipa_stats_vif_hw_stats *stats;
#endif
};

struct sta_240mhz_info {
	struct ieee80211_240mhz_vendor_oper_extn params_240MHz_extn;
	u8 mac_addr[ETH_ALEN];
	struct list_head list;
};

#ifndef CPTCFG_QCN_EXTN

static inline void
ath12k_mac_hw_allocate_extn(struct ieee80211_hw *hw,
			    const struct ieee80211_ops_extn *ops_extn)
{
	return;
}

static inline void ath12k_wifi7_hw_init_extn(struct ath12k_base *ab)
{
	return;
}

static inline void ath12k_mac_init_arvif_extn(struct ath12k_vif *ahvif)
{
	return;
}

static inline void ath12k_wmi_peer_migration_event_extn(struct ath12k_vif *ahvif)
{
	return;
}

static inline int
ath12k_add_sta_240mhz_info_extn(struct ath12k_vif *ahvif,
				u8 *mac,
				struct ieee80211_240mhz_vendor_oper_extn
				*params)
{
	return -1;
}

static inline void ath12k_mac_setup_radio_iface_comb_extn(
		struct ieee80211_iface_combination *comb)
{
	return;
}

static inline void ath12k_dp_ipa_pre_reset(struct ath12k_base *ab)
{
	return;
}

static inline void ath12k_dp_ipa_post_reset(struct ath12k_base *ab)
{
	return;
}

static inline bool ath12k_dp_ipa_rxdma_ring_setup(struct ath12k_base *ab)
{
	return false;
}

static inline void ath12k_dp_ipa_srng_hw_disable(struct ath12k_base *ab)
{
	return;
}

static inline void ath12k_dp_ipa_rx_desc_dma_unmap(struct ath12k_base *ab,
						     struct sk_buff *skb)
{
	return;
}

static inline void ath12k_dp_rx_ring_hp_resync(struct ath12k_base *ab)
{
	return;
}

#else

void ath12k_mac_hw_allocate_extn(struct ieee80211_hw *hw,
			  const struct ieee80211_ops_extn *ops_extn);

void ath12k_wifi7_hw_init_extn(struct ath12k_base *ab);

void ath12k_mac_init_arvif_extn(struct ath12k_vif *ahvif);

void ath12k_wmi_peer_migration_event_extn(struct ath12k_vif *ahvif);

int ath12k_add_sta_240mhz_info_extn(struct ath12k_vif *ahvif,
				     u8 *mac,
				     struct ieee80211_240mhz_vendor_oper_extn
				     *params);

void ath12k_mac_setup_radio_iface_comb_extn(
		struct ieee80211_iface_combination *comb);

#ifdef CPTCFG_EXT_IPA_OFFLOAD
/**
 * ath12k_dp_ipa_pre_reset() - Disable IPA pipes before SRNG ring reset.
 *
 * Called in the Mode-2 SSR pre_reset phase to prevent GSI from accessing
 * WLAN ring buffers after the rings are torn down.
 */
void ath12k_dp_ipa_pre_reset(struct ath12k_base *ab);

/**
 * ath12k_dp_ipa_post_reset() - Re-enable IPA pipes after SRNG ring reinit.
 *
 * Called after all SRNG rings are reinitialized.  Forces a TX doorbell HP
 * update (since rings are reset to HP/TP = 0) and reconfigures the REO
 * hash mapping before re-enabling the WDI pipes.
 */
void ath12k_dp_ipa_post_reset(struct ath12k_base *ab);

/**
 * ath12k_dp_ipa_rxdma_ring_setup() - IPA-specific RXDMA ring (re)initialization.
 *
 * Called from ath12k_dp_rxdma_ring_setup() so that IPA-only ring setup
 * (rx_refill_buf_ring2 and, where required, the per-pdev RX MAC buffer
 * rings) stays out of the common function's control flow.
 *
 * Return: true if IPA offload owns rx_refill_buf_ring replenishment (the
 * caller must skip its own common replenish), false otherwise.
 */
bool ath12k_dp_ipa_rxdma_ring_setup(struct ath12k_base *ab);

/**
 * ath12k_dp_ipa_srng_hw_disable() - Disable the IPA-related RXDMA SRNGs.
 *
 * Called from ath12k_dp_srng_hw_ring_disable() so that IPA-only SRNG
 * disable calls stay out of the common function's control flow.
 */
void ath12k_dp_ipa_srng_hw_disable(struct ath12k_base *ab);

/**
 * ath12k_dp_ipa_rx_desc_dma_unmap() - DMA-unmap an RX descriptor's buffer.
 *
 * Called from ath12k_dp_umac_txrx_desc_cleanup() and ath12k_dp_cc_cleanup()
 * so that the IPA-only DMA unmap stays out of the common cleanup loops.
 */
void ath12k_dp_ipa_rx_desc_dma_unmap(struct ath12k_base *ab, struct sk_buff *skb);

/**
 * ath12k_dp_rx_ring_hp_resync() - Resync rx_refill_buf_ring HP after an
 * IPA-owned UMAC reset.
 *
 * Called from ath12k_umac_reset_handle_post_reset_complete() to push the
 * shadow HP value back to the HW register once IRQs are re-enabled.
 */
void ath12k_dp_rx_ring_hp_resync(struct ath12k_base *ab);
#else
static inline void ath12k_dp_ipa_pre_reset(struct ath12k_base *ab) {}
static inline void ath12k_dp_ipa_post_reset(struct ath12k_base *ab) {}
static inline bool ath12k_dp_ipa_rxdma_ring_setup(struct ath12k_base *ab) { return false; }
static inline void ath12k_dp_ipa_srng_hw_disable(struct ath12k_base *ab) {}
static inline void ath12k_dp_ipa_rx_desc_dma_unmap(struct ath12k_base *ab,
						     struct sk_buff *skb) {}
static inline void ath12k_dp_rx_ring_hp_resync(struct ath12k_base *ab) {}
#endif /* CPTCFG_EXT_IPA_OFFLOAD */

struct ath12k_pci_extn {
	/* SMMU DS */
#ifdef CPTCFG_EXT_IPA_OFFLOAD
	struct dma_iommu_mapping *smmu_mapping;
	struct iommu_domain *iommu_domain;
	u8 smmu_s1_enable;
	dma_addr_t smmu_iova_start;
	size_t smmu_iova_len;
	dma_addr_t smmu_iova_ipa_start;
	dma_addr_t smmu_iova_ipa_current;
	size_t smmu_iova_ipa_len;
#endif
};

struct ath12k_base_extn {
#ifdef CPTCFG_EXT_IPA_OFFLOAD
	struct ath12k_wlan_ipa_priv *ipa_ctx;
	/* This is to hold the rx_buffers allocated initially in RXDMA for IPA
	 * as in UD we make use of common API for allocating RX buffer initially
	 * and for refilling the buffers.
	 * IPA ops are not initialized by the time RX buffers allocated initially
	 * 1. hence store the RX buffers in ath12k_base and once IPA is up map them
	 * 2. for any further refilling, check if IPA ops are initialized and
	 *    map the buffer from ath12k_rx_bufs_replinsh
	 */
	bool rx_init_buf_mapped;
	void **rx_buf_pool;
	u32 rx_buf_cnt;
	dma_addr_t mem_pa;
#endif
};

struct ath12k_dp_umac_reset_extn {
#ifdef CPTCFG_EXT_IPA_OFFLOAD
	/* Used on IPA-offload targets where pipe enable/disable requires
	 * process context (IPA driver uses wait calls internally).
	 */
	struct work_struct reset_work;
	/* Dedicated WQ_UNBOUND|WQ_HIGHPRI workqueue so the reset work item
	 * is scheduled at the highest normal priority (nice -20) on any
	 * available CPU, minimising latency relative to the firmware timeline.
	 */
	struct workqueue_struct *reset_wq;
#endif
};

#endif /* CPTCFG_QCN_EXTN */

#ifdef CPTCFG_EXT_IPA_OFFLOAD
struct ath12k_base;
int ath12k_dp_ipa_rx_refill_buf_ring_alloc(struct ath12k_base *ab);
void ath12k_dp_ipa_rx_refill_buf_ring_free(struct ath12k_base *ab);
#endif /* CPTCFG_EXT_IPA_OFFLOAD */

#endif /* ATH12K_CMN_EXTN_H */
