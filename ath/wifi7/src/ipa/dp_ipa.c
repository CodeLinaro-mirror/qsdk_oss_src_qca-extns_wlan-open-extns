/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_ipa.h"
#include "../../../dp_peer.h"
#include "../../../peer.h"
#include "../../../mac.h"
#include "../../../dp.h"
#include "../../dp_tx.h"

u32 ath12k_wifi7_ipa_config_set(void)
{
	return 0x46d;
};

/* dummy api to be extended when driver intrabss is implemented */
bool
ath12k_wifi7_dp_ipa_tx(struct ath12k_base *ab, struct sk_buff *skb, bool is_mcbc)
{
	return false;
}

static const struct ipa_wifi_ops ipa_wifi7_ops = {
	.tx = ath12k_wifi7_dp_ipa_tx,
};

void ath12k_wifi7_dp_ipa_init(struct ath12k_base *ab)
{
	if (!ab->ath12k_base_extn.ipa_ctx)
		return;

	ab->ath12k_base_extn.ipa_ctx->wifi_hw_ops = &ipa_wifi7_ops;
}
EXPORT_SYMBOL(ath12k_wifi7_dp_ipa_init);
