/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __ATH12K_EXTAP_EXTN_H
#define __ATH12K_EXTAP_EXTN_H

#include <linux/atomic.h>
#include <linux/jump_label.h>
#include <linux/notifier.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/if_arp.h>

#include "extap_db.h"

#include <linux/etherdevice.h>

#define DHCP_BOOT_REQ 1
#define DHCP_REPLY 2
#define DHCP_BROADCAST_FLAG cpu_to_be16(0x8000)
#define DHCP_SERVER_PORT 67
#define DHCP_MAGIC_COOKIE 0x63825363

#define NA_HDR_LEN (sizeof(struct nd_opt_hdr) + ETH_ALEN)

struct ath12k_vif;

struct arp_eth {
        u8 ar_sha[ETH_ALEN];       /* sender hardware address      */
        u8 ar_sip[4];              /* sender IP address            */
        u8 ar_tha[ETH_ALEN];       /* target hardware address      */
        u8 ar_tip[4];              /* target IP address            */
} __packed;

struct extap_dhcp_hdr {
	u8 op;
	u8 htype;
	u8 hlen;
	u8 hops;
	__be32 xid;
	__be16 secs;
	__be16 flags;
	__be32 ciaddr;
	__be32 yiaddr;
	__be32 siaddr;
	__be32 giaddr;
	u8 chaddr[16];	/* Client HW address */
	u8 sname[64];	/* Server hostname */
	u8 fname[128];	/* Bootp file name */
	__be32 cookie;	/* Magic cookie */
	u8 options[];	/* Variable size options */
} __packed;

/*
 * ExtAP IPv6 NDISC options handler
 */
struct extap_ndisc_opts {
	int (*proc)(struct extap_extn *, struct sk_buff *, void *);
};

/**
 * struct extap_stats - per-VAP ExtAP aggregate counters
 * Reset when the VAP is reinitialized.
 */
struct extap_stats {
	atomic64_t tx_pkts;		/* EXTAP Tx Packets */
	atomic64_t tx_conv;		/* outbound SMAC rewrites */
	atomic64_t rx_pkts;		/* EXTAP Rx Packets */
	atomic64_t rx_conv;		/* inbound DMAC rewrites */
};

/**
 * struct extap_extn - per-VAP ExtAP context
 * @db:         RCU-protected IP-to-MAC table for this VAP
 * @tx_cb:      TX classify hook; set on init, called from dp TX path
 * @rx_cb:      RX classify hook; set on init, called from dp RX path
 * @fdb_nb:     bridge FDB delete notifier; evicts stale entries when
 *              the bridge ages out a client MAC. Instantiated for each VAP
 *              instance to handle per-VAP FDB events independently.
 * @my_mac:     VAP MAC address
 * @ab:         ath12k_base for logging; set on init
 *
 * Embedded in ath12k_dp_vif_extn. One instance per STA VAP.
 * Both hooks are invoked directly under static_branch_unlikely(&extap_enabled),
 * symmetric with each other — no netfilter involved.
 */
struct extap_extn {
	struct mac_db __rcu *db;
	void (*tx_cb)(struct ath12k_vif *ahvif, struct sk_buff *skb);
	void (*rx_cb)(struct ath12k_vif *ahvif, struct sk_buff *skb);
	struct notifier_block fdb_nb;
	struct extap_stats stats;
	u8 my_mac[ETH_ALEN];
	struct ath12k_base *ab;
};

static inline struct mac_db *extap_get_db(struct extap_extn *ee)
{
	return rcu_dereference_bh(ee->db);
}

int extap_xlate_dmac(struct extap_extn *ee, struct sk_buff *skb);
int extap_parse_ndisc(struct extap_extn *ee, struct sk_buff *skb,
		const struct extap_ndisc_opts opts[]);
int extap_learn(struct extap_extn *ee, struct mac_hash_key *k);

#ifndef CPTCFG_QCN_EXTN

static inline void ath12k_extap_stats_dump(struct ath12k_vif *ahvif) {}
static inline void ath12k_extap_info_dump(struct ath12k_vif *ahvif) {}

static inline void ath12k_extap_rx(struct ath12k_vif *ahvif, struct sk_buff *skb) {}


static inline void ath12k_extap_tx(struct ath12k_vif *ahvif, struct sk_buff *skb) {}

static inline void ath12k_extap_deinit(struct ath12k_vif *ahvif) {}

static inline int ath12k_extap_init(struct ath12k_vif *ahvif, u16 max_clients)
{
	return 0;
}

#else /* CPTCFG_QCN_EXTN */

void ath12k_extap_stats_dump(struct ath12k_vif *ahvif);
void ath12k_extap_info_dump(struct ath12k_vif *ahvif);

void ath12k_extap_rx(struct ath12k_vif *ahvif, struct sk_buff *skb);
void ath12k_extap_tx(struct ath12k_vif *ahvif, struct sk_buff *skb);
void ath12k_extap_deinit(struct ath12k_vif *ahvif);
int ath12k_extap_init(struct ath12k_vif *ahvif, u16 max_clients);

#endif /* CPTCFG_QCN_EXTN */
#endif /* __ATH12K_EXTAP_EXTN_H */
