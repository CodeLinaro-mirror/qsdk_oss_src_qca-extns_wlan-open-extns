/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_ipa.h"
#include "../../../qcn_extns/ipa/dp_ipa.h"
#include "../../hal.h"
#include <linux/iommu.h>
#include "../../../qcn_extns/ini.h"

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

	printk(KERN_ERR "WDS_IPA_DBG: %s: ENTER ahvif=%p ahvif_id=%u dev=%s\n",
	       __func__, ahvif, ahvif_id, dev ? dev->name : "NULL");

	if (ahvif_id == ATH12K_INVALID_AHVIF_ID) {
		printk(KERN_ERR "WDS_IPA_DBG: %s: ahvif_id INVALID, alloc"
		       " ABORTED dev=%s\n", __func__, dev ? dev->name : "NULL");
		return -EINVAL;
	}

	idx = ahvif_id;
	if (idx >= ATH12K_DP_IPA_VP_RANGE) {
		printk(KERN_ERR "WDS_IPA_DBG: %s: idx=%d out of VP_RANGE=%u,"
		       " alloc ABORTED ahvif_id=%u dev=%s\n",
		       __func__, idx, ATH12K_DP_IPA_VP_RANGE, ahvif_id,
		       dev ? dev->name : "NULL");
		return -ENOSPC;
	}

	vp_num = ATH12K_DP_IPA_GET_VP_FROM_AHVIFID(idx);

	spin_lock(&ath12k_ipa_vp_lock);

	if (rcu_access_pointer(ath12k_ipa_vp_to_dev[idx])) {
		struct net_device *existing =
			rcu_dereference_protected(ath12k_ipa_vp_to_dev[idx],
						  lockdep_is_held(&ath12k_ipa_vp_lock));
		printk(KERN_ERR "WDS_IPA_DBG: %s: SLOT ALREADY OCCUPIED"
		       " ahvif_id=%u idx=%d existing_dev=%s new_dev=%s --"
		       " returning -EEXIST, table entry NOT overwritten\n",
		       __func__, ahvif_id, idx, existing->name,
		       dev ? dev->name : "NULL");
		spin_unlock(&ath12k_ipa_vp_lock);
		return -EEXIST;
	}

	rcu_assign_pointer(ath12k_ipa_vp_to_dev[idx], dev);
	dev_hold(dev);

	spin_unlock(&ath12k_ipa_vp_lock);

	printk(KERN_ERR "WDS_IPA_DBG: %s: SUCCESS ahvif_id=%u idx=%d"
	       " vp_num=%d dev=%s stored in ath12k_ipa_vp_to_dev[%d]\n",
	       __func__, ahvif_id, idx, vp_num, dev ? dev->name : "NULL", idx);

	return vp_num;
}

/**
 * ath12k_dp_ipa_dealloc_vp_num - remove a netdev from the IPA VP pool
 * @ahvif_id: ahvif_id whose VP mapping should be removed
 *
 * Removes the VP-to-netdev association for the given ahvif_id and drops the
 * device reference after an RCU grace period.
 *
 * Takes ahvif_id by value instead of a struct ath12k_vif * on purpose:
 * ahvif->dp_vif.ahvif_id is reset to ATH12K_INVALID_AHVIF_ID synchronously
 * inside ath12k_mac_op_remove_interface(), and this function is called
 * later from an async IPA workqueue callback -- re-reading the field from
 * a live ahvif would race against that reset. The caller already knows
 * the correct ahvif_id (captured by value at IPA-event-enqueue time), so
 * it's passed directly here instead.
 */
void ath12k_dp_ipa_dealloc_vp_num(u8 ahvif_id)
{
	struct net_device *dev;
	int idx;

	printk(KERN_ERR "WDS_IPA_DBG: %s: ENTER ahvif_id=%u\n",
	       __func__, ahvif_id);

	if (ahvif_id == ATH12K_INVALID_AHVIF_ID) {
		printk(KERN_ERR "WDS_IPA_DBG: %s: ahvif_id INVALID, dealloc"
		       " ABORTED -- nothing cleared\n", __func__);
		return;
	}

	idx = ahvif_id;
	if (idx >= ATH12K_DP_IPA_VP_RANGE) {
		printk(KERN_ERR "WDS_IPA_DBG: %s: idx=%d out of VP_RANGE=%u,"
		       " dealloc ABORTED ahvif_id=%u -- nothing cleared\n",
		       __func__, idx, ATH12K_DP_IPA_VP_RANGE, ahvif_id);
		return;
	}

	spin_lock(&ath12k_ipa_vp_lock);
	dev = rcu_dereference_protected(ath12k_ipa_vp_to_dev[idx],
					lockdep_is_held(&ath12k_ipa_vp_lock));
	RCU_INIT_POINTER(ath12k_ipa_vp_to_dev[idx], NULL);
	spin_unlock(&ath12k_ipa_vp_lock);

	if (!dev) {
		printk(KERN_ERR "WDS_IPA_DBG: %s: ahvif_id=%u idx=%d was"
		       " ALREADY EMPTY (dev=NULL) -- nothing to dev_put,"
		       " table entry was already cleared before this call\n",
		       __func__, ahvif_id, idx);
		return;
	}

	printk(KERN_ERR "WDS_IPA_DBG: %s: ahvif_id=%u idx=%d CLEARED"
	       " table entry, was pointing to dev=%s -- dropping reference\n",
	       __func__, ahvif_id, idx, dev->name);

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

	/*
	 * Use wdev_to_ieee80211_vif() NOT wdev_to_ieee80211_vif_vlan(wdev, false).
	 * For NL80211_IFTYPE_AP_VLAN (e.g. phy00-mld0.sta1), _vlan(wdev, false)
	 * unconditionally returns the MASTER AP's vif — not the AP_VLAN's own vif.
	 * wdev_to_ieee80211_vif() returns the interface's own vif for all types.
	 */
	vif = wdev_to_ieee80211_vif(wdev);
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

	/*
	 * Fix off-by-one vs ath12k_dp_ipa_alloc_vp_num():
	 *
	 * alloc_vp_num() stores at: ath12k_ipa_vp_to_dev[ahvif_id]
	 * VP formula:  VP = ahvif_id + VP_MIN - 1
	 * VP_IDX formula: VP_IDX = VP - VP_MIN = ahvif_id - 1
	 *
	 * Using VP_IDX directly misses by one: VP_IDX = ahvif_id - 1
	 * but alloc stored at ahvif_id.  Add 1 to get the correct index.
	 */
	idx = ATH12K_DP_IPA_GET_VP_IDX_FROM_VP(vp_num) + 1; /* = ahvif_id */

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
 * For normal AP/STA: returns the interface's standard bank_id
 * (INDEX_LOOKUP_EN=0).
 *
 * Returns: bank_id on success, DP_INVALID_BANK_ID on error.
 */
u32 ath12k_dp_ipa_get_bank_id(struct net_device *dev)
{
	struct wireless_dev *wdev;
	struct ieee80211_vif *vif;
	struct ath12k_vif *ahvif;

	if (!dev)
		return DP_INVALID_BANK_ID;
	wdev = dev->ieee80211_ptr;
	if (!wdev)
		return DP_INVALID_BANK_ID;
	/*
	 * Use wdev_to_ieee80211_vif() NOT wdev_to_ieee80211_vif_vlan(wdev, false).
	 * For NL80211_IFTYPE_AP_VLAN (e.g. phy00-mld0.sta1), _vlan(wdev, false)
	 * unconditionally returns the MASTER AP's vif — not the AP_VLAN's own vif.
	 * wdev_to_ieee80211_vif() returns the interface's own vif for all types.
	 */
	vif = wdev_to_ieee80211_vif(wdev);
	if (!vif)
		return DP_INVALID_BANK_ID;
	ahvif = ath12k_vif_to_ahvif(vif);
	return ahvif->dp_vif.bank_id;
}
EXPORT_SYMBOL(ath12k_dp_ipa_get_bank_id);

/**
 * ath12k_dp_ipa_get_search_idx_reg_num - get SRCH_IDX_REG_NUM for VP config
 * @dev: netdev of the interface being registered with IPA
 *
 * SRCH_IDX_REG_NUM in the HAL VP config table is only meaningful for
 * WDS-EXT (AP_VLAN with is_wds_4addr=true).  For normal AP/STA interfaces
 * the hardware does not use index-based lookup, so return 0.
 *
 * For WDS, the AST index (ast_idx) stored in dp_vif is the correct value —
 * it is the same index used in the TCL data command for TX.
 *
 * Returns: ast_idx for WDS (AP_VLAN), 0 for all other interface types.
 */
u8 ath12k_dp_ipa_get_search_idx_reg_num(struct net_device *dev)
{
	struct wireless_dev *wdev;
	struct ieee80211_vif *vif;
	struct ath12k_vif *ahvif;

	if (!dev)
		return 0;
	wdev = dev->ieee80211_ptr;
	if (!wdev)
		return 0;
	/*
	 * Use wdev_to_ieee80211_vif() NOT wdev_to_ieee80211_vif_vlan(wdev, false).
	 * For NL80211_IFTYPE_AP_VLAN (e.g. phy00-mld0.sta1), _vlan(wdev, false)
	 * unconditionally returns the MASTER AP's vif — not the AP_VLAN's own vif.
	 * wdev_to_ieee80211_vif() returns the interface's own vif for all types.
	 */
	vif = wdev_to_ieee80211_vif(wdev);
	if (!vif)
		return 0;
	ahvif = ath12k_vif_to_ahvif(vif);

	/*
	 * Only WDS-EXT (AP_VLAN with 4-address mode) uses index-based
	 * address search.  The ast_idx is the same value placed in
	 * HAL_TCL_DATA_CMD_INFO1_CACHE_SET_NUM for TX descriptors.
	 */
	if (vif->type == NL80211_IFTYPE_AP_VLAN && ahvif->dp_vif.is_wds_4addr)
		return (u8)ahvif->dp_vif.ast_idx;

	return 0;
}
EXPORT_SYMBOL(ath12k_dp_ipa_get_search_idx_reg_num);

/**
 * ath12k_dp_ipa_get_cache_set - get cache_set (ast_hash) for WDS VP config
 * @dev: netdev of the interface being registered with IPA
 *
 * Returns the ast_hash for WDS (AP_VLAN with is_wds_4addr=true), which is
 * stored in the vp_profile for reference and used when programming the AST
 * override map register.  Returns 0 for normal AP/STA interfaces.
 *
 * Returns: ast_hash for WDS (AP_VLAN), 0 for all other interface types.
 */
u8 ath12k_dp_ipa_get_cache_set(struct net_device *dev)
{
	struct wireless_dev *wdev;
	struct ieee80211_vif *vif;
	struct ath12k_vif *ahvif;

	if (!dev)
		return 0;
	wdev = dev->ieee80211_ptr;
	if (!wdev)
		return 0;
	/*
	 * Use wdev_to_ieee80211_vif() NOT wdev_to_ieee80211_vif_vlan(wdev, false).
	 * For NL80211_IFTYPE_AP_VLAN (e.g. phy00-mld0.sta1), _vlan(wdev, false)
	 * unconditionally returns the MASTER AP's vif — not the AP_VLAN's own vif.
	 * wdev_to_ieee80211_vif() returns the interface's own vif for all types.
	 */
	vif = wdev_to_ieee80211_vif(wdev);
	if (!vif)
		return 0;
	ahvif = ath12k_vif_to_ahvif(vif);

	if (vif->type == NL80211_IFTYPE_AP_VLAN && ahvif->dp_vif.is_wds_4addr)
		return (u8)ahvif->dp_vif.ast_hash;

	return 0;
}
EXPORT_SYMBOL(ath12k_dp_ipa_get_cache_set);

/**
 * ath12k_dp_ipa_set_ppe_vp_entry_config - populate VP profile for HAL programming
 * @vp_profile: profile to fill
 * @arvif:      parent AP's link_vif (provides vdev_id, lmac_id)
 * @ahvif:      interface's own ath12k_vif — for AP_VLAN this is the
 *              AP_VLAN's ahvif (NOT the parent AP's), obtained via
 *              ath12k_ahvif_from_wdev() which uses IEEE80211_WDEV_TO_SUB_IF.
 *
 * For WDS (AP_VLAN with is_wds_4addr):
 *   - bank_id            = bank with INDEX_LOOKUP_EN=1 (allocated at sta_set_4addr)
 *   - search_idx_reg_num = ahvif_id & 0x3F (SLOT into TCL_PPE_INDEX_MAPPING_TABLE;
 *                          NOT the ast_idx — that value is stored at TABLE[slot])
 *   - cache_set          = 0 (not encoded into the WiFi8 VP register)
 *
 * For normal AP/STA:
 *   - bank_id            = interface's bank_id (INDEX_LOOKUP_EN=0)
 *   - search_idx_reg_num = 0 (index-based lookup not used)
 *   - cache_set          = 0
 */
void ath12k_dp_ipa_set_ppe_vp_entry_config(struct ath12k_ipa_vp_profile *vp_profile,
					   struct ath12k_link_vif *arvif,
					   struct ath12k_vif *ahvif)
{
	vp_profile->drop_prec_enable = 0;
	vp_profile->to_fw_sw = 0;
	vp_profile->use_ppe_int_pri = 0;
	vp_profile->entry_valid = true;
	vp_profile->vdev_id = arvif->vdev_id;
	vp_profile->lmac_id = arvif->ar->lmac_id;

	/*
	 * Read ast_idx, ast_hash, bank_id directly from ahvif->dp_vif.
	 * This avoids the mac80211 wdev-to-vif API which is broken for
	 * AP_VLAN: both wdev_to_ieee80211_vif_vlan() and wdev_to_ieee80211_vif()
	 * return the master AP's vif/NULL instead of the AP_VLAN's own vif.
	 */
	/*
	 * AP_VLAN (WDS STA) has no firmware vdev so dp_vif.bank_id is never
	 * assigned by the normal vdev-configure path.  It is set by
	 * ath12k_wifi8_dp_ipa_wds_bank_srch_setup() (called from
	 * sta_set_4addr) — but that function has several early-return
	 * failure paths (no live parent link, bank-alloc failure, etc.)
	 * that leave it at DP_INVALID_BANK_ID even though is_wds_4addr is
	 * already true.  Fall back to the parent AP's bank_id in that case:
	 * AP_VLAN TX uses the parent AP's vdev context in firmware, so the
	 * parent's (non-WDS, INDEX_LOOKUP_EN=0) bank is a valid fallback.
	 */
	if (ahvif->dp_vif.is_wds_4addr &&
	    ahvif->dp_vif.bank_id != DP_INVALID_BANK_ID) {
		/*
		 * search_idx_reg_num = slot index (0-63) in the TCL PPE
		 * search-index table.  For IPA WDS, the slot = ahvif_id & 63,
		 * and ath12k_wifi8_dp_ipa_wds_bank_srch_setup() has already
		 * programmed TCL_PPE_INDEX_MAPPING_TABLE[slot] with the
		 * packed (ast_idx | ast_hash) value, and set dp_vif->bank_id
		 * to a bank with INDEX_LOOKUP_EN=1 to match.
		 *
		 * Hardware flow: VP[SRCH_IDX_REG_NUM=slot] →
		 *   TABLE[slot] contains packed(ast_idx[19:0] | ast_hash[23:20])
		 *   → hardware resolves the WDS peer by AST index (e.g. 754)
		 *
		 * cache_set: NOT encoded into the VP hardware register.
		 * ath12k_dp_ipa_hal_tx_set_ppe_vp_entry() has no
		 * HAL_TX_PPE_VP_CFG_CACHE_SET field — absent from the WiFi8
		 * VP register layout.  Set to 0.
		 */
		vp_profile->search_idx_reg_num = ahvif->dp_vif.ahvif_id & 0x3F;
		vp_profile->cache_set          = 0;
		vp_profile->bank_id            = ahvif->dp_vif.bank_id;
	} else {
		/*
		 * Either not WDS-4addr, or wds_bank_srch_setup() hasn't
		 * (yet, or ever) succeeded for this AP_VLAN: search_idx_reg_num
		 * MUST stay 0 here, matching the non-WDS bank_id fallback below —
		 * requesting index-based lookup (search_idx_reg_num != 0)
		 * against a bank with INDEX_LOOKUP_EN=0 is an inconsistent HW
		 * config that silently mis-programs the VP entry.
		 */
		if (ahvif->dp_vif.is_wds_4addr)
			printk(KERN_ERR "WDS_IPA_DBG: %s: is_wds_4addr=true but"
			       " bank_id still DP_INVALID_BANK_ID (ahvif_id=%u)"
			       " -- falling back to parent AP's non-WDS bank,"
			       " search_idx_reg_num forced to 0 to match\n",
			       __func__, ahvif->dp_vif.ahvif_id);

		vp_profile->search_idx_reg_num = 0;
		vp_profile->cache_set          = 0;
		vp_profile->bank_id            = arvif->ahvif->dp_vif.bank_id;
	}
}

#define HAL_TX_PPE_VP_CONFIG_TABLE_ADDR  0x00F1336C
#define HAL_TX_PPE_VP_CONFIG_TABLE_OFFSET 4

/**
 * ath12k_dp_ipa_hal_tx_set_ppe_vp_entry - program PPE VP config table entry
 * @ab:         ath12k_base of any SoC in the group (central SoC is resolved
 *              internally via ath12k_dp_get_ab_from_dp_hw_group)
 * @vp_num:     VP number in [ATH12K_DP_IPA_VP_MIN, ATH12K_DP_IPA_VP_MAX]
 * @vp_profile: VP profile to program, or NULL to clear the entry
 *
 * Writes the VP configuration to the PPE VP config table register at:
 *   HAL_TX_PPE_VP_CONFIG_TABLE_ADDR + (4 * (vp_num - VP_MIN))
 *
 * Register layout (32-bit):
 *   Bit  0:      ENTRY_VALID
 *   Bits 3:1:    LMAC_ID
 *   Bits 11:4:   BANK_ID   (WDS: INDEX_LOOKUP_EN=1; normal: INDEX_LOOKUP_EN=0)
 *   Bits 19:12:  VDEV_ID
 *   Bits 25:20:  SRCH_IDX_REG_NUM  (WDS: ast_idx; normal: 0)
 *   Bit  26:     USE_PPE_INT_PRI
 *   Bit  27:     TO_FW
 *   Bit  28:     DROP_PREC_EN
 *
 * Passing NULL for @vp_profile writes 0 to the register, invalidating the
 * entry (ENTRY_VALID=0).
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

	/* NULL profile → clear the entry (write 0, ENTRY_VALID=0) */
	if (!vp_profile) {
		ppe_vp_config = 0;
		goto reg_write;
	}

	if (vp_profile->lmac_id == HAL_WILDCARD_LMAC_ID)
		vp_profile->lmac_id = HAL_TX_PPE_VP_CFG_WILDCARD_LMAC_ID;

	ppe_vp_config |=
		u32_encode_bits(vp_profile->entry_valid,
				HAL_TX_PPE_VP_CFG_ENTRY_VALID) |
		/*
		 * SRCH_IDX_REG_NUM = 6-bit SLOT index into TCL_PPE_INDEX_MAPPING_TABLE.
		 * NOT the ast_idx itself.  For WDS (AP_VLAN):
		 *   slot = ahvif_id & 0x3F
		 *   TABLE[slot] = packed(ast_idx[19:0] | ast_hash[23:20])  ← programmed
		 *     in ath12k_wifi8_dp_ipa_wds_bank_srch_setup() at sta_set_4addr
		 * Hardware reads TABLE[slot] to resolve the AST peer entry.
		 * For normal AP/STA: 0 — not used (MAC-based lookup instead).
		 */
		u32_encode_bits(vp_profile->search_idx_reg_num,
				HAL_TX_PPE_VP_CFG_SRCH_IDX_REG_NUM) |
		u32_encode_bits(vp_profile->use_ppe_int_pri,
				HAL_TX_PPE_VP_CFG_USE_PPE_INT_PRI) |
		u32_encode_bits(vp_profile->to_fw_sw,
				HAL_TX_PPE_VP_CFG_TO_FW) |
		u32_encode_bits(vp_profile->drop_prec_enable,
				HAL_TX_PPE_VP_CFG_DROP_PREC_EN) |
		/*
		 * BANK_ID:
		 *   WDS (AP_VLAN): bank with INDEX_LOOKUP_EN=1
		 *   Normal AP/STA: standard bank (INDEX_LOOKUP_EN=0)
		 */
		u32_encode_bits(vp_profile->bank_id,
				HAL_TX_PPE_VP_CFG_BANK_ID) |
		u32_encode_bits(vp_profile->lmac_id,
				HAL_TX_PPE_VP_CFG_LMAC_ID) |
		u32_encode_bits(vp_profile->vdev_id,
				HAL_TX_PPE_VP_CFG_VDEV_ID);

reg_write:
	printk(KERN_ERR "WDS_IPA_DBG: %s: VP register write"
	       " vp_num=%u vp_ind=%u reg_addr=0x%08x reg_val=0x%08x"
	       " [entry_valid=%d lmac_id=%u bank_id=%u vdev_id=%u"
	       " search_idx_reg_num=%u(ast_idx) cache_set=%u"
	       " use_ppe_int_pri=%d to_fw=%d drop_prec=%d"
	       " %s]\n",
	       __func__, vp_num, vp_ind,
	       HAL_TX_PPE_VP_CONFIG_TABLE_ADDR +
	           (HAL_TX_PPE_VP_CONFIG_TABLE_OFFSET * vp_ind),
	       ppe_vp_config,
	       vp_profile ? vp_profile->entry_valid    : 0,
	       vp_profile ? vp_profile->lmac_id        : 0,
	       vp_profile ? vp_profile->bank_id        : 0,
	       vp_profile ? vp_profile->vdev_id        : 0,
	       vp_profile ? vp_profile->search_idx_reg_num : 0,
	       vp_profile ? vp_profile->cache_set      : 0,
	       vp_profile ? vp_profile->use_ppe_int_pri : 0,
	       vp_profile ? vp_profile->to_fw_sw       : 0,
	       vp_profile ? vp_profile->drop_prec_enable : 0,
	       vp_profile ?
	           (vp_profile->search_idx_reg_num ? "WDS-index-lookup" : "normal-AP/STA")
	           : "CLEAR-entry");
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

	return 0;

err_srng_cleanup:
	ath12k_wifi8_dp_ipa_tx_srng_cleanup(ab);
	return ret;
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
	ath12k_wifi8_dp_free_tx_rsrc(ab);
	ath12k_wifi8_dp_ipa_tx_buf_free(ab);
	ath12k_wifi8_dp_ipa_tx_srng_cleanup(ab);
}

