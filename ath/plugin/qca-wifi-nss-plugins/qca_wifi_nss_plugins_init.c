/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <qca_wifi_nss_plugins.h>
#include <qca_wifi_nss_plugins_ecm.h>
#include <qca_wifi_nss_plugins_ppe.h>
#include <qca_wifi_nss_plugins_ppeds.h>
#include <ppe_public.h>

/*
 * qca_wifi_nss_plugin_init_module()
 *	QCA_WIFI_PLUGIN module init function
 */
int __init qca_wifi_nss_plugin_init_module(void)
{
	int ret = 0;

	ret = qca_wifi_nss_plugins_emesh_register();
	if (ret) {
		qca_wifi_nss_plugins_info_always("EMESH callback registration failed\n");
		return ret;
	}

	ret = qca_wifi_nss_plugins_fse_cb_register();
	if (ret) {
		qca_wifi_nss_plugins_info_always("FSE callback registration failed\n");
		goto fail_fse_cb_register;
	}

	ret = qca_wifi_nss_plugins_wifi_cb_register();
	if (ret) {
		qca_wifi_nss_plugins_info_always("WIFI callback registration failed\n");
		goto fail_wifi_cb_register;
	}

	ret = qca_wifi_nss_plugins_ppe_fse_cb_register();
	if (ret) {
		qca_wifi_nss_plugins_info_always("PPE_Plugin callback registration failed\n");
		goto fail_ppe_fse__cb_register;
	}

	ret = qca_wifi_nss_plugins_ppeds_cb_register();
	if (ret) {
		qca_wifi_nss_plugins_info_always("PPEDS_Plugin callback registration failed\n");
		goto fail_ppeds_cb_register;
	}

#ifdef QCA_WIFI_NSS_PLUGINS_MSCS
	ret = qca_wifi_nss_plugins_mscs_register();
	if (ret) {
		qca_wifi_nss_plugins_info_always("MSCS_Plugin callback registration failed\n");
		goto fail_mscs_register;
	}
#endif

	ret = qca_wifi_nss_plugins_nl_event_start();
	if (ret) {
		qca_wifi_nss_plugins_info_always("NL_Event listening start failed\n");
		goto fail_nl_event_start;
	}

	qca_wifi_nss_plugins_info_always("QCA_WIFI_PLUGIN module loaded\n");
	return 0;

fail_nl_event_start:
#ifdef QCA_WIFI_NSS_PLUGINS_MSCS
	qca_wifi_nss_plugins_mscs_unregister();
fail_mscs_register:
#endif
	qca_wifi_nss_plugins_ppeds_cb_unregister();
fail_ppeds_cb_register:
	qca_wifi_nss_plugins_ppe_fse_cb_unregister();
fail_ppe_fse__cb_register:
	qca_wifi_nss_plugins_wifi_cb_unregister();
fail_wifi_cb_register:
	qca_wifi_nss_plugins_fse_cb_unregister();
fail_fse_cb_register:
	qca_wifi_nss_plugins_emesh_unregister();
	return ret;
}

/*
 * qca_wifi_nss_plugin_exit_module()
 *	QCA_WIFI_PLUGIN module exit function
 */
static void __exit qca_wifi_nss_plugin_exit_module(void)
{
	qca_wifi_nss_plugins_nl_event_stop();
#ifdef QCA_WIFI_NSS_PLUGINS_MSCS
	qca_wifi_nss_plugins_mscs_unregister();
#endif
	qca_wifi_nss_plugins_ppeds_cb_unregister();
	qca_wifi_nss_plugins_ppe_fse_cb_unregister();
	qca_wifi_nss_plugins_wifi_cb_unregister();
	qca_wifi_nss_plugins_fse_cb_unregister();
	qca_wifi_nss_plugins_emesh_unregister();

	qca_wifi_nss_plugins_info_always("QCA_WIFI_PLUGIN unloaded\n");
}

module_init(qca_wifi_nss_plugin_init_module);
module_exit(qca_wifi_nss_plugin_exit_module);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("QCA_WIFI_PLUGIN module");
