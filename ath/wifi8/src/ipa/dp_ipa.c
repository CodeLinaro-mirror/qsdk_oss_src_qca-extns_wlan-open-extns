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

void ath12k_wifi8_dp_ipa_set_metadata(struct ipa_wdi_reg_intf_in_params *in)
{
	in->is_meta_data_valid = 0;
}

void ath12k_wifi8_dp_ipa_iface_setup_set_vp_num(struct ipa_wdi_reg_intf_in_params *in)
{

}

void ath12k_wifi8_dp_ipa_iface_setup_set_intf_idx(struct ipa_wdi_reg_intf_in_params *in,
						  int intf_idx)
{

}

static u8 ath12k_wifi8_dp_ipa_get_wdi_version(void)
{
	return IPA_WDI_6;
}

static void
ath12k_wifi8_dp_ipa_rx_pipe_set_rdi(struct ath12k_ipa *ipa_ctx,
				    struct ipa_wdi_pipe_setup_info_smmu *rx_smmu)
{
	rx_smmu->rdi = ATH12K_DP_WIFI8_REO2SW5_RDI;
}

static void ath12k_wifi8_dp_ipa_srng_get_rsrc(struct ath12k_base *ab)
{
	ath12k_wifi8_dp_fill_rx_src(ab);
	ath12k_wifi8_dp_fill_tx_src(ab);

	return;
}

/**
 * ath12k_wifi8_dp_ipa_set_tx_doorbell_paddr() - Program TQM2PPE ring HP_ADDR
 *	registers with IPA's TX completion doorbell physical address
 * @ipa_ctx: IPA context holding the TX completion doorbell PA and ring state
 *
 * After this call the TQM2PPE ring hardware will DMA-write the updated head
 * pointer to IPA's doorbell PA each time TQM appends a new TX-completion
 * entry, letting IPA detect new completions without polling.
 */
static void ath12k_wifi8_dp_ipa_set_tx_doorbell_paddr(struct ath12k_ipa *ipa_ctx)
{
	struct ath12k_base *ab = ipa_ctx->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8 = ath12k_get_dp_wifi8(dp);
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa = dp_wifi8->extn.ipa;
	struct ath12k_hal *hal = &ab->hal;
	struct hal_srng *tqm_srng;
	u64 hp_addr = ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_paddr;
	u32 reg_base;
	u32 hp_lsb_off, hp_msb_off;
	int ring_id;

	/*
	 * HP_ADDR_LSB/MSB are at a uniform offset from BASE_LSB for all
	 * UMAC DST rings (REO, TQM2PPE, WBM).  Derive it from the REO1
	 * register map, which is the reference layout.
	 */
	hp_lsb_off = HAL_REO1_RING_HP_ADDR_LSB(hal) - HAL_REO1_RING_BASE_LSB(hal);
	hp_msb_off = HAL_REO1_RING_HP_ADDR_MSB(hal) - HAL_REO1_RING_BASE_LSB(hal);

	ring_id = dp_wifi8_ipa->tqm2ipa_ring.ring_id;
	tqm_srng = &ab->hal.srng_list[ring_id];
	reg_base = tqm_srng->hwreg_base[HAL_SRNG_REG_GRP_R0];

	ath12k_ipa_info(ab, "set_tx_db: ring_id=%d reg_base=0x%x "
	       "hp_lsb_off=0x%x hp_msb_off=0x%x "
	       "=> LSB_reg=0x%x MSB_reg=0x%x "
	       "hp_addr=0x%llx (lsb=0x%x msb=0x%x)",
	       ring_id, reg_base,
	       hp_lsb_off, hp_msb_off,
	       reg_base + hp_lsb_off, reg_base + hp_msb_off,
	       hp_addr,
	       (u32)(hp_addr & HAL_ADDR_LSB_REG_MASK),
	       (u32)(hp_addr >> HAL_ADDR_MSB_REG_SHIFT));

	ath12k_hif_write32(ab, reg_base + hp_lsb_off,
			   hp_addr & HAL_ADDR_LSB_REG_MASK);
	ath12k_hif_write32(ab, reg_base + hp_msb_off,
			   hp_addr >> HAL_ADDR_MSB_REG_SHIFT);
}

/**
 * ath12k_wifi8_dp_ipa_set_rx_doorbell_paddr() - Program REO2SW5 ring HP_ADDR
 *	registers with IPA's RX ready doorbell physical address
 * @ipa_ctx: IPA context holding the RX ready doorbell PA and ring state
 *
 * After this call the REO2SW5 ring hardware will DMA-write the updated head
 * pointer to IPA's doorbell PA each time REO steers a new RX packet to the
 * IPA ring, letting IPA detect new RX frames without polling.
 */
static void ath12k_wifi8_dp_ipa_set_rx_doorbell_paddr(struct ath12k_ipa *ipa_ctx)
{
	struct ath12k_base *ab = ipa_ctx->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8 = ath12k_get_dp_wifi8(dp);
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa = dp_wifi8->extn.ipa;
	struct ath12k_hal *hal = &ab->hal;
	struct hal_srng *reo_srng;
	u64 hp_addr = ipa_ctx->ipa_uc_rx_rsc.rx_ready_doorbell_paddr;
	u32 reg_base;
	u32 hp_lsb_off, hp_msb_off;
	int ring_id;

	hp_lsb_off = HAL_REO1_RING_HP_ADDR_LSB(hal) - HAL_REO1_RING_BASE_LSB(hal);
	hp_msb_off = HAL_REO1_RING_HP_ADDR_MSB(hal) - HAL_REO1_RING_BASE_LSB(hal);

	ring_id = dp_wifi8_ipa->reo2sw5.ring_id;
	reo_srng = &ab->hal.srng_list[ring_id];
	reg_base = reo_srng->hwreg_base[HAL_SRNG_REG_GRP_R0];

	ath12k_ipa_info(ab, "set_rx_db: ring_id=%d reg_base=0x%x "
	       "hp_lsb_off=0x%x hp_msb_off=0x%x "
	       "=> LSB_reg=0x%x MSB_reg=0x%x "
	       "hp_addr=0x%llx (lsb=0x%x msb=0x%x)",
	       ring_id, reg_base,
	       hp_lsb_off, hp_msb_off,
	       reg_base + hp_lsb_off, reg_base + hp_msb_off,
	       hp_addr,
	       (u32)(hp_addr & HAL_ADDR_LSB_REG_MASK),
	       (u32)(hp_addr >> HAL_ADDR_MSB_REG_SHIFT));

	ath12k_hif_write32(ab, reg_base + hp_lsb_off,
			   hp_addr & HAL_ADDR_LSB_REG_MASK);
	ath12k_hif_write32(ab, reg_base + hp_msb_off,
			   hp_addr >> HAL_ADDR_MSB_REG_SHIFT);
}

/**
 * ath12k_wifi8_dp_ipa_tx_comp_ring_init_hp() - Initialize TQM2PPE ring head
 *	pointer and ring IPA TX completion doorbell.
 * @ipa_ctx: IPA context for this device instance
 *
 * Remaps the TQM2PPE ring's hp_addr to point directly at IPA's TX completion
 * doorbell virtual address, writes the initial cached HP value to TQM hardware,
 * and rings the IPA doorbell with the same value so IPA starts consuming
 * pre-seeded TX buffer entries.
 *
 * Context: Must be called after IPA pipes are enabled via
 *          ipa_wdi_enable_pipes_per_inst().
 */
static void ath12k_wifi8_dp_ipa_tx_comp_ring_init_hp(struct ath12k_ipa *ipa_ctx)
{
	struct ath12k_base *ab = ipa_ctx->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8 = ath12k_get_dp_wifi8(dp);
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa = dp_wifi8->extn.ipa;
	struct hal_srng *tqm_srng;
	u32 cached_hp;
	int ring_id;

	/*
	 * IPA pipes must be enabled (via ipa_wdi_enable_pipes_per_inst) before
	 * ringing this doorbell, otherwise IPA will not process the entry.
	 * The caller (ath12k_dp_ipa_enable_pipes) guarantees this ordering.
	 */
	ath12k_ipa_info(ab, "tqm2ipa init_hp: ipa_pipes_enabled=%d",
	       atomic_read(&ipa_ctx->ipa_pipes_enabled));

	ring_id = dp_wifi8_ipa->tqm2ipa_ring.ring_id;
	tqm_srng = &ab->hal.srng_list[ring_id];

	/* Map the IPA doorbell TX completion vaddr as the ring HP address.
	 * This makes TQM hardware's shadow HP writes land directly at the
	 * IPA doorbell, so IPA is notified automatically on every TX completion
	 * entry TQM produces into this ring.
	 */
	tqm_srng->u.dst_ring.hp_addr =
		(u32 __iomem *)ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_vaddr;

	cached_hp = tqm_srng->u.dst_ring.cached_hp;

	ath12k_ipa_info(ab, "tqm2ipa ring params: ring_id=%d "
	       "ring_base_paddr=0x%llx ring_size=%u num_entries=%u entry_size=%u "
	       "cached_hp=%u hp_addr(doorbell_vaddr)=%p doorbell_paddr=0x%lx",
	       ring_id,
	       (u64)tqm_srng->ring_base_paddr,
	       tqm_srng->ring_size,
	       tqm_srng->num_entries,
	       tqm_srng->entry_size,
	       cached_hp,
	       tqm_srng->u.dst_ring.hp_addr,
	       ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_paddr);

	/* Tell TQM hardware the initial HP value so it knows positions
	 * 0..cached_hp-1 are pre-seeded by the host (not yet written by TQM).
	 * TQM will start appending real TX-completion entries at cached_hp.
	 */
	ath12k_hif_write32(ab, HAL_SEQ_WCSS_UMAC_TQM_REG +
			   HAL_TQM2PPE_RELEASE_RING_HP, cached_hp);
	ath12k_ipa_info(ab, "tqm2ipa TQM HP write: wrote %u readback=%u",
	       cached_hp,
	       ath12k_hif_read32(ab, HAL_SEQ_WCSS_UMAC_TQM_REG +
				 HAL_TQM2PPE_RELEASE_RING_HP));

	/* Update HP via hp_addr (now remapped to IPA doorbell) with cached HP.
	 * This gives IPA the initial HP so it starts consuming the pre-seeded
	 * TX buffer entries.
	 */
	if (ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_vaddr) {
		*tqm_srng->u.dst_ring.hp_addr = cached_hp;
		ath12k_ipa_info(ab, "tqm2ipa doorbell RANG via hp_addr: "
		       "wrote %u to hp_addr=%p (readback=0x%x)",
		       cached_hp,
		       tqm_srng->u.dst_ring.hp_addr,
		       *tqm_srng->u.dst_ring.hp_addr);
	} else {
		ath12k_ipa_info(ab, "ath12k DBG tqm2ipa doorbell NOT RANG: "
		       "tx_comp_doorbell_vaddr is NULL");
	}

	ath12k_ipa_info(ab,
			"tqm2ipa HP init: ring_id=%d cached_hp=%u hp_addr(doorbell)=%p",
			ring_id, cached_hp,
			tqm_srng->u.dst_ring.hp_addr);
}

static int ath12k_wifi8_dp_ipa_srng_setup(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag = ab->ag;
	struct ath12k_base *partner_ab;
	u64 orig_dma_mask;
	int i, ret = 0;

	/**
	 * Limit phys address withing 32 bit space during ring initialization since
	 * IPA cannot handle larger address space.
	 */
	orig_dma_mask = dma_get_mask(ab->dev);
	ret = dma_set_mask_and_coherent(ab->dev, DMA_BIT_MASK(32));
	if (ret) {
		ath12k_ipa_err(ab, "failed to set DMA_BIT_MASK(32): %d", ret);
		ath12k_ipa_err(ab, "ring PA may exceed 32-bit; IPA crash likely");
	} else {
		ath12k_ipa_info(ab, "DMA mask set to 32-bit (orig_dma_mask=0x%llx)",
				orig_dma_mask);
	}

	ret = ath12k_wifi8_dp_rx_ring_setup_ipa(ab);
	if (ret) {
		ath12k_ipa_err(ab, "failed to setup RX rings");
		goto restore_dma_mask;
	}

	for (i = 0; i < ag->num_devices; i++) {
		partner_ab = ag->ab[i];
		if (!partner_ab || partner_ab->is_bypassed)
			continue;

		ret = ath12k_wifi8_dp_rx_ipa2wbm_idle_buf_config_qcn9625(partner_ab);
		if (ret) {
			ath12k_ipa_err(partner_ab,
				       "failed to configure ipa2wbm_idle_buf ring");
			goto ipa2wbm_idle_buf_err;
		}
	}

	dma_set_mask_and_coherent(ab->dev, orig_dma_mask);
	ath12k_ipa_info(ab, "ath12k IPA RX rings: DMA mask restored to 0x%llx",
			orig_dma_mask);

	ret = ath12k_wifi8_dp_tx_ring_setup_ipa(ab);
	if (ret) {
		ath12k_ipa_err(ab, "failed to set up tx ipa extn rings :%d", ret);
		goto tx_err;
	}
	return ret;

ipa2wbm_idle_buf_err:
	ath12k_wifi8_dp_rx_ring_free_ipa(ab);
restore_dma_mask:
	dma_set_mask_and_coherent(ab->dev, orig_dma_mask);
	return ret;
tx_err:
	ath12k_wifi8_dp_rx_ring_free_ipa(ab);
	return ret;
}

static int ath12k_wifi8_dp_ipa_srng_cleanup(struct ath12k_base *ab)
{
	ath12k_wifi8_dp_tx_ring_cleanup_ipa(ab);
	ath12k_wifi8_dp_rx_ring_free_ipa(ab);
	return 0;
}

static int
ath12k_wifi8_dp_ipa_set_ipa_ctx_for_all_ab(struct ath12k_ipa *ipa_ctx)
{
	struct ath12k_hw_group *ag;
	struct ath12k_base *ab;
	int i;

	ag = ath12k_core_get_ag();
	if (!ag) {
		ath12k_ipa_err(NULL, "ag is NULL");
		return -EINVAL;
	}

	for (i = 0; i < ag->num_devices; i++) {
		ab = ag->ab[i];
		if (!ab)
			continue;
		IPA_CTX(ab) = ipa_ctx;
	}
	return 0;
}

static struct ath12k_ipa_arch_ops ipa_wifi8_arch_ops = {
	.dp_ipa_init = ath12k_wifi8_dp_ipa_init,
	.dp_srng_cleanup = ath12k_wifi8_dp_ipa_srng_cleanup,
	.dp_srng_setup = ath12k_wifi8_dp_ipa_srng_setup,
	.dp_srng_get_rsrc = ath12k_wifi8_dp_ipa_srng_get_rsrc,
	.dp_rx_default_routing = NULL,
	.dp_rxdma_buf_setup = NULL,
	.dp_get_tx_bank_id = NULL,
	.dp_rx_buf_smmu_map_unmap = NULL,
	.dp_srng_rx_init = ath12k_wifi8_dp_ipa_ipa2wbm_idle_buf_ring_init,
	.ipa_set_rx_doorbell_paddr = ath12k_wifi8_dp_ipa_set_rx_doorbell_paddr,
	.ipa_set_tx_doorbell_paddr = ath12k_wifi8_dp_ipa_set_tx_doorbell_paddr,
	.ipa_tx_comp_ring_init_hp = ath12k_wifi8_dp_ipa_tx_comp_ring_init_hp,
	.ipa_tx_buffer_attach = NULL,
	.dp_rx_pipe_set_rdi = ath12k_wifi8_dp_ipa_rx_pipe_set_rdi,
	.ipa_get_wdi_version = ath12k_wifi8_dp_ipa_get_wdi_version,
	.ipa_iface_setup_set_metadata = ath12k_wifi8_dp_ipa_set_metadata,
	.ipa_iface_setup_set_vp_num = ath12k_wifi8_dp_ipa_iface_setup_set_vp_num,
	.ipa_iface_setup_set_intf_idx = ath12k_wifi8_dp_ipa_iface_setup_set_intf_idx,
	.ipa_client_connect_ex = NULL,
	.ipa_client_disconnect = NULL,
};

int
ath12k_wifi8_dp_ipa_init(struct ath12k_base *ab)
{
	int ret;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8 = ath12k_get_dp_wifi8(dp);
	struct ath12k_ipa *ipa_ctx;

	ret = ath12k_dp_ipa_plugin_register_ops_extn(ab);
	if (ret) {
		ath12k_ipa_err(ab, "failed to allocate ipa_ctx");
		return ret;
	}

	ipa_ctx = IPA_CTX(ab);
	ipa_ctx->arch_ops = &ipa_wifi8_arch_ops;

	ret = ath12k_wifi8_dp_ipa_set_ipa_ctx_for_all_ab(ipa_ctx);
	if (ret) {
		ath12k_ipa_err(ab, "failed to set ipa_ctx for all ab");
		return ret;
	}

	dp_wifi8->extn.ipa = kmalloc(sizeof(struct ath12k_dp_wifi8_ipa), GFP_KERNEL);
	if (!dp_wifi8->extn.ipa)
		return -ENOMEM;
	memset(dp_wifi8->extn.ipa, 0, sizeof(struct ath12k_dp_wifi8_ipa));
	dp_wifi8->extn.ipa->dp_wifi8 = dp_wifi8;

	return ret;
}
