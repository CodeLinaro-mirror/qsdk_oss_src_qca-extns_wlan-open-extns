/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_ipa.h"
#include "../../../dp_peer.h"
#include "../../../peer.h"
#include "../../../mac.h"
#include "../../../dp.h"
#include "../../dp_tx.h"
#include "../../hal.h"
#include <linux/iommu.h>
#include "../../../qcn_extns/ipa/dp_ipa.h"
#include "../../dp.h"

static u8 ath12k_wifi7_dp_ipa_get_wdi_version(void)
{
	return IPA_WDI_4;
}

static int ath12k_wifi7_dp_ipa_setup_rx_refill_buf_ring(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int ring_id;
	int ret;

	ret = ath12k_dp_srng_setup(ab,
				   &dp->rx_refill_buf_ring2.refill_buf_ring,
				   HAL_RXDMA_BUF, 2, 0, ATH12K_IPA_RX_REFILL_BUF_RING);

	if (ret) {
		ath12k_err(ab, "Failed to setup rx_refill_buf_ring2 for IPA\n");
		return ret;
	}

	ring_id = dp->rx_refill_buf_ring2.refill_buf_ring.ring_id;
	ret = ath12k_dp_tx_htt_srng_setup(ab, ring_id, 0, HAL_RXDMA_BUF);
	if (ret) {
		ath12k_err(ab, "Failed to rAL_RXDMA_BUF ring for IPA\n");
		return ret;
	}

	return 0;
}

static void ath12k_wifi7_dp_ipa_srng_get_rsrc(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct hal_srng *srng;
	struct hal_srng_params params;
	struct ath12k_ipa_dp_tx_rsc *tx_rsc = &IPA_CTX(ab)->ipa_uc_tx_rsc;
	struct ath12k_ipa_dp_rx_rsc *rx_rsc = &IPA_CTX(ab)->ipa_uc_rx_rsc;
	int ring_id;
	struct device *dev = ab->dev;
	struct iommu_domain *domain = ath12k_dp_ipa_smmu_get_domain(ab);
	struct scatterlist *sg;
	int i, ret;
	unsigned long base_pa_addr, offset;

	ring_id = dp->tx_ring[ATH12K_IPA_TCL_RING].tcl_data_ring.ring_id;

	srng = &ab->hal.srng_list[ring_id];
	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);
	tx_rsc->ipa_tcl_ring_base_paddr = params.ring_base_paddr;
	tx_rsc->ipa_tcl_ring_base_vaddr = params.ring_base_vaddr;
	tx_rsc->ipa_tcl_ring_size = (params.num_entries * srng->entry_size) << 2;

	offset = (unsigned long)((unsigned long)(srng->u.src_ring.hp_addr_direct) -
			(unsigned long)ab->mem);
	base_pa_addr = (unsigned long)ab->ath12k_base_extn.mem_pa;
	tx_rsc->ipa_tcl_hp_paddr = (dma_addr_t)(offset + base_pa_addr);

	ath12k_info(ab, "TCL_HP_ADDR:%llx srng_HP_addr:%pad", tx_rsc->ipa_tcl_hp_paddr,
		    &srng->u.src_ring.hp_addr);

	if (domain)
		tx_rsc->tcl_pa = iommu_iova_to_phys(domain,
						    tx_rsc->ipa_tcl_ring_base_paddr);
	else
		tx_rsc->tcl_pa = tx_rsc->ipa_tcl_ring_base_paddr;
	ret = dma_get_sgtable(dev, &tx_rsc->sgtable_tcl,
			      tx_rsc->ipa_tcl_ring_base_vaddr,
			      tx_rsc->tcl_pa, tx_rsc->ipa_tcl_ring_size);
	if (ret) {
		ath12k_err(ab, "Unable to get DMA sgtable");
		return;
	}

	for_each_sg((&tx_rsc->sgtable_tcl)->sgl, sg, (&tx_rsc->sgtable_tcl)->nents, i) {
		if (!sg)
			break;
		sg->dma_address = sg_phys(sg);
	}
	i = 0;

	ring_id = dp->tx_ring[ATH12K_IPA_TX_COMP_RING].tcl_comp_ring.ring_id;
	srng = &ab->hal.srng_list[ring_id];
	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);
	tx_rsc->ipa_wbm_ring_base_paddr = params.ring_base_paddr;
	tx_rsc->ipa_wbm_ring_base_vaddr = params.ring_base_vaddr;
	tx_rsc->ipa_wbm_ring_size = (params.num_entries * srng->entry_size) << 2;
	offset = (unsigned long)((unsigned long)(srng->u.dst_ring.tp_addr_direct) -
			(unsigned long)ab->mem);
	base_pa_addr = (unsigned long)ab->ath12k_base_extn.mem_pa;
	tx_rsc->ipa_wbm_tp_paddr = (dma_addr_t)(offset + base_pa_addr);

	tx_rsc->ipa_wbm_hp_shadow_paddr = ath12k_hal_srng_get_hp_addr(ab, srng);

	if (domain)
		tx_rsc->wbm_pa = iommu_iova_to_phys(domain,
						    tx_rsc->ipa_wbm_ring_base_paddr);
	else
		tx_rsc->wbm_pa = tx_rsc->ipa_wbm_ring_base_paddr;
	ret = dma_get_sgtable(dev, &tx_rsc->sgtable_wbm,
			      tx_rsc->ipa_wbm_ring_base_vaddr,
			      tx_rsc->wbm_pa, tx_rsc->ipa_wbm_ring_size);
	if (ret) {
		ath12k_err(ab, "Unable to get DMA sgtable");
		return;
	}

	for_each_sg((&tx_rsc->sgtable_wbm)->sgl, sg, (&tx_rsc->sgtable_wbm)->nents, i) {
		if (!sg)
			break;
		sg->dma_address = sg_phys(sg);
	}
	i = 0;

	/* IPA REO_DEST ring - HAL_REO_DST REO2SW4 */
	ring_id = dp->reo_dst_ring[ATH12K_IPA_REO_DST_RING].ring_id;
	srng = &ab->hal.srng_list[ring_id];
	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);
	rx_rsc->ipa_reo_ring_base_paddr = params.ring_base_paddr;
	rx_rsc->ipa_reo_ring_base_vaddr = params.ring_base_vaddr;
	rx_rsc->ipa_reo_ring_size = (params.num_entries * srng->entry_size) << 2;
	offset = (unsigned long)((unsigned long)(srng->u.dst_ring.tp_addr_direct) -
			(unsigned long)ab->mem);
	base_pa_addr = (unsigned long)ab->ath12k_base_extn.mem_pa;
	rx_rsc->ipa_reo_tp_paddr = (dma_addr_t)(offset + base_pa_addr);

	if (domain)
		rx_rsc->reo_pa = iommu_iova_to_phys(domain,
						    rx_rsc->ipa_reo_ring_base_paddr);
	else
		rx_rsc->reo_pa = rx_rsc->ipa_reo_ring_base_paddr;
	ret = dma_get_sgtable(dev, &rx_rsc->sgtable_reo,
			      rx_rsc->ipa_reo_ring_base_vaddr,
			      rx_rsc->reo_pa, rx_rsc->ipa_reo_ring_size);
	if (ret) {
		ath12k_err(ab, "Unable to get DMA sgtable");
		return;
	}

	for_each_sg((&rx_rsc->sgtable_reo)->sgl, sg, (&rx_rsc->sgtable_reo)->nents, i) {
		if (!sg)
			break;
		sg->dma_address = sg_phys(sg);
	}
	i = 0;

	/* IPA RX REFILL BUF ring - HAL_RXDMA_BUF SW2RXDMA_BUF1 */
	ret = ath12k_wifi7_dp_ipa_setup_rx_refill_buf_ring(ab);
	if (ret) {
		ath12k_ipa_err(ab, "failed to setup rx refill_buf_ring");
		return;
	}

	ring_id = dp->rx_refill_buf_ring2.refill_buf_ring.ring_id;
	srng = &ab->hal.srng_list[ring_id];
	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);
	rx_rsc->ipa_rx_refill_buf_ring_base_paddr = params.ring_base_paddr;
	rx_rsc->ipa_rx_refill_buf_ring_base_vaddr = params.ring_base_vaddr;
	rx_rsc->ipa_rx_refill_buf_ring_size = (params.num_entries * srng->entry_size) << 2;
	rx_rsc->ipa_rx_refill_buf_hp_paddr = ath12k_hal_srng_get_hp_addr(ab, srng);

	ret = ath12k_dp_ipa_get_shared_mem_info(ab, &rx_rsc->sgtable_refill,
					  rx_rsc->ipa_rx_refill_buf_ring_base_vaddr,
					  rx_rsc->ipa_rx_refill_buf_ring_base_paddr,
					  rx_rsc->ipa_rx_refill_buf_ring_size);
	if (ret) {
		ath12k_ipa_err(ab, "failed to get shared mem info");
		return;
	}

	if (domain) {
		rx_rsc->refill_pa = iommu_iova_to_phys(domain,
					rx_rsc->ipa_rx_refill_buf_ring_base_paddr);
		rx_rsc->ipa_rx_refill_buf_hp_paddr = iommu_iova_to_phys(domain,
					rx_rsc->ipa_rx_refill_buf_hp_paddr);
	} else {
		rx_rsc->refill_pa = rx_rsc->ipa_rx_refill_buf_ring_base_paddr;
	}
	i = 0;
}

static void ath12k_wifi7_dp_ipa_set_tx_doorbell_paddr(struct ath12k_ipa *ipa_ctx)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ipa_ctx->ab);
	struct hal_srng *wbm_srng;
	u64 hp_addr = ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_paddr;
	int ring_id;

	ring_id = dp->tx_ring[ATH12K_IPA_TX_COMP_RING].tcl_comp_ring.ring_id;
	wbm_srng = &ipa_ctx->ab->hal.srng_list[ring_id];

	ath12k_info(ipa_ctx->ab, "Setting up the TX completion DB address to "
			"wbm_ring_id:%d dp_ring_id:%d srng_ring_id:%d Base_addr:%x "
			"LSB_ADDR:%x MSB_ADDR:%x DB_MAP_addr:%llx",
			   ATH12K_IPA_TX_COMP_RING,
			   ring_id,
			   wbm_srng->ring_id,
			   HAL_SEQ_WCSS_UMAC_WBM_REG,
			   HAL_WBM2SW3_RING_HP_LSB_ADDR,
			   HAL_WBM2SW3_RING_HP_MSB_ADDR,
			   hp_addr);

	ath12k_hif_write32(ipa_ctx->ab, HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM2SW3_RING_HP_LSB_ADDR,
			   hp_addr & HAL_ADDR_LSB_REG_MASK);
	ath12k_hif_write32(ipa_ctx->ab, HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM2SW3_RING_HP_MSB_ADDR,
			   hp_addr >> HAL_ADDR_MSB_REG_SHIFT);
}

static void ath12k_wifi7_dp_ipa_set_rx_doorbell_paddr(struct ath12k_ipa *ipa_ctx)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ipa_ctx->ab);
	struct hal_srng *reo_srng;
	u64 hp_addr = ipa_ctx->ipa_uc_rx_rsc.rx_ready_doorbell_paddr;
	int ring_id;

	ring_id = dp->reo_dst_ring[ATH12K_IPA_REO_DST_RING].ring_id;
	reo_srng = &ipa_ctx->ab->hal.srng_list[ring_id];

	ath12k_info(ipa_ctx->ab, "Setting up the RX ready DB address to reo_ring_id:%d ring_id:%d, reo_srng->ring_id:%d Base_addr:%x LSB_ADDR:%x MSB_ADDR:%x DB_MAP_addr:%llx",
		    ATH12K_IPA_REO_DST_RING,
		    ring_id,
		    reo_srng->ring_id,
		    HAL_SEQ_WCSS_UMAC_REO_REG,
		    HAL_REO2SW4_RING_HP_LSB_ADDR,
		    HAL_REO2SW4_RING_HP_MSB_ADDR,
		    hp_addr);

	ath12k_hif_write32(ipa_ctx->ab, HAL_SEQ_WCSS_UMAC_REO_REG +
			   HAL_REO2SW4_RING_HP_LSB_ADDR,
			   hp_addr & HAL_ADDR_LSB_REG_MASK);
	ath12k_hif_write32(ipa_ctx->ab, HAL_SEQ_WCSS_UMAC_REO_REG +
			   HAL_REO2SW4_RING_HP_MSB_ADDR,
			   hp_addr >> HAL_ADDR_MSB_REG_SHIFT);
}

/* Done with DB address */
static void ath12k_wifi7_dp_ipa_tx_comp_ring_init_hp(struct ath12k_ipa *ipa_ctx)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ipa_ctx->ab);
	struct hal_srng *wbm_srng;
	u32 cached_hp;
	int ring_id;

	ring_id = dp->tx_ring[ATH12K_IPA_TX_COMP_RING].tcl_comp_ring.ring_id;
	wbm_srng = &ipa_ctx->ab->hal.srng_list[ring_id];

	wbm_srng->u.dst_ring.hp_addr =
		(u32 __iomem *)ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_vaddr;

	/* Map the IPA doorbell TX completion vaddr with ring HP address,
	 * and update the ring HP address in DB vaddr
	 */
	cached_hp = wbm_srng->u.dst_ring.cached_hp;

	/* Updating the HP with cached HP value */
	ath12k_hif_write32(ipa_ctx->ab, HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM2SW3_RING_HP_VAL,
			   cached_hp & HAL_ADDR_LSB_REG_MASK);

	if (ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_vaddr)
		*wbm_srng->u.dst_ring.hp_addr = cached_hp;
}

static u32 ath12k_wifi7_dp_ipa_rx_default_routing(u32 reo_dest)
{
	if (dp_ipa_enable())
		return ATH12K_IPA_REO_DST_RING+1;
	else
		return reo_dest;
}

static inline int ath12k_wifi7_dp_ipa_rxdma_buf_setup(struct ath12k_base *ab)
{
	return ath12k_dp_rxdma_buf_setup(ab);
}

/* IPA BANK ID handling and PMAC ID handling */
static void
ath12k_wifi7_dp_ipa_tx_set_bank_config(u32 *bank_config)
{
	*bank_config |= u32_encode_bits(0, HAL_TX_BANK_CONFIG_ENCRYPT_TYPE);

	/* TODO: Dont hadrcode encap type, fix this based on pkt type currently setting to
	 * HAL_TCL_ENCAP_TYPE_ETHERNET
	 */
	*bank_config |= u32_encode_bits(2, HAL_TX_BANK_CONFIG_ENCAP_TYPE);

	*bank_config |= u32_encode_bits(0, HAL_TX_BANK_CONFIG_SRC_BUFFER_SWAP) |
			u32_encode_bits(0, HAL_TX_BANK_CONFIG_LINK_META_SWAP) |
			u32_encode_bits(0, HAL_TX_BANK_CONFIG_EPD);

	/* This is a limition for IPA, sta offload support require 1 for lookup_en,
	 * going to fix this on Amboseli AI:@Devender
	 */
	*bank_config |= u32_encode_bits(0, HAL_TX_BANK_CONFIG_INDEX_LOOKUP_EN);
	*bank_config |= u32_encode_bits(1, HAL_TX_BANK_CONFIG_ADDRX_EN);
	*bank_config |= u32_encode_bits(1, HAL_TX_BANK_CONFIG_ADDRY_EN);

	*bank_config |= u32_encode_bits(0, HAL_TX_BANK_CONFIG_MESH_EN);

	*bank_config |= u32_encode_bits(0, HAL_TX_BANK_CONFIG_VDEV_ID_CHECK_EN);

	*bank_config |= u32_encode_bits(0, HAL_TX_BANK_CONFIG_DSCP_TIP_MAP_ID);
	*bank_config |= u32_encode_bits(0, HAL_TX_BANK_CONFIG_PMAC_ID);
}

static int
ath12k_wifi7_dp_ipa_get_tx_bank_id(struct ath12k_base *ab, u8 *bid)
{
	u32 bank_config;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	*bid = ab->hw_params->num_tcl_banks - 1;
	ath12k_wifi7_dp_ipa_tx_set_bank_config(&bank_config);

	spin_lock_bh(&dp->tx_bank_lock);
	if (!dp->bank_profiles[*bid].is_configured) {
		dp->bank_profiles[*bid].is_configured = true;
		dp->bank_profiles[*bid].num_users++;
	}
	dp->bank_profiles[*bid].bank_config = bank_config;
	spin_unlock_bh(&dp->tx_bank_lock);

	ath12k_hal_tx_configure_bank_register(ab,  bank_config, *bid);
	return 0;
}

static int
ath12k_wifi7_dp_ipa_rx_buf_smmu_map_unmap(struct ath12k_base *ab, bool create)
{
	int ret = 0;
	struct sk_buff *skb;
	struct ath12k_rx_desc_info *rx_desc;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int i, j;

	if (!ipa_is_ready()) {
		ath12k_err(ab, "IPA is not READY");
		return 0;
	}

	/* Iterate RX descriptor pages and entries to locate active SKB's
	 * Unmap only buffers that were mapped to IPA, we don't free the buffer.
	 */
	spin_lock_bh(&dp->rx_desc_lock);
	for (i = 0; i < ATH12K_NUM_RX_SPT_PAGES; i++) {
		rx_desc = dp->rxbaddr[i];
		if (!rx_desc)
			continue;

		for (j = 0; j < ATH12K_MAX_SPT_ENTRIES; j++) {
			skb = rx_desc[j].skb;
			if (!skb)
				continue;

			/* Unmap only if previously mapped to IPA */
			if (ATH12K_SKB_CB(skb)->flags & ATH12K_SKB_IPA_MAP_UNMAP) {
				ret = ath12k_dp_ipa_handle_buf_smmu_map_unmap(ab, skb,
									 DP_RX_BUFFER_SIZE,
									 0,
									 ab->ath12k_base_extn.ipa_ctx->hdl);
			}
		}
	}
	spin_unlock_bh(&dp->rx_desc_lock);

	return ret;
}

/* Allocate TX buffers for IPA */
static
int ath12k_wifi7_dp_ipa_tx_buffer_attach(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct hal_srng *wbm_srng;
	struct sk_buff *skb;
	struct hal_srng_params srng_params;
	struct ath12k_buffer_addr *ring_entry;
	dma_addr_t buffer_paddr;
	u32 tx_buffer_count;
	u32 ring_base_align = 8;
	int num_entries;
	int retval = 0;
	int max_alloc_count = 0;
	u8 ring_id;
	u8 rbm_id;

	unsigned int uc_tx_buf_sz = ATH12K_IPA_UC_TX_BUF_SIZE_DEFAULT;
	unsigned int alloc_size = uc_tx_buf_sz + ring_base_align - 1;

	ring_id = dp->tx_ring[ATH12K_IPA_TX_COMP_RING].tcl_comp_ring.ring_id;
	wbm_srng = &ab->hal.srng_list[ring_id];
	memset(&srng_params, 0, sizeof(srng_params));
	ath12k_hal_srng_get_params(ab, wbm_srng, &srng_params);
	rbm_id = ab->hal.tcl_to_cmp_rbm_map[ATH12K_IPA_TX_COMP_RING].rbm_id;

	num_entries = srng_params.num_entries;

	max_alloc_count =
		num_entries - ATH12K_IPA_WAR_WBM2SW_REL_RING_NO_BUF_ENTRIES;
	if (max_alloc_count <= 0) {
		ath12k_err(ab, "incorrect value for buffer count %u", max_alloc_count);
		return -EINVAL;
	}

	IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned = kmalloc(num_entries *
			sizeof(*IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned),
			GFP_KERNEL);
	if (!IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned) {
		ath12k_err(ab, "IPA WBM Ring Tx buf pool vaddr alloc fail");
		return -ENOMEM;
	}

	ath12k_info(ab, "IPA: ALLOCTE tx buffer count:%d dev:%p dev_name:%s",
		    max_alloc_count, dp->dev, dev_name(dp->dev));
	/*
	 * Allocate Tx buffers as many as possible.
	 * Leave DP_IPA_WAR_WBM2SW_REL_RING_NO_BUF_ENTRIES empty
	 * Populate Tx buffers into WBM2IPA ring
	 * This initial buffer population will simulate H/W as source ring,
	 * and update HP
	 */
	spin_lock_bh(&wbm_srng->lock);
	ath12k_hal_srng_access_begin(ab, wbm_srng);
	for (tx_buffer_count = 0;
		tx_buffer_count < max_alloc_count - 1; tx_buffer_count++) {
		skb = dev_alloc_skb(alloc_size +
				    ATH12K_IPA_TX_BUFFER_ALIGN_SIZE);
		if (!skb)
			break;

		if (!IS_ALIGNED((unsigned long)skb->data,
				ATH12K_IPA_TX_BUFFER_ALIGN_SIZE)) {
			skb_pull(skb,
				 PTR_ALIGN(skb->data, ATH12K_IPA_TX_BUFFER_ALIGN_SIZE) -
				 skb->data);
		}

		buffer_paddr = dma_map_single(dp->dev, skb->data,
				       skb->len + skb_tailroom(skb),
				       DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dp->dev, buffer_paddr))
			goto fail_free_skb;

		ATH12K_SKB_CB(skb)->paddr = buffer_paddr;

		ring_entry = ath12k_hal_srng_dst_get_next_hp_entry(ab, wbm_srng);
		if (!ring_entry) {
			ath12k_err(ab, "Failed to get WBM TX completion ring next entry");
			goto fail_dma_unmap;
		}

		ath12k_hal_rx_buf_addr_info_set(ring_entry, buffer_paddr, 0,
						rbm_id);

		IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned[tx_buffer_count]
			= (void *)skb;
	}

	ath12k_hal_srng_access_end(ab, wbm_srng);
	spin_unlock_bh(&wbm_srng->lock);

	IPA_CTX(ab)->ipa_uc_tx_rsc.alloc_tx_buf_cnt = tx_buffer_count;

	if (tx_buffer_count) {
		ath12k_info(ab, "IPA WDI TX buffer: %d allocated", tx_buffer_count);
	} else {
		ath12k_err(ab, "No IPA WDI TX buffer allocated!");
		kfree(IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned);
		IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned = NULL;
		retval = -ENOMEM;
	}
	return retval;

fail_dma_unmap:
	dma_unmap_single(dp->dev, buffer_paddr, skb->len + skb_tailroom(skb),
			 DMA_BIDIRECTIONAL);
fail_free_skb:
	spin_unlock_bh(&wbm_srng->lock);
	dev_kfree_skb_any(skb);
	kfree(IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned);
	IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned = NULL;
	return retval;
}

static struct ath12k_ipa_arch_ops ipa_wifi7_arch_ops = {
	.dp_ipa_init = ath12k_wifi7_dp_ipa_init,
	.dp_srng_cleanup = NULL,
	.dp_srng_setup = NULL,
	.dp_srng_get_rsrc = ath12k_wifi7_dp_ipa_srng_get_rsrc,
	.dp_rx_default_routing = ath12k_wifi7_dp_ipa_rx_default_routing,
	.dp_rxdma_buf_setup = ath12k_wifi7_dp_ipa_rxdma_buf_setup,
	.dp_get_tx_bank_id = ath12k_wifi7_dp_ipa_get_tx_bank_id,
	.dp_rx_buf_smmu_map_unmap = ath12k_wifi7_dp_ipa_rx_buf_smmu_map_unmap,
	.ipa_set_rx_doorbell_paddr = ath12k_wifi7_dp_ipa_set_rx_doorbell_paddr,
	.ipa_set_tx_doorbell_paddr = ath12k_wifi7_dp_ipa_set_tx_doorbell_paddr,
	.ipa_tx_comp_ring_init_hp = ath12k_wifi7_dp_ipa_tx_comp_ring_init_hp,
	.ipa_tx_buffer_attach = ath12k_wifi7_dp_ipa_tx_buffer_attach,
	.dp_rx_pipe_set_rdi = NULL,
	.ipa_get_wdi_version = ath12k_wifi7_dp_ipa_get_wdi_version,
};

int
ath12k_wifi7_dp_ipa_init(struct ath12k_base *ab)
{
	int ret;

	ret = ath12k_dp_ipa_plugin_register_ops_extn(ab);
	if (ret)
		return ret;

	ab->ath12k_base_extn.ipa_ctx->arch_ops = &ipa_wifi7_arch_ops;

	return ret;
}
