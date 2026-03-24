/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.*/

#ifndef ATH12K_SA_CORE_H
#define ATH12K_SA_CORE_H

/* Smart Antenna Enable Mask */
#define SA_ENABLE_MASK			0x01
/* Indicates vap re-init */
#define SA_NEW_RECONFIG			0x00
/* Indicates vap init */
#define SA_NEW_CONFIG			0x01

/* Forward declaration */
struct ath12k_link_vif;

enum sa_state {
	SA_DEFAULT_STATE,
	SA_INIT_DONE_STATE,
	SA_DEINIT_DONE_STATE,
};

struct sa_ar_extn {
	u8 enable_mask;
	u8 sa_mode;
	u8 radio_id;
	enum sa_state state;
};

struct sa_base_extn {
	bool sa_enable;
	u8 sa_sw_test;
	u8 sa_support;
	s8 sa_mode_overwrite;
};

/**
 * sa_get_cfg_flags - Read Smart Antenna configuration flags
 * @ab: ath12k base pointer
 * @sa_en: output flag indicating if Smart Antenna is enabled by config
 * @sa_sw_test: output flag indicating Smart Antenna software test mode
 *
 * Fetches Smart Antenna related configuration from the driver config store
 * and updates the provided output flags. Safe to pass NULL for unused outputs.
 */
void sa_get_cfg_flags(struct ath12k_base *ab, u8 *sa_en, u8 *sa_sw_test);

/**
 * sa_core_init - Initialize Smart Antenna core for a vdev
 * @ar: ath12k device context
 * @arvif: link vif associated with the vdev
 * @init_flags: initialization flags (e.g. SA_NEW_CONFIG)
 *
 * Initializes Smart Antenna state for the given vdev, evaluates operating
 * mode, programs initial Tx/Rx antenna configuration and subscribes to PPDU
 * notifications when needed.
 *
 * Return: 0 on success, negative errno on failure.
 */
int sa_core_init(struct ath12k *ar,
		 struct ath12k_link_vif *arvif, u8 init_flags);

/**
 * sa_core_deinit - Deinitialize Smart Antenna core for a vdev
 * @ar: ath12k device context
 * @init_flags: deinitialization flags
 *
 * Tears down Smart Antenna state for the given vdev, optionally notifies the
 * algorithm implementation, disables firmware configuration and unregisters
 * PPDU notifications as required.
 *
 * Return: 0 on success, negative errno on failure.
 */
int sa_core_deinit(struct ath12k *ar, u8 init_flags);

#endif /* ATH12K_SA_CORE_H */
