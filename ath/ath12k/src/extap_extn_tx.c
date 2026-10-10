// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/if_arp.h>
#include <linux/icmpv6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rcupdate.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <asm/unaligned.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ndisc.h>

#include "../core.h"
#include "../debug.h"
#include "extap_db.h"
#include "extap_extn.h"

#ifdef CPTCFG_QCN_EXTN

/**
 * __proc_arp_tx() - ARP TX handler
 * @ee: per-VAP ExtAP context
 * @skb: outgoing ARP frame
 *
 * Learns sender IP->MAC, replaces the ARP SHA and Ethernet SMAC with
 * the STA VAP MAC.
 *
 * Return: 0 on success, -EINVAL on error
 */
static int __proc_ipv4_arp_tx(struct extap_extn *ee, struct sk_buff *skb)
{
	struct mac_hash_key k;
	struct arphdr *arp;
	struct arp_eth *ae;
	u8 skip_tx = 0;

	arp = arp_hdr(skb);
	skip_tx += ntohs(arp->ar_hrd) != ARPHRD_ETHER;
	skip_tx += ntohs(arp->ar_pro) != ETH_P_IP;
	if (skip_tx)
		return -EINVAL;

	ae = (struct arp_eth *)((u8 *)arp + sizeof(*arp));

	k.is_v6 = false;
	ether_addr_copy(k.mac_addr, ae->sender_mac);
	memcpy(&k.ip.v4.s_addr, ae->sender_ip, sizeof(k.ip.v4.s_addr));

	if (extap_learn(ee, &k)) {
		atomic64_inc(&ee->stats.tx_learn_error);
		return -EINVAL;
	}

	ether_addr_copy(ae->sender_mac, ee->my_mac);
	ether_addr_copy(eth_hdr(skb)->h_source, ee->my_mac);

	return 0;
}

/**
 * __proc_ipv4_dhcp_tx() - DHCP Discover/Request TX handler
 * @ee: per-VAP ExtAP context
 * @skb: outgoing DHCP frame
 *
 * Sets the BOOTP broadcast flag on DHCP Discover/Request frames to ensure
 * the server replies to broadcast, allowing the reply to reach the client
 * before an IP is assigned. Validates DHCP magic cookie before modifying.
 * Recomputes UDP checksum after flag modification.
 * Learns source IPv4->MAC and replaces Ethernet SMAC with VAP MAC.
 *
 * Return: 0 on success, -EINVAL on error
 */
static int __proc_ipv4_dhcp_tx(struct extap_extn *ee, struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct udphdr *udph = udp_hdr(skb);
	struct extap_dhcp_hdr *dhcph;
	int err = 0;

	/* Re-fetch iph after potential skb modifications */
	dhcph = (struct extap_dhcp_hdr *)(udph + 1);

	/* Validate DHCP magic cookie before modifying */
	if (ntohl(dhcph->cookie) != DHCP_MAGIC_COOKIE)
		return -EBADMSG;

	/* Donot handle non-Boot message */
	if (dhcph->op != DHCP_BOOT_REQ)
		return 0;

	/* Set broadcast flag to ensure server replies to broadcast */
	dhcph->flags |= DHCP_BROADCAST_FLAG;

	/* Recompute UDP checksum after flag modification */
	udph->check = csum_tcpudp_magic(iph->saddr, iph->daddr,
					ntohs(udph->len), IPPROTO_UDP,
					csum_partial(udph, ntohs(udph->len), 0));

	return err;
}

/*
 * __proc_ipv6_na_tx() - IPv6 NA TX handler
 * @ee: per-VAP ExtAP context
 * @skb: outgoing NA frame
 * @hdr: pointer to ND option header
 *
 * Replaces Target Link-Layer Address (TLLA) in NA packets with VAP MAC.
 * Handles both multicast and unicast NA replies.
 */
static int __proc_ipv6_na_tx(struct extap_extn *ee, struct sk_buff *skb, void *hdr)
{
	struct nd_opt_hdr *noh = hdr;
	u8 *lladdr = (u8 *)(noh + 1);

	/* Replace TLLA with VAP MAC for all NA packets */
	if ((noh->nd_opt_len * 8) >= NA_HDR_LEN)
		ether_addr_copy(lladdr, ee->my_mac);

	return 0;
}

static int __proc_ipv6_ns_tx(struct extap_extn *ee, struct sk_buff *skb, void *hdr)
{
	struct nd_opt_hdr *noh = hdr;
	u8 *lladdr = (u8 *)(noh + 1);

	if ((noh->nd_opt_len * 8) >= NA_HDR_LEN)
		ether_addr_copy(lladdr, ee->my_mac);

	return 0;
}

static const struct extap_ndisc_opts ndisc_tx_opts[__ND_OPT_ARRAY_MAX] = {
	[ND_OPT_SOURCE_LL_ADDR] = {.proc = __proc_ipv6_ns_tx},
	[ND_OPT_TARGET_LL_ADDR] = {.proc = __proc_ipv6_na_tx},
};

/**
 * __proc_ipv6_icmp_tx() - ICMPv6 TX handler (NS/NA)
 * @ee: per-VAP ExtAP context
 * @skb: outgoing ICMPv6 frame
 *
 * Handles Neighbor Solicitation (NS) and Neighbor Advertisement (NA) frames:
 * - NS: replaces Source Link-Layer Address (SLLA) unconditionally
 * - NA: replaces Target Link-Layer Address (TLLA) only when target is multicast
 *   or Override flag is set.
 *
 * Learns source IPv6->MAC mapping and replaces Ethernet SMAC with VAP MAC.
 * Recomputes ICMPv6 checksum if payload was modified.
 *
 * Return: 0 on success, -EINVAL on error
 */
static int __proc_ipv6_icmp_tx(struct extap_extn *ee, struct sk_buff *skb)
{
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct icmp6hdr *icmp6h;
	int err = 0;
	int len;

	icmp6h = (struct icmp6hdr *)skb_transport_header(skb);
	switch (icmp6h->icmp6_type) {
	case NDISC_NEIGHBOUR_SOLICITATION:
	case NDISC_NEIGHBOUR_ADVERTISEMENT:
		err = extap_parse_ndisc(ee, skb, ndisc_tx_opts);
		if (err < 0)
			return err;

		break;

	default:
		break;
	}

	/*
	 * We may have changed the ICMP payload, hence recompute the csum.
	 * Must zero the checksum field first so it does not feed into csum_partial.
	 */
	len = ntohs(ip6h->payload_len);
	icmp6h->icmp6_cksum = 0;
	icmp6h->icmp6_cksum = csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
					      len, IPPROTO_ICMPV6,
					      csum_partial(icmp6h, len, 0));

	return 0;
}

/**
 * __proc_ipv4_tx() - IPv4 TX handler
 * @ee: per-VAP ExtAP context
 * @skb: outgoing IPv4 frame
 *
 * Routes DHCP Discover/Request frames to __proc_ipv4_dhcp_tx() for special handling.
 * For other IPv4 frames, learns source IPv4->MAC mapping and replaces
 * Ethernet SMAC with VAP MAC. Skips learning for broadcast and multicast addresses.
 */
static int __proc_ipv4_tx(struct extap_extn *ee, struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct mac_hash_key k;
	u8 skip_tx = 0;
	int err = 0;

	/* Learn source IPv4->MAC for non-broadcast, non-multicast addresses */
	skip_tx += ipv4_is_lbcast(iph->saddr);
	skip_tx += ipv4_is_multicast(iph->saddr);
	if (skip_tx)
		return -EINVAL;

	/*
	 * Check for the DHCP packet
	 */
	if (iph->protocol == IPPROTO_UDP) {
		size_t offset = ((u8 *)iph - skb->data);
		skb_set_transport_header(skb, offset + (iph->ihl * 4));

		if (ntohs(udp_hdr(skb)->dest) == DHCP_SERVER_PORT) {
			err = __proc_ipv4_dhcp_tx(ee, skb);
			if (err < 0)
				return err;
		}
	}

	mac_hash_v4_keygen(skb, &k, false);
	ether_addr_copy(k.mac_addr, eth_hdr(skb)->h_source);

	/*
	 * Learn and update the MAC_DB with the IP --> SMAC tuple
	 */
	if (extap_learn(ee, &k)) {
		atomic64_inc(&ee->stats.tx_learn_error);
		return -EINVAL;
	}

	/* Always replace Ethernet SMAC with VAP MAC */
	ether_addr_copy(eth_hdr(skb)->h_source, ee->my_mac);
	return 0;
}

/**
 * __ipv6_is_ndisc() - check if an IPv6 skb is NS or NA
 * @skb: outgoing frame (network header already set to ETH_HLEN)
 *
 * NS/NA must bypass the TX fast path so __proc_ipv6_tx() runs and
 * replaces the SLLA/TLLA option with the VAP MAC. The fast path only
 * rewrites SMAC and skips proc().
 *
 * Return: true if the packet is NS or NA, false otherwise.
 */
static bool __ipv6_is_ndisc(struct sk_buff *skb)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	const struct icmp6hdr *icmp6h;

	if (ip6h->nexthdr != IPPROTO_ICMPV6)
		return false;

	icmp6h = (const struct icmp6hdr *)(ip6h + 1);
	return icmp6h->icmp6_type == NDISC_NEIGHBOUR_SOLICITATION ||
	       icmp6h->icmp6_type == NDISC_NEIGHBOUR_ADVERTISEMENT;
}

/**
 * __proc_ipv6_tx() - IPv6 TX handler
 * @ee: per-VAP ExtAP context
 * @skb: outgoing IPv6 frame
 *
 * Routes ICMPv6/NDP frames to __proc_ipv6_icmp_tx() for special handling.
 * For other IPv6 frames, learns source IPv6->MAC mapping and replaces
 * Ethernet SMAC with VAP MAC. Skips learning for multicast and any addresses.
 */
static int __proc_ipv6_tx(struct extap_extn *ee, struct sk_buff *skb)
{
	const struct ipv6hdr *ip6h;
	struct mac_hash_key k;
	u8 skip_tx = 0;
	int offset;
	u8 nexthdr;
	__be16 frag_off;

	ip6h = ipv6_hdr(skb);
	nexthdr = ip6h->nexthdr;

	skip_tx += ipv6_addr_any(&ip6h->saddr);
	skip_tx += ipv6_addr_is_multicast(&ip6h->saddr);
	if (skip_tx)
		return -EINVAL;

	/*
	 * Find the transport header by skipping IPv6 extension headers.
	 * Offset must start from skb->data, so include the Ethernet header.
	 */
	offset = ipv6_skip_exthdr(skb, skb_network_offset(skb) + sizeof(*ip6h),
				  &nexthdr, &frag_off);
	if (offset < 0)
		return -E2BIG;

	skb_set_transport_header(skb, offset);
	if (nexthdr == IPPROTO_ICMPV6) {
		int icmp_err = __proc_ipv6_icmp_tx(ee, skb);
		if (icmp_err < 0)
			return icmp_err;
	}

	mac_hash_v6_keygen(skb, &k, false);
	ether_addr_copy(k.mac_addr, eth_hdr(skb)->h_source);

	/* Learn source IPv6->MAC for non-multicast, non-any addresses */
	if (extap_learn(ee, &k)) {
		atomic64_inc(&ee->stats.tx_learn_error);
		return -EINVAL;
	}

	/* Always replace Ethernet SMAC with VAP MAC */
	ether_addr_copy(eth_hdr(skb)->h_source, ee->my_mac);
	return 0;
}

/**
 * __proc_misc_tx() - Miscellaneous TX handler
 * @ee: per-VAP ExtAP context
 * @skb: outgoing frame
 *
 * Handles all other ether_types (PAE, LLDP, etc).
 * Replaces Ethernet SMAC with VAP MAC.
 *
 * Return: 0 on success
 * FIXME: For non-IP how do we handle return direction
 */
static int __proc_misc_tx(struct extap_extn *ee, struct sk_buff *skb)
{
	ether_addr_copy(eth_hdr(skb)->h_source, ee->my_mac);
	return 0;
}

/**
 * ath12k_extap_tx() - ExtAP TX classify entry point
 * @ahvif: ath12k VIF context
 * @skb: outgoing Ethernet frame
 *
 * Called via ee->tx() under static_branch_unlikely(&extap_enabled).
 * Learns src IP->MAC and replaces Ethernet SMAC with the STA VAP MAC.
 */
void ath12k_extap_tx(struct ath12k_vif *ahvif, struct sk_buff *skb)
{
	int (*proc)(struct extap_extn *ee, struct sk_buff *skb);
	struct extap_extn *ee = &ahvif->dp_vif.dp_extn.extap;
	struct ethhdr *eh = eth_hdr(skb);
	struct mac_hash_key k;
	bool use_fastpath = true;
	int err = 0;

	atomic64_inc(&ee->stats.tx_pkts);

	/* Parse the L3 protocol */
	switch (ntohs(eh->h_proto)) {
	case ETH_P_ARP:
		use_fastpath = false;
		proc = __proc_ipv4_arp_tx;
		break;
	case ETH_P_IP:
		mac_hash_v4_keygen(skb, &k, false);
		ether_addr_copy(k.mac_addr, eth_hdr(skb)->h_source);
		skb_set_network_header(skb, ETH_HLEN);

		/* Fallback function */
		proc = __proc_ipv4_tx;
		break;
	case ETH_P_IPV6:
		mac_hash_v6_keygen(skb, &k, false);
		ether_addr_copy(k.mac_addr, eth_hdr(skb)->h_source);
		skb_set_network_header(skb, ETH_HLEN);

		if (__ipv6_is_ndisc(skb))
			use_fastpath = false;

		/* Fallback function */
		proc = __proc_ipv6_tx;
		break;
	default:
		use_fastpath = false;
		proc = __proc_misc_tx;
		break;
	}

	/*
	 * Fast path of EXTAP; handling IP packets separately
	 */
	rcu_read_lock_bh();
	if (likely(use_fastpath)) {
		struct mac_db_entry *me = mac_db_entry_get(extap_get_db(ee), &k);
		if (likely(me)) {
			mac_db_entry_deref(me);
			ether_addr_copy(eh->h_source, ee->my_mac);
			goto done;
		}
	}

	/*
	 * Non-linear SKBs pose challenge in parsing; linearize the whole
	 * buffer to avoid calling pskb_may_pull for selective header copy
	 */
	if (skb_is_nonlinear(skb)) {
		if (skb_linearize(skb)) {
			rcu_read_unlock_bh();
			return;
		}
	}

	err = proc(ee, skb);
	if (unlikely(err)) {
		rcu_read_unlock_bh();
		return;
	}
done:
	atomic64_inc(&ee->stats.tx_conv);
	rcu_read_unlock_bh();
}
EXPORT_SYMBOL(ath12k_extap_tx);

#endif /* CPTCFG_QCN_EXTN */
