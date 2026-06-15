/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/skbuff.h>
#include <net/mac80211.h>
#include "cmn_extn.h"

/*
 * Extended monitor RX notifier chain.
 *
 * Uses SRCU (Sleepable RCU) notifier chain because:
 *   - Notifications are dispatched from rxmon workqueue (process) context,
 *     so callbacks are allowed to sleep.
 *   - SRCU read-side critical sections are preemptible, making it
 *     suitable for potentially long-running packet processing callbacks.
 *   - Provides RCU-like protection without the atomic constraint of
 *     the PPDU atomic notifier chain above.
 *
 * Ownership semantics:
 *   The driver always frees the MPDU SKB after srcu_notifier_call_chain()
 *   returns. Listeners that need to retain the frame beyond the callback
 *   MUST call skb_clone() inside the callback and take ownership of the
 *   clone; they must NOT free or hold a reference to the original SKB.
 */
SRCU_NOTIFIER_HEAD_STATIC(ieee80211_ext_mon_rx_notifier_chain);

/* Listener count for fast has-listeners check in the RX hot path */
static atomic_t ieee80211_ext_mon_rx_listener_count = ATOMIC_INIT(0);

/**
 * ieee80211_register_ext_mon_rx_notifier_extn - Register extended monitor RX listener
 * @nb: Notifier block with callback function
 *
 * Registers a listener for ext_mon_event_type events. Multiple
 * listeners may be registered simultaneously. Each callback is invoked
 * from the rxmon workqueue (process) context and may sleep.
 *
 * The driver frees the MPDU SKB after the chain returns. Listeners that
 * need to retain the frame must call skb_clone() and free the clone
 * after use.
 *
 * Return: 0 on success, negative errno on failure
 */
int ieee80211_register_ext_mon_rx_notifier_extn(struct notifier_block *nb)
{
	int ret;

	if (!nb)
		return -EINVAL;

	pr_debug("ext_mon RX notifier register\n");

	ret = srcu_notifier_chain_register(&ieee80211_ext_mon_rx_notifier_chain, nb);
	if (ret)
		return ret;

	atomic_inc(&ieee80211_ext_mon_rx_listener_count);
	return 0;
}
EXPORT_SYMBOL(ieee80211_register_ext_mon_rx_notifier_extn);

/**
 * ieee80211_unregister_ext_mon_rx_notifier_extn - Unregister extended monitor RX listener
 * @nb: Notifier block to remove
 *
 * Return: 0 on success, negative errno on failure
 */
int ieee80211_unregister_ext_mon_rx_notifier_extn(struct notifier_block *nb)
{
	int ret;

	if (!nb)
		return -EINVAL;

	pr_debug("ext_mon RX notifier unregister\n");

	ret = srcu_notifier_chain_unregister(&ieee80211_ext_mon_rx_notifier_chain, nb);
	if (!ret)
		atomic_dec(&ieee80211_ext_mon_rx_listener_count);

	return ret;
}
EXPORT_SYMBOL(ieee80211_unregister_ext_mon_rx_notifier_extn);

/**
 * ieee80211_ext_mon_rx_notifier_has_listeners_extn - Check for active RX ext_mon listeners
 *
 * Used by the RX extended monitor data path to decide whether to dispatch
 * the MPDU via the RX notifier chain or fall back to mac80211 delivery.
 *
 * Return: true if at least one RX listener is registered, false otherwise
 */
bool ieee80211_ext_mon_rx_notifier_has_listeners_extn(void)
{
	return atomic_read(&ieee80211_ext_mon_rx_listener_count) > 0;
}
EXPORT_SYMBOL(ieee80211_ext_mon_rx_notifier_has_listeners_extn);

/**
 * ieee80211_ext_mon_rx_notifier_call_extn - Dispatch extended monitor RX event
 * @type: Event type (enum ieee80211_ext_mon_event_type_extn)
 * @event: Event payload (struct ieee80211_ext_mon_rx_event_extn *)
 *
 * Invokes all registered RX listeners via the SRCU notifier chain.
 *
 * Return: NOTIFY_DONE / NOTIFY_OK / NOTIFY_STOP_MASK
 */
int
ieee80211_ext_mon_rx_notifier_call_extn(enum ieee80211_ext_mon_event_type_extn type,
					struct ieee80211_ext_mon_rx_event_extn *event)
{
	return srcu_notifier_call_chain(&ieee80211_ext_mon_rx_notifier_chain,
					type, event);
}
EXPORT_SYMBOL(ieee80211_ext_mon_rx_notifier_call_extn);
