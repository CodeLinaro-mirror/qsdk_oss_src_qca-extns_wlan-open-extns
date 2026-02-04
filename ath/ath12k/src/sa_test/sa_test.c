/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/if_ether.h>
#include "sa_test.h"

/*
 * These extern declarations are intentional: sa_test is a standalone
 * loadable module and must not include ath12k's internal headers.
 * The symbols are provided by the ath12k module at runtime.
 */
extern int ath12k_register_smart_ant_ops(struct ath12k_smart_ant_ops *ops);
extern int ath12k_unregister_smart_ant_ops(void);

struct sa_global g_sa;
struct sa_test_train_params g_train_params;
u8 sa_enable_test_mode = 1;
/* Bind ops and module entry points kept at the end for clarity */
static struct ath12k_smart_ant_ops sa_ops;

static u8 sa_get_antenna_combinations(u8 chains)
{
	u8 i = 0;
	u8 result = 1;

	for (i = 0; i < chains; i++)
		result *= (SA_ANTENNAS_PER_CHAIN);

	return result;
}

static int sa_init_radio_params(struct smartantenna_params *sa_params,
				struct ath12k_sa_config *sa_config)
{
	u8 maxchains = sa_config->nss;

	/* Both radios uses same default config params
	 * init the default values */
	sa_params->lower_bound = SA_PER_LOWER_BOUND;
	sa_params->upper_bound = SA_PER_UPPER_BOUND;
	sa_params->per_diff_threshold = SA_PERDIFF_THRESHOLD;
	sa_params->config = 0;
	sa_params->training_mode = SA_TRAINING_MODE_EXISTING;
	sa_params->packetlen = SA_DEFAULT_PKT_LEN;
	sa_params->n_packets_to_train = 0;
	sa_params->max_throughput_change = SA_DEFAULT_TP_THRESHOLD;
	sa_params->retrain_interval =
		SA_DEFAULT_RETRAIN_INTERVAL * SA_NUM_MILLISECONDS_IN_MINUTE;
	sa_params->perftrain_interval = SA_PERF_TRAIN_INTERVEL;
	sa_params->num_pkt_threshold = SA_DEFAULT_MIN_TP;
	sa_params->hysteresis = SA_DEFALUT_HYSTERISYS;
	sa_params->min_goodput_threshold = MIN_GOODPUT_THRESHOLD;
	sa_params->goodput_avg_interval = SA_GPUT_AVERAGE_INTERVAL;
	sa_params->num_pretrain_packets = SA_MAX_PRETRAIN_PACKETS;
	sa_params->num_other_bw_pkts_th = SA_N_BW_PPDU_THRESHOLD;
	sa_params->goodput_ignore_interval = SA_GPUT_IGNORE_INTERVAL;
	if (sa_enable_test_mode)
		sa_params->train_enable = 0;
	else
		sa_params->train_enable =
				SA_INIT_TRAIN_EN | SA_PERIOD_TRAIN_EN |
				SA_PERF_TRAIN_EN | SA_RX_TRAIN_EN;
	sa_params->num_min_pkt_threshod[SA_BW_20] = SA_NUM_PKT_THRESHOLD_20;
	sa_params->num_min_pkt_threshod[SA_BW_40] = SA_NUM_PKT_THRESHOLD_40;
	sa_params->num_min_pkt_threshod[SA_BW_80] = SA_NUM_PKT_THRESHOLD_80;
	sa_params->num_min_pkt_threshod[SA_BW_160] = SA_NUM_PKT_THRESHOLD_160;
	sa_params->num_min_pkt_threshod[SA_BW_320] = SA_NUM_PKT_THRESHOLD_320;
	sa_params->max_train_ppdu = SA_NUM_MAX_PPDU;
	sa_params->default_tx_antenna = SA_DEFAULT_ANTENNA;
	sa_params->num_tx_antennas = sa_get_antenna_combinations(maxchains);
	sa_params->num_recv_chains = maxchains;
	if (maxchains > SA_MAX_RECV_CHAINS) {
		sa_params->num_recv_chains = SA_MAX_RECV_CHAINS;
		SA_DEBUG_PRINT(SA_DEBUG_LVL1, sa_config->radio_id,
				"%s ERROR fatal, unsupported receive chains %d\n",
				__func__, maxchains);
	}

	return SA_PARALLEL_MODE;
}

static void sa_reset_global_training_param(void)
{
	u8 i;

	for (i = 0; i < SA_NUM_BW; i++)
		g_train_params.ratecode[i] = 0;

	g_train_params.antenna = 0;
	g_train_params.numpkts = 0;
	g_train_params.minpkts = 0xffff;
	g_train_params.per_threshold = 0xffff;
}

static int sa_test_init(struct ath12k_sa_config *cfg, int new_init)
{
	struct radio_config *radio;

	if (!cfg) {
		SA_DEBUG_PRINT(SA_DEBUG_LVL1, 0, "Invalid cfg pointer!");
		return -1;
	}
	if (cfg->radio_id >= SA_MAX_RADIOS) {
		SA_DEBUG_PRINT(SA_DEBUG_LVL1, 0, "Invalid Radio ID %u",
			       cfg->radio_id);
		return -1;
	}

	radio = &g_sa.radio[cfg->radio_id];
	if (new_init) {
		memset(radio, 0, sizeof(struct radio_config));
		sa_init_radio_params(&radio->sa_params, cfg);
		radio->init_done = 1;
		radio->sa_dbg_level = SA_DEBUG_LVL1;
		radio->default_antenna = SA_DEFAULT_ANTENNA;
		sa_reset_global_training_param();
	}

	radio->bcn_antenna = SA_DEFAULT_ANTENNA;
	cfg->txrx_feedback = SA_TX_FEEDBACK_MASK | SA_RX_FEEDBACK_MASK;
	memcpy(&radio->init_config, cfg, sizeof(struct ath12k_sa_config));

	SA_DEBUG_PRINT(SA_DEBUG_LVL1, cfg->radio_id, " %s: RadioId: %d channel:"
		" %d band: %d bss mode: %d nss: %d chainmask: 0x%02x\n",
		(new_init ? "New Init":"Re Init"), cfg->radio_id, cfg->chan_num,
		cfg->band, cfg->bss_mode, cfg->nss, cfg->txrx_chainmask);


	return SA_PARALLEL_MODE;
}

static int sa_test_deinit(u8 radio_id)
{
	struct radio_config *radio = &g_sa.radio[radio_id];

	radio->init_done = 0;
	SA_DEBUG_PRINT(SA_DEBUG_LVL1, radio_id, " RadioId: %d\n", radio_id);

	return 0;
}

static void sa_node_init(struct sa_node_ccp *node)
{
	struct radio_config *radio =  &g_sa.radio[node->node_info.radio_id];

	node->train_info.selected_antenna = radio->sa_params.default_tx_antenna;
	node->train_info.previous_selected_antenna =
					radio->sa_params.default_tx_antenna;
	node->train_info.training_start = 0;
	node->train_info.last_train_bw = SA_BW_20;
	node->train_info.tx_feedback_config = SA_TX_FEEDBACK_CONFIG_DEFAULT;
	node->train_info.smartantenna_state = SMARTANTENNA_DEFAULT;

}

#define MAX_LOG_LEN 800
static void sa_print_ratecode(struct sa_node_ccp *node, u8 bw, bool legacy)
{
	u8 i, idx;
	u16 *ratecode = 0, index = 0;
	u8 log_buff[MAX_LOG_LEN] = {0};
	struct ath12k_sa_node_info *node_info;

	if (!node)
		return;

	node_info = &node->node_info;
	if (!legacy && (bw > node_info->max_ch_bw))
		return;

	if (legacy) {
		idx = SA_RATECOUNT_IDX_LEGACY;
		ratecode = &node_info->rate_cap.rc_legacy[0];
		SA_DEBUG_PRINT(SA_DEBUG_LVL2, node_info->radio_id,
			" Legacy Rates: Count: %d: ", node_info->rate_cap.num_rc[idx]);
	} else {
		switch (bw) {
		case SA_BW_20:
			idx = SA_RATECOUNT_IDX_20;
			ratecode = &node_info->rate_cap.rc_20[0];
			SA_DEBUG_PRINT(SA_DEBUG_LVL2, node_info->radio_id,
				" 20 MHz Ratecode Count: %d: ",
				node_info->rate_cap.num_rc[idx]);
			break;
		case SA_BW_40:
			idx = SA_RATECOUNT_IDX_40;
			ratecode = &node_info->rate_cap.rc_40[0];
			SA_DEBUG_PRINT(SA_DEBUG_LVL2, node_info->radio_id,
				" 40 MHz Ratecode Count: %d: ",
				node_info->rate_cap.num_rc[idx]);
			break;
		case SA_BW_80:
			idx = SA_RATECOUNT_IDX_80;
			ratecode = &node_info->rate_cap.rc_80[0];
			SA_DEBUG_PRINT(SA_DEBUG_LVL2, node_info->radio_id,
				" 80 MHz Ratecode Count: %d: ",
				node_info->rate_cap.num_rc[idx]);
			break;
		case SA_BW_160:
			idx = SA_RATECOUNT_IDX_160;
			ratecode = &node_info->rate_cap.rc_160[0];
			SA_DEBUG_PRINT(SA_DEBUG_LVL2, node_info->radio_id,
				" 160 MHz Ratecode Count: %d: ",
				node_info->rate_cap.num_rc[idx]);
			break;
		case SA_BW_320:
			idx = SA_RATECOUNT_IDX_320;
			ratecode = &node_info->rate_cap.rc_320[0];
			SA_DEBUG_PRINT(SA_DEBUG_LVL2, node_info->radio_id,
				" 320 MHz Ratecode Count: %d: ",
				node_info->rate_cap.num_rc[idx]);
			break;
		default:
			SA_DEBUG_PRINT(SA_DEBUG_LVL1, node_info->radio_id,
				"Invalid BW %d!\n", bw);
			return;
		};
	}

	for (i = 0; i < node_info->rate_cap.num_rc[idx]; i++) {
		index += snprintf(&log_buff[index], MAX_LOG_LEN - index,
				"0x%x, ", ratecode[i]);
		if (i && !(i % 8))
			index += snprintf(&log_buff[index], MAX_LOG_LEN - index, "\n");
	}
	SA_DEBUG_PRINT(SA_DEBUG_LVL2, node_info->radio_id, "%s\n", log_buff);
}

static int sa_test_peer_connect(void **ccp, struct ath12k_sa_node_info *node_info)
{
	struct sa_node_ccp *node = NULL;
	struct radio_config *radio;
	u8 i;

	if (!node_info)
		return -1;

	if (!*ccp) {
		node = kzalloc(sizeof(*node), GFP_KERNEL);
		*ccp = node;
		if (!node)
			return -1;
	} else {
		node = *ccp;
		SA_DEBUG_PRINT(SA_DEBUG_LVL1, node->node_info.radio_id,
			" Called with non NULL ccp, Mac: %pM RadioId: %d max bw:"
			" %d NSS(TX,RX): (%d,%d) opmode: %d channel: %d band : %d\n",
			node_info->mac_addr, node_info->radio_id,
			node_info->max_ch_bw, node_info->nss_tx,
			node_info->nss_rx, node_info->opmode,
			node_info->chan_num, node_info->band);
	}
	memcpy(&node->node_info, node_info, sizeof(*node_info));
	radio = &g_sa.radio[node->node_info.radio_id];

	sa_node_init(node);

	radio->nodes_connected++;
	radio->node_count_per_ant[node->train_info.previous_selected_antenna]++;
	SA_DEBUG_PRINT(SA_DEBUG_LVL2, node->node_info.radio_id,
		" Mac: %pM RadioId: %d max bw: %d NSS(TX,RX): (%d,%d) opmode: %d "
		"channel: %d band: %d\n", node_info->mac_addr, node_info->radio_id,
		node_info->max_ch_bw, node_info->nss_tx, node_info->nss_rx,
		node_info->opmode, node_info->chan_num, node_info->band);
	sa_print_ratecode(node, 0, true);
	for (i = 0; i < SA_NUM_BW; i++)
		sa_print_ratecode(node, i, false);

	return 0;
}

static int sa_test_peer_disconnect(void *ccp)
{
	struct sa_node_ccp *node = ccp;
	struct radio_config *radio;

	if (!node)
		return -1;

	radio = &g_sa.radio[node->node_info.radio_id];
	radio->nodes_connected--;
	radio->node_count_per_ant[node->train_info.previous_selected_antenna]--;
	SA_DEBUG_PRINT(SA_DEBUG_LVL2, node->node_info.radio_id,
		" Mac: %pM RadioId: %d max bw: %d NSS(TX,RX): (%d,%d) opmode: %d "
		"channel: %d band: %d\n", node->node_info.mac_addr,
		node->node_info.radio_id, node->node_info.max_ch_bw,
		node->node_info.nss_tx, node->node_info.nss_rx,
		node->node_info.opmode, node->node_info.chan_num,
		node->node_info.band);

	kfree(node);

	return 0;
}

static int sa_test_update_txfeedback(void *ccp, struct ath12k_sa_tx_feedback *fb, u8 *status)
{
	if (status)
		*status = 0;
	SA_DEBUG_PRINT(SA_DEBUG_LVL1, 0, "SA Test: update_txfeedback");
	return 0;
}

static int sa_test_update_rxfeedback(void *ccp, struct ath12k_sa_rx_feedback *fb, u8 *status)
{
	if (status)
		*status = 0;
	SA_DEBUG_PRINT(SA_DEBUG_LVL1, 0, "SA Test: update_rxfeedback");
	return 0;
}

static int sa_test_get_txantenna(void *ccp, u32 *antenna_array)
{
	int i;
	struct sa_node_ccp *node = ccp;
	struct sa_node_train_info *train_info = &node->train_info;

	if (!antenna_array)
		return -1;

	for (i = 0; i < SA_MAX_SA_CHAINS; i++)
		antenna_array[i] = train_info->selected_antenna;

	return 0;
}

static int sa_test_get_txdefaultantenna(u8 radio_id, u32 *antenna)
{
	struct radio_config *radio = &g_sa.radio[radio_id];

	if (!antenna)
		return -1;

	if (radio->init_done)
		*antenna = radio->sa_params.default_tx_antenna;
	else
		*antenna = radio->default_antenna;

	return 0;
}

static int sa_test_get_rxantenna(u8 radio_id, u32 *rx_antenna)
{
	if (!rx_antenna)
		return -1;

	*rx_antenna = g_sa.radio[radio_id].default_antenna;

	return 0;
}

static int sa_test_get_traininginfo(void *ccp, u32 *rate_array, u32 *antenna_array, u32 *numpkts)
{
	u8 i;

	if (!rate_array || !antenna_array || !numpkts)
		return -1;

	/* For 2 serieses of rate */
	rate_array[0] = (g_train_params.ratecode[0] |
			g_train_params.ratecode[1] << 16);
	rate_array[1] = (g_train_params.ratecode[2] |
			g_train_params.ratecode[3] << 16);
	rate_array[2] = (g_train_params.ratecode[4] & SA_WORD_MASK);
	rate_array[3] = (g_train_params.ratecode[0] |
			g_train_params.ratecode[1] << 16);
	rate_array[4] = (g_train_params.ratecode[2] |
			g_train_params.ratecode[3] << 16);
	rate_array[5] = (g_train_params.ratecode[4] & SA_WORD_MASK);

	/* For all chains same Antenna */
	for (i = 0; i < SA_MAX_SA_CHAINS; i++)
		antenna_array[i] = g_train_params.antenna;

	*numpkts = g_train_params.numpkts;

	return 0;
}

static int sa_test_get_bcn_txantenna(u8 radio_id, u32 *bcn_txantenna)
{
	if (!bcn_txantenna)
		return -1;

	*bcn_txantenna = g_sa.radio[radio_id].bcn_antenna;

	return 0;
}

static int sa_test_set_param(struct ath12k_sa_config_params *params)
{
	if (!params)
		return -1;

	SA_DEBUG_PRINT(SA_DEBUG_LVL1, 0, "SA Test: set_param type=%u id=%u val=%u",
		       params->param_type, params->param_id, params->param_value);

	return 0;
}

static int sa_test_get_param(struct ath12k_sa_config_params *params)
{
	if (!params)
		return -1;

	SA_DEBUG_PRINT(SA_DEBUG_LVL1, 0, "SA Test: get_param type=%u id=%u",
		       params->param_type, params->param_id);

	return 0;
}

static int sa_test_get_node_config(void *ccp, u32 id, u32 *config)
{
	struct sa_node_ccp *node = ccp;
	struct sa_node_train_info *train_info;

	if (!config || !node)
		return -1;

	train_info = &node->train_info;
	switch (id) {
	case SA_TX_FEEDBACK_CONFIG:
		*config = train_info->tx_feedback_config;
		break;
	default:
		return 0;
	}

	return 0;
}

static int sa_test_get_ext_traininginfo(void *ccp, u16 *minpkts, u16 *per_th)
{
	if (!minpkts || !per_th)
		return -1;

	*minpkts = g_train_params.minpkts;
	*per_th = g_train_params.per_threshold;

	return 0;
}

static int __init sa_test_module_init(void)
{
	int ret;

	SA_DEBUG_PRINT(SA_DEBUG_LVL1, 0, "SA Test module Init");

	/* bind test implementations */
	sa_ops.sa_algo_init = sa_test_init;
	sa_ops.sa_algo_deinit = sa_test_deinit;
	sa_ops.sa_peer_connect = sa_test_peer_connect;
	sa_ops.sa_peer_disconnect = sa_test_peer_disconnect;
	sa_ops.sa_update_txfeedback = sa_test_update_txfeedback;
	sa_ops.sa_update_rxfeedback = sa_test_update_rxfeedback;
	sa_ops.sa_get_txantenna = sa_test_get_txantenna;
	sa_ops.sa_get_txdefaultantenna = sa_test_get_txdefaultantenna;
	sa_ops.sa_get_rxantenna = sa_test_get_rxantenna;
	sa_ops.sa_get_traininginfo = sa_test_get_traininginfo;
	sa_ops.sa_get_bcn_txantenna = sa_test_get_bcn_txantenna;
	sa_ops.sa_set_param = sa_test_set_param;
	sa_ops.sa_get_param = sa_test_get_param;
	sa_ops.sa_get_node_config = sa_test_get_node_config;
	sa_ops.sa_get_ext_traininginfo = sa_test_get_ext_traininginfo;

	ret = ath12k_register_smart_ant_ops(&sa_ops);
	if (ret)
		SA_DEBUG_PRINT(SA_DEBUG_LVL1, 0,
			       "SA Test: register ops failed: %d", ret);

	return ret;
}

static void __exit sa_test_module_exit(void)
{
	int ret;

	ret = ath12k_unregister_smart_ant_ops();
	if (ret)
		SA_DEBUG_PRINT(SA_DEBUG_LVL1, 0,
			       "SA Test: unregister ops failed: %d", ret);

	SA_DEBUG_PRINT(SA_DEBUG_LVL1, 0, "SA Test module Exit");
}

module_init(sa_test_module_init);
module_exit(sa_test_module_exit);

MODULE_DESCRIPTION("Test module for Qualcomm Technologies Smart Antenna feature");
MODULE_LICENSE("Dual BSD/GPL");
