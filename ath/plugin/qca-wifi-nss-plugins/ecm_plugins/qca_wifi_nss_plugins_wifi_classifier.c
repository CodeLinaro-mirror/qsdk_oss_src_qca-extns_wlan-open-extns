/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <linux/version.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/module.h>

#include <qca_wifi_nss_plugins.h>
#include <qca_wifi_nss_plugins_ecm.h>

/*
 * qca_wifi_nss_plugins_get_wifi_metadata()
 *	Fill the wifi metadata and call into wlan driver.
 */
static inline uint32_t qca_wifi_nss_plugins_get_wifi_metadata(struct ecm_classifier_wifi_metadata *wifi_metadata)
{
	struct ath_dp_metadata_param ath_dp_mdata = {0};
	uint32_t skb_mark = 0;

	ath_dp_mdata.mlo_param.in_dest_dev = wifi_metadata->wifi_mdata.dest_dev;
	ath_dp_mdata.mlo_param.in_dest_mac = wifi_metadata->wifi_mdata.dest_mac;
	ath_dp_mdata.mlo_param.out_ppe_ds_node_id = QCA_WIFI_NSS_PLUGINS_METADATA_INVALID_DS_NODE;

	ath_dp_mdata.is_mlo_param_valid = (wifi_metadata->valid_params_flag & ECM_CLASSIFIER_WIFI_MLO_PARAM_VALID);
	ath_dp_mdata.is_sawf_param_valid = 0;

	skb_mark = ath_get_metadata_info(&ath_dp_mdata);

	if (ath_dp_mdata.ast_param.valid) {
		wifi_metadata->wifi_mdata.out_ast_info = ath_dp_mdata.ast_param.ast_info;
		wifi_metadata->wifi_mdata.out_peer_id = ath_dp_mdata.ast_param.hw_peer_id;
		wifi_metadata->wifi_mdata.out_ast_valid = true;
	}

	wifi_metadata->wifi_mdata.out_ppe_ds_node_id = ath_dp_mdata.mlo_param.out_ppe_ds_node_id;

	return skb_mark;
}

/*
 * qca_wifi_nss_plugins_wifi_flowq_setup()
 *	Setup wifi flow queue for peer mac address
 */
static inline uint32_t qca_wifi_nss_plugins_wifi_flowq_setup(struct ecm_classifier_wifi_flow_info *queue_info)
{
	struct ath_wifi_queue_param params = {0};

	params.dev = queue_info->dev;
	params.peer_mac = queue_info->peer_mac;
	params.dscp = queue_info->dscp;
	params.qos_tag = queue_info->qos_tag;
	params.protocol = queue_info->protocol;
	params.sawf_mark = queue_info->sawf_mark;
	params.sawf_service_class = queue_info->sawf_service_class;
	params.sawf_rule_valid = queue_info->sawf_rule_valid;

	ath_setup_peer_wifi_tid_queue(&params);

	return 0;
}

/*
 * Register WiFi related callbacks with ECM WiFi classifier to get the Wi-Fi metadata info.
 */
static struct ecm_classifier_wifi_callbacks qca_wifi_plugin_wifi = {
	.get_wifi_metadata = qca_wifi_nss_plugins_get_wifi_metadata,
	.wifi_flowq_setup = qca_wifi_nss_plugins_wifi_flowq_setup,
};

/*
 * qca_wifi_nss_plugins_wifi_cb_register()
 *	Register WIFI callbacks.
 */
int qca_wifi_nss_plugins_wifi_cb_register(void)
{
	if (ecm_classifier_wifi_callback_register(&qca_wifi_plugin_wifi)) {
		qca_wifi_nss_plugins_warning("ecm wifi classifier Wifi callback registration failed\n");
		return -1;
	}

	qca_wifi_nss_plugins_info_always("Wifi classifier callbacks registered\n");
	return 0;
}

/*
 * qca_wifi_plugin_wifi_cb_unregister()
 *	Unregister Wifi callbacks.
 */
void qca_wifi_nss_plugins_wifi_cb_unregister(void)
{
	ecm_classifier_wifi_callback_unregister();
	qca_wifi_nss_plugins_info_always("Wifi classifier callbacks unregistered\n");
}
