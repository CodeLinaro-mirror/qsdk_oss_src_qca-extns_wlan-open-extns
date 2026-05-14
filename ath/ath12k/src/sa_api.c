/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.*/

#include "../core.h"
#include "sa_api.h"
#include "../debug.h"

struct ath12k_smart_ant_ops *g_sa_ops;
atomic_t ag_sa_init;
static DEFINE_MUTEX(sa_ops_lock);

bool ath12k_smart_ant_enable(struct ath12k_base *ab)
{
	u8 wmi_sa_sw, wmi_sa_hw;
	bool sa_enable;
	u8 en_sa = 0, sa_sw_test = 0;

	if (!ab)
		return false;

	wmi_sa_sw =
		test_bit(WMI_TLV_SERVICE_SMART_ANTENNA_SW_SUPPORT, ab->wmi_ab.svc_map);
	wmi_sa_hw =
		test_bit(WMI_TLV_SERVICE_SMART_ANTENNA_HW_SUPPORT, ab->wmi_ab.svc_map);
	sa_get_cfg_flags(ab, &en_sa, &sa_sw_test);

	sa_enable = (en_sa && wmi_sa_sw && wmi_sa_hw) || (sa_sw_test && wmi_sa_sw);
	if (sa_enable)
		ath12k_dbg(ab, ATH12K_DBG_ANY,
			   "SA enabled; Host: en:%d test:%d  FW: sw:%d hw:%d\n",
			   en_sa, sa_sw_test, wmi_sa_sw, wmi_sa_hw);
	else
		ath12k_dbg(ab, ATH12K_DBG_ANY,
			   "SA disabled; Host: en:%d test:%d  FW: sw:%d hw:%d\n",
			   en_sa, sa_sw_test, wmi_sa_sw, wmi_sa_hw);

	return sa_enable;
}

int ath12k_register_smart_ant_ops(struct ath12k_smart_ant_ops *ops)
{
	if (!ops)
		return -EINVAL;

	mutex_lock(&sa_ops_lock);
	if (g_sa_ops) {
		mutex_unlock(&sa_ops_lock);
		return -EBUSY;
	}

	atomic_set(&ag_sa_init, 0);
	g_sa_ops = ops;
	mutex_unlock(&sa_ops_lock);

	return 0;
}
EXPORT_SYMBOL(ath12k_register_smart_ant_ops);

int ath12k_unregister_smart_ant_ops(void)
{
	int ret = 0;

	mutex_lock(&sa_ops_lock);
	if (atomic_read(&ag_sa_init) == 0)
		g_sa_ops = NULL;
	else
		ret = -EBUSY;
	mutex_unlock(&sa_ops_lock);

	return ret;
}
EXPORT_SYMBOL(ath12k_unregister_smart_ant_ops);
