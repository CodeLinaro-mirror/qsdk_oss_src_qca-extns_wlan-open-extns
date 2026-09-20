// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/if_arp.h>
#include <linux/if_bridge.h>
#include <linux/icmpv6.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <asm/unaligned.h>
#include <net/addrconf.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/switchdev.h>

#include "../core.h"
#include "../debug.h"
#include "extap_db.h"
#include "extap_extn.h"

#ifdef CPTCFG_QCN_EXTN

#define DHCP_REPLY		2
#define DHCP_SERVER_PORT	67
#define DHCP_MAGIC_COOKIE	0x63825363
#define IPV6_OFFSET		(ETH_HLEN + sizeof(struct ipv6hdr))
#define ICMPV6_OFFSET		(IPV6_OFFSET)

static int __proc_ipv6_ns_rx(struct extap_extn *, struct sk_buff *, void *);
static int __proc_ipv6_na_rx(struct extap_extn *, struct sk_buff *, void *);

static const struct extap_ndisc_opts ndisc_opts[__ND_OPT_ARRAY_MAX] = {
	[ND_OPT_SOURCE_LL_ADDR] = {.proc = __proc_ipv6_ns_rx},
	[ND_OPT_TARGET_LL_ADDR] = {.proc = __proc_ipv6_na_rx},
};

/**
 * __proc_ipv6_ns_rx() - Neighbor Solicitation RX handler
 * @ee: per-VAP ExtAP context
 * @skb: inbound IPv6 frame
 * @nd_msg: pointer to NS message
 *
 * Learns source IPv6->MAC from SLLAO in Neighbor Solicitation.
 */
static int __proc_ipv6_ns_rx(struct extap_extn *ee, struct sk_buff *skb, void *hdr)
{
	struct nd_opt_hdr *noh = hdr;
	struct mac_hash_key k;

	if ((noh->nd_opt_len * 8) < NA_HDR_LEN) {
		ath12k_err(ee->ab, "extap: invalid NS option header\n");
		return -EINVAL;
	}

	mac_hash_v6_keygen(skb, &k, false);
	ether_addr_copy(k.mac_addr, (u8 *)(noh + 1));

	return mac_db_entry_add(extap_get_db(ee), k.mac_addr, &k);
}

/**
 * __proc_ipv6_na_rx() - Neighbor Advertisement RX handler
 * @ee: per-VAP ExtAP context
 * @skb: inbound IPv6 frame
 * @nd_msg: pointer to NA message
 *
 * Learns target IPv6->MAC from TLLAO in Neighbor Advertisement.
 */
static int __proc_ipv6_na_rx(struct extap_extn *ee, struct sk_buff *skb, void *hdr)
{
	struct nd_msg *nm = (struct nd_msg *)skb_transport_header(skb);
	struct nd_opt_hdr *noh = hdr;
	struct mac_hash_key k;

	if ((noh->nd_opt_len * 8) < NA_HDR_LEN) {
		ath12k_err(ee->ab, "extap: invalid NA option header\n");
		return -EINVAL;
	}

	k.is_v6 = true;
	k.ip.v6 = nm->target;
	ether_addr_copy(k.mac_addr, (u8 *)(noh + 1));

	return mac_db_entry_add(extap_get_db(ee), k.mac_addr, &k);
}

/**
 * __proc_ipv4_arp_rx() - ARP RX handler
 * @ee: per-VAP ExtAP context
 * @skb: inbound ARP frame
 *
 * Rewrites Ethernet DMAC and ARP THA for ARP Replies.
 */
static int __proc_ipv4_arp_rx(struct extap_extn *ee, struct sk_buff *skb)
{
	struct arphdr *arp = arp_hdr(skb);
	struct mac_db_entry *me;
	struct mac_hash_key k;
	struct arp_eth *ae;
	u8 arp_skip = 0;

	arp_skip += ntohs(arp->ar_hrd) != ARPHRD_ETHER;
	arp_skip += ntohs(arp->ar_pro) != ETH_P_IP;
	arp_skip += arp->ar_hln != ETH_ALEN;
	arp_skip += arp->ar_pln != sizeof(__be32);

	if (arp_skip)
		return -EINVAL;

	ae = (struct arp_eth *)((u8 *)arp + sizeof(*arp));
	k.is_v6 = false;

	/* Check for echo before broadcast skip: TX rewrites SHA to vap_mac for
	 * all ARP frames. When rootAP reflects the broadcast back, SHA=vap_mac.
	 * Rewrite SHA back to real client MAC
	 */
	if (ether_addr_equal(ae->sender_mac, ee->my_mac)) {
		memcpy(&k.ip.v4.s_addr, ae->sender_ip, sizeof(k.ip.v4.s_addr));
		me = mac_db_entry_get(extap_get_db(ee), &k);
		if (!me) {
			atomic64_inc(&ee->stats.rx_misses);
			return ENOENT;
		}

		ether_addr_copy(ae->sender_mac, me->mac_addr);
		mac_db_entry_deref(me);
	}

	/* Broadcast ARP requests need no DMAC xlate — pass through */
	switch (ntohs(arp->ar_op)) {
	case ARPOP_REQUEST:
	case ARPOP_RREQUEST:
		if (is_broadcast_ether_addr(eth_hdr(skb)->h_dest))
			return -EINVAL;
		break;
	default:
		break;
	}

	/* Change the DMAC to end client's MAC */
	memcpy(&k.ip.v4.s_addr, ae->target_ip, sizeof(k.ip.v4.s_addr));
	me = mac_db_entry_get(extap_get_db(ee), &k);
	if (!me) {
		atomic64_inc(&ee->stats.rx_misses);
		return ENOENT;
	}

	ether_addr_copy(eth_hdr(skb)->h_dest, me->mac_addr);
	ether_addr_copy(ae->target_mac, me->mac_addr);
	mac_db_entry_deref(me);

	return 0;
}

/**
 * __proc_ipv4_dhcp_rx() - DHCP Offer/Ack RX handler
 * @ee: per-VAP ExtAP context
 * @skb: inbound DHCP frame
 *
 * Seeds IP->MAC from yiaddr/chaddr and rewrites Ethernet DMAC.
 * Validates DHCP magic cookie before seeding to prevent table poisoning.
 */
static int __proc_ipv4_dhcp_rx(struct extap_extn *ee, struct sk_buff *skb)
{
	struct extap_dhcp_hdr *dhcph;
	struct mac_hash_key k;
	u8 skip_proc = 0;
	u32 hdr_offset;

	/* DHPC/BOOTP header starts after UDP header from transport header */
	hdr_offset = skb_transport_offset(skb) + sizeof(struct udphdr);
	dhcph = (struct extap_dhcp_hdr *)(skb->data + hdr_offset);

	skip_proc += !dhcph->yiaddr;
	skip_proc += (dhcph->op != DHCP_REPLY);
	skip_proc += is_zero_ether_addr(dhcph->chaddr);
	if (skip_proc)
		return -EINVAL;

	/* Validate DHCP magic cookie before seeding the table */
	if (ntohl(dhcph->cookie) != DHCP_MAGIC_COOKIE)
		return -EBADMSG;

	k.is_v6 = false;
	k.ip.v4.s_addr = dhcph->yiaddr;
	ether_addr_copy(k.mac_addr, dhcph->chaddr);
	mac_db_entry_add(extap_get_db(ee), k.mac_addr, &k);

	ether_addr_copy(eth_hdr(skb)->h_dest, dhcph->chaddr);
	return 0;
}

/**
 * __proc_ipv4_mcast_rx() - IPv4 multicast RX handler (first packet for group)
 * @ee: per-VAP ExtAP context
 * @skb: inbound IPv4 multicast frame
 *
 * For first packet: derives the 01:00:5e multicast MAC from destination IP
 * using ip_eth_mc_map(), seeds the table so subsequent packets for the same
 * group hit directly via fast path lookup.
 * For subsequent packets: lookup in DB returns stored multicast MAC.
 */
static void __proc_ipv4_mcast_rx(struct extap_extn *ee, struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	struct mac_db_entry *me;
	struct mac_hash_key k;

	mac_hash_v4_keygen(skb, &k, true);

	me = mac_db_entry_get(extap_get_db(ee), &k);
	if (me) {
		ether_addr_copy(eth_hdr(skb)->h_dest, me->mac_addr);
		mac_db_entry_deref(me);
		return;
	}

	atomic64_inc(&ee->stats.rx_misses);

	ip_eth_mc_map(iph->daddr, k.mac_addr);
	ether_addr_copy(eth_hdr(skb)->h_dest, k.mac_addr);

	mac_db_entry_add(extap_get_db(ee), k.mac_addr, &k);
}

/**
 * __proc_ipv6_mcast_rx() - IPv6 multicast RX handler (first packet for group)
 * @ee: per-VAP ExtAP context
 * @skb: inbound IPv6 multicast frame
 *
 * For first packet: derives the 33:33 multicast MAC from destination IP
 * using ipv6_eth_mc_map(), seeds the table so subsequent packets for the same
 * group hit directly via fast path lookup.
 * For subsequent packets: lookup in DB returns stored multicast MAC.
 *
 * Return: 0 on success, -EINVAL on error
 */
static void __proc_ipv6_mcast_rx(struct extap_extn *ee, struct sk_buff *skb)
{
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct mac_db_entry *me;
	struct mac_hash_key k;

	mac_hash_v6_keygen(skb, &k, true);

	me = mac_db_entry_get(extap_get_db(ee), &k);
	if (me) {
		ether_addr_copy(eth_hdr(skb)->h_dest, me->mac_addr);
		mac_db_entry_deref(me);
		return;
	}

	atomic64_inc(&ee->stats.rx_misses);

	ipv6_eth_mc_map(&ip6h->daddr, k.mac_addr);
	ether_addr_copy(eth_hdr(skb)->h_dest, k.mac_addr);

	mac_db_entry_add(extap_get_db(ee), k.mac_addr, &k);
}

/**
 * __proc_ipv4_rx - IPv4 RX handler
 * @ee: per-VAP ExtAP context
 * @db: per-VAP IP-MAC table
 * @skb: inbound IPv4 frame
 *
 * Handles DHCP before the broadcast guard (daddr may be 255.255.255.255).
 * Rewrites DMAC for unicast; seeds multicast MAC on first miss.
 */
static int __proc_ipv4_rx(struct extap_extn *ee, struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);
	size_t offset = ETH_HLEN + iph->ihl * 4;

	skb_set_transport_header(skb, offset);

	/* Skip processing local broadcast packets */
	if (unlikely(ipv4_is_lbcast(iph->daddr)))
		return -EINVAL;

	/*
	 * Process Multicast packets differently
	 */
	if (unlikely(ipv4_is_multicast(iph->daddr))) {
		__proc_ipv4_mcast_rx(ee, skb);
		return 0;
	}

	/*
	 * Packet is received from WLAN, it will have all the headers linearized
	 * in the first SKB segment
	 */
	if (iph->protocol == IPPROTO_UDP) {
		/* source port 67 = server reply */
		if (unlikely(ntohs(udp_hdr(skb)->source) == DHCP_SERVER_PORT))
			return __proc_ipv4_dhcp_rx(ee, skb);
	}

	return extap_xlate_dmac(ee, skb);
}

/**
 * __proc_ipv6_rx - IPv6 RX handler
 * @ee:  per-VAP ExtAP context
 * @skb: inbound IPv6 frame
 *
 * Learns from NS/NA. Rewrites DMAC for unicast; seeds multicast on first miss.
 */
static int __proc_ipv6_rx(struct extap_extn *ee, struct sk_buff *skb)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	const struct icmp6hdr *icmp6h;

	/* Multicast packet doesn't need DMAC xlate, replace with mcast mac */
	if (unlikely(ipv6_addr_is_multicast(&ip6h->daddr))) {
		__proc_ipv6_mcast_rx(ee, skb);
		return 0;
	}

	/*
	 * ICMPv6 packets with ND options has to be processed
	 * differently
	 */
	if (unlikely(ip6h->nexthdr == IPPROTO_ICMPV6)) {
		const int offset = ICMPV6_OFFSET;

		/* Re-fetch icmp6h after potential reallocation */
		skb_set_transport_header(skb, offset);
		icmp6h = (struct icmp6hdr *)skb_transport_header(skb);

		switch (icmp6h->icmp6_type) {
		case NDISC_NEIGHBOUR_SOLICITATION:
		case NDISC_NEIGHBOUR_ADVERTISEMENT:
			extap_parse_ndisc(ee, skb, ndisc_opts);
			break;
		default:
			break;
		}
	}

	return extap_xlate_dmac(ee, skb);
}

/**
 * __proc_eapol_rx - EAPOL RX handler
 * @ahvif: ath12k VIF context
 * @skb: inbound EAPOL frame
 *
 * EAPOL frames must reach wpa_supplicant via the VAP MAC, not the client MAC.
 * Rewrites Ethernet DMAC to the VAP MAC and increments RX conversion counter.
 */
static int __proc_eapol_rx(struct extap_extn *ee, struct sk_buff *skb)
{
	ether_addr_copy(eth_hdr(skb)->h_dest, ee->my_mac);
	return 0;
}

/**
 * ath12k_extap_rx_classify() - ExtAP RX entry point
 * @ahvif: ath12k VIF context
 * @skb: inbound Ethernet frame
 *
 * Called via ee->rx() under static_branch_unlikely(&extap_enabled).
 * Rewrites Ethernet DMAC to the real end device MAC.
 * Returns true if the frame should be dropped, false otherwise.
 */
int ath12k_extap_rx(struct ath12k_vif *ahvif, struct sk_buff *skb)
{
	int (*proc)(struct extap_extn *ee, struct sk_buff *skb);
	struct extap_extn *ee = &ahvif->dp_vif.dp_extn.extap;
	struct mac_hash_key k = {.is_v6 = false};
	struct ethhdr *eh;
	bool ip_pkt = true;
	int err = 0;

	skb_reset_mac_header(skb);
	skb_set_network_header(skb, ETH_HLEN);

	atomic64_inc(&ee->stats.rx_pkts);

	eh = eth_hdr(skb);

	/* Drop frames reflected back with our own VAP MAC as source —
	 * these are echoes of our TX broadcasts and would cause duplicates.
	 */
	if (ether_addr_equal(eh->h_source, ee->my_mac))
		return ENOENT;

	/* Parse the L3 protocol */
	switch (ntohs(eh->h_proto)) {
	case ETH_P_ARP:
		ip_pkt = false;
		proc = __proc_ipv4_arp_rx;
		break;

	case ETH_P_IP:
		mac_hash_v4_keygen(skb, &k, true);

		/* Fallback function */
		proc = __proc_ipv4_rx;
		break;

	case ETH_P_IPV6:
		mac_hash_v6_keygen(skb, &k, true);

		/* Fallback function */
		proc = __proc_ipv6_rx;
		break;

	case ETH_P_PAE:
		ip_pkt = false;
		proc = __proc_eapol_rx;
		break;

	default: /* Unhandled protocol */
		ip_pkt = false;
		atomic64_inc(&ee->stats.rx_unhandled);
		return 0;
	}

	/*
	 * Fast path of EXTAP; handling IP packets separately
	 */
	rcu_read_lock_bh();
	if (likely(ip_pkt)) {
		struct mac_db_entry *me = mac_db_entry_get(extap_get_db(ee), &k);
		if (unlikely(!me)) {
			atomic64_inc(&ee->stats.rx_misses);
			goto proc_pkt;
		}

		ether_addr_copy(eh->h_dest, me->mac_addr);
		mac_db_entry_deref(me);

		atomic64_inc(&ee->stats.rx_conv);
		rcu_read_unlock_bh();
		return 0;
	}

	/*
	 * Non-IP packets or miss in DB lookup
	 */
proc_pkt:

	/*
	 * Non-linear SKBs pose challenge in parsing; linearize the whole
	 * buffer to avoid calling pskb_may_pull for selective header copy
	 */
	if (skb_is_nonlinear(skb)) {
		if (skb_linearize(skb)) {
			rcu_read_unlock_bh();
			return 0;
		}
	}

	err = proc(ee, skb);
	atomic64_add(!err, &ee->stats.rx_conv);
	rcu_read_unlock_bh();

	return err > 0 ? err : 0;
}
EXPORT_SYMBOL(ath12k_extap_rx);

#endif /* CPTCFG_QCN_EXTN */
