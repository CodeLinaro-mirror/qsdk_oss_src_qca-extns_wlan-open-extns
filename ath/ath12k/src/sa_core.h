/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.*/

#ifndef ATH12K_SA_CORE_H
#define ATH12K_SA_CORE_H

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

#endif /* ATH12K_SA_CORE_H */
