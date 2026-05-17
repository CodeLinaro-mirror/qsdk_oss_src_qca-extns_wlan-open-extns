/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.*/

#ifndef ATH12K_SA_API_H
#define ATH12K_SA_API_H

#include "../vendor.h"
#include "sa_api_defs.h"
#include "sa_core.h"

/**
 * ath12k_smart_ant_is_enable - Query whether Smart Antenna is enabled
 * @ab: ath12k base pointer
 *
 * Checks the cached enable state for Smart Antenna feature based on host cfg
 * and firmware capability detection performed during wmi init.
 *
 * Return: true if Smart Antenna is enabled, false otherwise.
 */
bool ath12k_smart_ant_is_enable(struct ath12k_base *ab);

/**
 * ath12k_smart_ant_enable - Evaluate and set Smart Antenna enable state
 * @ab: ath12k base pointer
 *
 * Evaluates host configuration flags and firmware advertised services to decide
 * whether Smart Antenna should be enabled. Updates the internal base extension
 * state accordingly and emits an informational log.
 *
 * Return: true if Smart Antenna gets enabled, false otherwise.
 */
bool ath12k_smart_ant_enable(struct ath12k_base *ab);

/**
 * ath12k_register_smart_ant_ops - Register Smart Antenna algorithm hooks
 * @ops: algorithm operations to register
 *
 * Registers the implementation of Smart Antenna algorithm callbacks used by
 * the core for per-radio and per-peer operations such as antenna selection and
 * training planning. Typically called by a module providing the algorithm.
 *
 * Return: 0 on success, negative errno on failure.
 */
int ath12k_register_smart_ant_ops(struct ath12k_smart_ant_ops *ops);

/**
 * ath12k_unregister_smart_ant_ops - Unregister Smart Antenna algorithm hooks
 *
 * Unregisters the Smart Antenna algorithm when no radios are initialized
 * under Smart Antenna (based on an internal init counter). Intended to be
 * called by the provider module at unload time.
 *
 * Return: 0 on success, negative errno on failure.
 */
int ath12k_unregister_smart_ant_ops(void);

/**
 * ath12k_wmi_peer_ratecode_list_event_handler - Handle WMI RC list event
 * @ab: ath12k base context
 * @skb: event skb from firmware containing peer ratecode list
 *
 * Processes the peer ratecode list event from firmware and forwards it to the
 * Smart Antenna core handler to update per-peer ratecode state.
 */
static inline
void ath12k_wmi_peer_ratecode_list_event_handler(struct ath12k_base *ab,
						 struct sk_buff *skb)
{
	sa_core_wmi_peer_rc_list_ev_handler(ab, skb);
}

/**
 * ath12k_smart_ant_api_peer_disconnect - Notify SA of peer disconnection
 * @arsta: ath12k link sta corresponding to the peer
 *
 * Notifies Smart Antenna core that a peer has disconnected so that any
 * per-peer state can be cleaned up. This wraps
 * sa_core_peer_disconnect_notify().
 */
static inline
void ath12k_smart_ant_api_peer_disconnect(struct ath12k_link_sta *arsta)
{
	sa_core_peer_disconnect_notify(arsta);
}
#endif /* ATH12K_SA_API_H */
