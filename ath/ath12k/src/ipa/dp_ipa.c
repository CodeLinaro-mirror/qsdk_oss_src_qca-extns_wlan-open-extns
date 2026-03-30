/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/iommu.h>
#include <linux/qcom-iommu-util.h>
#include "dp_ipa.h"
#include "../../wmi.h"
#include "../../dp_peer.h"

static void ath12k_dp_ipa_w2i(void *priv, enum ipa_dp_evt_type evt, unsigned long data);

/* global control for IPA status */
atomic_t g_instances_added;

static bool g_ipa_is_ready;
static struct ath12k_ipa_config *g_ipa_config;
static DEFINE_MUTEX(g_ipa_config_lock);
static int ath12k_dp_ipa_setup_rx_refill_buf_ring(struct ath12k_base *ab);

/* DE Bruijn Sequence to fetch the instance id */
static const int deBruijnIdx[32] = {
	0, 1, 28, 2, 29, 14, 24, 3,
	30, 22, 20, 15, 25, 17, 4, 8,
	31, 27, 13, 23, 21, 19, 16, 7,
	26, 12, 18, 6, 11, 5, 10, 9
};

/* IPA config update */
void ath12k_dp_ipa_component_config_init(void)
{
	g_ipa_config->ipa_config = 0x6d;

	g_ipa_config->desc_size = 800;
	g_ipa_config->txbuf_count = 512;
	g_ipa_config->ipa_bw_high = 400;
	g_ipa_config->ipa_bw_medium = 200;
	g_ipa_config->ipa_bw_low = 100;
	g_ipa_config->bus_bw_high = 2000;
	g_ipa_config->bus_bw_medium = 500;
	g_ipa_config->bus_bw_low = 150;
	g_ipa_config->ipa_force_voting = false;
	g_ipa_config->ipa_wds = 0;
	g_ipa_config->ipa_vlan_support = 0;
}
/* Done IPA config update */

/* IPA attach/detach */
int ath12k_dp_ipa_global_config_alloc(void)
{
	guard(mutex)(&g_ipa_config_lock);
	if (g_ipa_config)
		return 0;

	g_ipa_config = kmalloc(sizeof(struct ath12k_ipa_config), GFP_KERNEL);
	if (!g_ipa_config)
		return -ENOMEM;
	else 
		ath12k_dp_ipa_component_config_init();
	return 0;
}

void ath12k_dp_ipa_config_free(void)
{
	if (!atomic_read(&g_instances_added)) {
		mutex_lock(&g_ipa_config_lock);
		if (!g_ipa_config) {
			mutex_unlock(&g_ipa_config_lock);
			return;
		} else {
			kfree(g_ipa_config);
			g_ipa_config = NULL;
			mutex_unlock(&g_ipa_config_lock);
		}
	}
}

int ath12k_dp_ipa_attach(struct ath12k_base *ab)
{
	IPA_CTX(ab) = kmalloc(sizeof(struct ath12k_ipa), GFP_KERNEL);

	if (!IPA_CTX(ab)) {
		ath12k_err(ab, "Failed to allocate IPA_CTX !!");
		return -ENOMEM;
	} else {
		ath12k_info(ab ,"IPA_CTX allocated for ab");
		if (ath12k_dp_ipa_global_config_alloc()) {
			ath12k_err(ab, "Global config allocation Failed !!");
			return -ENOMEM;
                }
		IPA_CTX(ab)->ab = ab;
		return 0;
	}
}

void ath12k_dp_ipa_detach(struct ath12k_base *ab)
{
	if (IPA_CTX(ab)) {
		kfree(IPA_CTX(ab));
		IPA_CTX(ab) = NULL;
		ath12k_dp_ipa_config_free();
	} else {
		ath12k_err(ab, "Invalid Free called !!");
	}
}

/* DONE IPA attach/detach */

/* SMMU DEV settings */

int ath12k_dp_ipa_get_smmu_enabled(void)
{
	struct ipa_smmu_in_params params_in;
	struct ipa_smmu_out_params params_out;

	params_in.smmu_client = IPA_SMMU_WLAN_CLIENT;
	ipa_get_smmu_params(&params_in, &params_out);

	return params_out.smmu_enable;
}

int ath12k_dp_ipa_iommu_get_attr(struct iommu_domain *domain, enum iommu_attr attr,
				 int *data)
{
	int mapping_config;
	int mapping_bitmap;

	mapping_bitmap = attr;
	if (mapping_bitmap < 0)
		return -EINVAL;

	/* Return Bitmask of SMMU MAPPING configs */
	mapping_config = qcom_iommu_get_mappings_configuration(domain);
	if (mapping_config < 0)
		return -EINVAL;

	*data = (mapping_config & mapping_bitmap) ? 1 : 0;

	return 0;
}

static int ath12k_dp_ipa_smmu_mem_map_setup(struct ath12k_base *ab, bool ipa_present)
{
	struct iommu_domain *domain;
	bool ipa_smmu_enabled;
	bool wlan_smmu_enabled;
	struct ath12k_pci *pci_priv = ath12k_pci_priv(ab);
	int attr = 0;
	int errno = 0;

	domain = ath12k_dp_ipa_smmu_get_domain(ab);
	if (domain) {
		attr = 0;
		errno = ath12k_dp_ipa_iommu_get_attr(domain,
						     ATH12K_IOMMU_MAPPING_CONF_S1_BYPASS,
						     &attr);

		wlan_smmu_enabled = !errno && !attr;
	} else {
		ath12k_err(ab, "No SMMU mapping present");
		wlan_smmu_enabled = false;
	}

	if (!wlan_smmu_enabled) {
		pci_priv->ath12k_pci_extn.smmu_s1_enable = false;
		goto exit_with_success;
	}

	if (!ipa_present) {
		pci_priv->ath12k_pci_extn.smmu_s1_enable = true;
		goto exit_with_success;
	}

	ipa_smmu_enabled = ath12k_dp_ipa_get_smmu_enabled();

	pci_priv->ath12k_pci_extn.smmu_s1_enable = ipa_smmu_enabled &&
							wlan_smmu_enabled;
	if (ipa_smmu_enabled != wlan_smmu_enabled) {
		ath12k_err(ab, "SMMU mismatch; IPA:%s, WLAN:%s",
				ipa_smmu_enabled ? "enabled" : "disabled",
				wlan_smmu_enabled ? "enabled" : "disabled");
		return -EFAULT;
	}

exit_with_success:

	ath12k_info(ab, "SMMU S1 %s",
		    pci_priv->ath12k_pci_extn.smmu_s1_enable ? "enabled" : "disabled");

	return 0;
}

/* Done with SMMU settings */



/* Set Ix register */
void ath12k_dp_ipa_setup_ix_reg(struct ath12k_base *ab)
{
	if (!dp_ipa_enable())
		return;

	ath12k_hal_reo_hw_setup_ipa(ab);
	return;
}

void ath12k_dp_ipa_cleanup_ix_reg(struct ath12k_base *ab)
{
	u32 ring_hash_map;

	if (!dp_ipa_enable())
		return;

	/* Call HAL API to remap REO rings to REO2IPA ring */
	/* When hash based routing of rx packet is enabled, 32 entries to map
	 * the hash values to the ring will be configured. Each hash entry uses
	 * four bits to map to a particular ring. The ring mapping will be
	 * 0:TCL, 1:SW1, 2:SW2, 3:SW3, 4:SW4, 5:Release, 6:FW and 7:SW5
	 * 8:SW6, 9:SW7, 10:SW8, 11:Not used.
	 */
	ring_hash_map = HAL_WIFI7_HASH_ROUTING_RING_SW1 |
			HAL_WIFI7_HASH_ROUTING_RING_SW2 << 4 |
			HAL_WIFI7_HASH_ROUTING_RING_SW3 << 8 |
			HAL_WIFI7_HASH_ROUTING_RING_SW4 << 12 |
			HAL_WIFI7_HASH_ROUTING_RING_SW1 << 16 |
			HAL_WIFI7_HASH_ROUTING_RING_SW2 << 20 |
			HAL_WIFI7_HASH_ROUTING_RING_SW3 << 24 |
			HAL_WIFI7_HASH_ROUTING_RING_SW4 << 28;

	ath12k_hal_reo_hw_setup(ab);

	return;
}

/* Done with IX register settings */

/* Allocate TX buffers for IPA */
int ath12k_dp_ipa_tx_buffer_attach(struct ath12k_base *ab)
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

/**
 * ath12k_dp_ipa_tx_buffer_detach - Free TX resources
 * @ab: ath12k_base
 *
 * Free allocated TX buffers with WBM SRNG
 *
 * Return: none
 */
static void ath12k_dp_ipa_tx_buffer_detach(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct sk_buff *skb;
	int idx;

	for (idx = 0; idx < IPA_CTX(ab)->ipa_uc_tx_rsc.alloc_tx_buf_cnt; idx++) {
		skb = (struct sk_buff *)
			IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned[idx];
		if (!skb)
			continue;
		dma_unmap_single(dp->dev, ATH12K_SKB_CB(skb)->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_BIDIRECTIONAL);
		dev_kfree_skb_any(skb);
		IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned[idx] =
						(void *)NULL;
	}

	kfree(IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned);
	IPA_CTX(ab)->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned = NULL;

	//TODO: fix this code 
	//sg_free_table(&IPA_CTX(ab)->ipa_uc_tx_rsc.sgtable_tcl);
	//sg_free_table(&IPA_CTX(ab)->ipa_uc_tx_rsc.sgtable_wbm);
}

int ath12k_dp_ipa_uc_detach(struct ath12k_base *ab)
{
	ath12k_dp_ipa_tx_buffer_detach(ab);
	return 0;
}

int dp_ipa_uc_attach(struct ath12k_base *ab)
{
	if (ath12k_dp_ipa_tx_buffer_attach(ab))
		return -ENOMEM;
	return 0;
}

/* Done allocating TX buffers */

/* TX-RX buffer SMMU Mapping towards IPA */
static inline unsigned long
ath12k_dp_ipa_mem_paddr_from_dmaaddr(struct ath12k_base *ab, unsigned long dma_addr)
{
	struct ath12k_pci *pci_priv = ath12k_pci_priv(ab);
	phys_addr_t addr;

	if (!(pci_priv->ath12k_pci_extn.iommu_domain))
		ath12k_err(ab, "IPA: iommu_domain is null");
	if (pci_priv->ath12k_pci_extn.smmu_s1_enable &&
			pci_priv->ath12k_pci_extn.iommu_domain) {
		addr = iommu_iova_to_phys(pci_priv->ath12k_pci_extn.iommu_domain,
					  dma_addr);
		if (!addr) {
			ath12k_err(ab, "IPA: iova returned is null");
			return dma_addr;
		}
	}
	if (addr)
		return addr;
	else
		return dma_addr;
}

static inline void ath12k_dp_ipa_update_mem_map_table(struct ath12k_base *ab,
						     struct ipa_wdi_buffer_info *mem_info,
						     unsigned long dma_addr,
						     u32 mem_size)
{
	mem_info->pa = ath12k_dp_ipa_mem_paddr_from_dmaaddr(ab, dma_addr);
	mem_info->iova = dma_addr;
	mem_info->size = mem_size;
	return;
}

static int ath12k_dp_ipa_handle_buf_smmu_map_unmap(struct ath12k_base *ab,
						   struct sk_buff *skb, u32 size,
						   bool create, ipa_wdi_hdl_t hdl)
{
	struct ipa_wdi_buffer_info mem_map_table = {0};
	int ret = 0;
	dma_addr_t frag_addr;

	/* Need to handle the case when one soc will
	 * have multiple pdev(radio's), Currently non-split phy case.
	 */
	if (hdl == ATH12K_IPA_HDL_INVALID) {
		ath12k_err(ab, "IPA handle is invalid");
		return -EFAULT;
	}

	/* TODO: Need to handle the frag case
	 * Assuming single frag for now
	 */
	frag_addr = ATH12K_SKB_CB(skb)->paddr;
	ath12k_dp_ipa_update_mem_map_table(ab, &mem_map_table, frag_addr, size);
	if (ATH12K_SKB_CB(skb)->flags & ATH12K_SKB_IPA_MAP_UNMAP){
		ath12k_err(ab, "buffer is allready mapped with IPA");
		return 0;
	}


	if (create) {
		/* Assert if PA is zero */
		BUG_ON(mem_map_table.pa == 0);
		ATH12K_SKB_CB(skb)->flags |= ATH12K_SKB_IPA_MAP_UNMAP;

		ret = ipa_wdi_create_smmu_mapping_per_inst(hdl, 1,
							   &mem_map_table);
	} else {
		ATH12K_SKB_CB(skb)->flags &= ~ATH12K_SKB_IPA_MAP_UNMAP;
		ret = ipa_wdi_release_smmu_mapping_per_inst(hdl, 1,
							    &mem_map_table);
	}
	BUG_ON(ret);

	if (create)
		BUG_ON(mem_map_table.result != 0);
	else
		BUG_ON(mem_map_table.result != mem_map_table.size);
	return ret;
}

static int ath12k_dp_ipa_rx_buf_smmu_map_unmap(struct ath12k_base *ab, bool create)
{
	u32 index;
	int ret = 0;
	struct sk_buff *skb;
	u32 buf_len;

	if (!ipa_is_ready()) {
		ath12k_err(ab, "IPA is not READY");
		return 0;
	}

	for (index = 0; index < ab->ath12k_base_extn.rx_buf_cnt; index++) {
		skb = (struct sk_buff *)ab->ath12k_base_extn.rx_buf_pool[index];
		if (!skb)
			continue;
		buf_len = skb_end_pointer(skb) - skb->data;
		ret = ath12k_dp_ipa_handle_buf_smmu_map_unmap(ab, skb, buf_len, create,
							      IPA_CTX(ab)->hdl);
	}

	return ret;
}

static int ath12k_dp_ipa_tx_buf_smmu_map_unmap(struct ath12k_base *ab, bool create)
{
	u32 index;
	int ret = 0;
	struct ath12k_ipa *ipa_ctx = IPA_CTX(ab);
	u32 tx_buffer_cnt = ipa_ctx->ipa_uc_tx_rsc.alloc_tx_buf_cnt;
	struct sk_buff *skb;
	u32 buf_len;

	if (!ipa_is_ready()) {
		ath12k_err(ab, "IPA is not READY");
		return 0;
	}

	ath12k_info(ab, "IPA: mapping %d Tx buffer",tx_buffer_cnt);
	for (index = 0; index < tx_buffer_cnt; index++) {
		skb = (struct sk_buff *)
			ipa_ctx->ipa_uc_tx_rsc.tx_buf_pool_vaddr_unaligned[index];
		if (!skb)
			continue;
/*
+------------------+------------------+------------------+------------------+
|                  |                  |                  |                  |
|     head         |      data        |      tail        |       end        |
|  (start of buf)  | (start of data)  |  (end of data)   | (end of buffer)  |
+------------------+------------------+------------------+------------------+
^                  ^                  ^                  ^                  ^
|                  |<-- skb->len ---->|                  |                  |
|                  |<--------------- buf_len ----------->|                  |
|                  |<---skb_end_pointer() - skb->data -->|                  |
|                  |<---skb->len + skb_tailroom()------->|                  |
|                                     |<--skb_tailroom-->|                  |
|<--------- skb_end_pointer() = head + end_offset ------>|                  |
*/
		buf_len = skb_end_pointer(skb) - skb->data;
		ret = ath12k_dp_ipa_handle_buf_smmu_map_unmap(ab, skb, buf_len,
							      create, ipa_ctx->hdl);
	}

	return ret;
}

/* Done with SMMU mappings */

/* Handle Doorbell address */

static void ath12k_dp_ipa_set_tx_doorbell_paddr(struct ath12k_ipa *ipa_ctx)
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

static void ath12k_dp_ipa_set_rx_doorbell_paddr(struct ath12k_ipa *ipa_ctx)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ipa_ctx->ab);
	struct hal_srng *reo_srng;
	u64 hp_addr = ipa_ctx->ipa_uc_rx_rsc.rx_ready_doorbell_paddr;
	int ring_id;

	ring_id = dp->reo_dst_ring[ATH12K_IPA_REO_DST_RING].ring_id;
	reo_srng = &ipa_ctx->ab->hal.srng_list[ring_id];

	ath12k_info(ipa_ctx->ab, "Setting up the RX ready DB address to reo_ring_id:%d "
			"ring_id:%d, reo_srng->ring_id:%d Base_addr:%x LSB_ADDR:%x "
			"MSB_ADDR:%x DB_MAP_addr:%llx",
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

void ath12k_dp_ipa_map_ring_doorbell_paddr(struct ath12k_ipa *ipa_ctx)
{
	struct ath12k_ipa_dp_tx_rsc *tx_rsc = (struct ath12k_ipa_dp_tx_rsc *)&ipa_ctx->ipa_uc_tx_rsc;
	struct ath12k_ipa_dp_rx_rsc *rx_rsc = (struct ath12k_ipa_dp_rx_rsc *)&ipa_ctx->ipa_uc_rx_rsc;
	struct ath12k_pci *pci_priv = ath12k_pci_priv(ipa_ctx->ab);
	u32 rx_ready_doorbell_dmaaddr;
	u32 tx_comp_doorbell_dmaaddr;
	int ret;

	tx_rsc->tx_comp_doorbell_vaddr = ioremap(tx_rsc->tx_comp_doorbell_paddr, 4);

	if (pci_priv->ath12k_pci_extn.smmu_s1_enable) {
		ret = ath12k_dp_ipa_smmu_map(ipa_ctx->ab,
					     tx_rsc->tx_comp_doorbell_paddr,
					     &tx_comp_doorbell_dmaaddr, sizeof(u32));
		tx_rsc->tx_comp_doorbell_paddr = tx_comp_doorbell_dmaaddr;
		BUG_ON(ret != 0);

		ret = ath12k_dp_ipa_smmu_map(ipa_ctx->ab,
					     rx_rsc->rx_ready_doorbell_paddr,
					     &rx_ready_doorbell_dmaaddr, sizeof(u32));
		rx_rsc->rx_ready_doorbell_paddr = rx_ready_doorbell_dmaaddr;
		BUG_ON(ret != 0);
	}
}

static void ath12k_dp_ipa_unmap_ring_doorbell_paddr(struct ath12k_ipa *ipa_ctx)
{

	struct ath12k_ipa_dp_tx_rsc *tx_rsc = (struct ath12k_ipa_dp_tx_rsc *)&ipa_ctx->ipa_uc_tx_rsc;
	struct ath12k_ipa_dp_rx_rsc *rx_rsc = (struct ath12k_ipa_dp_rx_rsc *)&ipa_ctx->ipa_uc_rx_rsc;
	struct ath12k_pci *pci_priv = ath12k_pci_priv(ipa_ctx->ab);
	int ret;

	if (pci_priv->ath12k_pci_extn.smmu_s1_enable) {
		ret = ath12k_dp_ipa_smmu_unmap(ipa_ctx->ab,
					       rx_rsc->rx_ready_doorbell_paddr,
					       sizeof(u32));
		BUG_ON(ret != 0);

		ret = ath12k_dp_ipa_smmu_unmap(ipa_ctx->ab,
					       tx_rsc->tx_comp_doorbell_paddr,
					       sizeof(u32));
		BUG_ON(ret != 0);
	}
}

int ath12k_dp_ipa_set_doorbell_paddr(struct ath12k_ipa *ipa_ctx)
{
	ath12k_dp_ipa_map_ring_doorbell_paddr(ipa_ctx);

	ath12k_dp_ipa_set_tx_doorbell_paddr(ipa_ctx);
	ath12k_dp_ipa_set_rx_doorbell_paddr(ipa_ctx);

	return 0;
}

static inline void ath12k_dp_ipa_set_pipe_db(struct ath12k_ipa *ipa_ctx,
					     struct ipa_wdi_conn_out_params *out)
{
	struct ath12k_ipa_dp_tx_rsc *tx_rsc = (struct ath12k_ipa_dp_tx_rsc *)&ipa_ctx->ipa_uc_tx_rsc;
	struct ath12k_ipa_dp_rx_rsc *rx_rsc = (struct ath12k_ipa_dp_rx_rsc *)&ipa_ctx->ipa_uc_rx_rsc;

	tx_rsc->tx_comp_doorbell_paddr = out->tx_uc_db_pa;
	rx_rsc->rx_ready_doorbell_paddr = out->rx_uc_db_pa;
}

/* Done with DB address */
static void ath12k_dp_ipa_tx_comp_ring_init_hp(struct ath12k_ipa *ipa_ctx)
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

int ath12k_dp_ipa_enable_pipes(struct ath12k_ipa *ipa_ctx)
{
	int result;

	atomic_set(&ipa_ctx->ipa_pipes_enabled, 1);

	/* RX buffers mapping will be done as part of ipa_uc_ol_init only */
	atomic_set(&ipa_ctx->ipa_map_allowed, 1);

	ath12k_info(ipa_ctx->ab, "Enabling IPA pipes for  hdl:%d",ipa_ctx->hdl);
	result = ipa_wdi_enable_pipes_per_inst(ipa_ctx->hdl);
	if (result) {
		ath12k_err(ipa_ctx->ab, "%s: Enable WDI PIPE fail, code %d", __func__,
			   result);
		atomic_set(&ipa_ctx->ipa_pipes_enabled, 0);
		return -EFAULT;
	}

	if (ipa_ctx->ipa_first_tx_db_access) {
		ath12k_dp_ipa_tx_comp_ring_init_hp(ipa_ctx);
		ipa_ctx->ipa_first_tx_db_access = false;
	}
	/* seeting up REO mapping */
	ath12k_dp_ipa_setup_ix_reg(ipa_ctx->ab);
	return 0;
}

int ath12k_dp_ipa_disable_pipes(struct ath12k_ipa *ipa_ctx)
{
	int result;

	result = ipa_wdi_disable_pipes_per_inst(ipa_ctx->hdl);
	if (result) {
		ath12k_err(ipa_ctx->ab, "%s: Disable WDI PIPE fail, code %d", __func__,
			   result);
		BUG_ON(1);
		return -EFAULT;
	}

	atomic_set(&ipa_ctx->ipa_pipes_enabled, 0);
	atomic_set(&ipa_ctx->ipa_map_allowed, 0);

	return result;
}

int ath12k_wlan_ipa_enable_pipes(struct ath12k_ipa *ipa_ctx)
{
	int result = 0;

	spin_lock_bh(&ipa_ctx->enable_disable_lock);
	if (ipa_ctx->pipes_enable_in_progress) {
		ath12k_err(ipa_ctx->ab, "IPA Pipes Enable in progress");
		spin_unlock_bh(&ipa_ctx->enable_disable_lock);
		return -EEXIST;
	}
	ipa_ctx->pipes_enable_in_progress = true;
	spin_unlock_bh(&ipa_ctx->enable_disable_lock);

	if (atomic_read(&ipa_ctx->pipes_disabled)) {
		result = ath12k_dp_ipa_enable_pipes(ipa_ctx);
		if (result) {
			ath12k_err(ipa_ctx->ab, "Enable IPA WDI PIPE failed: ret=%d",
					result);
			result = -EFAULT;
			goto end;
		}
		ath12k_info(ipa_ctx->ab, "IPA pipes are enabled for hdl:%d",ipa_ctx->hdl);
		atomic_set(&ipa_ctx->pipes_disabled, 0);
	}

	reinit_completion(&ipa_ctx->ipa_resource_comp);

	if (atomic_read(&ipa_ctx->autonomy_disabled)) {
		atomic_set(&ipa_ctx->autonomy_disabled, 0);
	}
	ipa_ctx->ipa_init_state = ATH12K_IPA_STATE_PIPE_ENABLED;
end:
	spin_lock_bh(&ipa_ctx->enable_disable_lock);
	if ((!atomic_read(&ipa_ctx->autonomy_disabled)) &&
	    !atomic_read(&ipa_ctx->pipes_disabled))
		ipa_ctx->ipa_pipes_down = false;

	ipa_ctx->pipes_enable_in_progress = false;
	spin_unlock_bh(&ipa_ctx->enable_disable_lock);

	ath12k_info(ipa_ctx->ab, "exit: ipa_pipes_down=%d", ipa_ctx->ipa_pipes_down);
	return result;
}

void ath12k_dp_ipa_dma_get_sgtable_dma_addr(struct sg_table *sgt)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		if (!sg)
			break;

		sg->dma_address = sg_phys(sg);
	}
}

static int ath12k_dp_ipa_get_shared_mem_info(struct ath12k_base *ab,
					     shared_mem_t *shared_mem, void *cpu_addr,
					     dma_addr_t dma_addr, u32 size)
{
	dma_addr_t paddr;
	struct iommu_domain *domain = ath12k_dp_ipa_smmu_get_domain(ab);
	int ret;

	shared_mem->vaddr = cpu_addr;
	shared_mem->mem_info.size = size;
	shared_mem->mem_info.iova = dma_addr;

	if (domain)
		paddr = iommu_iova_to_phys(domain, dma_addr);
	shared_mem->mem_info.pa = paddr;

	ret = dma_get_sgtable(ab->dev,
			      (struct sg_table *)(&shared_mem->sgtable),
			      shared_mem->vaddr,
			      dma_addr,
			      size);
	if (ret) {
		ath12k_err(ab, "Unable to get DMA sgtable");
		return -1;
	}

	ath12k_dp_ipa_dma_get_sgtable_dma_addr((struct sg_table *)&shared_mem->sgtable);

	return 0;
}

void ath12k_dp_ipa_get_resources(struct ath12k_base *ab)
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

	ath12k_info(ab, "TCL_HP_ADDR:%llx srng_HP_addr:%pad",tx_rsc->ipa_tcl_hp_paddr,
		    &srng->u.src_ring.hp_addr);

	if (domain)
		tx_rsc->tcl_pa = iommu_iova_to_phys(domain,
						    tx_rsc->ipa_tcl_ring_base_paddr);
	else
		tx_rsc->tcl_pa = tx_rsc->ipa_tcl_ring_base_paddr;
	ret = dma_get_sgtable(dev, &tx_rsc->sgtable_tcl,
				tx_rsc->ipa_tcl_ring_base_vaddr,
				tx_rsc->tcl_pa, tx_rsc->ipa_tcl_ring_size);
	if (ret)
	{
		ath12k_err(ab, "Unable to get DMA sgtable");
		return;
	}

	for_each_sg((&tx_rsc->sgtable_tcl)->sgl, sg, (&tx_rsc->sgtable_tcl)->nents, i)
	{
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
	if (ret)
	{
		ath12k_err(ab, "Unable to get DMA sgtable");
		return;
	}

	for_each_sg((&tx_rsc->sgtable_wbm)->sgl, sg, (&tx_rsc->sgtable_wbm)->nents, i)
	{
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
	if (ret)
	{
		ath12k_err(ab, "Unable to get DMA sgtable");
		return;
	}

	for_each_sg((&rx_rsc->sgtable_reo)->sgl, sg, (&rx_rsc->sgtable_reo)->nents, i)
	{
		if (!sg)
			break;
		sg->dma_address = sg_phys(sg);
	}
	i = 0;

	/* IPA RX REFILL BUF ring - HAL_RXDMA_BUF SW2RXDMA_BUF1 */
	ret = ath12k_dp_ipa_setup_rx_refill_buf_ring(ab);
	ring_id = dp->rx_refill_buf_ring2.refill_buf_ring.ring_id;
	srng = &ab->hal.srng_list[ring_id];
	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);
	rx_rsc->ipa_rx_refill_buf_ring_base_paddr = params.ring_base_paddr;
	rx_rsc->ipa_rx_refill_buf_ring_base_vaddr = params.ring_base_vaddr;
	rx_rsc->ipa_rx_refill_buf_ring_size = (params.num_entries * srng->entry_size) << 2;
	rx_rsc->ipa_rx_refill_buf_hp_paddr = ath12k_hal_srng_get_hp_addr(ab, srng);

	ath12k_dp_ipa_get_shared_mem_info(ab, &rx_rsc->sgtable_refill,
			rx_rsc->ipa_rx_refill_buf_ring_base_vaddr,
			rx_rsc->ipa_rx_refill_buf_ring_base_paddr,
			rx_rsc->ipa_rx_refill_buf_ring_size);

	if (domain) {
		rx_rsc->refill_pa = iommu_iova_to_phys(domain,
					rx_rsc->ipa_rx_refill_buf_ring_base_paddr);
		rx_rsc->ipa_rx_refill_buf_hp_paddr = iommu_iova_to_phys(domain,
						rx_rsc->ipa_rx_refill_buf_hp_paddr);
	} else {
		rx_rsc->refill_pa = rx_rsc->ipa_rx_refill_buf_ring_base_paddr;
	}
	i = 0;

	return;
}

static void
inline ath12k_dp_ipa_set_smmu_mapped(struct ath12k_ipa *ipa_ctx, int val)
{
	atomic_set(&ipa_ctx->ipa_map_allowed, val);
}

/* IPA_UC_Defered loading */

/**
 * ath12k_dp_ipa_uc_loaded_handler() - Process IPA uC loaded indication
 * @ipa_ctx: ipa ipa local context
 *
 * Will handle IPA UC image loaded indication comes from IPA kernel
 *
 * Return: None
 */
void ath12k_dp_ipa_uc_loaded_handler(struct ath12k_ipa *ipa_ctx)
{
	int status;

	ath12k_info(ipa_ctx->ab, "UC READY");

	if (true == ipa_ctx->uc_loaded) {
		ath12k_err(ipa_ctx->ab, "UC already loaded");
		return;
	}

	/* Connect pipe */
	status = ath12k_dp_ipa_setup(ipa_ctx);
	if (status) {
		ath12k_err(ipa_ctx->ab, "Failure to setup IPA pipes (status=%d)",
			status);
		return;
	}
	/* Setup the Tx buffer SMMU mapings */
	status = ath12k_dp_ipa_tx_buf_smmu_map_unmap(ipa_ctx->ab, 1);
	if (status) {
		ath12k_err(ipa_ctx->ab, "Failure to map Tx buffers for IPA(status=%d)",
			status);
		goto smmu_map_fail;
	}
	ath12k_info(ipa_ctx->ab, "TX buffers mapped to IPA");
	ath12k_dp_ipa_set_doorbell_paddr(ipa_ctx);

	status = ath12k_dp_ipa_perf_init_perf_level(ipa_ctx);
	if (status)
		ath12k_err(ipa_ctx->ab, "Failed to init perf level");
	/*
	 * Enable IPA/FW PIPEs if
	 * 1. any clients connected to SAP or
	 * 2. STA connected to remote AP if STA only offload is enabled
	 */
	if (ipa_ctx->sap_num_connected_sta ||
	     ipa_ctx->sta_connected) {
		ath12k_err(ipa_ctx->ab, "Client already connected, enable IPA/FW PIPEs");
	}

	ipa_ctx->uc_loaded = true;

	return;

smmu_map_fail:
	ipa_wdi_disconn_pipes();
}

/**
 * ath12k_dp_ipa_uc_op_cb() - IPA uC operation callback
 * @op_msg: operation message received from firmware
 * @ipa_ctx: IPA context
 *
 * Return: None
 */
void ath12k_dp_ipa_uc_op_cb(struct op_msg_type *op_msg,
			    struct ath12k_ipa *ipa_ctx)
{
	struct op_msg_type *msg = op_msg;

	if (!ipa_ctx || !op_msg) {
		ath12k_err(ipa_ctx->ab, "INVALID ARG");
		return;
	}

	if (msg->op_code >= ATH12K_IPA_UC_OPCODE_MAX) {
		ath12k_err(ipa_ctx->ab, "INVALID OPCODE %d",  msg->op_code);
		kfree(op_msg);
		return;
	}

	ath12k_info(ipa_ctx->ab, "OPCODE=%d", msg->op_code);
	if (msg->op_code == ATH12K_IPA_UC_OPCODE_UC_READY) {
		mutex_lock(&ipa_ctx->ipa_lock);
		ath12k_dp_ipa_uc_loaded_handler(ipa_ctx);
		mutex_unlock(&ipa_ctx->ipa_lock);
	} else{
		ath12k_err(ipa_ctx->ab, "Invalid message: op_code=%d",
			msg->op_code);
	}

	kfree(op_msg);
}

/**
 * ath12k_dp_ipa_uc_loaded_uc_cb() - IPA UC loaded event callback
 * @priv_ctxt: IPA context
 *
 * Will be called by IPA context.
 * It's atomic context, then should be scheduled to kworker thread
 *
 * Return: None
 */
void ath12k_dp_ipa_uc_loaded_uc_cb(void *priv_ctxt)
{
	struct ath12k_ipa *ipa_ctx;
	struct op_msg_type *msg;
	struct uc_op_work_struct *uc_op_work;

	if (!g_ipa_is_ready) {
		printk("IPA is not READY");
		return;
	}

	if (!priv_ctxt) {
		printk("Invalid IPA context");
		return;
	}

	ipa_ctx = (struct ath12k_ipa *)priv_ctxt;

	uc_op_work = &ipa_ctx->uc_op_work[ATH12K_IPA_UC_OPCODE_UC_READY];
	if (!list_empty(&uc_op_work->work.entry)) {
		/* uc_op_work is not initialized yet */
		ipa_ctx->uc_loaded = true;
		return;
	}

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return;

	msg->op_code = ATH12K_IPA_UC_OPCODE_UC_READY;

	/* When the same uC OPCODE is already pended, just return */
	if (uc_op_work->msg)
		goto done;

	uc_op_work->msg = msg;

	if (!atomic_read(&ipa_ctx->deinit_in_prog)) {
		schedule_work(&uc_op_work->work);
	} else {
		uc_op_work->msg = NULL;
		goto done;
	}

	/* work handler will free the msg buffer */
	return;

done:
	kfree(msg);
}


void ath12k_dp_ipa_uc_fw_op_event_handler(struct work_struct *work)
{
	struct uc_op_work_struct *uc_op_work;
	struct op_msg_type *msg;
	struct ath12k_ipa *ipa_ctx;

	/* TODO: Need to handle wlan SSR case */

	uc_op_work = container_of(work, struct uc_op_work_struct, work);
	ipa_ctx = uc_op_work->ipa_priv_bp;
	msg = uc_op_work->msg;
	uc_op_work->msg = NULL;
	ath12k_info(ipa_ctx->ab, "posted msg %d", msg->op_code);

	ath12k_dp_ipa_uc_op_cb(msg, ipa_ctx);
}

/* Done IPA_UC_Defered loading */


int ath12k_dp_ipa_wdi_init(struct ath12k_ipa *ipa_ctx)
{
	struct ipa_wdi_init_in_params in;
	struct ipa_wdi_init_out_params out;
	int ret;

	ath12k_info(ipa_ctx->ab, "Enter: ath12k_dp_ipa_wdi_init");

	ipa_ctx->uc_loaded = false;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));

	in.wdi_version = ipa_ctx->wdi_version;
	in.notify = ath12k_dp_ipa_uc_loaded_uc_cb;
	in.priv = ipa_ctx;
	in.inst_id = ipa_ctx->instance_id;
	ret = ipa_wdi_init_per_inst(&in, &out);
	if (ret) {
		ath12k_err(ipa_ctx->ab, "IPA wdi init failed with ret: %d", ret);
		return -EFAULT;
	}
	ipa_ctx->over_gsi = out.is_over_gsi;
	ipa_ctx->is_smmu_enabled = out.is_smmu_enabled;
	ipa_ctx->hdl = out.hdl;

	ath12k_info(ipa_ctx->ab, "ipa_over_gsi: %d, is_smmu_enabled: %d, handle: %d "
			"inst_id: %d", ipa_ctx->over_gsi, ipa_ctx->is_smmu_enabled,
			ipa_ctx->hdl,in.inst_id);

	ipa_ctx->uc_loaded = out.is_uC_ready;
	if (ipa_ctx->uc_loaded)
		ath12k_info(ipa_ctx->ab, "IPA uC READY");
	else
		return -EBUSY;

	ath12k_info(ipa_ctx->ab, "Exit: IPA WDI INIT SUCCESS for HDL:%d",ipa_ctx->hdl);
	return 0;
}

void ath12k_dp_ipa_obj_cleanup(struct ath12k_ipa *ipa_ctx)
{
	complete(&ipa_ctx->ipa_resource_comp);
	/*TODO: check if memset is needed for ipa_resource_comp ? */

	if (ipa_wdi_cleanup_per_inst(ipa_ctx->hdl))
		ath12k_err(ipa_ctx->ab, "IPA_WDI_CLEANUP is failed !!");

	ipa_ctx->handle_initialized = false;
}

int ath12k_dp_ipa_obj_setup(struct ath12k_ipa *ipa_ctx)
{
	int i;
	struct ath12k_ipa_iface_context *iface_context = NULL;
	int ret = 0;

	ath12k_info(ipa_ctx->ab, "enter");

	ipa_ctx->num_iface = 0;
	/*TODO: set the right ipa config which we get from ini */
	ipa_ctx->config = g_ipa_config;
	ipa_ctx->wdi_version = IPA_WDI_4;

	/* Create the interface context */
	for (i = 0; i < ATH12K_IPA_MAX_IFACE; i++) {
		iface_context = &ipa_ctx->iface_context[i];
		iface_context->ipa_ctx = ipa_ctx;
		iface_context->iface_id = i;
		iface_context->dev = NULL;
		iface_context->device_mode = ATH12K_IPA_MAX_NO_OF_MODE;
		iface_context->session_id = ATH12K_IPA_MAX_SESSION;
		atomic_set(&iface_context->conn_count, 0);
		atomic_set(&iface_context->disconn_count, 0);
		spin_lock_init(&iface_context->interface_lock);
	}

	spin_lock_init(&ipa_ctx->enable_disable_lock);
	ipa_ctx->pipes_down_in_progress = false;
	ipa_ctx->pipes_enable_in_progress = false;
	ipa_ctx->ipa_init_state = ATH12K_IPA_STATE_INIT;

	mutex_init(&ipa_ctx->event_lock);
	mutex_init(&ipa_ctx->ipa_lock);
	atomic_set(&ipa_ctx->deinit_in_prog, 0);

	ath12k_dp_ipa_set_smmu_mapped(ipa_ctx, 0);

	if (dp_ipa_enable()) {
		memset(&ipa_ctx->stats, 0, sizeof(ipa_ctx->stats));
		ipa_ctx->sap_num_connected_sta = 0;
		ipa_ctx->sap_num_mlo_connected_sta = 0;
		ipa_ctx->ipa_tx_packets_diff = 0;
		ipa_ctx->ipa_rx_packets_diff = 0;
		ipa_ctx->ipa_p_tx_packets = 0;
		ipa_ctx->ipa_p_rx_packets = 0;
		ipa_ctx->resource_loading = false;
		ipa_ctx->resource_unloading = false;
		ipa_ctx->num_sap_connected = 0;
		ipa_ctx->sta_connected = 0;
		ipa_ctx->ipa_pipes_down = true;
		atomic_set(&ipa_ctx->pipes_disabled, 1);
		atomic_set(&ipa_ctx->autonomy_disabled, 1);
		ipa_ctx->wdi_enabled = false;

		ret = ath12k_dp_ipa_wdi_init(ipa_ctx);
		if (ret) {
			ath12k_err(ipa_ctx->ab, "IPA WDI init failed: ret=%d", ret);
			goto ipa_wdi_destroy;
		}
	} else {
		if (ret)
			goto ipa_wdi_destroy;
	}

	ipa_ctx->ipa_init_state = ATH12K_IPA_STATE_SETUP_DONE;

	/*TODO: Re-check if we need completeion support, as everythig is done in sequence,
	 * no parallel thread where status need to be delivered
	 */
	init_completion(&ipa_ctx->ipa_resource_comp);

	if (ath12k_dp_ipa_perf_set_perf_level_bw_enabled(ipa_ctx))
		ipa_ctx->curr_bw_level = ATH12K_IPA_BW_LEVEL_MAX;

	ath12k_info(ipa_ctx->ab, "exit: success");

	return 0;

ipa_wdi_destroy:
	ath12k_err(ipa_ctx->ab, "exit: fail");
	return -EFAULT;
}

/* IPA BANK ID handling and PMAC ID handling */
static void
ath12k_dp_ipa_tx_set_bank_config(u32 *bank_config)
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

int ath12k_dp_ipa_get_tx_bank_id(struct ath12k_base *ab, u8 *bid)
{
	u32 bank_config;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	*bid = ab->hw_params->num_tcl_banks - 1;
	ath12k_dp_ipa_tx_set_bank_config(&bank_config);

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

void
ath12k_dp_ipa_setup_tx_smmu_params_bank_id(struct ath12k_ipa *ipa_ctx,
					   struct ipa_wdi_pipe_setup_info_smmu *tx_smmu)
{
	u8 bank_id;

	if (!ath12k_dp_ipa_get_tx_bank_id(ipa_ctx->ab, &bank_id))
		((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->rx_bank_id = bank_id;
}

void
ath12k_dp_ipa_setup_tx_smmu_params_pmac_id(struct ath12k_ipa *ipa_ctx,
					   struct ipa_wdi_pipe_setup_info_smmu *tx_smmu)
{
	u8 pmac_id = 0;

	/*TODO: handle pmac id from ath pov, in case of split phy, else it is always 0 in single wkk */
	((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->rx_pmac_id = pmac_id;
}

/* DONE handling IPA bank ID and pmac ID */

/**
 * ath12k_dp_ipa_set_smmu_txr_rn_db_addr() - Indicate IPA to set or clear the
 * 40th bit of transfer ring DB addr
 * @ab: ath12k base
 * @txrx_smmu: WDI TX/RX configuration
 *
 * Return: None
 */
void
ath12k_dp_ipa_set_smmu_txr_rn_db_addr(struct ath12k_base *ab,
				      struct ipa_wdi_pipe_setup_info_smmu *txrx_smmu)
{
	struct ath12k_pci *pci_priv = ath12k_pci_priv(ab);

	if (pci_priv->dev_id)
		((struct ipa_wdi_pipe_setup_info_smmu *)(txrx_smmu))->is_txr_rn_db_pcie_addr =
			false;
	else
		((struct ipa_wdi_pipe_setup_info_smmu *)(txrx_smmu))->is_txr_rn_db_pcie_addr =
			true;
}

/**
 * ath12k_dp_ipa_set_smmu_evt_rn_db_addr() - Indicate IPA to set or clear the
 * 40th bit of evt ring DB addr
 * @ab: ath12k base
 * @txrx_smmu: WDI TX/RX configuration
 *
 * Return: None
 */
void
ath12k_dp_ipa_set_smmu_evt_rn_db_addr(struct ath12k_base *ab,
				      struct ipa_wdi_pipe_setup_info_smmu *txrx_smmu)
{
	struct ath12k_pci *pci_priv = ath12k_pci_priv(ab);

	if (pci_priv->dev_id)
		((struct ipa_wdi_pipe_setup_info_smmu *)(txrx_smmu))
			->is_evt_rn_db_pcie_addr = false;
	else
		((struct ipa_wdi_pipe_setup_info_smmu *)(txrx_smmu))
			->is_evt_rn_db_pcie_addr = true;
}

/* WDI TX/RX SMMU params setting up */
void
ath12k_dp_ipa_wdi_tx_smmu_params(struct ath12k_ipa *ipa_ctx,
				 struct ipa_wdi_pipe_setup_info_smmu *tx_smmu)
{
	struct ath12k_ipa_dp_tx_rsc *tx_rsc = &ipa_ctx->ipa_uc_tx_rsc;
	bool over_gsi = ipa_ctx->over_gsi;
	ipa_wdi_hdl_t hdl = ipa_ctx->hdl;

	if (over_gsi) {
		if (hdl == ATH12K_IPA_HDL_FIRST)
			((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->client =
				IPA_CLIENT_WLAN2_CONS;
		else if (hdl == ATH12K_IPA_HDL_SECOND)
			((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->client =
				IPA_CLIENT_WLAN1_CONS;
		else if (hdl == ATH12K_IPA_HDL_THIRD)
			((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->client =
				IPA_CLIENT_WLAN4_CONS;
	} else {
			((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->client =
				IPA_CLIENT_WLAN1_CONS;
	}

	memcpy(&(((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->transfer_ring_base),
	       &tx_rsc->sgtable_wbm, sizeof(struct sg_table));
	((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->transfer_ring_size =
		tx_rsc->ipa_wbm_ring_size;
	/* WBM Tail Pointer Address */
	((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->transfer_ring_doorbell_pa =
		tx_rsc->ipa_wbm_tp_paddr;
	ath12k_dp_ipa_set_smmu_txr_rn_db_addr(ipa_ctx->ab, tx_smmu);

	memcpy(&(((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->event_ring_base),
		     &tx_rsc->sgtable_tcl,
		     sizeof(struct sg_table));
	((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->event_ring_size =
		tx_rsc->ipa_tcl_ring_size;
	/* TCL Head Pointer Address */
	((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->event_ring_doorbell_pa =
		tx_rsc->ipa_tcl_hp_paddr;
	ath12k_dp_ipa_set_smmu_evt_rn_db_addr(ipa_ctx->ab, tx_smmu);

	((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->num_pkt_buffers =
		tx_rsc->alloc_tx_buf_cnt;
	((struct ipa_wdi_pipe_setup_info_smmu *)(tx_smmu))->pkt_offset = 0;

	ath12k_dp_ipa_setup_tx_smmu_params_bank_id(ipa_ctx, tx_smmu);

	/* Set Pmac ID, extract pmac_id from first pdev for TX ring */
	ath12k_dp_ipa_setup_tx_smmu_params_pmac_id(ipa_ctx, tx_smmu);
}

void
ath12k_dp_ipa_wdi_rx_smmu_params(struct ath12k_ipa *ipa_ctx,
				 struct ipa_wdi_pipe_setup_info_smmu *rx_smmu)
{
	struct ath12k_ipa_dp_rx_rsc *rx_rsc = &ipa_ctx->ipa_uc_rx_rsc;
	bool over_gsi = ipa_ctx->over_gsi;
	ipa_wdi_hdl_t hdl = ipa_ctx->hdl;
	/*TODO: need to handle this get proper value for RX tlv packet */
	u32 rx_pkt_tlv_size = 128;

	if (over_gsi) {
		if (hdl == ATH12K_IPA_HDL_FIRST)
			((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->client =
				IPA_CLIENT_WLAN2_PROD;
		else if (hdl == ATH12K_IPA_HDL_SECOND)
			((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->client =
				IPA_CLIENT_WLAN1_PROD;
		else if (hdl == ATH12K_IPA_HDL_THIRD)
			((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->client =
				IPA_CLIENT_WLAN3_PROD;
	} else {
			((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->client =
				IPA_CLIENT_WLAN1_PROD;
	}

	memcpy(&(((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->transfer_ring_base),
	       &rx_rsc->sgtable_reo, sizeof(struct sg_table));
	((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->transfer_ring_size =
		rx_rsc->ipa_reo_ring_size;
	/* REO Tail Pointer Address */
	((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->transfer_ring_doorbell_pa =
		rx_rsc->ipa_reo_tp_paddr;
	ath12k_dp_ipa_set_smmu_txr_rn_db_addr(ipa_ctx->ab, rx_smmu);

	memcpy(&(((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->event_ring_base),
		     &rx_rsc->sgtable_refill.sgtable,
		     sizeof(struct sg_table));
	((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->event_ring_size =
		rx_rsc->ipa_rx_refill_buf_ring_size;

	((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->event_ring_doorbell_pa =
		rx_rsc->ipa_rx_refill_buf_hp_paddr;

	((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->pkt_offset =
		rx_pkt_tlv_size + L3_HEADER_PADDING;

	/* Set Chip ID, and pass to IPA */
	if (ipa_ctx->ab->ag->mlo_capable)
		((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->mlo_chip_id =
			ipa_ctx->ab->device_id;
	else
		((struct ipa_wdi_pipe_setup_info_smmu *)(rx_smmu))->mlo_chip_id = 0xFF;
}
/* DONE WDI TX/RX SMMU PARAMS settings */

int ath12k_dp_ipa_setup(struct ath12k_ipa *ipa_ctx)
{
	struct ipa_ep_cfg *tx_cfg;
	struct ipa_ep_cfg *rx_cfg;
	struct ipa_wdi_pipe_setup_info *tx = NULL;
	struct ipa_wdi_pipe_setup_info *rx = NULL;
	struct ipa_wdi_pipe_setup_info_smmu *tx_smmu = NULL;
	struct ipa_wdi_pipe_setup_info_smmu *rx_smmu = NULL;
	struct ipa_wdi_conn_in_params *pipe_in = NULL;
	struct ipa_wdi_conn_out_params pipe_out;
	struct ath12k_pci *pci_priv = ath12k_pci_priv(ipa_ctx->ab);
	int ret = 0;
	/* need to derive below fields from ipa_ctx */
	bool is_smmu_enabled = ipa_ctx->is_smmu_enabled &&
				pci_priv->ath12k_pci_extn.smmu_s1_enable;

	pipe_in = kzalloc(sizeof(*pipe_in), GFP_KERNEL);
	if (!pipe_in)
		return -ENOMEM;

	memset(&pipe_out, 0, sizeof(pipe_out));

	if (is_smmu_enabled) {
		pipe_in->is_smmu_enabled = true;
		tx_smmu = &pipe_in->u_tx.tx_smmu;
		tx_cfg = &tx_smmu->ipa_ep_cfg;
	} else {
		pipe_in->is_smmu_enabled = false;
		tx = &pipe_in->u_tx.tx;
		tx_cfg = &tx->ipa_ep_cfg;
	}

	tx_cfg->nat.nat_en = IPA_BYPASS_NAT;
	tx_cfg->hdr.hdr_len = ATH12K_IPA_UC_WLAN_TX_HDR_LEN;
	tx_cfg->hdr.hdr_ofst_pkt_size_valid = 0;
	tx_cfg->hdr.hdr_ofst_pkt_size = 0;
	tx_cfg->hdr.hdr_additional_const_len = 0;
	tx_cfg->mode.mode = IPA_BASIC;
	tx_cfg->hdr_ext.hdr_little_endian = true;

	/*
	 * Transfer Ring: WBM Ring
	 * Transfer Ring Doorbell PA: WBM Tail Pointer Address
	 * Event Ring: TCL ring
	 */

	if (is_smmu_enabled)
		ath12k_dp_ipa_wdi_tx_smmu_params(ipa_ctx, tx_smmu);

	/* RX PIPE Setup */
	if (is_smmu_enabled) {
		rx_smmu = &pipe_in->u_rx.rx_smmu;
		rx_cfg = &rx_smmu->ipa_ep_cfg;
	} else {
		rx = &pipe_in->u_rx.rx;
		rx_cfg = &rx->ipa_ep_cfg;
	}

	rx_cfg->nat.nat_en = IPA_BYPASS_NAT;
	rx_cfg->hdr.hdr_len = ATH12K_IPA_UC_WLAN_RX_HDR_LEN;
	rx_cfg->hdr.hdr_ofst_pkt_size_valid = 1;
	rx_cfg->hdr.hdr_ofst_pkt_size = 0;
	rx_cfg->hdr.hdr_additional_const_len = 0;
	rx_cfg->mode.mode = IPA_BASIC;
	rx_cfg->hdr_ext.hdr_little_endian = true;
	rx_cfg->hdr.hdr_ofst_metadata_valid = 0;
	rx_cfg->hdr.hdr_metadata_reg_valid = 1;

	/**
	 * Transfer Ring: REO Ring
	 * Transfer Ring Doorbell PA: REO Tail Pointer Address
	 * Event Ring: FW ring
	 * Event Ring Doorbell PA: FW Head Pointer Address
	 */
	if (is_smmu_enabled)
		ath12k_dp_ipa_wdi_rx_smmu_params(ipa_ctx, rx_smmu);

	pipe_in->notify = ath12k_dp_ipa_w2i;
	pipe_in->priv = ipa_ctx;
	pipe_in->hdl = ipa_ctx->hdl;

	ath12k_info(ipa_ctx->ab, "conn_pipe enable start HDL:%d",ipa_ctx->hdl);
	/* Connect WDI IPA PIPEs */
	ret = ipa_wdi_conn_pipes_per_inst(pipe_in, &pipe_out);

	if (ret) {
		ath12k_err(ipa_ctx->ab,
				"%s: ipa_wdi_conn_pipes: IPA pipe setup failed: ret=%d",
				__func__, ret);
		kfree(pipe_in);
		return -EFAULT;
	}
	ath12k_info(ipa_ctx->ab, "conn_pipe enable completed HDL:%d", ipa_ctx->hdl);

	/* IPA uC Doorbell registers */
	ath12k_info(ipa_ctx->ab, "Tx DB PA=0x%x, Rx DB PA=0x%x",
			(unsigned int)pipe_out.tx_uc_db_pa,
			(unsigned int)pipe_out.rx_uc_db_pa);

	ath12k_dp_ipa_set_pipe_db(ipa_ctx, &pipe_out);

	ipa_ctx->is_db_ddr_mapped =
		((struct ipa_wdi_conn_out_params *)&(pipe_out))->is_ddr_mapped;

	ipa_ctx->ipa_first_tx_db_access = true;
	kfree(pipe_in);

	return ret;
}

int ath12k_dp_ipa_cleanup(struct ath12k_ipa *ipa_ctx)
{
	int ret = 0;

	ret = ipa_wdi_disconn_pipes();
	if (ret) {
		ath12k_err(ipa_ctx->ab,
				"ipa_wdi_disconn_pipes: IPA pipe cleanup failed ret:%d",
				ret);
		ret = -EFAULT;
	}

	ath12k_dp_ipa_unmap_ring_doorbell_paddr(ipa_ctx);

	return ret;
}

int ath12k_dp_ipa_uc_ol_init(struct ath12k_ipa *ipa_ctx)
{
	u8 i;
	int ret = 0;
	struct device *dev = ipa_ctx->ab->dev;

	ath12k_info(ipa_ctx->ab, "enter");

	if (!dev) {
		ath12k_err(ipa_ctx->ab, "dev null");
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < ATH12K_IPA_MAX_SESSION; i++) {
		ipa_ctx->vdev_to_iface[i] = ATH12K_IPA_MAX_SESSION;
		ipa_ctx->vdev_offload_enabled[i] = false;
		ipa_ctx->disable_intrabss_fwd[i] = false;
	}

	/* Not adding the support for ALL upcode supported by IPA,
	 * added support only for UC_READY event
	 */
	for (i = 0; i < ATH12K_IPA_UC_OPCODE_MAX; i++) {
		ipa_ctx->uc_op_work[i].dev = dev;
		ipa_ctx->uc_op_work[i].msg = NULL;
		ipa_ctx->uc_op_work[i].ipa_priv_bp = ipa_ctx;
		INIT_WORK(&ipa_ctx->uc_op_work[i].work,
			  ath12k_dp_ipa_uc_fw_op_event_handler);
	}

	if (true == ipa_ctx->uc_loaded) {
		ret = ath12k_dp_ipa_setup(ipa_ctx);

		if (ret) {
			ath12k_err(ipa_ctx->ab, "Failure to setup IPA pipes (status=%d)",
				ret);
			ret = -EFAULT;

			ipa_ctx->uc_loaded = false;

			goto free_res;
		}

		ret = ath12k_dp_ipa_tx_buf_smmu_map_unmap(ipa_ctx->ab, 1);
		if (ret) {
			ath12k_err(ipa_ctx->ab,
					"Failure to map Tx buffers for IPA(status=%d)",
					ret);
			goto free_res;
		}
		ath12k_info(ipa_ctx->ab, "TX buffers mapped to IPA");

		ret = ath12k_dp_ipa_rx_buf_smmu_map_unmap(ipa_ctx->ab, 1);
		if (ret) {
			ath12k_err(ipa_ctx->ab,
					"Failure to map Rx buffers for IPA(status=%d)",
					ret);
			goto free_res;
		}
		ath12k_info(ipa_ctx->ab, "RX buffers mapped to IPA");

		ath12k_dp_ipa_set_doorbell_paddr(ipa_ctx);
		ret = ath12k_dp_ipa_perf_init_perf_level(ipa_ctx);
		if (ret)
			ath12k_err(ipa_ctx->ab, "Failed to init perf level");

		/* enable IPA pipes, no need to enable/disable based
		 * on first client connect & last client disconenct
		 * Seen NOC error in past where access is made to IPA pipe
		 * after it is disable in TX completion path, while posting
		 * free buffer to ring.
		 */
		ath12k_dp_ipa_enable_pipes(ipa_ctx);

	}

	ipa_ctx->ipa_init_state = ATH12K_IPA_STATE_PIPE_CONNECTION_DONE;
	goto out;

free_res:
	ath12k_err(ipa_ctx->ab, "failure case: free allocated resources");
	for (i = 0; i < ATH12K_IPA_UC_OPCODE_MAX; i++) {
		cancel_work_sync(&ipa_ctx->uc_op_work[i].work);
	}
out:
	ath12k_info(ipa_ctx->ab, "exit: status=%d", ret);
	atomic_inc(&g_instances_added);
	return 0;
}

int ath12k_dp_ipa_uc_ol_deinit(struct ath12k_base *ab)
{
	u8 i;
	int ret = 0;
	struct ath12k_ipa *ipa_ctx = IPA_CTX(ab);
	struct device *dev = ab->dev;

	ath12k_info(ab, "enter in uc ol deinit");

	if (!dev) {
		ath12k_err(ab, "dev null");
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&ipa_ctx->g_init_deinit_lock);

	if (!(ipa_ctx->handle_initialized)) {
		ath12k_err(ab, "IPA is already deinit for hdl:%d",ipa_ctx->hdl);
		ret = 0;
		goto out;
	}

	ath12k_dp_ipa_disable_pipes(ipa_ctx);
	atomic_set(&ipa_ctx->deinit_in_prog, 1);

	for (i = 0; i < ATH12K_IPA_UC_OPCODE_MAX; i++) {
		cancel_work_sync(&ipa_ctx->uc_op_work[i].work);
		kfree(ipa_ctx->uc_op_work[i].msg);
		ipa_ctx->uc_op_work[i].msg = NULL;
	}

	if (!ipa_ctx->is_db_ddr_mapped)
		iounmap(ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_vaddr);

	if (true == ipa_ctx->uc_loaded) {
		ret = ath12k_dp_ipa_tx_buf_smmu_map_unmap(ipa_ctx->ab, 0);
		if (ret)
			ath12k_err(ab, "Failure to unmap Tx buffers for IPA(status=%d)",
				ret);
		else
			ath12k_info(ab, "TX buffers unmapped to IPA");

		ret = ath12k_dp_ipa_rx_buf_smmu_map_unmap(ipa_ctx->ab, 0);
		if (ret)
			ath12k_err(ab, "Failure to unmap Rx buffers for IPA(status=%d)",
				ret);
		else
			ath12k_info(ab, "RX buffers unmapped to IPA");
		ret = ath12k_dp_ipa_cleanup(ipa_ctx);
		if (ret)
			ath12k_err(ab, "Failure to cleanup IPA pipes (status=%d)",
				ret);
	}

	ath12k_dp_ipa_obj_cleanup(ipa_ctx);
	if (atomic_read(&g_instances_added))
		atomic_dec(&g_instances_added);

	if (!atomic_read(&g_instances_added))
		g_ipa_is_ready = false;

	ath12k_info(ab, "exit: ret=%d", ret);
out:
	mutex_unlock(&ipa_ctx->g_init_deinit_lock);
	return ret;
}

void ath12k_dp_ipa_ready_cb(void *data)
{
	struct ath12k_ipa *ipa_priv = (struct ath12k_ipa *)data;
	int ret;

	ath12k_info(ipa_priv->ab, "ENTER in IPA code");
	if (!ipa_priv)
		return;

	if (!mutex_trylock(&ipa_priv->g_init_deinit_lock)) {
		ath12k_err(ipa_priv->ab, "MUTEX is already taken");
		return;
	}

	if (ipa_priv->handle_initialized) {
		ath12k_err(ipa_priv->ab, "IPA is already initialized !");
		goto out;
	}

	g_ipa_is_ready = true;
	ipa_priv->instance_id = deBruijnIdx[(ipa_priv->ab->qmi.target.board_id *
			DEBRUIJIN_MAGIC_IDX) >> DEBRUIJIN_MAGIC_SHIFT];

	ret = ath12k_dp_ipa_obj_setup(ipa_priv);
	if (ret) {
		ath12k_err(ipa_priv->ab, "IPA Object setup failed");
		g_ipa_is_ready = false;
		goto out;
	}
	ret = ath12k_dp_ipa_uc_ol_init(ipa_priv);
	if (ret) {
		ath12k_err(ipa_priv->ab, "IPA UC ol init failed");
		ath12k_dp_ipa_obj_cleanup(ipa_priv);
		g_ipa_is_ready = false;
		goto out;
	}

	ipa_priv->handle_initialized = true;
	ath12k_err(ipa_priv->ab, "IPA instance added :%d",atomic_read(&g_instances_added));
out:
	mutex_unlock(&ipa_priv->g_init_deinit_lock);
	return;
}

/**
 * ath12k_dp_ipa_register_is_ipa_ready() - Check if ipa readiness,
 * and register fallback handler
 * @ab: ath12k base
 *
 * Return: 1 if IPA is ready, 0 is IPA is not ready
 */
static int ath12k_dp_ipa_register_is_ipa_ready(struct ath12k_base *ab)
{
	int ret = 0;
	/* setup the IPA ring resources and call into in the
	 * IPA driver for readiness status
	 */
	ath12k_dp_ipa_get_resources(ab);
	ret = ipa_register_ipa_ready_cb(ath12k_dp_ipa_ready_cb, (void *)(IPA_CTX(ab)));
	if (ret == -EEXIST) {
		ath12k_info(ab, "IPA is ready, invoke callback\n");
		ath12k_dp_ipa_ready_cb(IPA_CTX(ab));
	} else if (ret) {
		ath12k_err(ab, "Failed to check for IPA readiness\n");
		return ret;
	}
	return ret;
}

/**
 * ath12k_dp_ipa_deregister_is_ipa_ready() - Disable IPA functionality
 *
 */
static void ath12k_dp_ipa_deregister_is_ipa_ready(void)
{
	g_ipa_is_ready = false;
	return;
}

/* Allocate Intermediate RX refill ring for Refilling buffers from IPA and HOST */
static int ath12k_dp_ipa_setup_rx_refill_buf_ring(struct ath12k_base *ab)
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

/* Free Intermediate RX refill buf ring for refilling buffers from IPA and Host */
static void ath12k_dp_ipa_cleanup_rx_refill_buf_ring(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	ath12k_dp_srng_cleanup(ab,
			       &dp->rx_refill_buf_ring2.refill_buf_ring);
}

static u32 ath12k_dp_ipa_set_default_routing(u32 reo_dest)
{
	/* If IPA is enabled */
	if (dp_ipa_enable())
		return ATH12K_IPA_REO_DST_RING+1;
	else
		return reo_dest;
}


static struct ath12k_ipa_ops dp_ipa_ops = {
	.ipa_register_is_ipa_ready = ath12k_dp_ipa_register_is_ipa_ready,
	.ipa_deregister_is_ipa_ready = ath12k_dp_ipa_deregister_is_ipa_ready,
	.ipa_setup_rx_refill_buf_ring = ath12k_dp_ipa_setup_rx_refill_buf_ring,
	.ipa_cleanup_rx_refill_buf_ring = ath12k_dp_ipa_cleanup_rx_refill_buf_ring,
	.ipa_smmu_mem_map_setup = ath12k_dp_ipa_smmu_mem_map_setup,
	.ipa_set_default_routing = ath12k_dp_ipa_set_default_routing,
	.ipa_set_rx_buf_smmu_map_unmap = ath12k_dp_ipa_handle_buf_smmu_map_unmap,
	.ipa_uc_ol_deinit = ath12k_dp_ipa_uc_ol_deinit,
	.ipa_tx_buffer_alloc = dp_ipa_uc_attach,
	.ipa_tx_buffer_free = ath12k_dp_ipa_uc_detach,
};

int ath12k_dp_ipa_plugin_register_ops_extn(struct ath12k_base *ab)
{
	if (ath12k_dp_ipa_attach(ab))
		return -ENOMEM;

	IPA_CTX(ab)->ipa_ops = &dp_ipa_ops;

	if (!IPA_CTX(ab)->ipa_ops) {
		ath12k_err(ab, "Error in registering IPA plugin ops\n");
		return -EINVAL;
	}
	mutex_init(&IPA_CTX(ab)->g_init_deinit_lock);
	IPA_CTX(ab)->handle_initialized = false;
	/* setting smmu domain */
	if(ath12k_dp_ipa_smmu_mem_map_setup(ab, 1)) {
		ath12k_err(ab, "IPA: Fail in setting smmu domain");
		return -EFAULT;
	}
	return 0;
}
EXPORT_SYMBOL(ath12k_dp_ipa_plugin_register_ops_extn);

void ath12k_dp_ipa_plugin_deregister_ops_extn(struct ath12k_base *ab)
{
	g_ipa_is_ready = false;
	IPA_CTX(ab)->ipa_ops = NULL;
	ath12k_dp_ipa_detach(ab);
}
EXPORT_SYMBOL(ath12k_dp_ipa_plugin_deregister_ops_extn);

/* SMMU support for SDX */
int ath12k_dp_ipa_pci_smmu_fault_handler(struct iommu_domain *domain,
					 struct device *dev, unsigned long iova,
					 int flags, void *handler_token)
{
	struct ath12k_pci *pci_priv = (struct ath12k_pci *)handler_token;

	ath12k_err(pci_priv->ab, "SMMU fault happened with IOVA 0x%lx\n", iova);

	/* TODO: need to handle SMMU fault handler recovery */

	/* Return ENOSYS to initiate IOMMU default fault handler */
	return -ENOSYS;
}

int ath12k_pci_init_smmu_extn(struct ath12k_pci *pci_priv)
{
	struct pci_dev *pci_dev = pci_priv->pdev;
	struct ath12k_base *ab = pci_priv->ab;
	struct device_node *of_node;
	const char *iommu_dma_type;
	const __be32 *prop;
	int len;
	u32 addr_win[4];
	int ret = 0;

	of_node = of_parse_phandle(ab->dev->of_node, "qcom,iommu-group", 0);
	if (!of_node) {
		ath12k_err(pci_priv->ab, "No iommu group configuration\n");
		return ret;
	}

	ath12k_dbg(ab, ATH12K_DBG_PCI, "Initializing SMMU\n");

	pci_priv->ath12k_pci_extn.iommu_domain = iommu_get_domain_for_dev(&pci_dev->dev);
	if (!pci_priv->ath12k_pci_extn.iommu_domain) {
		ath12k_err(pci_priv->ab, "No iommu domain found\n");
		return -EINVAL;
	}

	ret = of_property_read_string(of_node, "qcom,iommu-dma",
				      &iommu_dma_type);
	if (!ret && (!strcmp("fastmap", iommu_dma_type) ||
	    !strcmp("atomic", iommu_dma_type))) {
		ath12k_dbg(pci_priv->ab, ATH12K_DBG_PCI, "Enabling SMMU S1 stage\n");
		pci_priv->ath12k_pci_extn.smmu_s1_enable = true;
		iommu_set_fault_handler(pci_priv->ath12k_pci_extn.iommu_domain,
					ath12k_dp_ipa_pci_smmu_fault_handler, pci_priv);
	}
	ath12k_dbg(pci_priv->ab, ATH12K_DBG_PCI,
			"IPA: iommu_domain->type:%d dma_type:%s S1 enable:%d",
			pci_priv->ath12k_pci_extn.iommu_domain->type, iommu_dma_type,
			pci_priv->ath12k_pci_extn.smmu_s1_enable);

	ret = of_property_read_u32_array(of_node,  "qcom,iommu-dma-addr-pool",
					 addr_win, ARRAY_SIZE(addr_win));
	if (ret) {
		ath12k_err(pci_priv->ab, "Invalid SMMU size window, err = %d\n", ret);
		of_node_put(of_node);
		return ret;
	}

	pci_priv->ath12k_pci_extn.smmu_iova_start = addr_win[1];
	pci_priv->ath12k_pci_extn.smmu_iova_len = addr_win[3];

	ath12k_dbg(pci_priv->ab, ATH12K_DBG_PCI,
			"smmu_iova_start: %pa, smmu_iova_len: 0x%zx\n",
			&pci_priv->ath12k_pci_extn.smmu_iova_start,
			pci_priv->ath12k_pci_extn.smmu_iova_len);

	prop = of_get_property(ab->dev->of_node, "smmu_reg", &len);
	if (!prop) {
		ath12k_err(pci_priv->ab, "Invalid property in DT");
	}
	if (!prop || len < (4 * sizeof(__be32))) {
		ath12k_err(pci_priv->ab, "Invalid SMMU Reg property in DT");
		return -EINVAL;
	}

	pci_priv->ath12k_pci_extn.smmu_iova_ipa_start = of_read_number(prop, 2);
	pci_priv->ath12k_pci_extn.smmu_iova_ipa_current =
		pci_priv->ath12k_pci_extn.smmu_iova_ipa_start;
	pci_priv->ath12k_pci_extn.smmu_iova_ipa_len = of_read_number(prop + 2, 2);
	ath12k_dbg(pci_priv->ab, ATH12K_DBG_PCI,
			"smmu_iova_ipa_start: %llu a smmu_iova_ipa_len: 0x%zx\n",
			pci_priv->ath12k_pci_extn.smmu_iova_ipa_start,
			pci_priv->ath12k_pci_extn.smmu_iova_ipa_len);

	of_node_put(of_node);

	return 0;
}
EXPORT_SYMBOL(ath12k_pci_init_smmu_extn);

void ath12k_pci_deinit_smmu_extn(struct ath12k_pci *pci_priv)
{
	pci_priv->ath12k_pci_extn.iommu_domain = NULL;
}
EXPORT_SYMBOL(ath12k_pci_deinit_smmu_extn);

struct iommu_domain *ath12k_dp_ipa_smmu_get_domain(struct ath12k_base *ab)
{
	struct ath12k_pci *pci_priv = ath12k_pci_priv(ab);

	if (!pci_priv)
		return NULL;

	return pci_priv->ath12k_pci_extn.iommu_domain;
}

int ath12k_dp_ipa_smmu_map(struct ath12k_base *ab, phys_addr_t paddr, u32 *iova_addr,
			   size_t size)
{
	struct ath12k_pci *pci_priv = ath12k_pci_priv(ab);
	struct platform_device *plat_priv;
	unsigned long iova;
	int flag = IOMMU_READ | IOMMU_WRITE;
	struct pci_dev *root_port;
	struct device_node *root_of_node;
	struct pci_dev *pci_dev = pci_priv->pdev;
	size_t len;
	bool dma_coherent = false;
	dma_addr_t dma;

	if (!pci_priv)
		return -ENODEV;

	plat_priv = ab->pdev;
	if (!iova_addr) {
		ath12k_err(ab, "iova_addr is NULL, paddr %pa, size %zu\n",
			    &paddr, size);
		return -EINVAL;
	}

	len = roundup(size + paddr - rounddown(paddr, PAGE_SIZE), PAGE_SIZE);
	iova = roundup(pci_priv->ath12k_pci_extn.smmu_iova_ipa_current, PAGE_SIZE);

	if (iova >=
	    (pci_priv->ath12k_pci_extn.smmu_iova_ipa_start +
	     pci_priv->ath12k_pci_extn.smmu_iova_ipa_len)) {
		ath12k_err(ab, "No IOVA space to map, iova %lx, smmu_iova_ipa_start %pad,"
				"smmu_iova_ipa_len %zu\n", iova,
				&pci_priv->ath12k_pci_extn.smmu_iova_ipa_start,
				pci_priv->ath12k_pci_extn.smmu_iova_ipa_len);
		return -ENOMEM;
	}

	root_port = pcie_find_root_port(pci_dev);
	if (!root_port) {
		ath12k_err(ab, "Root port is null, so dma_coherent is disabled\n");
	} else {
		root_of_node = root_port->dev.of_node;
		if (root_of_node && root_of_node->parent) {
			dma_coherent =
				of_property_read_bool(root_of_node->parent,
							"dma-coherent");
			ath12k_dbg(ab, ATH12K_DBG_PCI, "dma-coherent is %s\n",
					dma_coherent ? "enabled" : "disabled");
			if (dma_coherent)
				flag |= IOMMU_CACHE;
		}
	}

	ath12k_dbg(ab, ATH12K_DBG_PCI, "IOMMU map: iova %lx, len %zu\n", iova, len);

	dma = (uint32_t)dma_map_resource(&pci_dev->dev, (phys_addr_t)paddr, len,
					 DMA_BIDIRECTIONAL, 0);
	if (dma_mapping_error(&pci_dev->dev, dma)) {
		ath12k_err(ab, "DMA mapping failed for doorbell register\n");
		return -ENOMEM;
	}
	if (upper_32_bits(dma) != 0) {
		ath12k_err(ab, "DMA address exceeds 32-bit space: 0x%llx\n", (u64)dma);
		dma_unmap_resource(&pci_dev->dev, dma, len, DMA_BIDIRECTIONAL, 0);
		return -EINVAL;
	}

	*iova_addr = lower_32_bits(dma);

	pci_priv->ath12k_pci_extn.smmu_iova_ipa_current = iova + len;
	ath12k_dbg(ab, ATH12K_DBG_PCI, "IOMMU map: iova_addr %x\n", *iova_addr);

	return 0;
}

int ath12k_dp_ipa_smmu_unmap(struct ath12k_base *ab, u32 iova_addr, size_t size)
{
	struct ath12k_pci *pci_priv = ath12k_pci_priv(ab);
	struct pci_dev *pci_dev = pci_priv->pdev;
	struct platform_device *plat_priv;
	unsigned long iova;
	size_t len;

	if (!pci_priv)
		return -ENODEV;

	plat_priv = ab->pdev;
	iova = rounddown(iova_addr, PAGE_SIZE);
	len = roundup(size + iova_addr - iova, PAGE_SIZE);

	if (iova >= pci_priv->ath12k_pci_extn.smmu_iova_ipa_start +
		    pci_priv->ath12k_pci_extn.smmu_iova_ipa_len) {
		ath12k_err(ab, "Out of IOVA space to unmap, iova %lx,"
				"smmu_iova_ipa_start %pad, smmu_iova_ipa_len %zu\n",
				iova,
				&pci_priv->ath12k_pci_extn.smmu_iova_ipa_start,
				pci_priv->ath12k_pci_extn.smmu_iova_ipa_len);
		return -ENOMEM;
	}

	ath12k_dbg(ab, ATH12K_DBG_PCI, "IOMMU unmap: iova %lx, len %zu\n", iova, len);

	dma_unmap_resource(&pci_dev->dev, (dma_addr_t)iova_addr, len,
			   DMA_BIDIRECTIONAL, 0);

	pci_priv->ath12k_pci_extn.smmu_iova_ipa_current = iova;
	return 0;
}
/* Done SMMU Init for SDX */

static u8 wlan_ipa_set_session_id(u8 session_id, bool is_2g_iface)
{
	return session_id;
}

static inline bool wlan_ipa_is_ipv6_enabled(struct ath12k_ipa_config *ipa_cfg)
{
	return ATH12K_IPA_IS_CONFIG_ENABLED(ipa_cfg, ATH12K_IPA_IPV6_ENABLE_MASK);
}

int wlan_ipa_check_iface_netdev_sessid(struct ath12k_ipa_iface_context *iface_ctx,
				       struct dp_ipa_net_device_priv *net_dev_priv, u8 session_id)
{
	if ((memcmp(iface_ctx->net_dev.dev_addr, net_dev_priv->dev_addr, ETH_ALEN) == 0) &&
		iface_ctx->session_id == session_id)
		return 1;

	return 0;
}

static inline bool wlan_ipa_is_enabled(struct ath12k_ipa_config *ipa_cfg)
{
	return ATH12K_IPA_IS_CONFIG_ENABLED(ipa_cfg, ATH12K_IPA_ENABLE_MASK);
}

bool ipa_config_is_enabled(void)
{
	return g_ipa_config ? wlan_ipa_is_enabled(g_ipa_config) : 0;
}

static inline bool
wlan_ipa_is_two_tx_pipes_enabled(struct ath12k_ipa_config *ipa_cfg)
{
	return ATH12K_IPA_IS_CONFIG_ENABLED(ipa_cfg,
					  ATH12K_IPA_TWO_TX_PIPES_ENABLE_MASK);
}

bool ipa_config_is_two_tx_pipes_enabled(void)
{
	return g_ipa_config ? (ipa_config_is_enabled() ?
		wlan_ipa_is_two_tx_pipes_enabled(g_ipa_config) : 0) : 0;
}

static inline char
*wlan_ipa_get_iface_ifname(struct ath12k_ipa_iface_context *iface)
{
	return iface->net_dev.name;
}

static inline void
dp_ipa_set_wdi_hdr_type(struct ipa_wdi_hdr_info *hdr_info)
{
	hdr_info->hdr_type = IPA_HDR_L2_ETHERNET_II;
}

static void dp_ipa_setup_meta_data_mask(struct ipa_wdi_reg_intf_in_params *in)
{
	in->meta_data_mask = WLAN_IPA_AST_META_DATA_MASK;
}

static void dp_ipa_setup_iface_session_id(struct ipa_wdi_reg_intf_in_params *in,
					  u8 session_id, bool is_tx1_used)
{
	in->meta_data = htonl(session_id);
	in->is_tx1_used = is_tx1_used;
}

bool ipa_config_is_vlan_enabled(void)
{
	if (!ipa_config_is_enabled())
		return false;

	return g_ipa_config ? g_ipa_config->ipa_vlan_support : 0;
}

bool ipa_is_wds_enabled(void)
{
	return g_ipa_config ? g_ipa_config->ipa_wds : 0;
}

int dp_ipa_cleanup_iface(char *ifname, ipa_wdi_hdl_t hdl, uint8_t session_id, bool mld_enabled)
{
	int ret;
	ret = ipa_wdi_dereg_intf_per_inst_mlo(ifname, hdl, session_id, mld_enabled);
	if (ret) {
		ath12k_err(NULL, "ipa_wdi_dereg_intf: IPA pipe deregistration failed: ret=%d \n", ret);
		return -EPERM;
	}

	return 0;
}


int dp_ipa_setup_iface(char *ifname,
		       u8 *mac_addr,
		       enum ipa_client_type prod_client,
		       enum ipa_client_type cons_client,
		       u8 session_id, bool is_ipv6_enabled,
		       ipa_wdi_hdl_t hdl,
		       bool is_tx1_used,
		       int is_mlo)
{
	struct ipa_wdi_reg_intf_in_params in = { 0 };
	struct ipa_wdi_hdr_info hdr_info = { 0 };
	struct ath12k_ipa_uc_tx_hdr uc_tx_hdr;
	struct ath12k_ipa_uc_tx_hdr uc_tx_hdr_v6;
	int ret = -EINVAL;

	ether_addr_copy(uc_tx_hdr.eth.h_source, mac_addr);

	/* IPV4 header */
	uc_tx_hdr.eth.h_proto = htons(ETH_P_IP);

	hdr_info.hdr = (u8 *)&uc_tx_hdr;
	hdr_info.hdr_len = ATH12K_IPA_UC_WLAN_TX_HDR_LEN;
	dp_ipa_set_wdi_hdr_type(&hdr_info);

	hdr_info.dst_mac_addr_offset = DP_IPA_UC_WLAN_HDR_DES_MAC_OFFSET;

	in.netdev_name = ifname;
	memcpy(&(in.hdr_info[IPA_IP_v4]),
	&hdr_info, sizeof(struct ipa_wdi_hdr_info));
	in.alt_dst_pipe = cons_client;
	in.is_meta_data_valid = 1;
	dp_ipa_setup_meta_data_mask(&in);
	in.hdl = hdl;

	dp_ipa_setup_iface_session_id(&in, session_id, is_tx1_used);

	/* IPV6 header */
	if (is_ipv6_enabled) {
		memcpy(&uc_tx_hdr_v6, &uc_tx_hdr, ATH12K_IPA_UC_WLAN_TX_HDR_LEN);
		uc_tx_hdr_v6.eth.h_proto = htons(ETH_P_IPV6);
		hdr_info.hdr = (u8 *)&uc_tx_hdr_v6;
		memcpy(&(in.hdr_info[IPA_IP_v6]),
		&hdr_info, sizeof(struct ipa_wdi_hdr_info));
	}

	in.mld_enabled = is_mlo;
	ret = ipa_wdi_reg_intf_per_inst(&in);
	if (ret) {
		ath12k_err(NULL, "ipa_wdi_reg_intf: register IPA interface failed: ret = %d",ret);
		return -EPERM;
	}

	return 0;
}


void wlan_ipa_cleanup_iface(struct ath12k_ipa_iface_context *iface_context,
				   const u8 *mac_addr, ipa_wdi_hdl_t hdl, uint8_t session_id, 
				   bool mld_enabled)
{
	struct ath12k_ipa *ipa_ctx = iface_context->ipa_ctx;
	char *ifname;

	if (iface_context->session_id == ATH12K_IPA_MAX_SESSION)
		return;

	ifname = wlan_ipa_get_iface_ifname(iface_context);

	if (dp_ipa_cleanup_iface(ifname, hdl, session_id, mld_enabled)) {
		ath12k_err(ipa_ctx->ab, "ipa_cleanup_iface failed \n");
	}

	if (iface_context->device_mode == ATH12K_IPA_SAP) {
		if(ipa_ctx->num_sap_connected > 0) {
			ipa_ctx->num_sap_connected--;
		} else {
			ath12k_warn(ipa_ctx->ab, "num_sap_connected is already 0\n");
		}
	}

	spin_lock_bh(&iface_context->interface_lock);
	if (atomic_read(&iface_context->disconn_count) ==
			atomic_read(&iface_context->conn_count) - 1) {
		atomic_inc(&iface_context->disconn_count);
	} else {
		ath12k_err(ipa_ctx->ab, "connect/disconnect out of sync\n");
		BUG_ON(1);
	}

	iface_context->is_mlo_vdev = false;
	iface_context->is_authenticated = false;
	iface_context->device_mode = ATH12K_IPA_MAX_NO_OF_MODE;
	iface_context->session_id = ATH12K_IPA_MAX_SESSION;

	memset(iface_context->mac_addr, 0, ETH_ALEN);

	spin_unlock_bh(&iface_context->interface_lock);
	iface_context->ifa_address = 0;
	eth_zero_addr(iface_context->bssid.bytes);
	if (!iface_context->ipa_ctx->num_iface) {
		ath12k_err(ipa_ctx->ab, "NUM INTF 0, Invalid \n");
		WARN_ON(1);
	}
	iface_context->ipa_ctx->num_iface--;
}


int wlan_ipa_setup_iface(struct ath12k_ipa *ipa_ctx,
			 struct dp_ipa_net_device_priv *net_dev_priv,
			 enum ath12k_ipa_opmode device_mode,
			 u8 session_id,
			 const u8 *mac_addr,
			 bool is_2g_iface, bool is_mlo_vdev, bool is_tx1_used)
{
	struct ath12k_ipa_iface_context *iface_context = NULL;
	int status = 0;
	u8 sessid;
	bool ipv6_en;
	char *ifname;
	int i;

	for (i = 0; i < ATH12K_IPA_MAX_IFACE; i++) {
		iface_context = &(ipa_ctx->iface_context[i]);
		if (wlan_ipa_check_iface_netdev_sessid(iface_context, net_dev_priv,
						       session_id)) {
			if (iface_context->device_mode == device_mode) {
				if (device_mode == ATH12K_IPA_SAP) {
					return 0; // success as dfs send us multiple start
				}
			}

			wlan_ipa_cleanup_iface(iface_context, NULL, ipa_ctx->hdl, session_id, is_mlo_vdev);
		} else if (iface_context->session_id == session_id) {
			wlan_ipa_cleanup_iface(iface_context, NULL, ipa_ctx->hdl, session_id, is_mlo_vdev);
		}
	}

	if (ATH12K_IPA_MAX_IFACE == ipa_ctx->num_iface) {
		status = -ENOMEM; // no mem
		ath12k_err(ipa_ctx->ab, "Max interface reached %d \n", ATH12K_IPA_MAX_IFACE);
		iface_context = NULL;
		WARN_ON(1);
		goto end;
	}

	for (i = 0; i < ATH12K_IPA_MAX_IFACE; i++) {
		if (ipa_ctx->iface_context[i].session_id == ATH12K_IPA_MAX_SESSION) {
			iface_context = &(ipa_ctx->iface_context[i]);
			break;
		}
	}

	if (!iface_context) {
		ath12k_err(ipa_ctx->ab, "All the IPA interfaces are in use \n");
		status = -ENOMEM; // No mem
		WARN_ON(1);
		goto end;
	}

	spin_lock_bh(&iface_context->interface_lock);
	if (atomic_read(&iface_context->conn_count) ==
			atomic_read(&iface_context->disconn_count)) {
		atomic_inc(&iface_context->conn_count);
	} else {
		ath12k_err(ipa_ctx->ab,"connect/disconnect out of sync \n");
		BUG_ON(1);
	}

	iface_context->is_mlo_vdev = is_mlo_vdev;
	memcpy(&iface_context->net_dev, net_dev_priv, sizeof(struct dp_ipa_net_device_priv));
	iface_context->device_mode = device_mode;
	iface_context->session_id = session_id;
	ether_addr_copy(iface_context->mac_addr, mac_addr);

	spin_unlock_bh(&iface_context->interface_lock);

	if (ipa_ctx->uc_loaded) {
		sessid = wlan_ipa_set_session_id(session_id, is_2g_iface);
		ipv6_en = wlan_ipa_is_ipv6_enabled(ipa_ctx->config);
		ifname = wlan_ipa_get_iface_ifname(iface_context);

		if(device_mode == ATH12K_IPA_SAP) {
			status = dp_ipa_setup_iface(ifname,
					(u8 *)mac_addr,
					iface_context->prod_client,
					iface_context->cons_client,
					sessid,
					ipv6_en,
					ipa_ctx->hdl,
					is_tx1_used,
					is_mlo_vdev);
		} else {
			status = dp_ipa_setup_iface(ifname,
					(u8 *)net_dev_priv->dev_addr,
					iface_context->prod_client,
					iface_context->cons_client,
					sessid,
					ipv6_en,
					ipa_ctx->hdl,
					is_tx1_used,
					is_mlo_vdev);
		}

		if (status)
			goto end;
	}

	ipa_ctx->num_iface++;

	if (device_mode == ATH12K_IPA_SAP)
		ipa_ctx->num_sap_connected++;

	return status;

end:
	if (iface_context)
		wlan_ipa_cleanup_iface(iface_context, mac_addr, ipa_ctx->hdl, session_id, is_mlo_vdev);

	return status;
}

int ath12k_ipa_global_ctx_alloc(struct ath12k_dp_hw_group *dp_hw_grp)
{
	struct ath12k_ipa_global_ctx *ctx;
	int ret;
	struct rhashtable *table;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dp_hw_grp = dp_hw_grp;
	INIT_WORK(&ctx->work, ath12k_wlan_ipa_evt_handler);

	spin_lock_init(&ctx->list_lock);
	INIT_LIST_HEAD(&ctx->list);

	ctx->mac_table_params.head_offset = offsetof(struct ath12k_ipa_event_entry, node);
	ctx->mac_table_params.key_offset =
		offsetof(struct ath12k_ipa_event_entry, mac_addr);
	ctx->mac_table_params.key_len = ETH_ALEN;
	ctx->mac_table_params.automatic_shrinking = true;
	ctx->mac_table_params.nelem_hint =
			(128 + ATH12K_IPA_MAX_SESSION) * ATH12K_GROUP_MAX_RADIO;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table) {
		kfree(ctx);
		return -ENOMEM;
	}

	ret = rhashtable_init(table, &ctx->mac_table_params);
	if (ret) {
		ath12k_err(NULL, "Unable to initialize rhash table\n");
		kfree(table);
		kfree(ctx);
		return ret;
	}

	ctx->mac_table = table;
	dp_hw_grp->ipa_global_ctx = ctx;
	return 0;
}
EXPORT_SYMBOL(ath12k_ipa_global_ctx_alloc);

void ath12k_ipa_global_ctx_free(struct ath12k_ipa_global_ctx *ctx)
{
	struct wlan_ipa_evt_wq_args *arg, *tmp;

	if (!ctx) {
		ath12k_info(NULL, "IPA global context is NULL\n");
		return;
	}

	cancel_work_sync(&ctx->work);

	spin_lock_bh(&ctx->list_lock);

	list_for_each_entry_safe(arg, tmp, &ctx->list, list_elem) {
		list_del(&arg->list_elem);
		kfree(arg);
	}

	spin_unlock_bh(&ctx->list_lock);

	if (ctx->mac_table) {
		rhashtable_destroy(ctx->mac_table);
		kfree(ctx->mac_table);
	}

	kfree(ctx);
}
EXPORT_SYMBOL(ath12k_ipa_global_ctx_free);

int ath12k_ipa_wlan_evt(struct ath12k_ipa *ipa_ctx,
			enum ipa_wlan_event event,
			u32 vdev_id, u8 pdev_idx, const u8 mac[ETH_ALEN],
			struct dp_ipa_net_device_priv *dev, enum nl80211_iftype mode,
			enum nl80211_band band, bool is_mlo)
{
	struct ath12k_ipa_iface_context *iface_ctx = NULL;
	struct ath12k_base *ab;
	u8 sta_session_id = ATH12K_IPA_MAX_SESSION;
	u8 session_id = vdev_id;
	bool is_2g_iface = band == NL80211_BAND_2GHZ ? true : false;
	bool is_tx1_used = false;
	int i;

	if (!ipa_ctx) {
		ath12k_info(NULL, "IPA context is NULL\n");
		return -EINVAL;
	}

	ab = ipa_ctx->ab;
	if (!ab) {
		ath12k_info(NULL, "ab is NULL\n");
		return -EINVAL;
	}

	if (!g_ipa_is_ready) {
		ath12k_warn(ab, "IPA not ready yet\n");
		return 0;
	}

	if (pdev_idx >= ab->num_radios || pdev_idx >= MAX_RADIOS) {
		ath12k_err(ab, "Invalid pdev_idx %d\n", pdev_idx);
		return -EINVAL;
	}

	if (session_id >= ATH12K_IPA_MAX_SESSION) {
		ath12k_err(ab, "Invalid session_id %d\n", session_id);
		return -EINVAL;
	}

	if (mode == NL80211_IFTYPE_P2P_CLIENT)
		mode = NL80211_IFTYPE_STATION;
	else if (mode == NL80211_IFTYPE_P2P_GO)
		mode = NL80211_IFTYPE_AP;

	if (!(mode == NL80211_IFTYPE_STATION || mode == NL80211_IFTYPE_AP)) {
		ath12k_info(ab, "Invalid interface type : %d\n", mode);
		return 0;
	}

	if (event >= IPA_WLAN_EVENT_MAX) {
		ath12k_err(ab, "Invalid event %d\n", event);
		return -EINVAL;
	}

	if (ath12k_wlan_ipa_uc_is_enabled(ipa_ctx->config) &&
	    !ath12k_wlan_ipa_uc_sta_is_enabled(ipa_ctx->config) &&
	    mode != NL80211_IFTYPE_AP) {
		ath12k_info(ab, "IPA UC enabled and STA disabled but it's not in AP mode\n");
		return 0;
	}

	if (ipa_ctx->sta_connected) {
		iface_ctx = ath12k_wlan_ipa_get_iface(ipa_ctx, NL80211_IFTYPE_STATION);
		if (iface_ctx)
			sta_session_id = iface_ctx->session_id;
		else
			ath12k_err(ab, "STA iface_ctx is NULL\n");
	}

	is_tx1_used = ath12k_ipa_get_is_tx1_used(ab, pdev_idx);
	enum ath12k_ipa_opmode device_mode = ath12k_ipa_mode(mode);

	ipa_ctx->stats.event[event]++;
	switch (event) {
	case WLAN_STA_CONNECT:
		mutex_lock(&ipa_ctx->event_lock);
		if (ipa_ctx->sta_connected) {
			bool mlo = false;
			u8 id = ATH12K_IPA_MAX_SESSION;

			iface_ctx =
			ath12k_wlan_ipa_get_iface_by_mode_netdev(ipa_ctx,
								 NL80211_IFTYPE_STATION,
								 dev, session_id);
			if (iface_ctx) {
				id = iface_ctx->session_id;
				mlo = iface_ctx->is_mlo_vdev;
				ipa_ctx->sta_connected--;
				wlan_ipa_cleanup_iface(iface_ctx, NULL, ipa_ctx->hdl,
						       id, mlo);
			}
			if (ath12k_wlan_ipa_send_msg(ipa_ctx, WLAN_STA_DISCONNECT,
						     mac, dev, mlo, id) != 0) {
				ath12k_err(ab, "Failed to send STA disconnect message\n");
				mutex_unlock(&ipa_ctx->event_lock);
				goto end;
			}
		}
		if (wlan_ipa_setup_iface(ipa_ctx, dev, device_mode, session_id, mac,
					 is_2g_iface, is_mlo, is_tx1_used) != 0) {
			ath12k_err(ab, "Setup iface failed\n");
			mutex_unlock(&ipa_ctx->event_lock);
			goto end;
		}
		ipa_ctx->vdev_to_iface[session_id] =
			ath12k_wlan_ipa_get_ifaceid(ipa_ctx, session_id);
		u8 ifaceid = ipa_ctx->vdev_to_iface[session_id];

		ether_addr_copy(ipa_ctx->iface_context[ifaceid].bssid.bytes, mac);
		if (ath12k_wlan_ipa_uc_sta_is_enabled(ipa_ctx->config) &&
		    (ipa_ctx->sap_num_connected_sta > 0 ||
		     ath12k_wlan_ipa_is_sta_only_offload_enabled()) &&
		    !ipa_ctx->sta_connected) {
			mutex_unlock(&ipa_ctx->event_lock);
			ath12k_wlan_ipa_uc_offload_enable(ipa_ctx,
							  WMI_STA_RX_DATA_OFFLOAD,
							  vdev_id,
							  true);
			mutex_lock(&ipa_ctx->event_lock);
		}
		ipa_ctx->sta_connected++;
		mutex_unlock(&ipa_ctx->event_lock);
		ath12k_info(ab, "STA connected = %d, vdev_to_iface[%u] = %u\n",
			    ipa_ctx->sta_connected, session_id,
			    ipa_ctx->vdev_to_iface[session_id]);
		break;

	case WLAN_AP_CONNECT:
		mutex_lock(&ipa_ctx->event_lock);
		if (ipa_ctx->vdev_to_iface[session_id] != ATH12K_IPA_MAX_SESSION) {
			ath12k_info(ab, "Vdev already connected\n");
			mutex_unlock(&ipa_ctx->event_lock);
			return 0;
		}
		if (wlan_ipa_setup_iface(ipa_ctx, dev, device_mode, session_id, mac,
					 is_2g_iface, is_mlo, is_tx1_used) != 0) {
			ath12k_err(ab, "Setup iface failed\n");
			mutex_unlock(&ipa_ctx->event_lock);
			goto end;
		}
		if (ath12k_wlan_ipa_uc_is_enabled(ipa_ctx->config)) {
			mutex_unlock(&ipa_ctx->event_lock);
			ath12k_wlan_ipa_uc_offload_enable(ipa_ctx,
							  WMI_AP_RX_DATA_OFFLOAD,
							  vdev_id,
							  true);
			mutex_lock(&ipa_ctx->event_lock);
		}
		ipa_ctx->vdev_to_iface[session_id] =
			ath12k_wlan_ipa_get_ifaceid(ipa_ctx, session_id);
		mutex_unlock(&ipa_ctx->event_lock);
		ath12k_info(ab, "AP connected, vdev_to_iface[%u] = %u\n",
			    session_id, ipa_ctx->vdev_to_iface[session_id]);
		break;

	case WLAN_STA_DISCONNECT:
		mutex_lock(&ipa_ctx->event_lock);
		if (!ipa_ctx->sta_connected) {
			struct ath12k_ipa_iface_context *iface;

			mutex_unlock(&ipa_ctx->event_lock);
			ath12k_info(ab, "STA already disconnected\n");
			iface =
			ath12k_wlan_ipa_get_iface_by_mode_netdev(ipa_ctx,
								 NL80211_IFTYPE_STATION,
								 dev, session_id);
			if (iface) {
				ath12k_info(ab, "Found existing STA iface\n");
				wlan_ipa_cleanup_iface(iface, mac, ipa_ctx->hdl,
						       session_id, is_mlo);
			}
			return -EINVAL;
		}
		ipa_ctx->sta_connected--;
		if (ath12k_wlan_ipa_uc_sta_is_enabled(ipa_ctx->config) &&
		    (ipa_ctx->sap_num_connected_sta > 0 ||
		     (ath12k_wlan_ipa_is_sta_only_offload_enabled() &&
		      !ipa_ctx->sta_connected))) {
			mutex_unlock(&ipa_ctx->event_lock);
			ath12k_wlan_ipa_uc_offload_enable(ipa_ctx,
							  WMI_STA_RX_DATA_OFFLOAD,
							  vdev_id,
							  false);
			mutex_lock(&ipa_ctx->event_lock);
		}
		ipa_ctx->vdev_to_iface[session_id] = ATH12K_IPA_MAX_SESSION;
		iface_ctx =
			ath12k_wlan_ipa_get_iface_by_mode_netdev(ipa_ctx,
								 NL80211_IFTYPE_STATION,
								 dev, session_id);
		if (iface_ctx) {
			ath12k_info(ab, "Found existing STA iface\n");
			wlan_ipa_cleanup_iface(iface_ctx, mac, ipa_ctx->hdl,
					       session_id, is_mlo);
		}
		mutex_unlock(&ipa_ctx->event_lock);
		ath12k_info(ab, "STA connected = %d, vdev_to_iface[%u] = %u\n",
			    ipa_ctx->sta_connected, session_id,
			    ipa_ctx->vdev_to_iface[session_id]);
		break;

	case WLAN_AP_DISCONNECT:
		mutex_lock(&ipa_ctx->event_lock);
		if (ath12k_wlan_ipa_uc_is_enabled(ipa_ctx->config)) {
			mutex_unlock(&ipa_ctx->event_lock);
			ath12k_wlan_ipa_uc_offload_enable(ipa_ctx,
							  WMI_AP_RX_DATA_OFFLOAD,
							  vdev_id,
							  false);
			mutex_lock(&ipa_ctx->event_lock);
			ipa_ctx->vdev_to_iface[session_id] = ATH12K_IPA_MAX_SESSION;
		}
		for (i = 0; i < ATH12K_IPA_MAX_IFACE; i++) {
			iface_ctx = &ipa_ctx->iface_context[i];
			if (wlan_ipa_check_iface_netdev_sessid(iface_ctx,
							       dev, session_id)) {
				ath12k_info(ab, "Found matching AP iface\n");
				wlan_ipa_cleanup_iface(iface_ctx, mac, ipa_ctx->hdl,
						       session_id, is_mlo);
				break;
			}
		}
		mutex_unlock(&ipa_ctx->event_lock);
		ath12k_info(ab, "AP disconnected, vdev_to_iface[%u] = %u\n",
			    session_id, ipa_ctx->vdev_to_iface[session_id]);
		break;

	case WLAN_CLIENT_CONNECT_EX:
		if (!ath12k_wlan_ipa_uc_is_enabled(ipa_ctx->config)) {
			ath12k_info(ab,
				    "%s: Evt: %d, IPA UC Offload not enabled\n",
				    dev->name, event);
			return 0;
		}
		mutex_lock(&ipa_ctx->event_lock);
		if (((is_mlo && (ipa_ctx->sap_num_mlo_connected_sta == 0)) ||
		     (!is_mlo && (ipa_ctx->sap_num_connected_sta == 0))) &&
		    ipa_ctx->uc_loaded)	{
			if (ath12k_wlan_ipa_uc_sta_is_enabled(ipa_ctx->config) &&
			    ipa_ctx->sta_connected) {
				mutex_unlock(&ipa_ctx->event_lock);
				ath12k_wlan_ipa_uc_offload_enable(ipa_ctx,
								  WMI_STA_RX_DATA_OFFLOAD,
								  vdev_id,
								  true);
				mutex_lock(&ipa_ctx->event_lock);
			}
			ath12k_info(ab, "First SAP client connected\n");
		}
		mutex_unlock(&ipa_ctx->event_lock);
		if (is_mlo)
			ipa_ctx->sap_num_mlo_connected_sta++;
		else
			ipa_ctx->sap_num_connected_sta++;
		if (ath12k_wlan_ipa_send_msg_ex(ipa_ctx, event, mac, dev,
						is_mlo, session_id) != 0) {
			ath12k_err(ab, "Failed to send CLIENT CONNECT ex message to IPA\n");
			return -EIO;
		}
		if (is_mlo)
			ath12k_info(ab,
				    "SAP MLO client connected, sap_num_mlo_connected_sta = %d\n",
				    ipa_ctx->sap_num_mlo_connected_sta);
		else
			ath12k_info(ab,
				    "SAP client connected, sap_num_connected_sta = %d\n",
				    ipa_ctx->sap_num_connected_sta);
		return 0;
	case WLAN_CLIENT_DISCONNECT:
		if (!ath12k_wlan_ipa_uc_is_enabled(ipa_ctx->config)) {
			ath12k_info(ab,
				    "%s: Evt: %d, IPA UC Offload not enabled\n",
				    dev->name, event);
			return 0;
		}
		mutex_lock(&ipa_ctx->event_lock);
		if (!ipa_ctx->sap_num_connected_sta &&
		    !ipa_ctx->sap_num_mlo_connected_sta) {
			mutex_unlock(&ipa_ctx->event_lock);
			ath12k_info(ab, "Client already disconnected\n");
			return 0;
		}
		if (is_mlo)
			ipa_ctx->sap_num_mlo_connected_sta--;
		else
			ipa_ctx->sap_num_connected_sta--;
		if (!ipa_ctx->sap_num_connected_sta &&
		    !ipa_ctx->sap_num_mlo_connected_sta && ipa_ctx->uc_loaded)	{
			if (ath12k_wlan_ipa_uc_sta_is_enabled(ipa_ctx->config) &&
			    ipa_ctx->sta_connected) {
				mutex_unlock(&ipa_ctx->event_lock);
				ath12k_wlan_ipa_uc_offload_enable(ipa_ctx,
								  WMI_STA_RX_DATA_OFFLOAD,
								  vdev_id,
								  false);
			} else {
				mutex_unlock(&ipa_ctx->event_lock);
			}
		} else {
			mutex_unlock(&ipa_ctx->event_lock);
		}
		if (is_mlo)
			ath12k_info(ab,
				    "SAP MLO client disconnected, sap_num_mlo_connected_sta = %d\n",
				    ipa_ctx->sap_num_mlo_connected_sta);
		else
			ath12k_info(ab,
				    "SAP client disconnected, sap_num_connected_sta = %d\n",
				    ipa_ctx->sap_num_connected_sta);
		break;
	default:
		ath12k_info(ab, "Invalid event %d\n", event);
		return 0;
	}

	if (!ipa_ctx->uc_loaded)
		goto end;

	if (ath12k_wlan_ipa_send_msg(ipa_ctx, event, mac, dev,
				     is_mlo, session_id) != 0) {
		ath12k_err(ab, "Failed to send message to IPA\n");
		return -EIO;
	}

end:
	return 0;
}
EXPORT_SYMBOL(ath12k_ipa_wlan_evt);

int ath12k_wlan_ipa_send_msg(struct ath12k_ipa *ipa_ctx,
			     enum ipa_wlan_event event, const u8 mac[ETH_ALEN],
			     struct dp_ipa_net_device_priv *dev, bool mlo, u8 id)
{
	struct ipa_msg_meta meta = {0};
	struct ipa_wlan_msg *msg = NULL;
	struct ath12k_base *ab;

	if (!ipa_ctx || !dev) {
		ath12k_err(NULL, "IPA context or dev is NULL\n");
		return -EINVAL;
	}
	ab = ipa_ctx->ab;
	if (!ab) {
		ath12k_err(ab, "ab is NULL\n");
		return -EINVAL;
	}

	meta.msg_len = sizeof(struct ipa_wlan_msg);
	msg = kmalloc(meta.msg_len, GFP_KERNEL);
	if (!msg) {
		ath12k_err(ab, "Failed to allocate msg\n");
		return -ENOMEM;
	}
	meta.msg_type = event;
	strscpy(msg->name, dev->name, IPA_RESOURCE_NAME_MAX);
	memcpy(msg->mac_addr, mac, IPA_MAC_ADDR_SIZE);
	msg->if_index = dev->ifindex;
	if (event == WLAN_AP_CONNECT)
		msg->ast_update = ipa_ctx->config->ipa_wds;
	else
		msg->ast_update = false;
	msg->instance_id = ipa_ctx->hdl;
	msg->vdev_id = id;
	msg->mld_enabled = mlo;

	if (ipa_send_msg(&meta, msg, ath12k_wlan_ipa_msg_free_fn)) {
		ath12k_err(ab, "%s: Evt: %d fail\n", msg->name, meta.msg_type);
		ipa_ctx->stats.failed_msg++;
		kfree(msg);
		return -EIO;
	}
	ipa_ctx->stats.send_msg++;
	return 0;
}
EXPORT_SYMBOL(ath12k_wlan_ipa_send_msg);

int ath12k_wlan_ipa_send_msg_ex(struct ath12k_ipa *ipa_ctx,
				enum ipa_wlan_event event, const u8 mac[ETH_ALEN],
				struct dp_ipa_net_device_priv *dev, bool mlo, u8 id)
{
	struct ipa_msg_meta meta = {0};
	struct ipa_wlan_msg_ex *msg_ex = NULL;
	struct ath12k_base *ab;

	if (!ipa_ctx || !dev) {
		ath12k_err(NULL, "IPA context or dev is NULL\n");
		return -EINVAL;
	}
	ab = ipa_ctx->ab;
	if (!ab) {
		ath12k_err(ab, "ab is NULL\n");
		return -EINVAL;
	}

	meta.msg_len = (sizeof(struct ipa_wlan_msg_ex) +
				sizeof(struct ipa_wlan_hdr_attrib_val));
	msg_ex = kmalloc(meta.msg_len, GFP_KERNEL);
	if (!msg_ex) {
		ath12k_err(ab, "Failed to allocate msg\n");
		return -ENOMEM;
	}
	meta.msg_type = event;
	strscpy(msg_ex->name, dev->name, IPA_RESOURCE_NAME_MAX);
	msg_ex->num_of_attribs = 1;
	msg_ex->attribs[0].attrib_type = WLAN_HDR_ATTRIB_MAC_ADDR;
	if (ath12k_wlan_ipa_uc_is_enabled(ipa_ctx->config))
		msg_ex->attribs[0].offset = WLAN_IPA_UC_WLAN_HDR_DES_MAC_OFFSET;
	else
		msg_ex->attribs[0].offset = WLAN_IPA_WLAN_HDR_DES_MAC_OFFSET;
	memcpy(msg_ex->attribs[0].u.mac_addr, mac, IPA_MAC_ADDR_SIZE);
	msg_ex->instance_id = ipa_ctx->hdl;
	msg_ex->vdev_id = id;
	msg_ex->mld_enabled = mlo;

	if (ipa_send_msg(&meta, msg_ex, ath12k_wlan_ipa_msg_free_fn)) {
		ath12k_err(ab, "%s: Evt: %d fail\n", msg_ex->name, meta.msg_type);
		ipa_ctx->stats.failed_msg++;
		kfree(msg_ex);
		return -EIO;
	}
	ipa_ctx->stats.send_msg++;
	return 0;
}
EXPORT_SYMBOL(ath12k_wlan_ipa_send_msg_ex);

bool ath12k_ipa_get_is_tx1_used(struct ath12k_base *ab, u8 pdev_idx)
{
	if (!ab || pdev_idx >= ab->num_radios || pdev_idx >= MAX_RADIOS)
		return false;
	struct ath12k_pdev *pdev = &ab->pdevs[pdev_idx];

	if (!pdev)
		return false;
	return ((ab->num_radios > 1) && (pdev->pdev_id == 1)) ? true : false;
}
EXPORT_SYMBOL(ath12k_ipa_get_is_tx1_used);

void ath12k_wlan_ipa_msg_free_fn(void *buff, u32 len, u32 type)
{
	kfree(buff);
}
EXPORT_SYMBOL(ath12k_wlan_ipa_msg_free_fn);

void ath12k_wlan_ipa_evt_handler (struct work_struct *work)
{
	struct ath12k_dp_hw_group *dp_hw_grp;
	struct ath12k_ipa_global_ctx *ctx =
		container_of(work, struct ath12k_ipa_global_ctx, work);
	struct wlan_ipa_evt_wq_args *curr_evt, *next_evt;
	struct list_head ipa_evt_list;
	int ret = 0;

	if (!ctx) {
		ath12k_info(NULL, "Invalid ipa_global_ctx\n");
		return;
	}

	dp_hw_grp = ctx->dp_hw_grp;

	do {
		INIT_LIST_HEAD(&ipa_evt_list);

		spin_lock_bh(&ctx->list_lock);
		list_splice_init(&ctx->list, &ipa_evt_list);
		spin_unlock_bh(&ctx->list_lock);

		list_for_each_entry_safe(curr_evt, next_evt, &ipa_evt_list, list_elem) {
			struct ath12k_ipa_event_entry *entry;

			list_del_init(&curr_evt->list_elem);

			struct ath12k_base *ab = dp_hw_grp->dp[curr_evt->device_id]->ab;
			struct ath12k_ipa *ipa_ctx = ab->ath12k_base_extn.ipa_ctx;

			if (!ipa_ctx) {
				ath12k_err(ab, "IPA context not initialized\n");
				goto err_free;
			}

			switch (curr_evt->event) {
			case WLAN_CLIENT_CONNECT_EX:
				entry =
				rhashtable_lookup_fast(ctx->mac_table,
						       curr_evt->mac_addr,
						       ctx->mac_table_params);

				if (entry) {
					struct ath12k_base *ab_old =
						dp_hw_grp->dp[entry->device_id]->ab;
					ath12k_ipa_wlan_evt(ab_old->ath12k_base_extn.ipa_ctx,
							    WLAN_CLIENT_DISCONNECT,
							    entry->vdev_id,
							    entry->pdev_idx,
							    entry->mac_addr, &entry->dev,
							    NL80211_IFTYPE_AP,
							    NL80211_BAND_UNSPECIFIED,
							    entry->is_mlo);
					rhashtable_remove_fast(ctx->mac_table,
							       &entry->node,
							       ctx->mac_table_params);
					kfree(entry);
					entry = NULL;
				}

				entry = kzalloc(sizeof(*entry), GFP_KERNEL);
				if (!entry)
					goto err_free;

				ether_addr_copy(entry->mac_addr, curr_evt->mac_addr);
				entry->vdev_id    = curr_evt->vdev_id;
				entry->device_id = curr_evt->device_id;
				entry->dev = curr_evt->dev;
				entry->pdev_idx = curr_evt->pdev_idx;
				entry->is_mlo = curr_evt->is_mlo;

				ret =
				rhashtable_insert_fast(ctx->mac_table,
						       &entry->node,
						       ctx->mac_table_params);
				if (ret) {
					kfree(entry);
					entry = NULL;
					goto err_free;
				}
				break;

			case WLAN_CLIENT_DISCONNECT:
				entry =
				rhashtable_lookup_fast(ctx->mac_table,
						       curr_evt->mac_addr,
						       ctx->mac_table_params);
				if (entry && entry->device_id == curr_evt->device_id) {
					rhashtable_remove_fast(ctx->mac_table,
							       &entry->node,
							       ctx->mac_table_params);
					kfree(entry);
					entry = NULL;
				} else {
					goto err_free;
				}
				break;

			default:
				/* No action needed */
				break;
			}

			ath12k_ipa_wlan_evt(ipa_ctx, curr_evt->event, curr_evt->vdev_id,
					    curr_evt->pdev_idx, curr_evt->mac_addr,
					    &curr_evt->dev, curr_evt->mode,
					    curr_evt->band, curr_evt->is_mlo);
			kfree(curr_evt);
			continue;

err_free:
			kfree(curr_evt);
		}
	} while (!list_empty(&ctx->list));
}
EXPORT_SYMBOL(ath12k_wlan_ipa_evt_handler);

bool ath12k_wlan_ipa_is_sta_only_offload_enabled(void)
{
	return true;
}
EXPORT_SYMBOL(ath12k_wlan_ipa_is_sta_only_offload_enabled);

bool ath12k_ipa_config_is_enabled(void)
{
	return g_ipa_config
		? ATH12K_IPA_IS_CONFIG_ENABLED(g_ipa_config, ATH12K_IPA_UC_ENABLE_MASK)
		: 0;
}
EXPORT_SYMBOL(ath12k_ipa_config_is_enabled);

void ath12k_ipa_enqueue_evt(enum ipa_wlan_event event, struct ath12k_link_vif *arvif,
			    const u8 mac[ETH_ALEN], bool is_mlo)
{
	struct ath12k_base *ab = arvif->ar->ab;
	struct ieee80211_vif *vif = arvif->ahvif->vif;
	struct wireless_dev *wdev = ieee80211_vif_to_wdev(vif);
	struct ath12k_dp_hw_group *dp_hw_grp = ab->dp->dp_hw_grp;
	struct ath12k_ipa_global_ctx *ipa_global_ctx = dp_hw_grp->ipa_global_ctx;
	struct wlan_ipa_evt_wq_args *ev;
	struct net_device *dev;
	struct ieee80211_chanctx_conf *chanctx = &arvif->chanctx;

	if (!ipa_global_ctx) {
		ath12k_err(ab, "Invalid ipa_global_ctx\n");
		return;
	}

	if (!wdev || !wdev->netdev) {
		ath12k_warn(ab, "Invalid wdev or netdev\n");
		return;
	}
	dev = wdev->netdev;

	if (!chanctx || !chanctx->def.chan) {
		ath12k_warn(ab, "Invalid channel context\n");
		return;
	}

	ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev)
		return;
	ev->event = event;
	ev->vdev_id = arvif->vdev_id;
	ev->pdev_idx = arvif->ar->pdev_idx;
	ev->device_id = ab->device_id;
	strscpy(ev->dev.name, dev->name, IFNAMSIZ);
	ether_addr_copy(ev->dev.dev_addr, (const u8 *)dev->dev_addr);
	ev->dev.ifindex = dev->ifindex;
	ev->mode = vif->type;
	ev->band = chanctx->def.chan->band;
	ether_addr_copy(ev->mac_addr, mac);
	ev->is_mlo = is_mlo;

	spin_lock_bh(&ipa_global_ctx->list_lock);
	list_add_tail(&ev->list_elem, &ipa_global_ctx->list);
	spin_unlock_bh(&ipa_global_ctx->list_lock);

	queue_work(system_unbound_wq, &ipa_global_ctx->work);
}
EXPORT_SYMBOL(ath12k_ipa_enqueue_evt);

void ath12k_wlan_ipa_intrabss_enable_disable(struct ath12k_ipa *ipa_ctx,
					     u32 vdev_id, bool enable)
{
		struct ipa_intrabss_control_params intrabss_req = {0};
		struct ath12k_base *ab = ipa_ctx->ab;

		u32 intra_bss_fwd = 0;

		if (!enable || ipa_ctx->disable_intrabss_fwd[vdev_id]) {
			ath12k_info(ab, "%s: ipa_offload->enable=%d, rx_fwd_disabled=%d\n",
				    __func__, enable,
				    ipa_ctx->disable_intrabss_fwd[vdev_id]);
			intra_bss_fwd = 1;
		}

		intrabss_req.vdev_id = vdev_id;
		intrabss_req.enable = intra_bss_fwd;

		if (ath12k_ipa_send_intrabss_enable_disable(ab,
							    &intrabss_req)) {
			ath12k_err(ab, "intrabss offload vdev_id=%d, enable=%d failure\n",
				   vdev_id, intra_bss_fwd);
		}
}
EXPORT_SYMBOL(ath12k_wlan_ipa_intrabss_enable_disable);

void ath12k_wlan_ipa_uc_offload_enable(struct ath12k_ipa *ipa_ctx,
				       u32 offload_type, u32 vdev_id,
				       bool enable)
{
	struct ipa_uc_offload_control_params req = {0};
	struct ath12k_base *ab = ipa_ctx->ab;

	if (vdev_id >= ATH12K_IPA_MAX_SESSION) {
		ath12k_err(ab, "invalid vdev id: %d\n", vdev_id);
		return;
	}

	if (enable == ipa_ctx->vdev_offload_enabled[vdev_id]) {
		ath12k_info(ab,
			    "IPA ofld status already set: ofld_type=%d, vdev_id=%d, en=%d\n",
			    offload_type, vdev_id, enable);
		return;
	}

	ath12k_info(ab, "offload_type=%d, vdev_id=%d, enable=%d\n",
		    offload_type, vdev_id, enable);

	req.offload_type = offload_type;
	req.vdev_id = vdev_id;
	req.enable = enable;

	if (ath12k_ipa_send_uc_offload_enable_disable(ab, &req)) {
		ath12k_err(ab,
			   "Fail to enable IPA ofld: ofld type=%d, vdev_id=%d, en=%d\n",
			   offload_type, vdev_id, enable);
	} else {
		ipa_ctx->vdev_offload_enabled[vdev_id] = enable;
	}

	ath12k_wlan_ipa_intrabss_enable_disable(ipa_ctx, vdev_id, enable);
}
EXPORT_SYMBOL(ath12k_wlan_ipa_uc_offload_enable);

int ath12k_ipa_send_intrabss_enable_disable(struct ath12k_base *ab,
					    struct ipa_intrabss_control_params *params)
{
	if (!ab) {
		ath12k_info(NULL, "ab is NULL\n");
		return -EINVAL;
	}

	rcu_read_lock();
	struct ath12k *ar = ath12k_mac_get_ar_by_vdev_id(ab, params->vdev_id);
	int ret;

	if (!ar) {
		ath12k_warn(ab, "Invalid vdev id %d\n", params->vdev_id);
		rcu_read_unlock();
		return -EINVAL;
	}
	ret = ath12k_wmi_vdev_set_param_cmd(ar, params->vdev_id,
					    WMI_VDEV_PARAM_INTRA_BSS_FWD,
					    params->enable);
	if (ret) {
		ath12k_warn(ab, "Failed to set param for vdev %d: %d\n",
			    params->vdev_id, ret);
		rcu_read_unlock();
		return ret;
	}
	rcu_read_unlock();
	return 0;
}
EXPORT_SYMBOL(ath12k_ipa_send_intrabss_enable_disable);

int
ath12k_ipa_send_uc_offload_enable_disable(struct ath12k_base *ab,
					  struct ipa_uc_offload_control_params *params)
{
	if (!ab) {
		ath12k_info(NULL, "ab is NULL\n");
		return -EINVAL;
	}

	rcu_read_lock();
	struct ath12k *ar = ath12k_mac_get_ar_by_vdev_id(ab, params->vdev_id);
	int ret;

	if (!ar) {
		ath12k_warn(ab, "Invalid vdev id %d\n", params->vdev_id);
		rcu_read_unlock();
		return -EINVAL;
	}
	ret = ath12k_wmi_ipa_offload_enable_disable_param_cmd(ar, params->vdev_id,
							      params->offload_type,
							      params->enable);
	if (ret) {
		ath12k_warn(ab,
			    "Failed to set IPA offload status for vdev %d: %d\n",
			    params->vdev_id, ret);
		rcu_read_unlock();
		return ret;
	}
	rcu_read_unlock();
	return 0;
}
EXPORT_SYMBOL(ath12k_ipa_send_uc_offload_enable_disable);

bool ath12k_wlan_ipa_is_enabled(struct ath12k_ipa_config *ipa_cfg)
{
	return ATH12K_IPA_IS_CONFIG_ENABLED(ipa_cfg, ATH12K_IPA_ENABLE_MASK);
}
EXPORT_SYMBOL(ath12k_wlan_ipa_is_enabled);

bool ath12k_wlan_ipa_uc_is_enabled(struct ath12k_ipa_config *ipa_cfg)
{
	return ATH12K_IPA_IS_CONFIG_ENABLED(ipa_cfg, ATH12K_IPA_UC_ENABLE_MASK);
}
EXPORT_SYMBOL(ath12k_wlan_ipa_uc_is_enabled);

bool ath12k_wlan_ipa_uc_sta_is_enabled(struct ath12k_ipa_config *ipa_cfg)
{
	return ATH12K_IPA_IS_CONFIG_ENABLED(ipa_cfg, ATH12K_IPA_UC_STA_ENABLE_MASK);
}
EXPORT_SYMBOL(ath12k_wlan_ipa_uc_sta_is_enabled);

int ath12k_wlan_ipa_get_ifaceid(struct ath12k_ipa *ipa_ctx, u8 session_id)
{
	struct ath12k_ipa_iface_context *iface_ctx;
	int i;

	for (i = 0; i < ATH12K_IPA_MAX_IFACE; i++) {
		iface_ctx = &ipa_ctx->iface_context[i];
		if (iface_ctx->session_id == session_id)
			break;
	}

	return i;
}
EXPORT_SYMBOL(ath12k_wlan_ipa_get_ifaceid);

struct ath12k_ipa_iface_context *
ath12k_wlan_ipa_get_iface(struct ath12k_ipa *ipa_ctx,
			  enum nl80211_iftype mode)
{
	struct ath12k_ipa_iface_context *iface_ctx = NULL;
	int i;

	for (i = 0; i < ATH12K_IPA_MAX_IFACE; i++) {
		iface_ctx = &ipa_ctx->iface_context[i];

		if (iface_ctx->device_mode == ath12k_ipa_mode(mode))
			return iface_ctx;
	}

	return NULL;
}
EXPORT_SYMBOL(ath12k_wlan_ipa_get_iface);

struct ath12k_ipa_iface_context *
ath12k_wlan_ipa_get_iface_by_mode_netdev(struct ath12k_ipa *ipa_ctx,
					 enum nl80211_iftype mode,
					 struct dp_ipa_net_device_priv *net_dev_priv,
					 u8 session_id)
{
	struct ath12k_ipa_iface_context *iface_ctx = NULL;
	int i;

	for (i = 0; i < ATH12K_IPA_MAX_IFACE; i++) {
		iface_ctx = &ipa_ctx->iface_context[i];

		if (iface_ctx->device_mode == ath12k_ipa_mode(mode) &&
		    wlan_ipa_check_iface_netdev_sessid(iface_ctx, net_dev_priv,
						       session_id))
			return iface_ctx;
	}

	return NULL;
}
EXPORT_SYMBOL(ath12k_wlan_ipa_get_iface_by_mode_netdev);

enum ath12k_ipa_opmode ath12k_ipa_mode(enum nl80211_iftype mode)
{
	enum ath12k_ipa_opmode ipa_mode;

	switch (mode) {
	case NL80211_IFTYPE_STATION:
		ipa_mode = ATH12K_IPA_STA;
		break;
	case NL80211_IFTYPE_AP:
		ipa_mode = ATH12K_IPA_SAP;
		break;
	case NL80211_IFTYPE_MAX:
	default:
		ipa_mode = ATH12K_IPA_MAX_NO_OF_MODE;
		break;
	}
	return ipa_mode;
}
EXPORT_SYMBOL(ath12k_ipa_mode);

static struct ath12k_ipa_iface_context *
ath12k_dp_ipa_get_if_ctx(struct ath12k_ipa *ipa_ctx, u8 vdev_id)
{
	struct ath12k_ipa_iface_context *if_ctx;
	u8 if_idx;

	if_idx = ipa_ctx->vdev_to_iface[vdev_id];

	if (if_idx >= ATH12K_IPA_MAX_IFACE) {
		ath12k_err(ipa_ctx->ab, "vdev_id=%d not in ipa_ctx", vdev_id);
		return NULL;
	}

	if_ctx = &ipa_ctx->iface_context[if_idx];
	if (if_ctx->session_id >= ATH12K_IPA_MAX_SESSION) {
		ath12k_err(ipa_ctx->ab, "vdev_id=%d at if_idx=%u is invalid",
			   if_ctx->session_id, if_idx);
		return NULL;
	}

	return if_ctx;
}

void
ath12k_dp_ipa_peer_unmap_event_wds(struct ath12k_base *ab, u8 vdev_id, u16 peer_id,
				   u8 *mac_addr)
{
	struct ath12k_link_vif *arvif;
	struct ath12k_ipa *ipa_ctx;
	struct ath12k_ipa_iface_context *if_ctx;
	bool mlo;

	rcu_read_lock();

	arvif = ath12k_mac_get_arvif_by_vdev_id(ab, vdev_id);
	if (!arvif) {
		ath12k_err(ab, "arvif is null");
		goto exit;
	}

	ipa_ctx = ab->ath12k_base_extn.ipa_ctx;
	if (!ipa_ctx) {
		ath12k_err(ab, "ipa_ctx is null");
		goto exit;
	}

	if_ctx = ath12k_dp_ipa_get_if_ctx(ipa_ctx, vdev_id);
	if (!if_ctx) {
		ath12k_err(ab, "if_ctx is null");
		goto exit;
	}

	mlo = if_ctx->is_mlo_vdev;

	ath12k_ipa_enqueue_evt(WLAN_CLIENT_DISCONNECT, arvif, mac_addr,
			       mlo);
exit:
	rcu_read_unlock();
}

void
ath12k_dp_ipa_peer_map_event_wds(struct ath12k_base *ab, u8 vdev_id, u16 peer_id,
				 u8 *mac_addr)
{
	struct ath12k_dp_peer *dp_peer;
	struct ath12k_link_vif *arvif;
	struct ieee80211_vif *vif;
	struct wireless_dev *wdev;
	struct net_device *dev;
	struct ath12k_dp *dp;
	struct ath12k_pdev_dp *dp_pdev;
	struct ath12k_ipa *ipa_ctx;
	struct ath12k_ipa_iface_context *if_ctx;
	bool mlo;

	rcu_read_lock();

	arvif = ath12k_mac_get_arvif_by_vdev_id(ab, vdev_id);
	if (!arvif) {
		ath12k_err(ab, "arvif is null");
		goto exit;
	}

	ipa_ctx = ab->ath12k_base_extn.ipa_ctx;
	if (!ipa_ctx) {
		ath12k_err(ab, "ipa_ctx is null");
		goto exit;
	}

	if_ctx = ath12k_dp_ipa_get_if_ctx(ipa_ctx, vdev_id);
	if (!if_ctx) {
		ath12k_err(ab, "if_ctx is null");
		goto exit;
	}

	vif = arvif->ahvif->vif;
	wdev = ieee80211_vif_to_wdev(vif);
	dev = wdev->netdev;

	dp = ath12k_ab_to_dp(ab);
	dp_pdev = ath12k_dp_to_dp_pdev(dp, arvif->ar->pdev_idx);
	dp_peer = ath12k_dp_peer_find_by_peerid_index(dp, dp_pdev, peer_id);

	if (dp_peer) {
		mlo = if_ctx->is_mlo_vdev;
		ath12k_ipa_enqueue_evt(WLAN_CLIENT_CONNECT_EX, arvif, mac_addr,
				       mlo);

		ath12k_info(ab, "send rx_unexpected_4addr to hostapd");
		cfg80211_rx_unexpected_4addr_frame(dev, dp_peer->addr, GFP_ATOMIC, -1);
	}
exit:
	rcu_read_unlock();
}

/**
 * ath12k_dp_ipa_mac80211_rx() - Submit packet to mac80211 RX path
 * @hw: ieee80211_hw structure
 * @sta: station structure
 * @skb: socket buffer containing the packet
 *
 * Submits the received packet to the mac80211 RX processing path
 * with bottom half disabled for proper synchronization.
 *
 * Context: Can be called from process context
 * Return: None
 */
static inline void
ath12k_dp_ipa_mac80211_rx(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
			  struct sk_buff *skb)
{
	local_bh_disable();
	ieee80211_rx_napi(hw, sta, skb, NULL);
	local_bh_enable();
}

/**
 * ath12k_dp_ipa_w2i() - IPA exception flow handler (WLAN to IPA)
 * @priv: private context (ath12k_wlan_ipa_priv)
 * @evt: IPA event type
 * @data: event data (typically sk_buff pointer)
 *
 * Handles packets received from IPA in the exception path. This includes
 * EAPOL and WAPI packets that need special handling, as well as packets
 * for unauthorized peers. Valid data packets are submitted to mac80211
 * for processing.
 *
 * Context: Can be called from process context
 * Return: None
 */
static void ath12k_dp_ipa_w2i(void *priv, enum ipa_dp_evt_type evt,
			      unsigned long data)
{
	struct ath12k_ipa *ipa_ctx;
	struct sk_buff *skb;
	u8 if_idx;
	struct ath12k_ipa_iface_context *if_ctx;
	struct ethhdr *eth;
	struct net_device *dev;
	struct ath12k_base *ab;
	struct ath12k_dp *dp;
	struct ath12k_pdev_dp *dp_pdev;
	struct ath12k *ar;
	struct ieee80211_hw *hw;
	struct ath12k_dp_peer *dp_peer;
	u16 peer_id;
	u8 vdev_id;
	struct ieee80211_rx_status *status;

	skb = (struct sk_buff *)data;
	if (!skb) {
		ath12k_err(NULL, "skb is NULL");
		return;
	}

	if (evt != IPA_RECEIVE) {
		ath12k_err(NULL, "ipa gave wrong event: 0x%X", evt);
		goto drop;
	}

	ipa_ctx = (struct ath12k_ipa *)priv;
	if (!ipa_ctx) {
		ath12k_err(NULL, "ipa_ctx is NULL, dropping pkt");
		if (evt == IPA_RECEIVE)
			dev_kfree_skb_any(skb);
		return;
	}

	ipa_ctx->stats.rx_excep++;

	ab = ipa_ctx->ab;
	dp = ath12k_ab_to_dp(ab);

	eth = skb_eth_hdr(skb);
	peer_id = ath12k_dp_ipa_skb_get_peer_id(skb);
	vdev_id = ath12k_dp_ipa_skb_get_vdev_id(skb);

	/* TODO: add check for WLAN_IPA_UC_ENABLE_MASK */
	if_idx = ipa_ctx->vdev_to_iface[vdev_id];

	if (if_idx >= ATH12K_IPA_MAX_IFACE) {
		ath12k_err(ab, "pkt received before intf setup vdev_id=%d",
			   vdev_id);
		ipa_ctx->stats.rx_no_iface++;
		/* TODO: If EAPOL is rx before iface setup, send them to nw stack */
		goto drop;
	}

	if_ctx = &ipa_ctx->iface_context[if_idx];
	if (if_ctx->session_id >= ATH12K_IPA_MAX_SESSION) {
		ath12k_err(ab, "vdev_id of if_idx %u is invalid:%d",
			   if_idx, if_ctx->session_id);
		goto drop;
	}

	if (eth->h_proto == cpu_to_be16(ETH_P_PAE) ||
		eth->h_proto == cpu_to_be16(ETH_P_WAPI)) {
		ipa_ctx->stats.rx_eapol++;
		dev = dev_get_by_name(&init_net, if_ctx->net_dev.name);
		if (!dev) {
			ath12k_err(ab, "netdev is null for '%s'",
				   if_ctx->net_dev.name);
			goto drop;
		}
		skb->protocol = eth_type_trans(skb, dev);
		cfg80211_rx_control_port(dev, skb, false, -1);
		dev_put(dev);
		return;
	}

	rcu_read_lock();

	ar = ath12k_mac_get_ar_by_vdev_id(ab, vdev_id);
	if (!ar) {
		ath12k_err(ab, "ar is NULL");
		rcu_read_unlock();
		goto drop;
	}

	dp_pdev = ath12k_dp_to_dp_pdev(dp, ar->pdev_idx);

	dp_peer = ath12k_dp_peer_find_by_peerid_index(dp, dp_pdev, peer_id);
	if (!dp_peer) {
		/* NOTE: For MLO case, peer_id sent by IPA is not correct.
		 * So try mac addr search
		 */
		spin_lock_bh(&ar->ah->dp_hw.peer_lock);
		dp_peer = ath12k_dp_peer_find(&ar->ah->dp_hw, eth->h_source);
		if (!dp_peer) {
			ath12k_err(ab, "dp_peer is null (peer_id=%d mac=%pM)",
				   peer_id, eth->h_source);
			spin_unlock_bh(&ar->ah->dp_hw.peer_lock);
			rcu_read_unlock();
			goto drop;
		}
		peer_id = dp_peer->peer_id;
		spin_unlock_bh(&ar->ah->dp_hw.peer_lock);

		dp_peer = ath12k_dp_peer_find_by_peerid_index(dp, dp_pdev, peer_id);
		if (!dp_peer) {
			ath12k_err(ab, "dp_peer is null (peer_id=%d mac=%pM)",
				   peer_id, eth->h_source);
			rcu_read_unlock();
			goto drop;
		}
	}

	if (!dp_peer->is_authorized) {
		ath12k_err(ab, "peer is unauthorized. peer_id=%d src=%pM dst=%pM proto=0x%X",
			   peer_id, eth->h_source, eth->h_dest,
			   cpu_to_be16(eth->h_proto));
		rcu_read_unlock();
		goto drop;
	}

	hw = ath12k_ar_to_hw(ar);
	skb->protocol = eth->h_proto;

	/* reset skb cb and fill with ieee80211_rx_status */
	memset(skb->cb, 0, sizeof(skb->cb));
	status = IEEE80211_SKB_RXCB(skb);

	status->flag |= RX_FLAG_8023;
	status->band = ar->rx_channel->band;

	status->link_id = dp_peer->hw_links[dp_pdev->hw_link_id];
	status->link_valid = true;

	ath12k_dp_ipa_mac80211_rx(hw, dp_peer->sta, skb);
	ipa_ctx->stats.sent_to_mac++;

	rcu_read_unlock();
	return;
drop:
	ipa_ctx->stats.rx_drop_excep++;
	dev_kfree_skb_any(skb);
	return;
}
