/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "../../core.h"
#include "../../qcn_extns/ath12k_cmn_extn.h"

static const struct ieee80211_ops_extn ath12k_ops_wifi8_extn = {
	.validate_6ghz_chandef_extn = ath12k_validate_6ghz_chandef_extn,
};

void ath12k_wifi8_hw_init_extn(struct ath12k_base *ab)
{
	ab->ath12k_ops_extn = &ath12k_ops_wifi8_extn;
}
