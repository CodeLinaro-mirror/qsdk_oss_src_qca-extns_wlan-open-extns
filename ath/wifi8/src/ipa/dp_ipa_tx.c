/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_ipa.h"
#include "../../../qcn_extns/ipa/dp_ipa.h"
#include "../../hal.h"
#include <linux/iommu.h>
#include <linux/netdevice.h>
#include "../../../qcn_extns/ini.h"
#include <linux/seq_file.h>

#define ATH12K_TQM2SW6_POLL_INTERVAL_MS    10

/*
 * Global VP-to-netdev mapping table indexed by ahvif_id.
 *
 * VP is derived as: vp = ahvif_id + ATH12K_DP_IPA_VP_MIN
 * Reverse:          ahvif_id = vp - ATH12K_DP_IPA_VP_MIN
 *
 * Both regular vdevs (AP, STA, …) and wds-ext AP_VLAN vdevs carry a unique
 * ahvif_id from ath12k_mac_ahvif_id_allocate().  Regular vdevs are inserted
 * at add_interface time; wds-ext vdevs are inserted when the AP_VLAN is first
 * marked wds-4addr in the sta_set_4addr callback.
 */
static struct net_device __rcu *ath12k_ipa_vp_to_dev[ATH12K_DP_IPA_VP_RANGE];
static DEFINE_SPINLOCK(ath12k_ipa_vp_lock);

/**
 * ath12k_dp_ipa_alloc_vp_num - register a netdev in the IPA VP pool
 * @ahvif: vif handle; dp_vif.ahvif_id must already be valid
 * @dev:   netdev to associate with the VP
 *
 * Return: VP number on success, negative errno otherwise.
 */
int ath12k_dp_ipa_alloc_vp_num(struct ath12k_vif *ahvif, struct net_device *dev)
{
	u8 ahvif_id = ahvif->dp_vif.ahvif_id;
	int idx, vp_num;

	if (ahvif_id == ATH12K_INVALID_AHVIF_ID)
		return -EINVAL;

	idx = ahvif_id;
	if (idx >= ATH12K_DP_IPA_VP_RANGE)
		return -ENOSPC;

	vp_num = ATH12K_DP_IPA_GET_VP_FROM_AHVIFID(idx);

	spin_lock(&ath12k_ipa_vp_lock);

	if (rcu_access_pointer(ath12k_ipa_vp_to_dev[idx])) {
		spin_unlock(&ath12k_ipa_vp_lock);
		return -EEXIST;
	}

	rcu_assign_pointer(ath12k_ipa_vp_to_dev[idx], dev);
	dev_hold(dev);

	spin_unlock(&ath12k_ipa_vp_lock);

	return vp_num;
}

/**
 * ath12k_dp_ipa_dealloc_vp_num - remove a netdev from the IPA VP pool
 * @ahvif: vif handle whose VP mapping should be removed
 *
 * Removes the VP-to-netdev association for the given vif and drops the
 * device reference after an RCU grace period.
 */
void ath12k_dp_ipa_dealloc_vp_num(struct ath12k_vif *ahvif)
{
	u8 ahvif_id = ahvif->dp_vif.ahvif_id;
	struct net_device *dev;
	int idx;

	if (ahvif_id == ATH12K_INVALID_AHVIF_ID)
		return;

	idx = ahvif_id;
	if (idx >= ATH12K_DP_IPA_VP_RANGE)
		return;

	spin_lock(&ath12k_ipa_vp_lock);
	dev = rcu_dereference_protected(ath12k_ipa_vp_to_dev[idx],
					lockdep_is_held(&ath12k_ipa_vp_lock));
	RCU_INIT_POINTER(ath12k_ipa_vp_to_dev[idx], NULL);
	spin_unlock(&ath12k_ipa_vp_lock);

	if (!dev)
		return;

	synchronize_rcu();
	dev_put(dev);
}

/**
 * ath12k_dp_ipa_get_vp_from_dev - retrieve VP number for a vif netdev
 * @dev: a mac80211-registered netdev (AP, STA, AP_VLAN / wds-ext)
 *
 * Derives the VP from dp_vif.ahvif_id via the ieee80211_ptr without
 * touching any private netdev storage.
 *
 * Return: VP number on success, negative errno otherwise.
 */
int ath12k_dp_ipa_get_vp_from_dev(struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct ieee80211_vif *vif;
	struct ath12k_vif *ahvif;
	u8 ahvif_id;
	int vp_num;

	if (!wdev)
		return -EINVAL;

	vif = wdev_to_ieee80211_vif_vlan(wdev, false);
	if (!vif)
		return -EINVAL;

	ahvif = ath12k_vif_to_ahvif(vif);
	ahvif_id = ahvif->dp_vif.ahvif_id;

	if (ahvif_id == ATH12K_INVALID_AHVIF_ID)
		return -EINVAL;

	vp_num = ATH12K_DP_IPA_GET_VP_FROM_AHVIFID(ahvif_id);

	if(ATH12K_DP_IPA_VP_NUM_INVALID(vp_num))
		return -EINVAL;
	return vp_num;
}

/**
 * ath12k_dp_ipa_get_netdev_from_vp - look up netdev by VP number
 * @vp_num: VP number in [ATH12K_DP_IPA_VP_MIN, ATH12K_DP_IPA_VP_MAX]
 *
 * Return: netdev with an elevated reference count, or NULL if not registered.
 *         Caller must dev_put() the returned netdev when done.
 *         Caller must use rcu lock and unlock while using netdev
 */
struct net_device *ath12k_dp_ipa_get_netdev_from_vp(u8 vp_num)
{
	struct net_device *dev;
	int idx;

	if (ATH12K_DP_IPA_VP_NUM_INVALID(vp_num))
		return NULL;

	idx = ATH12K_DP_IPA_GET_VP_IDX_FROM_VP(vp_num);

	rcu_read_lock();
	dev = rcu_dereference(ath12k_ipa_vp_to_dev[idx]);
	if (dev)
		dev_hold(dev);
	rcu_read_unlock();

	return dev;
}

/**
 * ath12k_ipa_get_bank_id - get TCL bank_id for VP config from netdev
 * @dev: netdev of the interface (AP_VLAN for WDS, AP/STA for normal)
 *
 * For WDS (AP_VLAN): returns the AP_VLAN's own bank_id which has
 * HAL_TX_BANK_CONFIG_INDEX_LOOKUP_EN=1 (allocated during add_interface).
 * For normal AP/STA: returns the interface's standard bank_id.
 *
 * Return: bank_id for the interface, or DP_INVALID_BANK_ID on error.
 */
static u32 ath12k_ipa_get_bank_id(struct net_device *dev)
{
	struct wireless_dev *wdev;
	struct ieee80211_vif *vif;
	struct ath12k_vif *ahvif;

	if (!dev)
		return DP_INVALID_BANK_ID;
	wdev = dev->ieee80211_ptr;
	if (!wdev)
		return DP_INVALID_BANK_ID;
	vif = wdev_to_ieee80211_vif_vlan(wdev, false);
	if (!vif)
		return DP_INVALID_BANK_ID;
	ahvif = ath12k_vif_to_ahvif(vif);
	return ahvif->dp_vif.bank_id;
}

/**
 * get_search_idx_reg_num - return PPE search index register number
 * @dev: netdev for which search index register number is required
 *
 * Placeholder for platform-specific logic; currently returns 0.
 *
 * Return: register number to program into HAL_TX_PPE_VP_CFG_SRCH_IDX_REG_NUM
 */
static u8 get_search_idx_reg_num(struct net_device *dev)
{
	return 0;
}

/**
 * ath12k_dp_ipa_set_ppe_vp_entry_config - fill PPE VP profile for a vdev
 * @vp_profile: profile structure to populate
 * @dev: netdev associated with the vdev (AP_VLAN for WDS, AP/STA otherwise)
 * @arvif: link-vif providing vdev_id and lmac context
 *
 * Initializes vp_profile fields used by hardware tables before programming
 * PPE VP configuration for the given vdev context.
 */
void ath12k_dp_ipa_set_ppe_vp_entry_config(struct ath12k_ipa_vp_profile *vp_profile,
					   struct net_device *dev,
					   struct ath12k_link_vif *arvif)
{
	vp_profile->drop_prec_enable = 0;
	vp_profile->to_fw_sw = 0;
	vp_profile->use_ppe_int_pri = 0;
	vp_profile->vdev_id = arvif->vdev_id;
	vp_profile->search_idx_reg_num = get_search_idx_reg_num(dev);
	vp_profile->bank_id = ath12k_ipa_get_bank_id(dev);
	vp_profile->lmac_id = arvif->ar->lmac_id;
	vp_profile->entry_valid = true;
}

#define HAL_TX_PPE_VP_CONFIG_TABLE_ADDR  0x00F1336C
#define HAL_TX_PPE_VP_CONFIG_TABLE_OFFSET 4

/**
 * ath12k_dp_ipa_hal_tx_set_ppe_vp_entry - program PPE VP config table entry
 * @ab: device base pointer
 * @vp_num: VP number in [ATH12K_DP_IPA_VP_MIN, ATH12K_DP_IPA_VP_MAX]
 * @vp_profile: profile to encode; if NULL, clears the entry
 *
 * Encodes vp_profile into the HAL TX PPE VP configuration format and writes it
 * into the central DP HW group's register space for the specified VP. Passing
 * a NULL vp_profile clears the table entry.
 */
void ath12k_dp_ipa_hal_tx_set_ppe_vp_entry(struct ath12k_base *ab,
					   u8 vp_num,
					   struct ath12k_ipa_vp_profile *vp_profile)
{
	u32 ppe_vp_config = 0;
	u32 vp_ind;
	struct ath12k_base *central_ab =
		ath12k_dp_get_ab_from_dp_hw_group(ab->dp->dp_hw_grp);

	if (ATH12K_DP_IPA_VP_NUM_INVALID(vp_num) || !central_ab)
		return;

	vp_ind = ATH12K_DP_IPA_GET_VP_IDX_FROM_VP(vp_num);
	if (!vp_profile) {
		ppe_vp_config = 0;
		goto reg_write;
	}

	if (vp_profile->lmac_id == HAL_WILDCARD_LMAC_ID)
		vp_profile->lmac_id = HAL_TX_PPE_VP_CFG_WILDCARD_LMAC_ID;

	ppe_vp_config |=
		u32_encode_bits(vp_profile->entry_valid,
				HAL_TX_PPE_VP_CFG_ENTRY_VALID) |
		u32_encode_bits(vp_profile->search_idx_reg_num,
				HAL_TX_PPE_VP_CFG_SRCH_IDX_REG_NUM) |
		u32_encode_bits(vp_profile->use_ppe_int_pri,
				HAL_TX_PPE_VP_CFG_USE_PPE_INT_PRI) |
		u32_encode_bits(vp_profile->to_fw_sw,
				HAL_TX_PPE_VP_CFG_TO_FW) |
		u32_encode_bits(vp_profile->drop_prec_enable,
				HAL_TX_PPE_VP_CFG_DROP_PREC_EN) |
		u32_encode_bits(vp_profile->bank_id, HAL_TX_PPE_VP_CFG_BANK_ID) |
		u32_encode_bits(vp_profile->lmac_id, HAL_TX_PPE_VP_CFG_LMAC_ID) |
		u32_encode_bits(vp_profile->vdev_id, HAL_TX_PPE_VP_CFG_VDEV_ID);

reg_write:
	ath12k_hif_write32(central_ab, HAL_TX_PPE_VP_CONFIG_TABLE_ADDR +
			   (HAL_TX_PPE_VP_CONFIG_TABLE_OFFSET * vp_ind),
			   ppe_vp_config);
}

/**
 * ath12k_wifi8_dp_ipa_tx_srng_cleanup() - Free IPA TX SRNG resources
 * @ab: Pointer to ath12k base structure
 *
 * Cleans up TX-related IPA <-> WLAN ring (SRNG) resources:
 * IPA2TCL data ring and TQM2IPA completion ring.
 */
static void ath12k_wifi8_dp_ipa_tx_srng_cleanup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8;
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa;

	if (!dp)
		return;

	dp_wifi8 = ath12k_get_dp_wifi8(dp);
	if (!dp_wifi8)
		return;

	dp_wifi8_ipa = ath12k_wifi8_get_ipa_dp(dp_wifi8);
	if (!dp_wifi8_ipa)
		return;

	ath12k_dp_srng_cleanup(ab, &dp_wifi8_ipa->tqm2ipa_ring);
	ath12k_dp_srng_cleanup(ab, &dp_wifi8_ipa->ipa2tcl_ring);
	ath12k_dp_srng_cleanup(ab, &dp_wifi8_ipa->tqm2sw6_ring);
}

/**
 * ath12k_wifi8_dp_ipa_tx_srng_setup() - Setup IPA2TCL, TQM2IPA rings
 * @ab: Pointer to ath12k base structure
 *
 * Initializes TX-related IPA <-> WLAN rings.
 * ipa2tcl_ring: TX data ring to which IPA pushes packets
 * tqm2ipa_ring: TX completion ring from which IPA consumes free buffers
 *
 * Return: 0 on success, negative error code on failure. On failure,
 *         any previously allocated  rings are cleaned up automatically.
 */
static int ath12k_wifi8_dp_ipa_tx_srng_setup(struct ath12k_base *ab)
{
	int ret;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8;
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa;
	u32 ipa2tcl_ring_sz;
	u32 tqm2ipa_ring_sz;

	if (!dp)
		return -EINVAL;

	dp_wifi8 = ath12k_get_dp_wifi8(dp);
	if (!dp_wifi8)
		return -EINVAL;

	dp_wifi8_ipa = ath12k_wifi8_get_ipa_dp(dp_wifi8);
	if (!dp_wifi8_ipa)
		return -EINVAL;

	ipa2tcl_ring_sz = ath12k_cfg_get(ab, ATH12K_INI_DP_IPA2TCL_RING_SIZE);
	tqm2ipa_ring_sz = ath12k_cfg_get(ab, ATH12K_INI_DP_TQM2IPA_RING_SIZE);

	ret = ath12k_dp_srng_setup(ab, &dp_wifi8_ipa->ipa2tcl_ring, HAL_PPE2TCL, 0, 0,
				   ipa2tcl_ring_sz);
	if (ret) {
		ath12k_ipa_err(ab, "failed to set up ipa2tcl tx data ring :%d", ret);
		goto err;
	}

	ath12k_ipa_info(ab, "Setup ipa2tcl ring_id=%d paddr=%pad",
			dp_wifi8_ipa->ipa2tcl_ring.ring_id,
			&dp_wifi8_ipa->ipa2tcl_ring.paddr);

	ath12k_wifi8_hal_tx_config_rbm_mapping(ab, 0 /* IPA2TCL ring 0 */,
					       HAL_RX_BUF_RBM_SW6_BM,
					       HAL_PPE2TCL);

	ret = ath12k_dp_srng_setup(ab, &dp_wifi8_ipa->tqm2ipa_ring, HAL_TQM2PPE, 0, 0,
				   tqm2ipa_ring_sz);
	if (ret) {
		ath12k_ipa_err(ab, "failed to set up tqm2ipa tx completion ring :%d", ret);
		goto err;
	}

	ath12k_ipa_info(ab, "setup tqm2ipa ring_id=%d paddr=%pad",
			dp_wifi8_ipa->tqm2ipa_ring.ring_id,
			&dp_wifi8_ipa->tqm2ipa_ring.paddr);

	ret = ath12k_dp_srng_setup(ab, &dp_wifi8_ipa->tqm2sw6_ring, HAL_TX_COMPLETION, 6, 0,
				   tqm2ipa_ring_sz);

	if (ret) {
		ath12k_ipa_err(ab, "tqm2sw6 ring alloc/init failed: %d", ret);
		return 0;
	}

	return 0;

err:
	ath12k_wifi8_dp_ipa_tx_srng_cleanup(ab);
	return ret;
}

/**
 * ath12k_wifi8_dp_ipa_tx_buf_alloc() - Allocate and post IPA TX buffers
 * @ab: Pointer to ath12k base structure
 *
 * Allocates TX buffers for the IPA uC TX datapath and populates the
 * TQM2IPA ring with free buffers.
 *
 * This API allocates SKBs, aligns them to the required IPA TX buffer
 * boundary, DMA maps the buffers, and posts the mapped addresses into
 * the TQM2IPA ring. Successfully allocated buffers are tracked in the
 * IPA TX resource context for later cleanup.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int ath12k_wifi8_dp_ipa_tx_buf_alloc(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8;
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa;
	struct ath12k_ipa *ipa_ctx = IPA_CTX(ab);
	struct ath12k_ipa_dp_tx_rsc *tx_rsc;
	struct hal_srng *tqm_srng;
	struct sk_buff *skb = NULL, *tmp_skb;
	struct hal_srng_params srng_params;
	struct ath12k_buffer_addr *ring_entry;
	dma_addr_t buffer_paddr;
	dma_addr_t first_paddr;
	u32 tx_buffer_count, i;
	u32 ring_base_align = 8;
	int num_entries;
	int retval = 0;
	int max_alloc_count = 0;
	u8 ring_id;
	u8 rbm_id;
	unsigned int alloc_size = ATH12K_IPA_UC_TX_BUF_SIZE_DEFAULT + ring_base_align - 1;
	unsigned int align_off;
	struct sk_buff *first_skb;

	if (!dp)
		return -EINVAL;

	dp_wifi8 = ath12k_get_dp_wifi8(dp);
	if (!dp_wifi8)
		return -EINVAL;

	dp_wifi8_ipa = ath12k_wifi8_get_ipa_dp(dp_wifi8);
	if (!dp_wifi8_ipa)
		return -EINVAL;

	if (!ipa_ctx)
		return -EINVAL;

	tx_rsc = &ipa_ctx->ipa_uc_tx_rsc;

	ring_id = dp_wifi8_ipa->tqm2ipa_ring.ring_id;
	tqm_srng = &ab->hal.srng_list[ring_id];

	memset(&srng_params, 0, sizeof(srng_params));
	ath12k_hal_srng_get_params(ab, tqm_srng, &srng_params);

	rbm_id = HAL_RX_BUF_RBM_SW6_BM;
	num_entries = srng_params.num_entries;
	max_alloc_count = num_entries - ATH12K_IPA_WAR_WBM2SW_REL_RING_NO_BUF_ENTRIES;

	if (max_alloc_count <= 0) {
		ath12k_ipa_err(ab, "incorrect value for buffer count %d", max_alloc_count);
		return -EINVAL;
	}

	tx_rsc->tx_buf_pool_vaddr_unaligned =
		kcalloc(num_entries,
			sizeof(*tx_rsc->tx_buf_pool_vaddr_unaligned),
			GFP_KERNEL);
	if (!tx_rsc->tx_buf_pool_vaddr_unaligned) {
		ath12k_ipa_err(ab, "tqm2ipa ring buffer pool vaddr alloc fail");
		return -ENOMEM;
	}

	ath12k_ipa_info(ab, "ipa: allocate tx buffer count:%d dev:%p dev_name:%s",
			max_alloc_count, dp->dev, dev_name(dp->dev));

	/*
	 * Allocate Tx buffers as many as possible.
	 * Leave ATH12K_IPA_WAR_WBM2SW_REL_RING_NO_BUF_ENTRIES empty
	 * Populate Tx buffers into TQM2IPA ring
	 * This initial buffer population will simulate H/W as source ring,
	 * and update HP
	 */
	spin_lock_bh(&tqm_srng->lock);
	ath12k_hal_srng_access_begin(ab, tqm_srng);

	for (tx_buffer_count = 0;
		tx_buffer_count < max_alloc_count - 1; tx_buffer_count++) {
		skb = dev_alloc_skb(alloc_size + ATH12K_IPA_TX_BUFFER_ALIGN_SIZE);
		if (!skb)
			break;

		if (!IS_ALIGNED((unsigned long)skb->data,
		    ATH12K_IPA_TX_BUFFER_ALIGN_SIZE)) {
			align_off =
				PTR_ALIGN(skb->data, ATH12K_IPA_TX_BUFFER_ALIGN_SIZE) -
				skb->data;
			skb_reserve(skb, align_off);
		}

		buffer_paddr = dma_map_single(dp->dev, skb->data,
					      skb->len + skb_tailroom(skb),
					      DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dp->dev, buffer_paddr)) {
			retval = -ENOMEM;
			goto fail_free_skb;
		}

		ATH12K_SKB_CB(skb)->paddr = buffer_paddr;

		ring_entry = ath12k_hal_srng_dst_get_next_hp_entry(ab, tqm_srng);
		if (!ring_entry) {
			ath12k_ipa_err(ab,
				       "failed to get tqm2ipa completion ring next entry");
			retval = -ENOSPC;
			goto fail_dma_unmap;
		}

		ath12k_hal_rx_buf_addr_info_set(ring_entry, buffer_paddr, 0, rbm_id);
		tx_rsc->tx_buf_pool_vaddr_unaligned[tx_buffer_count] = skb;
		skb = NULL;
	}

	ath12k_hal_srng_access_end(ab, tqm_srng);
	spin_unlock_bh(&tqm_srng->lock);

	tx_rsc->alloc_tx_buf_cnt = tx_buffer_count;

	if (tx_buffer_count) {
		ath12k_ipa_info(ab, "ipa wdi tx buffers: %d allocated", tx_buffer_count);

		first_skb = tx_rsc->tx_buf_pool_vaddr_unaligned[0];
		first_paddr = first_skb ? ATH12K_SKB_CB(first_skb)->paddr : 0;

		ath12k_ipa_info(ab,
				"ath12k IPA TX buf alloc: dev=%s mapped=%u/%u "
				"first_paddr=0x%llx rbm=%u ring_id=%u",
				dev_name(dp->dev),
				tx_buffer_count, max_alloc_count - 1,
				(u64)first_paddr, rbm_id, ring_id);
		return 0;
	}

	ath12k_ipa_err(ab, "no ipa wdi tX buffers allocated!");
	kfree(tx_rsc->tx_buf_pool_vaddr_unaligned);
	tx_rsc->tx_buf_pool_vaddr_unaligned = NULL;
	tx_rsc->alloc_tx_buf_cnt = 0;
	return -ENOMEM;

fail_dma_unmap:
	dma_unmap_single(dp->dev, buffer_paddr, skb->len + skb_tailroom(skb),
			 DMA_BIDIRECTIONAL);
fail_free_skb:
	ath12k_hal_srng_access_end(ab, tqm_srng);
	spin_unlock_bh(&tqm_srng->lock);

	if (skb)
		dev_kfree_skb_any(skb);

	for (i = 0; i < tx_buffer_count; i++) {
		tmp_skb = tx_rsc->tx_buf_pool_vaddr_unaligned[i];
		if (!tmp_skb)
			continue;

		dma_unmap_single(dp->dev, ATH12K_SKB_CB(tmp_skb)->paddr,
				 tmp_skb->len + skb_tailroom(tmp_skb), DMA_BIDIRECTIONAL);
		dev_kfree_skb_any(tmp_skb);
		tx_rsc->tx_buf_pool_vaddr_unaligned[i] = NULL;
	}

	kfree(tx_rsc->tx_buf_pool_vaddr_unaligned);
	tx_rsc->tx_buf_pool_vaddr_unaligned = NULL;
	tx_rsc->alloc_tx_buf_cnt = 0;

	return retval;
}

/**
 * ath12k_wifi8_dp_ipa_tx_buf_free() - Free IPA TX buffers
 * @ab: Pointer to ath12k base structure
 *
 * Frees all TX buffers previously allocated for the IPA uC TX datapath.
 *
 * Iterates through the IPA TX buffer pool, DMA unmaps each valid buffer,
 * frees the associated SKB, clears the software tracking entries, and
 * releases the buffer pool memory.
 *
 * Return: None.
 */
static void ath12k_wifi8_dp_ipa_tx_buf_free(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_ipa *ipa_ctx = IPA_CTX(ab);
	struct ath12k_ipa_dp_tx_rsc *tx_rsc;
	struct sk_buff *skb;
	int idx;

	if (!dp || !ipa_ctx)
		return;

	tx_rsc = &ipa_ctx->ipa_uc_tx_rsc;

	if (!tx_rsc->tx_buf_pool_vaddr_unaligned)
		goto out;

	for (idx = 0; idx < tx_rsc->alloc_tx_buf_cnt; idx++) {
		skb = (struct sk_buff *)tx_rsc->tx_buf_pool_vaddr_unaligned[idx];
		if (!skb)
			continue;

		dma_unmap_single(dp->dev, ATH12K_SKB_CB(skb)->paddr,
				 skb->len + skb_tailroom(skb), DMA_BIDIRECTIONAL);
		dev_kfree_skb_any(skb);
		tx_rsc->tx_buf_pool_vaddr_unaligned[idx] = NULL;
	}

	kfree(tx_rsc->tx_buf_pool_vaddr_unaligned);
	tx_rsc->tx_buf_pool_vaddr_unaligned = NULL;

out:
	tx_rsc->alloc_tx_buf_cnt = 0;
}

/**
 * ath12k_wifi8_dp_fill_tx_src() - Fill IPA TX ring configuration
 * @ab: Pointer to ath12k base structure
 *
 * Populates the IPA TX resource structure with hardware ring parameters
 * for the IPA2TCL data ring and TQM2IPA completion ring.
 * This includes configuring:
 * - Ring base physical and virtual addresses
 * - Ring sizes and head/tail pointer addresses
 * - Scatter-gather tables for DMA operations
 * - IOMMU address translations when applicable
 *
 * The function extracts ring parameters from the HAL SRNG structures and
 * prepares them for IPA subsystem consumption. It handles both IOMMU-enabled
 * and non-IOMMU configurations by performing appropriate address translations.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ath12k_wifi8_dp_fill_tx_src(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8;
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa;
	struct ath12k_ipa *ipa_ctx = IPA_CTX(ab);
	struct hal_srng *srng;
	struct hal_srng_params params;
	struct ath12k_ipa_dp_tx_rsc *tx_rsc;
	struct device *dev = ab->dev;
	struct iommu_domain *domain = ath12k_dp_ipa_smmu_get_domain(ab);
	struct scatterlist *sg;
	unsigned long base_pa_addr, offset;
	int ring_id;
	int i, ret;

	if (!dp)
		return -EINVAL;

	dp_wifi8 = ath12k_get_dp_wifi8(dp);
	if (!dp_wifi8)
		return -EINVAL;

	dp_wifi8_ipa = ath12k_wifi8_get_ipa_dp(dp_wifi8);
	if (!dp_wifi8_ipa)
		return -EINVAL;

	if (!ipa_ctx)
		return -EINVAL;

	tx_rsc = &ipa_ctx->ipa_uc_tx_rsc;
	ring_id = dp_wifi8_ipa->ipa2tcl_ring.ring_id;
	srng = &ab->hal.srng_list[ring_id];

	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);

	tx_rsc->ipa_tcl_ring_base_paddr = params.ring_base_paddr;
	tx_rsc->ipa_tcl_ring_base_vaddr = params.ring_base_vaddr;
	tx_rsc->ipa_tcl_ring_size = (params.num_entries * srng->entry_size) << 2;

	/* hp_addr_direct is now set for HAL_PPE2TCL (same formula as HAL_TCL_DATA)
	 * in ath12k_wifi8_hal_set_umac_srng_ptr_addr().
	 */
	offset = (unsigned long)srng->u.src_ring.hp_addr_direct - (unsigned long)ab->mem;
	base_pa_addr = (unsigned long)ab->ath12k_base_extn.mem_pa;
	tx_rsc->ipa_tcl_hp_paddr = (dma_addr_t)(offset + base_pa_addr);

	ath12k_ipa_info(ab, "tcl_hp_addr:%llx, srng_hp_addr:%pad",
			tx_rsc->ipa_tcl_hp_paddr, &srng->u.src_ring.hp_addr);

	if (domain)
		tx_rsc->tcl_pa = iommu_iova_to_phys(domain, tx_rsc->ipa_tcl_ring_base_paddr);
	else
		tx_rsc->tcl_pa = tx_rsc->ipa_tcl_ring_base_paddr;

	ret = dma_get_sgtable(dev, &tx_rsc->sgtable_tcl, tx_rsc->ipa_tcl_ring_base_vaddr,
			      tx_rsc->tcl_pa, tx_rsc->ipa_tcl_ring_size);
	if (ret) {
		ath12k_ipa_err(ab, "unable to get tcl dma sgtable");
		return ret;
	}

	for_each_sg(tx_rsc->sgtable_tcl.sgl, sg, tx_rsc->sgtable_tcl.nents, i)
		sg_dma_address(sg) = sg_phys(sg);

	ring_id = dp_wifi8_ipa->tqm2ipa_ring.ring_id;
	srng = &ab->hal.srng_list[ring_id];

	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);

	tx_rsc->ipa_wbm_ring_base_paddr = params.ring_base_paddr;
	tx_rsc->ipa_wbm_ring_base_vaddr = params.ring_base_vaddr;
	tx_rsc->ipa_wbm_ring_size = (params.num_entries * srng->entry_size) << 2;

	/* tp_addr_direct is now set for HAL_TQM2PPE (same formula as HAL_TX_COMPLETION)
	 * in ath12k_wifi8_hal_set_umac_srng_ptr_addr().
	 */
	offset = (unsigned long)srng->u.dst_ring.tp_addr_direct - (unsigned long)ab->mem;
	base_pa_addr = (unsigned long)ab->ath12k_base_extn.mem_pa;
	tx_rsc->ipa_wbm_tp_paddr = (dma_addr_t)(offset + base_pa_addr);
	tx_rsc->ipa_wbm_hp_shadow_paddr = ath12k_hal_srng_get_hp_addr(ab, srng);

	ath12k_ipa_info(ab, "tqm_tp_addr:%llx, srng_tp_addr:%pad",
			tx_rsc->ipa_wbm_tp_paddr, &srng->u.dst_ring.tp_addr);

	if (domain)
		tx_rsc->wbm_pa = iommu_iova_to_phys(domain, tx_rsc->ipa_wbm_ring_base_paddr);
	else
		tx_rsc->wbm_pa = tx_rsc->ipa_wbm_ring_base_paddr;

	ret = dma_get_sgtable(dev, &tx_rsc->sgtable_wbm, tx_rsc->ipa_wbm_ring_base_vaddr,
			      tx_rsc->wbm_pa, tx_rsc->ipa_wbm_ring_size);
	if (ret) {
		ath12k_ipa_err(ab, "unable to get tqm dma sgtable");
		sg_free_table(&tx_rsc->sgtable_tcl);
		memset(&tx_rsc->sgtable_tcl, 0, sizeof(tx_rsc->sgtable_tcl));
		return ret;
	}

	for_each_sg(tx_rsc->sgtable_wbm.sgl, sg, tx_rsc->sgtable_wbm.nents, i)
		sg_dma_address(sg) = sg_phys(sg);

	ath12k_ipa_info(ab, "ath12k IPA TX rsrc: SMMU=%s\n"
			"  ipa2tcl : paddr=0x%llx size=%u hp_paddr=0x%llx tcl_pa=0x%llx\n"
			"  tqm2ipa : paddr=0x%llx size=%u tp_paddr=0x%llx hp_shadow=0x%llx wbm_pa=0x%llx",
			domain ? "enabled" : "disabled",
			(u64)tx_rsc->ipa_tcl_ring_base_paddr, tx_rsc->ipa_tcl_ring_size,
			(u64)tx_rsc->ipa_tcl_hp_paddr, (u64)tx_rsc->tcl_pa,
			(u64)tx_rsc->ipa_wbm_ring_base_paddr, tx_rsc->ipa_wbm_ring_size,
			(u64)tx_rsc->ipa_wbm_tp_paddr, (u64)tx_rsc->ipa_wbm_hp_shadow_paddr,
			(u64)tx_rsc->wbm_pa);

	return 0;
}

/**
 * ath12k_wifi8_dp_free_tx_rsrc() - Free IPA TX ring resource metadata
 * @ab: Pointer to ath12k base structure
 *
 * Releases SG tables allocated for IPA TX ring resources.
 */
static void ath12k_wifi8_dp_free_tx_rsrc(struct ath12k_base *ab)
{
	struct ath12k_ipa *ipa_ctx = IPA_CTX(ab);
	struct ath12k_ipa_dp_tx_rsc *tx_rsc;

	if (!ipa_ctx)
		return;

	tx_rsc = &ipa_ctx->ipa_uc_tx_rsc;

	if (tx_rsc->sgtable_tcl.sgl) {
		sg_free_table(&tx_rsc->sgtable_tcl);
		memset(&tx_rsc->sgtable_tcl, 0, sizeof(tx_rsc->sgtable_tcl));
	}

	if (tx_rsc->sgtable_wbm.sgl) {
		sg_free_table(&tx_rsc->sgtable_wbm);
		memset(&tx_rsc->sgtable_wbm, 0, sizeof(tx_rsc->sgtable_wbm));
	}
}

/**
 * ath12k_wifi8_dp_tx_ring_setup_ipa() - Setup TX rings for IPA offload
 * @ab: Pointer to ath12k base structure
 *
 * Sets up all TX ring resources required for IPA offload:
 * - IPA2TCL and TQM2IPA SRNGs
 * - TQM2IPA buffer pool
 * - IPA TX ring resource metadata
 *
 * Return: 0 on success, negative error code on failure
 */
int ath12k_wifi8_dp_tx_ring_setup_ipa(struct ath12k_base *ab)
{
	int ret;
	u64 orig_dma_mask;

	orig_dma_mask = dma_get_mask(ab->dev);
	ret = dma_set_mask_and_coherent(ab->dev, DMA_BIT_MASK(32));
	if (ret)
		ath12k_ipa_err(ab, "IPA Tx rings: failed to set DMA_BIT_MASK(32): %d", ret);

	ret = ath12k_wifi8_dp_ipa_tx_srng_setup(ab);

	dma_set_mask_and_coherent(ab->dev, orig_dma_mask);
	ath12k_ipa_info(ab, "ath12k IPA TX rings: DMA mask restored to 0x%llx",
			orig_dma_mask);

	if (ret) {
		ath12k_ipa_err(ab, "ppe rings: ipa2tcl, tqm2ipa setup failed: %d", ret);
		return ret;
	}

	ret = ath12k_wifi8_dp_ipa_tx_buf_alloc(ab);
	if (ret) {
		ath12k_ipa_err(ab, "tqm2ipa ring buffer allocation failed: %d", ret);
		goto err_srng_cleanup;
	}

	ret = ath12k_wifi8_dp_ipa_tqm2sw6_poller_start(ab);
	if (ret) {
		ath12k_ipa_err(ab, "tqm2sw6 poller start failed: %d", ret);
		goto err_buf_free;
	}

	return 0;

err_buf_free:
	ath12k_wifi8_dp_ipa_tx_buf_free(ab);
err_srng_cleanup:
	ath12k_wifi8_dp_ipa_tx_srng_cleanup(ab);
	return ret;
}

/**
 * ath12k_wifi8_dp_ipa_tqm2sw6_handler() - Dummy handler for tqm2sw6 ring
 * @dp: Pointer to ath12k datapath structure
 * @budget: Maximum number of ring entries to drain
 *
 * Experimental interrupt handler for the tqm2sw6 TX completion ring.
 * Uses the nolock srng access APIs (caller holds no lock; the begin/end
 * pair re-arms the ring interrupt internally).  Drains available entries
 * up to budget, counts them, and prints the total.
 *
 * Return: Number of entries processed.
 */
int ath12k_wifi8_dp_ipa_tqm2sw6_handler(struct ath12k_dp *dp, int budget)
{
	struct ath12k_base *ab = dp->ab;
	struct ath12k_dp_wifi8 *dp_wifi8 = ath12k_get_dp_wifi8(dp);
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa;
	struct hal_tqm2sw_completion_ring *tx_desc;
	struct hal_srng *srng;
	dma_addr_t paddr;
	u32 failed_refill = 0;
	u32 desc_id;
	u8 rbm;
	int valid_entries;
	int count = 0;

	if (!dp_wifi8)
		return 0;

	dp_wifi8_ipa = ath12k_wifi8_get_ipa_dp(dp_wifi8);
	if (!dp_wifi8_ipa)
		return 0;

	srng = &ab->hal.srng_list[dp_wifi8_ipa->tqm2sw6_ring.ring_id];

	ath12k_hal_srng_access_dst_ring_begin_nolock(ab, srng);

	valid_entries = __ath12k_hal_srng_dst_num_free(srng, false);
	if (!valid_entries) {
		ath12k_hal_srng_access_dst_ring_end_nolock(srng);
		dp_wifi8_ipa->tqm2sw6_stats.napi_empty_polls++;
		return 0;
	}

	if (valid_entries > budget)
		valid_entries = budget;

	while (valid_entries--) {
		tx_desc = __ath12k_hal_srng_dst_get_next_cached_entry(srng, NULL);
		if (!tx_desc)
			break;
		count++;
		paddr = ((u64)(le32_get_bits(tx_desc->buf_addr_info.info1,
					     BUFFER_ADDR_INFO1_ADDR)) << 32) |
			le32_get_bits(tx_desc->buf_addr_info.info0,
				      BUFFER_ADDR_INFO0_ADDR);
		desc_id = le32_get_bits(tx_desc->buf_addr_info.info1,
				       BUFFER_ADDR_INFO1_SW_COOKIE);
		rbm = le32_get_bits(tx_desc->buf_addr_info.info1,
				    BUFFER_ADDR_INFO1_RET_BUF_MGR);
		if (ath12k_wifi8_dp_tx_exception_to_tx_comp(dp, paddr, rbm, desc_id))
			failed_refill++;
	}

	ath12k_hal_srng_access_dst_ring_end_nolock(srng);

	dp_wifi8_ipa->tqm2sw6_stats.entries_processed += count;
	dp_wifi8_ipa->tqm2sw6_stats.last_poll_count = count;
	dp_wifi8_ipa->tqm2sw6_stats.failed_refill = failed_refill;
	dp_wifi8_ipa->tqm2sw6_stats.rbm = rbm;
	dp_wifi8_ipa->tqm2sw6_stats.cookie = desc_id;
	if (count > dp_wifi8_ipa->tqm2sw6_stats.max_entries_per_poll)
		dp_wifi8_ipa->tqm2sw6_stats.max_entries_per_poll = count;

	return count;
}

/**
 * ath12k_dp_ipa_tqm2sw6_poll() - NAPI poll function for tqm2sw6 ring
 * @napi:   NAPI instance embedded in tqm2sw6_napi_ctxt
 * @budget: Maximum entries the NAPI layer allows us to process
 *
 * Mirrors ath12k_dp_ppeds_tx_comp_poll(): calls the ring handler, and if
 * work done is below budget completes the NAPI cycle so the timer can
 * re-schedule it on the next interval.
 *
 * Return: Number of entries processed (capped at budget).
 */
static int ath12k_dp_ipa_tqm2sw6_poll(struct napi_struct *napi, int budget)
{
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa =
		container_of(napi, struct ath12k_dp_wifi8_ipa,
			     tqm2sw6_napi_ctxt.napi);
	struct ath12k_dp *dp = ath12k_get_dp(dp_wifi8_ipa->dp_wifi8);
	int work_done;

	dp_wifi8_ipa->tqm2sw6_stats.napi_polls++;

	work_done = ath12k_wifi8_dp_ipa_tqm2sw6_handler(dp, budget);

	if (budget > work_done) {
		napi_complete(napi);
		dp_wifi8_ipa->tqm2sw6_stats.napi_complete_cnt++;
	}

	return (work_done > budget) ? budget : work_done;
}

/**
 * ath12k_dp_ipa_tqm2sw6_poll_timer() - Periodic timer to drive tqm2sw6 NAPI
 * @t: Kernel timer list embedded in dp_wifi8_ipa
 *
 * Schedules the tqm2sw6 NAPI poll and re-arms the timer for the next
 * interval.  Replaces the IRQ handler used by the PPE DS path.
 */
static void ath12k_dp_ipa_tqm2sw6_poll_timer(struct timer_list *t)
{
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa =
		from_timer(dp_wifi8_ipa, t, tqm2sw6_poll_timer);

	dp_wifi8_ipa->tqm2sw6_stats.timer_fires++;
	napi_schedule(&dp_wifi8_ipa->tqm2sw6_napi_ctxt.napi);
	mod_timer(&dp_wifi8_ipa->tqm2sw6_poll_timer,
		  jiffies + msecs_to_jiffies(ATH12K_TQM2SW6_POLL_INTERVAL_MS));
}

/**
 * ath12k_wifi8_dp_ipa_tqm2sw6_poller_start() - Set up tqm2sw6 NAPI poller
 * @ab: Device base pointer
 *
 * Initialises a dummy netdev + NAPI context and starts the periodic
 * timer that drives ring polling.  Must be called after the tqm2sw6_ring
 * has been set up by ath12k_wifi8_dp_ipa_tx_srng_setup().
 *
 * Return: 0 on success, negative errno on failure.
 */
int ath12k_wifi8_dp_ipa_tqm2sw6_poller_start(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8 = ath12k_get_dp_wifi8(dp);
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa;
	struct net_device *ndev;

	if (!dp_wifi8)
		return -EINVAL;

	dp_wifi8_ipa = ath12k_wifi8_get_ipa_dp(dp_wifi8);
	if (!dp_wifi8_ipa)
		return -EINVAL;

	/* Replicate init_dummy_netdev() which is static in net/core/dev.c */
	ndev = &dp_wifi8_ipa->tqm2sw6_napi_ctxt.ndev;
	INIT_LIST_HEAD(&ndev->napi_list);
	ndev->reg_state = NETREG_DUMMY;
	set_bit(__LINK_STATE_PRESENT, &ndev->state);
	set_bit(__LINK_STATE_START, &ndev->state);

	netif_napi_add_weight(ndev,
			      &dp_wifi8_ipa->tqm2sw6_napi_ctxt.napi,
			      ath12k_dp_ipa_tqm2sw6_poll,
			      NAPI_POLL_WEIGHT);

	napi_enable(&dp_wifi8_ipa->tqm2sw6_napi_ctxt.napi);

	timer_setup(&dp_wifi8_ipa->tqm2sw6_poll_timer,
		    ath12k_dp_ipa_tqm2sw6_poll_timer, 0);
	mod_timer(&dp_wifi8_ipa->tqm2sw6_poll_timer,
		  jiffies + msecs_to_jiffies(ATH12K_TQM2SW6_POLL_INTERVAL_MS));

	dp_wifi8_ipa->tqm2sw6_stats.poller_start_cnt++;
	return 0;
}

/**
 * ath12k_wifi8_dp_ipa_tqm2sw6_poller_stop() - Tear down tqm2sw6 NAPI poller
 * @ab: Device base pointer
 *
 * Stops the poll timer, disables NAPI, and removes the NAPI context.
 * Must be called before the tqm2sw6_ring is cleaned up.
 */
void ath12k_wifi8_dp_ipa_tqm2sw6_poller_stop(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8 = ath12k_get_dp_wifi8(dp);
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa;

	if (!dp_wifi8)
		return;

	dp_wifi8_ipa = ath12k_wifi8_get_ipa_dp(dp_wifi8);
	if (!dp_wifi8_ipa)
		return;

	del_timer_sync(&dp_wifi8_ipa->tqm2sw6_poll_timer);
	napi_disable(&dp_wifi8_ipa->tqm2sw6_napi_ctxt.napi);
	netif_napi_del(&dp_wifi8_ipa->tqm2sw6_napi_ctxt.napi);

	dp_wifi8_ipa->tqm2sw6_stats.poller_stop_cnt++;
}

/**
 * ath12k_wifi8_dp_tx_ring_cleanup_ipa() - Free IPA TX ring resources
 * @ab: Pointer to ath12k base structure
 *
 * Tears down IPA TX ring resources. This is the
 * cleanup counterpart to ath12k_wifi8_dp_tx_ring_setup_ipa()
 *
 * Make sure pipe_disconnect is done before this.
 *
 */
void ath12k_wifi8_dp_tx_ring_cleanup_ipa(struct ath12k_base *ab)
{
	ath12k_wifi8_dp_ipa_tqm2sw6_poller_stop(ab);
	ath12k_wifi8_dp_free_tx_rsrc(ab);
	ath12k_wifi8_dp_ipa_tx_buf_free(ab);
	ath12k_wifi8_dp_ipa_tx_srng_cleanup(ab);
}

/* -------------------------------------------------------------------------
 * Debugfs: tqm2sw6_stats
 *
 * Exposes tqm2sw6 ring and poller health counters under the per-pdev IPA
 * debugfs directory.  Write "reset" to zero all counters.
 * -------------------------------------------------------------------------
 */

static struct ath12k_dp_wifi8_ipa *
tqm2sw6_dbg_get_ipa(struct ath12k *ar)
{
	struct ath12k_dp *dp;
	struct ath12k_dp_wifi8 *dp_wifi8;

	if (!ar || !ar->ab)
		return NULL;
	dp = ath12k_ab_to_dp(ar->ab);
	if (!dp)
		return NULL;
	dp_wifi8 = ath12k_get_dp_wifi8(dp);
	if (!dp_wifi8)
		return NULL;
	return ath12k_wifi8_get_ipa_dp(dp_wifi8);
}

static int ath12k_wifi8_tqm2sw6_stats_show(struct seq_file *file, void *data)
{
	struct ath12k *ar = file->private;
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa = tqm2sw6_dbg_get_ipa(ar);
	struct ath12k_dp_wifi8_ipa_tqm2sw6_stats *s;
	struct hal_srng *srng;
	int ring_id;

	if (!dp_wifi8_ipa)
		return -ENODEV;

	s = &dp_wifi8_ipa->tqm2sw6_stats;
	ring_id = dp_wifi8_ipa->tqm2sw6_ring.ring_id;
	srng = &ar->ab->hal.srng_list[ring_id];

	seq_printf(file, "tqm2sw6 Ring Info:\n");
	seq_printf(file, "  ring_id        : %d\n", ring_id);
	seq_printf(file, "  ring_paddr     : %pad\n",
		   &dp_wifi8_ipa->tqm2sw6_ring.paddr);
	seq_printf(file, "  num_entries    : %u\n", srng->num_entries);
	seq_printf(file, "  entry_size     : %u dwords\n", srng->entry_size);
	seq_printf(file, "  poll_interval  : %d ms\n",
		   ATH12K_TQM2SW6_POLL_INTERVAL_MS);

	seq_printf(file, "\ntqm2sw6 Poller Lifecycle:\n");
	seq_printf(file, "  poller_start_cnt : %llu\n", s->poller_start_cnt);
	seq_printf(file, "  poller_stop_cnt  : %llu\n", s->poller_stop_cnt);

	seq_printf(file, "\ntqm2sw6 NAPI / Timer Activity:\n");
	seq_printf(file, "  timer_fires      : %llu\n", s->timer_fires);
	seq_printf(file, "  napi_polls       : %llu\n", s->napi_polls);
	seq_printf(file, "  napi_empty_polls : %llu\n", s->napi_empty_polls);
	seq_printf(file, "  napi_complete    : %llu\n", s->napi_complete_cnt);

	seq_printf(file, "\ntqm2sw6 Ring Throughput:\n");
	seq_printf(file, "  entries_processed    : %llu\n", s->entries_processed);
	seq_printf(file, "  max_entries_per_poll : %llu\n",
		   s->max_entries_per_poll);
	seq_printf(file, "  last_poll_count      : %u\n", s->last_poll_count);

	seq_printf(file, "\ntqm2sw6 Refill stats:\n");
	seq_printf(file, "  Rx Refill Failed : %u\n", s->failed_refill);
	seq_printf(file, "  Cookie           : %u\n", s->cookie);
	seq_printf(file, "  RBM              : %u\n", s->rbm);

	return 0;
}

static int ath12k_wifi8_tqm2sw6_stats_open(struct inode *inode,
					   struct file *file)
{
	return single_open(file, ath12k_wifi8_tqm2sw6_stats_show,
			   inode->i_private);
}

static ssize_t ath12k_wifi8_tqm2sw6_stats_write(struct file *file,
						 const char __user *ubuf,
						 size_t count, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct ath12k *ar;
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa;
	char buf[32] = {0};
	ssize_t len;

	if (!seq)
		return -EINVAL;

	ar = seq->private;
	dp_wifi8_ipa = tqm2sw6_dbg_get_ipa(ar);
	if (!dp_wifi8_ipa)
		return -ENODEV;

	if (count == 0 || count >= sizeof(buf))
		return -EINVAL;

	len = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, ubuf, count);
	if (len < 0)
		return len;

	buf[len] = '\0';

	if (strstr(buf, "reset")) {
		memset(&dp_wifi8_ipa->tqm2sw6_stats, 0,
		       sizeof(dp_wifi8_ipa->tqm2sw6_stats));
		return count;
	}

	return -EINVAL;
}

static const struct file_operations fops_tqm2sw6_stats = {
	.read    = seq_read,
	.write   = ath12k_wifi8_tqm2sw6_stats_write,
	.open    = ath12k_wifi8_tqm2sw6_stats_open,
	.llseek  = seq_lseek,
	.release = single_release,
	.owner   = THIS_MODULE,
};

/**
 * ath12k_wifi8_debugfs_register_tqm2sw6() - Register tqm2sw6_stats debugfs file
 * @ar: Radio (pdev) for which to create the entry
 *
 * Creates "tqm2sw6_stats" under the per-pdev IPA debugfs directory.
 * Called via ipa_ctx->arch_ops->debugfs_register() from
 * ath12k_debugfs_register_ipa_extn().
 */
void ath12k_wifi8_debugfs_register_tqm2sw6(struct ath12k *ar)
{
	debugfs_create_file("tqm2sw6_stats", 0600,
			    ar->debug.debugfs_pdev, ar,
			    &fops_tqm2sw6_stats);
}
