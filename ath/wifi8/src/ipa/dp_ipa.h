/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __ATH12K_WIFI8_EXTNS_DP_IPA_H__
#define __ATH12K_WIFI8_EXTNS_DP_IPA_H__

#include "../../../core.h"
#include "../../dp.h"

/**
 * enum ath12k_dp_wifi8_ipa2wbm_ring_e - IPA to WBM ring types
 * @ATH12K_DP_WIFI8_IPA2WBM_IPA: ring given to IPA for refill
 * @ATH12K_DP_WIFI8_IPA2WBM_HOST: ring used by HOST to refill err pkts
 * @ATH12K_DP_WIFI8_IPA2WBM_MAX: Maximum number of ring types
 */
enum ath12k_dp_wifi8_ipa2wbm_ring_e {
	ATH12K_DP_WIFI8_IPA2WBM_IPA,
	ATH12K_DP_WIFI8_IPA2WBM_HOST,
	ATH12K_DP_WIFI8_IPA2WBM_MAX,
};

/**
 * struct ath12k_dp_wifi8_ipa - WiFi8 IPA datapath context
 * @dp_wifi8: Pointer to the WiFi8 datapath structure
 * @ipa2wbm_idle_buf_ring: ring for providing initial rx bufs to IPA WBM pool
 * @ipa2wbm_ring: Array of IPA to WBM rings indexed by ring type
 * @reo2sw6: reo dst ring used by IPA for processing RX packets
 */
struct ath12k_dp_wifi8_ipa {
	struct ath12k_dp_wifi8 *dp_wifi8;
	struct dp_srng ipa2wbm_idle_buf_ring;
	struct dp_srng ipa2wbm_ring[ATH12K_DP_WIFI8_IPA2WBM_MAX];
	struct dp_srng reo2sw6;
};

#endif /* __ATH12K_WIFI8_EXTNS_DP_IPA_H__ */
