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
#include "../../../dp_cmn.h"

void ath12k_wifi8_dp_ipa_set_metadata(struct ipa_wdi_reg_intf_in_params *in)
{
	in->is_meta_data_valid = 0;
}

static struct ath12k_vif *get_ahvif_from_dev_name(const char *net_dev_name)
{
	struct wireless_dev *wdev;
	struct ieee80211_vif *vif;
	struct net_device *dev = dev_get_by_name(&init_net, net_dev_name);

	if (!dev)
		goto err;
	wdev = dev->ieee80211_ptr;
	if (!wdev)
		goto err;

	vif = wdev_to_ieee80211_vif_vlan(wdev, false);
	if (!vif)
		goto err;

	dev_put(dev);
	return ath12k_vif_to_ahvif(vif);
err:
	dev_put(dev);
	return NULL;
}

static int get_vp_num_from_dev_name(const char *net_dev_name, struct ath12k_vif *ahvif)
{
	int vp_num;
	struct net_device *dev = dev_get_by_name(&init_net, net_dev_name);

	vp_num = ath12k_dp_ipa_alloc_vp_num(ahvif, dev);
	dev_put(dev);
	return vp_num;
}

void ath12k_wifi8_dp_ipa_iface_setup_set_vp_num(u8 link_id,
						struct ipa_wdi_reg_intf_in_params *in)
{
	int vp_num;
	struct ath12k_vif *ahvif;
	struct ath12k_link_vif *arvif;
	struct ath12k_ipa_vp_profile vp_profile;
	struct net_device *dev;

	ahvif = get_ahvif_from_dev_name(in->netdev_name);
	if (!ahvif) {
		ath12k_warn(NULL, "ipa vp_num setup: ahvif not found for netdev=%s\n",
			    in->netdev_name);
		return;
	}

	vp_num = get_vp_num_from_dev_name(in->netdev_name, ahvif);
	if (ATH12K_DP_IPA_VP_NUM_INVALID(vp_num)) {
		ath12k_warn(NULL, "ipa vp_num setup: invalid vp_num=%d for netdev=%s\n",
			    vp_num, in->netdev_name);
		return;
	}

	rcu_read_lock();
	arvif = ath12k_get_arvif_from_link_id(ahvif, link_id);
	if (!arvif) {
		ath12k_warn(NULL, "ipa vp_num setup: arvif not found for link_id=%u netdev=%s\n",
			    link_id, in->netdev_name);
		goto err;
	}

	dev = dev_get_by_name(&init_net, in->netdev_name);
	if (!dev) {
		ath12k_warn(NULL, "ipa vp_num setup: net_device not found for netdev=%s\n",
			    in->netdev_name);
		goto err;
	}

	ath12k_dp_ipa_set_ppe_vp_entry_config(&vp_profile, dev, arvif);
	dev_put(dev);

	ath12k_dp_ipa_hal_tx_set_ppe_vp_entry(arvif->ar->ab, vp_num, &vp_profile);
err:
	rcu_read_unlock();
}

void ath12k_wifi8_dp_ipa_iface_cleanup_vp_num(const char *dev_name, u8 link_id)
{
	struct ath12k_vif *ahvif;
	struct ath12k_link_vif *arvif;
	int vp_num;

	ahvif = get_ahvif_from_dev_name(dev_name);
	if (!ahvif)
		return;

	vp_num = get_vp_num_from_dev_name(dev_name, ahvif);
	if (ATH12K_DP_IPA_VP_NUM_INVALID(vp_num))
		return;

	rcu_read_lock();
	arvif = ath12k_get_arvif_from_link_id(ahvif, link_id);
	ath12k_dp_ipa_dealloc_vp_num(ahvif);
	ath12k_dp_ipa_hal_tx_set_ppe_vp_entry(arvif->ar->ab, vp_num, NULL);
	rcu_read_unlock();

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
	if (ret)
		ath12k_ipa_err(ab, "failed to set DMA_BIT_MASK(32): %d", ret);

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

/**
 * ath12k_wifi8_ipa_iface_context_init - Allocate iface_context and all
 * session-indexed arrays for the WiFi8 cumac ipa_ctx.
 *
 * WiFi8 uses a single cumac ipa_ctx for the entire group (3 SoCs):
 *   max_iface = 48 (ATH12K_IPA_MAX_IFACE_WIFI8 = 3 × 16)
 *   max_session_id =  256 (ATH12K_MAX_AHVIF_ID + 1)
 *   invalid_session_id = 0 (ATH12K_INVALID_AHVIF_ID)
 *   wdi_version = IPA_WDI_6
 *
 * Return: 0 on success, negative errno on failure.
 */
static int ath12k_wifi8_ipa_iface_context_init(struct ath12k_ipa *ipa_ctx)
{
	struct ath12k_ipa_iface_context *iface_context;
	int i;

	ipa_ctx->max_iface = ATH12K_IPA_MAX_IFACE_WIFI8;
	ipa_ctx->max_session_id = ATH12K_MAX_AHVIF_ID + 1;
	ipa_ctx->invalid_session_id = ATH12K_INVALID_AHVIF_ID;
	ipa_ctx->wdi_version = IPA_WDI_6;

	/* Allocate iface_context array */
	ipa_ctx->iface_context = vzalloc(array_size(ipa_ctx->max_iface,
					 sizeof(*ipa_ctx->iface_context)));
	if (!ipa_ctx->iface_context)
		return -ENOMEM;

	/* Allocate session-indexed arrays */
	ipa_ctx->vdev_to_iface = vzalloc(array_size(ipa_ctx->max_session_id,
					 sizeof(*ipa_ctx->vdev_to_iface)));
	if (!ipa_ctx->vdev_to_iface)
		goto err_free_iface;
	/* init to max_iface ("no iface" sentinel); vzalloc gives 0 which
	 * collides with valid iface_context index 0 for WiFi8
	 */
	memset(ipa_ctx->vdev_to_iface, ipa_ctx->max_iface,
	       array_size(ipa_ctx->max_session_id, sizeof(*ipa_ctx->vdev_to_iface)));

	/* Initialize iface_context slots */
	for (i = 0; i < ipa_ctx->max_iface; i++) {
		iface_context = &ipa_ctx->iface_context[i];
		iface_context->ipa_ctx = ipa_ctx;
		iface_context->iface_id = i;
		iface_context->dev = NULL;
		iface_context->device_mode = ATH12K_IPA_MAX_NO_OF_MODE;
		iface_context->session_id = ipa_ctx->invalid_session_id;
		atomic_set(&iface_context->conn_count, 0);
		atomic_set(&iface_context->disconn_count, 0);
		spin_lock_init(&iface_context->interface_lock);
	}

	ath12k_ipa_info(ipa_ctx->ab,
			"WiFi8: iface_context[%u] allocated (cumac), wdi_version=%u",
			ipa_ctx->max_iface, ipa_ctx->wdi_version);
	return 0;

err_free_iface:
	vfree(ipa_ctx->iface_context);
	ipa_ctx->iface_context = NULL;
	return -ENOMEM;
}

/**
 * ath12k_wifi8_ipa_iface_context_deinit - Free all WiFi8 IPA arrays.
 */
static void ath12k_wifi8_ipa_iface_context_deinit(struct ath12k_ipa *ipa_ctx)
{
	vfree(ipa_ctx->vdev_to_iface);
	ipa_ctx->vdev_to_iface = NULL;
	vfree(ipa_ctx->iface_context);
	ipa_ctx->iface_context = NULL;
	ipa_ctx->max_iface = 0;
	ipa_ctx->max_session_id = 0;
	ipa_ctx->invalid_session_id = ATH12K_INVALID_AHVIF_ID;
}

/**
 * ath12k_wifi8_ipa_get_session_id - Return the group-wide ahvif_id as session ID.
 *
 * WiFi8 uses a single cumac ipa_ctx for all SoCs in the group. Each vif
 * gets a globally unique ahvif_id (1..63) allocated from a 64-bit bitmap.
 * The firmware sets global_vdev_id = ahvif_id, so the skb CB vdev_id field
 * already contains ahvif_id in the RX exception path — no changes needed there.
 */
static u16 ath12k_wifi8_ipa_get_session_id(struct ath12k_link_vif *arvif)
{
	return arvif->ahvif->dp_vif.ahvif_id;
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

/**
 * ath12k_wifi8_dp_ipa_w2i() - IPA exception flow handler (WLAN to IPA)
 * @priv: private context (ath12k_wlan_ipa_priv)
 * @evt: IPA event type
 * @data: event data (typically sk_buff pointer)
 *
 * Valid data packets are submitted to mac80211
 * for processing.
 *
 * Context: Can be called from process context
 * Return: None
 */
static void ath12k_wifi8_dp_ipa_w2i(void *priv,
				    enum ipa_dp_evt_type evt,
				    unsigned long data)
{
	struct sk_buff *skb = (struct sk_buff *)data;
	struct ath12k_ipa *ipa_ctx;
	struct ethhdr *eth;
	struct ath12k_base *cumac_ab;
	struct ath12k_dp *cumac_dp;
	struct ath12k_pdev_dp *cumac_dp_pdev, *dp_pdev;
	struct ath12k *cumac_ar, *ar;
	struct ieee80211_hw *hw;
	struct ath12k_dp_peer *dp_peer;
	struct ath12k_dp_link_peer *dp_link_peer;
	struct ieee80211_rx_status *status;
	struct ath12k_link_vif *cumac_arvif;
	int i;
	u16 peer_id;
	u8 vdev_id, hw_link_id;

	if (!skb) {
		ath12k_ipa_err(NULL, "skb is NULL");
		return;
	}

	if (evt != IPA_RECEIVE) {
		ath12k_ipa_err(NULL, "ipa gave wrong event: 0x%X", evt);
		goto drop;
	}

	ipa_ctx = (struct ath12k_ipa *)priv;
	if (!ipa_ctx) {
		ath12k_ipa_err(NULL, "ipa_ctx is NULL, dropping pkt");
		if (evt == IPA_RECEIVE)
			dev_kfree_skb_any(skb);
		return;
	}
	ipa_ctx->stats.rx_excep++;

	cumac_ab = ipa_ctx->ab;
	cumac_dp = ath12k_ab_to_dp(cumac_ab);

	eth = skb_eth_hdr(skb);
	peer_id = ath12k_ipa_cb_get_ta_peer_id(skb->cb);
	vdev_id = ath12k_ipa_cb_get_vap_id(skb->cb);

	rcu_read_lock();

	cumac_arvif = ath12k_mac_get_arvif_by_vdev_id(cumac_ab, vdev_id);
	if (!cumac_arvif) {
		ath12k_ipa_err(cumac_ab, "cumac_arvif is NULL");
		rcu_read_unlock();
		goto drop;
	}

	cumac_ar = cumac_arvif->ar;
	if (!cumac_ar) {
		ath12k_ipa_err(cumac_ab, "cumac_ar is NULL");
		rcu_read_unlock();
		goto drop;
	}

	cumac_dp_pdev = ath12k_dp_to_dp_pdev(cumac_dp, cumac_ar->pdev_idx);
	dp_peer = ath12k_dp_peer_find_by_peerid_index(cumac_dp, cumac_dp_pdev, peer_id);
	if (!dp_peer) {
		ath12k_ipa_err(cumac_ab, "dp_peer is null (peer_id=%d mac=%pM)", peer_id,
			   eth->h_source);
		rcu_read_unlock();
		goto drop;
	}

	for (i = 0; i < ATH12K_DP_PEER_MAX_MLO_LINKS; i++) {
		dp_link_peer = rcu_dereference(dp_peer->link_peers[i]);
		if (dp_link_peer) {
			hw_link_id = (u8)i;
			break;
		}
	}

	if (!dp_peer->is_authorized) {
		ath12k_ipa_err(cumac_ab, "peer is unauthorized. peer_id=%d src=%pM dst=%pM proto=0x%X dp_peer->addr=%pM",
			   peer_id, eth->h_source, eth->h_dest,
			   cpu_to_be16(eth->h_proto), dp_peer->addr);
		rcu_read_unlock();
		goto drop;
	}

	dp_pdev = ath12k_dp_hw_grp_to_dp_pdev(cumac_dp->dp_hw_grp, hw_link_id);
	if (!dp_pdev) {
		ath12k_ipa_err(cumac_ab, "IPA_W2I: dp_pdev NULL for hw_link_id=%u", hw_link_id);
		rcu_read_unlock();
		goto drop;
	}

	hw = dp_pdev->hw;
	ar = dp_pdev->ar;
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

static struct ath12k_ipa_arch_ops ipa_wifi8_arch_ops = {
	.dp_srng_cleanup = ath12k_wifi8_dp_ipa_srng_cleanup,
	.dp_srng_setup = ath12k_wifi8_dp_ipa_srng_setup,
	.dp_srng_get_rsrc = ath12k_wifi8_dp_ipa_srng_get_rsrc,
	.dp_rx_default_routing = NULL,
	.dp_rxdma_buf_setup = NULL,
	.dp_get_tx_bank_id = NULL,
	.dp_rx_buf_smmu_map_unmap = ath12k_wifi8_dp_ipa_rx_buf_smmu_map_unmap,
	.dp_srng_rx_init = ath12k_wifi8_dp_ipa_ipa2wbm_idle_buf_ring_init,
	.ipa_set_rx_doorbell_paddr = ath12k_wifi8_dp_ipa_set_rx_doorbell_paddr,
	.ipa_set_tx_doorbell_paddr = ath12k_wifi8_dp_ipa_set_tx_doorbell_paddr,
	.ipa_tx_comp_ring_init_hp = ath12k_wifi8_dp_ipa_tx_comp_ring_init_hp,
	.ipa_tx_buffer_attach = NULL,
	.dp_rx_pipe_set_rdi = ath12k_wifi8_dp_ipa_rx_pipe_set_rdi,
	.ipa_get_wdi_version = ath12k_wifi8_dp_ipa_get_wdi_version,
	.ipa_iface_setup_set_metadata = ath12k_wifi8_dp_ipa_set_metadata,
	.ipa_iface_setup_set_vp_num = ath12k_wifi8_dp_ipa_iface_setup_set_vp_num,
	.ipa_iface_cleanup_vp_num = ath12k_wifi8_dp_ipa_iface_cleanup_vp_num,
	.ipa_iface_setup_set_intf_idx = ath12k_wifi8_dp_ipa_iface_setup_set_intf_idx,
	.ipa_client_connect_ex = NULL,
	.ipa_client_disconnect = NULL,
	.ipa_iface_context_init = ath12k_wifi8_ipa_iface_context_init,
	.ipa_iface_context_deinit = ath12k_wifi8_ipa_iface_context_deinit,
	.ipa_get_session_id = ath12k_wifi8_ipa_get_session_id,
	.ipa_initialize_default = NULL,
	.ipa_uc_offload_enable = NULL,
	.dp_ipa_w2i = ath12k_wifi8_dp_ipa_w2i,
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

	ret = ath12k_dp_ipa_register_is_ipa_ready(ab);
	if (ret) {
		ath12k_err(ab, "IPA: ipa init failed");
		ath12k_dp_ipa_plugin_deregister_ops_extn(ab);
	}

	return ret;
}
