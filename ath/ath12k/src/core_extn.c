/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/skbuff.h>
#include <linux/ctype.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/completion.h>
#include <linux/if_ether.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/uuid.h>
#include <linux/time.h>
#include <linux/of.h>
#include "../core.h"
#include "../ce.h"
#include "../hw.h"
#include "ath12k_cmn_extn.h"
#include "../net/mac80211/ieee80211_i.h"
#include "esp_extn.h"
#include "vendor_extn.h"
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include "../ini.h"

struct ath12k_vif_sysfs_entry {
	struct kobject kobj;
	struct ath12k *ar;
	u8 radio_idx;
	u8 vif_id;
};

static ssize_t macaddr_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buf)
{
	struct ath12k_vif_sysfs_entry *entry;
	struct ath12k *ar;
	struct wiphy *wiphy;
	u8 mac_addr[ETH_ALEN];
	bool byte0 = true;
	u8 radio_idx;

	entry =	container_of(kobj, struct ath12k_vif_sysfs_entry, kobj);
	ar = entry->ar;
	wiphy = ar->ah->hw->wiphy;
	radio_idx = entry->radio_idx;

	if (ath12k_cfg_get(ar->ab, ATH12K_CFG_COHOSTED_BSS_IND_ENABLE) & (1 << ar->pdev_idx))
		byte0 = false;

	ath12k_compute_link_vif_mac(mac_addr,
				    wiphy->addresses[radio_idx].addr,
				    entry->vif_id,
				    byte0);

	return scnprintf(buf, PAGE_SIZE, "%pM\n", mac_addr);
}

static ssize_t flags_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct ath12k_vif_sysfs_entry *entry;
	u32 used;

	entry = container_of(kobj, struct ath12k_vif_sysfs_entry, kobj);
	used = (entry->ar->vendor_mac_used_bitmap & (1 << entry->vif_id)) ? 1 : 0;

	return scnprintf(buf, PAGE_SIZE, "%u\n", used);
}

static ssize_t flags_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	struct ath12k_vif_sysfs_entry *entry;
	struct ath12k *ar;
	unsigned long val;

	entry = container_of(kobj, struct ath12k_vif_sysfs_entry, kobj);
	ar = entry->ar;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val == 1)
		ar->vendor_mac_used_bitmap |= (1 << entry->vif_id);
	else if (val == 0)
		ar->vendor_mac_used_bitmap &= ~(1 << entry->vif_id);
	else
		return -EINVAL;

	return count;
}

static struct kobj_attribute macaddr_attr = __ATTR_RO(macaddr);
static struct kobj_attribute flags_attr = __ATTR(flags, 0644, flags_show, flags_store);

static struct attribute *ath12k_vif_attrs[] = {
	&macaddr_attr.attr,
	&flags_attr.attr,
	NULL,
};

static const struct attribute_group ath12k_vif_attr_group = {
	.attrs = ath12k_vif_attrs,
};

static const struct attribute_group *ath12k_vif_default_groups[] = {
	&ath12k_vif_attr_group,
	NULL,
};

extern const struct sysfs_ops kobj_sysfs_ops;

static void ath12k_vif_kobj_release(struct kobject *kobj)
{
	/* nothing */
}

static struct kobj_type ath12k_vif_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = ath12k_vif_default_groups,
	.release = ath12k_vif_kobj_release,
};

static void ath12k_sysfs_setup_vif_entry(struct ath12k *ar,
					 struct ath12k_vif_sysfs_entry *entry,
					 u8 vif_id,
					 u8 radio_idx)
{
	entry->vif_id = vif_id;
	entry->ar = ar;
	entry->radio_idx = radio_idx;
}

void ath12k_sysfs_init_extn(struct ath12k *ar)
{
	struct wiphy *wiphy = ar->ah->hw->wiphy;
	struct device *dev = wiphy_dev(wiphy);
	struct kobject *parent;
	char rname[16];
	int i, r;

	if (!dev)
		return;

	/* Place under the wiphy (class ieee80211 -> phyX) */
	parent = &dev->kobj;

	/*
	 * Create radio directory under phyX: radio<radio_idx>,
	 * determine radio_idx within ah->radio[]
	 */
	for (r = 0; r < ar->ah->num_radio; r++) {
		if (&ar->ah->radio[r] == ar) {
			snprintf(rname, sizeof(rname), "radio%d", r);
			break;
		}
	}

	ar->ar_extn.radio_kobj = kobject_create_and_add(rname, parent);
	if (!ar->ar_extn.radio_kobj)
		return;

	ar->ar_extn.vifs_kobj = kobject_create_and_add("vifs", ar->ar_extn.radio_kobj);
	if (!ar->ar_extn.vifs_kobj)
		return;

	for (i = 0; i <= 16; i++) {
		struct ath12k_vif_sysfs_entry *entry;
		char name[8];

		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry)
			break;

		snprintf(name, sizeof(name), "vif%d", i);
		kobject_init(&entry->kobj, &ath12k_vif_ktype);
		if (kobject_add(&entry->kobj, ar->ar_extn.vifs_kobj, "%s", name)) {
			kobject_put(&entry->kobj);
			kfree(entry);
			break;
		}
		for (r = 0; r < ar->ah->num_radio; r++) {
			if (&ar->ah->radio[r] == ar)
				break;
		}
		ath12k_sysfs_setup_vif_entry(ar, entry, i, r);
		ar->ar_extn.vif_entries[i] = entry;
	}
}

void ath12k_sysfs_cleanup_extn(struct ath12k *ar)
{
	int i;
	struct ath12k_vif_sysfs_entry *entry;

	if (ar->ar_extn.vifs_kobj) {
		for (i = 0; i <= 16; i++) {
			entry = ar->ar_extn.vif_entries[i];
			if (!entry)
				continue;
			kobject_put(&entry->kobj);
			kfree(entry);
			ar->ar_extn.vif_entries[i] = NULL;
		}

		kobject_put(ar->ar_extn.vifs_kobj);
		ar->ar_extn.vifs_kobj = NULL;

		if (ar->ar_extn.radio_kobj) {
			kobject_put(ar->ar_extn.radio_kobj);
			ar->ar_extn.radio_kobj = NULL;
		}
	}
}


void ath12k_wmi_peer_migration_event_extn(struct ath12k_vif *ahvif)
{
	struct sta_240mhz_info *sta, *tmp;

	spin_lock_bh(&ahvif->ath12k_vif_extn.data_lock);
	list_for_each_entry_safe(sta, tmp, &ahvif->ath12k_vif_extn.peer_240mhz_list_extn,
				 list) {
		list_del(&sta->list);
		kfree(sta);
	}

	spin_unlock_bh(&ahvif->ath12k_vif_extn.data_lock);
}

void ath12k_mac_hw_allocate_extn(struct ieee80211_hw *hw,
			  const struct ieee80211_ops_extn *ops_extn)
{
	struct ieee80211_local *local = wiphy_priv(hw->wiphy);
	local->ops_extn = ops_extn;
}

int
ath12k_add_sta_240mhz_info_extn(struct ath12k_vif *ahvif,
				u8 *mac,
				struct ieee80211_240mhz_vendor_oper_extn
				*params)
{
	struct sta_240mhz_info *sta;

	sta = kmalloc(sizeof(*sta), GFP_KERNEL);
	if (!sta)
		return -ENOMEM;

	ether_addr_copy(sta->mac_addr, mac);
	memcpy(&sta->params_240MHz_extn, params,
	       sizeof(sta->params_240MHz_extn));
	INIT_LIST_HEAD(&sta->list);
	spin_lock_bh(&ahvif->ath12k_vif_extn.data_lock);
	list_add_tail(&sta->list, &ahvif->ath12k_vif_extn.peer_240mhz_list_extn);
	spin_unlock_bh(&ahvif->ath12k_vif_extn.data_lock);

	return 0;
}

void ath12k_mac_init_arvif_extn(struct ath12k_vif *ahvif)
{
	/* Protects the extensions data update for an interface*/
	spin_lock_init(&ahvif->ath12k_vif_extn.data_lock);

	INIT_LIST_HEAD(&ahvif->ath12k_vif_extn.peer_240mhz_list_extn);
}

void ath12k_mac_setup_radio_iface_comb_extn(
		struct ieee80211_iface_combination *comb)
{
	/* Add 320MHz support for 240MHz */
	comb->radar_detect_widths |= BIT(NL80211_CHAN_WIDTH_320);
}

int ath12k_wmi_op_rx_extn(enum wmi_tlv_event_id id, struct ath12k_base *ab,
			  struct sk_buff *skb)
{
	switch (id) {
	case WMI_ESP_ESTIMATE_EVENTID:
		ath12k_wmi_esp_estimate_event_extn(ab, skb);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void ath12k_mac_setup_extn(struct ath12k *ar)
{
	ath12k_esp_init_extn(ar);
	ath12k_custom_txrx_init(ar);
}

void ath12k_mac_cleanup_unregister_extn(struct ath12k *ar)
{
	ath12k_esp_cleanup_extn(ar);
	ath12k_custom_txrx_deinit(ar);
}
