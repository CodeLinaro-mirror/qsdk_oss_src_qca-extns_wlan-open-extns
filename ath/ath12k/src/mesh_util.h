/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include "../vendor.h"

/**
 * enum ath12k_extn_mesh_conf_cmd_type: Mesh peer config command types
 *
 * @ATH12K_EXTN_MESH_CONF_ADD_LOCAL_PEER: Add local mesh peer command
 * @ATH12K_EXTN_MESH_CONF_ALLOW_DATA: Authorize local mesh peer command
 * @ATH12K_EXTN_MESH_CONF_DEL_LOCAL_PEER: Delete local mesh peer command
 * @ATH12K_EXTN_MESH_CONF_SET_PEER_TIMEOUT_CNT: Configure Peer timeout count
 * @ATH12K_EXTN_MESH_CONF_PEER_TIMEOUT_EN: Configure mesh peer cleanup timer
 * @ATH12K_EXTN_MESH_CONF_PEER_DUMP: Dump mesh peer information
 */
enum ath12k_extn_mesh_conf_cmd_type {
	ATH12K_EXTN_MESH_CONF_ADD_LOCAL_PEER = 1,
	ATH12K_EXTN_MESH_CONF_ALLOW_DATA = 2,
	ATH12K_EXTN_MESH_CONF_DEL_LOCAL_PEER = 3,
	ATH12K_EXTN_MESH_CONF_SET_PEER_TIMEOUT_CNT = 4,
	ATH12K_EXTN_MESH_CONF_PEER_TIMEOUT_EN = 5,
	ATH12K_EXTN_MESH_CONF_PEER_DUMP = 6,
};
