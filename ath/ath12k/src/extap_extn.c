// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/if_bridge.h>
#include <linux/netdevice.h>
#include <linux/rcupdate.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>
#include <net/ndisc.h>
#include <net/switchdev.h>

#include "../core.h"
#include "../debug.h"
#include "extap_db.h"
#include "extap_extn.h"

#ifdef CPTCFG_QCN_EXTN

DEFINE_STATIC_KEY_FALSE(extap_enabled);
EXPORT_SYMBOL(extap_enabled);

/*
 * extap_vif_count - number of STA VAPs with ExtAP currently active.
 * Gates static_branch_enable/disable(&extap_enabled).
 */
static atomic_t extap_vif_count = ATOMIC_INIT(0);

/**
 * extap_parse_ndisc() - Parse NDP options and dispatch handlers
 * @ee: per-VAP ExtAP context
 * @skb: inbound IPv6 frame
 * @nd_msg: Pointer to ND message (NS/NA)
 * @len: Total ICMP payload length
 */
int extap_parse_ndisc(struct extap_extn *ee, struct sk_buff *skb,
		const struct extap_ndisc_opts opts[])
{
	struct nd_msg *nm = (struct nd_msg *)skb_transport_header(skb);
	u8 *tail = skb_tail_pointer(skb);
	struct nd_opt_hdr *noh;
	int next_opt = 0;
	u8 *p;

#define NDISC_OPT_SZ sizeof(struct nd_opt_hdr)

	for (p = nm->opt; (p + NDISC_OPT_SZ) < tail; p += next_opt) {
		int (*proc)(struct extap_extn *, struct sk_buff *, void *);
		int err = 0;
		int type;

		/* The next offset should be non-zero */
		noh = (struct nd_opt_hdr *)p;
		next_opt = noh->nd_opt_len << 3; /* Bytes */
		if (!next_opt)
			break;

		/* Check if the next_opt is within the range */
		if ((p + next_opt) > tail)
			break;

		/* Skip unhandled types */
		type = noh->nd_opt_type;
		if (type >= __ND_OPT_ARRAY_MAX)
			continue;

		/*
		 * Search if the option has process function
		 */
		proc = opts[type].proc;
		if (!proc)
			continue;

		/*
		 * Call the process handler, break if the function
		 * returns an error
		 */
		err = proc(ee, skb, noh);
		if (err < 0)
			return err;

	}

	return 0;
}

/**
 * extap_learn() - add an IP->MAC mapping to the per-VAP table
 * @ee:       per-VAP ExtAP context
 * @k:        lookup key carrying the client IP and MAC address
 *
 * Skips the VAP's own MAC to avoid learning it. Stats are incremented
 * by mac_db_entry_add() in extap_db.c.
 *
 * Return: 0 on success (entry added or already exists), -ENODATA if skipped,
 *         -ENOMEM if table full
 */
int extap_learn(struct extap_extn *ee, struct mac_hash_key *k)
{
	int ret;
	u8 skip_learn = 0;

	skip_learn += ether_addr_equal(k->mac_addr, ee->my_mac);
	skip_learn += is_zero_ether_addr(k->mac_addr);
	skip_learn += is_multicast_ether_addr(k->mac_addr);
	if (skip_learn)
		return -ENODATA;

	ret = mac_db_entry_add(extap_get_db(ee), k->mac_addr, k);

	/* -EEXIST means already learned — not an error for TX */
	return (ret == -EEXIST) ? 0 : ret;
}

int extap_xlate_dmac(struct extap_extn *ee, struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	struct mac_db_entry *me;
	struct mac_hash_key k;

	/* Caller must hold rcu_read_lock_bh() */
	RCU_LOCKDEP_WARN(!rcu_read_lock_bh_held(),
			 "extap_xlate_dmac called without rcu_read_lock_bh");

	switch (iph->version) {
	case 4:
		mac_hash_v4_keygen(skb, &k, true);
		break;
	case 6:
		mac_hash_v6_keygen(skb, &k, true);
		break;
	default:
		return -EINVAL;
	}

	me = mac_db_entry_get(extap_get_db(ee), &k);
	if (!me)
		return -ENOENT;

	ether_addr_copy(eth_hdr(skb)->h_dest, me->mac_addr);
	mac_db_entry_deref(me);

	return 0;
}

/**
 * extap_fdb_event() - bridge FDB delete notifier callback
 * @nb:     notifier block embedded in struct extap_extn
 * @event:  switchdev event type
 * @ptr:    pointer to struct switchdev_notifier_fdb_info
 *
 * Called when the bridge ages out a client MAC. Evicts the corresponding
 * IP->MAC entry from the per-VAP table so stale rewrites are not made for
 * a client that is no longer reachable.
 *
 * Only processes events for client MACs (not infrastructure MACs like the
 * bridge itself or locally-administered addresses).
 *
 * Return: NOTIFY_OK if the event was handled, NOTIFY_DONE otherwise.
 */
static int extap_fdb_event(struct notifier_block *nb, unsigned long event,
			   void *ptr)
{
	struct extap_extn *ee = container_of(nb, struct extap_extn, fdb_nb);
	const struct switchdev_notifier_fdb_info *info = ptr;
	struct mac_db *db;
	u8 skip_mac = 0;

	switch (event) {
	case SWITCHDEV_FDB_DEL_TO_BRIDGE:
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		break;
	default:
		return NOTIFY_DONE;
	}

	if (!info->addr)
		return NOTIFY_DONE;

	/* Skip infrastructure MACs: multicast, broadcast, locally-administered */
	skip_mac += is_multicast_ether_addr(info->addr);
	skip_mac += is_zero_ether_addr(info->addr);
	if (skip_mac)
		return NOTIFY_DONE;

	rcu_read_lock();
	db = rcu_dereference(ee->db);
	if (db)
		mac_db_entry_del(db, (u8 *)info->addr);
	rcu_read_unlock();

	return NOTIFY_OK;
}

/**
 * ath12k_extap_deinit() - tear down ExtAP for a STA VAP
 * @ahvif: ath12k VIF context
 */
void ath12k_extap_deinit(struct ath12k_vif *ahvif)
{
	struct extap_extn *ee = &ahvif->dp_vif.dp_extn.extap;
	struct mac_db *db;

	db = rcu_dereference_protected(ee->db, true);
	if (!db)
		return;

	/* Disable static branch if this is the last VAP */
	if (!atomic_dec_return(&extap_vif_count))
		static_branch_disable(&extap_enabled);

	/* Clear pointers to prevent new operations */
	RCU_INIT_POINTER(ee->db, NULL);
	ee->tx_cb = NULL;
	ee->rx_cb = NULL;

	/* Wait for RCU readers to finish */
	synchronize_rcu();

	/* Unregister bridge FDB notifier */
	br_fdb_unregister_notify(&ee->fdb_nb);

	/* Flush all entries */
	mac_db_entry_flush(db);

	/* Wait for any remaining references to be released */
	synchronize_rcu();

	mac_db_deref(db);
}
EXPORT_SYMBOL(ath12k_extap_deinit);

/**
 * ath12k_extap_init() - initialise ExtAP for a STA VAP
 * @ahvif: ath12k VIF context
 * @max_clients: IP-MAC table capacity; 0 selects the default
 *
 * Return: 0 on success, negative error code on failure
 */
int ath12k_extap_init(struct ath12k_vif *ahvif, u16 max_clients)
{
	struct extap_extn *ee = &ahvif->dp_vif.dp_extn.extap;
	struct mac_db *db;

	if (rcu_access_pointer(ee->db))
		return -EEXIST;

	db = mac_db_alloc_ref(max_clients);
	if (!db)
		return -ENOMEM;

	/*
	 * Register for bridge notification events for the VAP
	 */
	ee->fdb_nb.notifier_call = extap_fdb_event;
	br_fdb_register_notify(&ee->fdb_nb);

	/* Reset stats for this init cycle */
	memset(&ee->stats, 0, sizeof(ee->stats));

	ee->tx_cb = ath12k_extap_tx;
	ee->rx_cb = ath12k_extap_rx;
	ee->ab = ath12k_ah_to_ar(ahvif->ah, 0)->ab;

	ether_addr_copy(ee->my_mac, ahvif->vif->addr);

	rcu_assign_pointer(ee->db, db);

	/*
	 * Enable the hook(s) when the first VIF gets configured for EXTAP
	 */
	if (atomic_inc_return(&extap_vif_count) == 1)
		static_branch_enable(&extap_enabled);

	return 0;
}
EXPORT_SYMBOL(ath12k_extap_init);

/**
 * ath12k_extap_info_dump() - print the IP->MAC table to dmesg
 * @ahvif: ath12k VIF context
 *
 * Iterates every active entry in the per-VAP MAC database and prints
 * the IP address, MAC address, entry type (Unicast/Multicast) and the
 * number of successful DMAC rewrites for each entry.
 */
void ath12k_extap_info_dump(struct ath12k_vif *ahvif)
{
	struct extap_extn *ee = &ahvif->dp_vif.dp_extn.extap;
	DECLARE_BITMAP(snapshot, MAC_DB_ENTRIES);
	struct mac_db *db;
	unsigned long bit;

	rcu_read_lock();
	db = rcu_dereference(ee->db);
	if (!db) {
		rcu_read_unlock();
		pr_info("EXTAP Info Dump: ExtAP not active on %pM\n",
				ahvif->vif->addr);
		return;
	}

	pr_info("EXTAP Info Dump: %pM\n", ahvif->vif->addr);
	pr_info("%-26s %-24s %s\n", "IP Address", "MAC Address", "Type");

	/* Snapshot bitmap under the db lock to get a consistent view */
	spin_lock_bh(&db->lock);
	bitmap_copy(snapshot, db->data_bmap, MAC_DB_ENTRIES);
	spin_unlock_bh(&db->lock);

	for_each_set_bit(bit, snapshot, db->max_entries) {
		struct mac_db_entry *me = mac_db_entry_ref(db, (u16)bit);
		const char *type = "Unicast";

		if (!me)
			continue;

		if (is_multicast_ether_addr(me->mac_addr))
			type = "Multicast";

		if (me->key.is_v6)
			pr_info("%-26pI6 %-24pM %s\n",
					&me->key.ip.v6, me->mac_addr, type);
		else
			pr_info("%-26pI4 %-24pM %s\n",
					&me->key.ip.v4, me->mac_addr, type);

		mac_db_entry_deref(me);
	}

	pr_info("Total RX Hits: %lld\n", atomic64_read(&db->stats.matched));

	rcu_read_unlock();
}
EXPORT_SYMBOL(ath12k_extap_info_dump);

/**
 * ath12k_extap_stats_dump() - print aggregate ExtAP counters to dmesg
 * @ahvif: ath12k VIF context
 */
void ath12k_extap_stats_dump(struct ath12k_vif *ahvif)
{
	struct extap_extn *ee = &ahvif->dp_vif.dp_extn.extap;
	struct extap_stats *s = &ee->stats;
	struct mac_db *db;

	pr_info("EXTAP Stats: %pM\n", ahvif->vif->addr);
	pr_info("TX Packets Converted : %lld\n",
		atomic64_read(&s->tx_conv));
	pr_info("RX Packets Converted : %lld\n",
		atomic64_read(&s->rx_conv));

	db = extap_get_db(ee);
	if (db) {
		pr_info("TX Learns :            %lld\n",
			atomic64_read(&db->stats.created));
		pr_info("RX Hits :              %lld\n",
			atomic64_read(&db->stats.matched));
		pr_info("FDB Deletes :          %lld\n",
			atomic64_read(&db->stats.deleted));
		pr_info("Add Failures :         %lld\n",
			atomic64_read(&db->stats.failed));
		mac_db_deref(db);
	}
}
EXPORT_SYMBOL(ath12k_extap_stats_dump);

#endif /* CPTCFG_QCN_EXTN */
