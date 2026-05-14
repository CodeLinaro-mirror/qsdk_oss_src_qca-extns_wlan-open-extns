/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "../../core.h"
#include "../../dp_peer.h"

#ifdef CPTCFG_EXT_IPA_OFFLOAD
void ath12k_dp_ipa_peer_notify(struct ath12k *ar,
			       struct ath12k_dp_link_peer *peer,
			       struct ath12k_link_vif *arvif,
			       u32 vdev_id, bool connect);

void ath12k_dp_ipa_vif_notify(struct ath12k_link_vif *arvif,
			      bool restart, bool connect);

int ath12k_dp_ipa_hw_group_init(struct ath12k_base *ab,
				struct ath12k_dp_hw_group *dp_hw_grp);

void ath12k_dp_ipa_hw_group_deinit(struct ath12k_dp_hw_group *dp_hw_grp);
#else
static inline void
ath12k_dp_ipa_peer_notify(struct ath12k *ar,
			  struct ath12k_dp_link_peer *peer,
			  struct ath12k_link_vif *arvif,
			  u32 vdev_id, bool connect)
{
}

static inline void
ath12k_dp_ipa_vif_notify(struct ath12k_link_vif *arvif,
			 bool restart, bool connect)
{
}

static inline int
ath12k_dp_ipa_hw_group_init(struct ath12k_base *ab,
			    struct ath12k_dp_hw_group *dp_hw_grp)
{
	return 0;
}

static inline void
ath12k_dp_ipa_hw_group_deinit(struct ath12k_dp_hw_group *dp_hw_grp)
{
}
#endif /* CPTCFG_EXT_IPA_OFFLOAD */
