/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __ATH12K_WIFI8_EXTNS_DP_IPA_H__
#define __ATH12K_WIFI8_EXTNS_DP_IPA_H__

#include "../../../core.h"
#include "../../dp.h"
#include "../../hal.h"
#include "../../../qcn_extns/ipa/dp_ipa.h"

#define ATH12K_DP_WIFI8_REO2SW5_RDI	7

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

int ath12k_wifi8_dp_ipa_init(struct ath12k_base *ab);

#define ATH12K_DP_IPA_VP_MIN	64
#define ATH12K_DP_IPA_VP_MAX	255
#define ATH12K_DP_IPA_VP_RANGE	(ATH12K_DP_IPA_VP_MAX - ATH12K_DP_IPA_VP_MIN + 1)
/*Since AHVIF_ID starts from 1 to make vp number start from 64 we are substracting 1*/
#define ATH12K_DP_IPA_GET_VP_FROM_AHVIFID(__ahvifid) (__ahvifid + ATH12K_DP_IPA_VP_MIN - 1)
#define ATH12K_DP_IPA_GET_VP_IDX_FROM_VP(__vp) (__vp - ATH12K_DP_IPA_VP_MIN)
#define ATH12K_DP_IPA_VP_NUM_INVALID(vp_num) \
	(((vp_num) < ATH12K_DP_IPA_VP_MIN) || ((vp_num) > ATH12K_DP_IPA_VP_MAX))

/**
 * struct ath12k_dp_ipa_vp_profile - IPA PPE VP entry profile
 * @drop_prec_enable: Whether precedence-drop is enabled for this VP
 * @to_fw_sw: Whether packets should be routed to FW/SW path for this VP
 * @use_ppe_int_pri: Whether to use PPE internal priority for this VP
 * @search_idx_reg_num: Search-index register number used for VP programming
 * @vdev_id: VDEV identifier associated with this VP
 * @bank_id: PPE bank identifier used for VP programming
 * @lmac_id: LMAC identifier corresponding to the radio/PHY
 * @entry_valid: True if this VP profile entry is valid
*/
struct ath12k_ipa_vp_profile {
	u8 drop_prec_enable;
	u8 to_fw_sw;
	u8 use_ppe_int_pri;
	u8 search_idx_reg_num;
	u32 vdev_id;
	u32 bank_id;
	u32 lmac_id;
	bool entry_valid;
};

int ath12k_dp_ipa_alloc_vp_num(struct ath12k_vif *ahvif, struct net_device *dev);
void ath12k_dp_ipa_dealloc_vp_num(struct ath12k_vif *ahvif);
int ath12k_dp_ipa_get_vp_from_dev(struct net_device *dev);
struct net_device *ath12k_dp_ipa_get_netdev_from_vp(u8 vp_num);
void ath12k_dp_ipa_set_ppe_vp_entry_config(struct ath12k_ipa_vp_profile *vp_profile,
					   struct net_device *dev,
					   struct ath12k_link_vif *arvif);
void ath12k_dp_ipa_hal_tx_set_ppe_vp_entry(struct ath12k_base *ab,
					   u8 vp_num,
					   struct ath12k_ipa_vp_profile *vp_profile);

#endif /* __ATH12K_WIFI8_EXTNS_DP_IPA_H__ */
