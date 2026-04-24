/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_ipa.h"

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
