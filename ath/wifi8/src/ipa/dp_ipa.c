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
#include "../../hal.h"

static u8 ath12k_wifi8_dp_ipa_get_wdi_version(void)
{
	return IPA_WDI_6;
}

static void
ath12k_wifi8_dp_ipa_rx_pipe_set_rdi(struct ath12k_ipa *ipa_ctx,
				    struct ipa_wdi_pipe_setup_info_smmu *rx_smmu)
{
	rx_smmu->rdi = ATH12K_DP_WIFI8_REO2SW5_RDI;
}

static void ath12k_wifi8_dp_ipa_srng_get_rsrc(struct ath12k_base *ab)
{
	/* TODO: fill rx_rsc and tx_rsc from here */
	return;
}

static void ath12k_wifi8_dp_ipa_set_tx_doorbell_paddr(struct ath12k_ipa *ipa_ctx)
{
	return;
}

static void ath12k_wifi8_dp_ipa_set_rx_doorbell_paddr(struct ath12k_ipa *ipa_ctx)
{
	return;
}

static void ath12k_wifi8_dp_ipa_tx_comp_ring_init_hp(struct ath12k_ipa *ipa_ctx)
{
	return;
}

static int ath12k_wifi8_dp_ipa_srng_setup(struct ath12k_base *ab)
{
	/* TODO: call RX and TX ring setup from here */
	return 0;
}

static int ath12k_wifi8_dp_ipa_srng_cleanup(struct ath12k_base *ab)
{
	/* TODO: call RX and TX ring cleanup from here */
	return 0;
}

static int
ath12k_wifi8_dp_ipa_set_ipa_ctx_for_all_ab(struct ath12k_ipa *ipa_ctx)
{
	struct ath12k_hw_group *ag;
	struct ath12k_base *ab;
	int i;

	ag = ath12k_core_get_ag();
	if (!ag) {
		ath12k_ipa_err(NULL, "ag is NULL");
		return -EINVAL;
	}

	for (i = 0; i < ag->num_devices; i++) {
		ab = ag->ab[i];
		if (!ab)
			continue;
		IPA_CTX(ab) = ipa_ctx;
	}
	return 0;
}

static struct ath12k_ipa_arch_ops ipa_wifi8_arch_ops = {
	.dp_ipa_init = ath12k_wifi8_dp_ipa_init,
	.dp_srng_cleanup = ath12k_wifi8_dp_ipa_srng_cleanup,
	.dp_srng_setup = ath12k_wifi8_dp_ipa_srng_setup,
	.dp_srng_get_rsrc = ath12k_wifi8_dp_ipa_srng_get_rsrc,
	.dp_rx_default_routing = NULL,
	.dp_rxdma_buf_setup = NULL,
	.dp_get_tx_bank_id = NULL,
	.dp_rx_buf_smmu_map_unmap = NULL,
	.ipa_set_rx_doorbell_paddr = ath12k_wifi8_dp_ipa_set_rx_doorbell_paddr,
	.ipa_set_tx_doorbell_paddr = ath12k_wifi8_dp_ipa_set_tx_doorbell_paddr,
	.ipa_tx_comp_ring_init_hp = ath12k_wifi8_dp_ipa_tx_comp_ring_init_hp,
	.ipa_tx_buffer_attach = NULL,
	.dp_rx_pipe_set_rdi = ath12k_wifi8_dp_ipa_rx_pipe_set_rdi,
	.ipa_get_wdi_version = ath12k_wifi8_dp_ipa_get_wdi_version,
};

int
ath12k_wifi8_dp_ipa_init(struct ath12k_base *ab)
{
	int ret;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8 = ath12k_get_dp_wifi8(dp);
	struct ath12k_ipa *ipa_ctx;

	ret = ath12k_dp_ipa_plugin_register_ops_extn(ab);
	if (ret) {
		ath12k_ipa_err(ab, "failed to allocate ipa_ctx");
		return ret;
	}

	ipa_ctx = IPA_CTX(ab);
	ipa_ctx->arch_ops = &ipa_wifi8_arch_ops;

	ret = ath12k_wifi8_dp_ipa_set_ipa_ctx_for_all_ab(ipa_ctx);
	if (ret) {
		ath12k_ipa_err(ab, "failed to set ipa_ctx for all ab");
		return ret;
	}

	dp_wifi8->extn.ipa = kmalloc(sizeof(struct ath12k_dp_wifi8_ipa), GFP_KERNEL);
	if (!dp_wifi8->extn.ipa)
		return -ENOMEM;
	memset(dp_wifi8->extn.ipa, 0, sizeof(struct ath12k_dp_wifi8_ipa));
	dp_wifi8->extn.ipa->dp_wifi8 = dp_wifi8;

	return ret;
}