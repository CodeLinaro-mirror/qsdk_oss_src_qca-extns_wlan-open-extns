/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.*/

#include "ini.h"

void sa_get_cfg_flags(struct ath12k_base *ab, u8 *sa_en, u8 *sa_sw_test)
{
	if (ab) {
		if (sa_en)
			*sa_en = !!ath12k_cfg_get(ab, ATH12K_CFG_SA_ENABLE);
		if (sa_sw_test)
			*sa_sw_test = ath12k_cfg_get(ab, ATH12K_CFG_SA_SW_TEST);
	}
}
