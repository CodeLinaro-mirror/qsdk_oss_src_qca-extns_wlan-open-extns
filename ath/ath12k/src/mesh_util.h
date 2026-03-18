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
 */
enum ath12k_extn_mesh_conf_cmd_type {
	ATH12K_EXTN_MESH_CONF_ADD_LOCAL_PEER = 1,
	ATH12K_EXTN_MESH_CONF_ALLOW_DATA = 2,
	ATH12K_EXTN_MESH_CONF_DEL_LOCAL_PEER = 3,
};
