// SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <net/netlink.h>
#include <net/mac80211.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include <linux/etherdevice.h>
#include "../core.h"
#include "ath12k_cmn_extn.h"
#include "vendor_extn.h"
#include "../debug.h"
#include "../mac.h"
#include "esp_extn.h"
#include "../ini.h"
#include "../cmn_defs.h"
#include "mesh_util.h"
#include "../../../../../../net/mac80211/qcn_extns/cmn_extn.h"

int ath12k_vendor_mesh_params_config_extn(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     struct ath12k_wifi_generic_params *params)
{
	struct ieee80211_vif *vif = NULL;
	struct ieee80211_mesh_config_params_extn *mesh_params = NULL;
	int ret = -1;

	lockdep_assert_wiphy(wiphy);

	if (!params) {
		ath12k_err(NULL, "Mesh config params is NULL\n");
		return -EINVAL;
	}

	if (!params->data || !params->data_len) {
		ath12k_err(NULL, "Missing data for Mesh peer config\n");
		return -EINVAL;
	}

	if (params->data_len < sizeof(struct ieee80211_mesh_config_params_extn)) {
		ath12k_err(NULL, "Insufficient data length for mesh config\n");
		return -EINVAL;
	}

	vif = wdev_to_ieee80211_vif(wdev);
	if (!vif) {
		ath12k_err(NULL, "vif pointer is NULL\n");
		return -EINVAL;
	}

	mesh_params = (struct ieee80211_mesh_config_params_extn *)params->data;

	switch (mesh_params->cmd_type) {
	case ATH12K_EXTN_MESH_CONF_ADD_LOCAL_PEER:
		ret = ieee80211_vendor_mesh_add_local_peer(wiphy, vif, mesh_params);
		break;
	case ATH12K_EXTN_MESH_CONF_ALLOW_DATA:
		ret = ieee80211_vendor_mesh_authorize_peer(wiphy, vif, mesh_params->peer_mld_mac);
		break;
	case ATH12K_EXTN_MESH_CONF_DEL_LOCAL_PEER:
		ret = ieee80211_vendor_mesh_delete_local_peer(wiphy, vif, mesh_params->peer_mld_mac);
		break;
	default:
		ath12k_err(NULL, "Invalid cmd type: %d\n", mesh_params->cmd_type);
		return -EINVAL;
	}

	return ret;
}
