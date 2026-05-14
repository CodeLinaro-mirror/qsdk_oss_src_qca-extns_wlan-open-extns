/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.*/

#ifndef ATH12K_SA_API_H
#define ATH12K_SA_API_H

#include "sa_api_defs.h"
#include "sa_core.h"

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

#endif /* ATH12K_SA_API_H */
