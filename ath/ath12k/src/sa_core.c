/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.*/

#include "ini.h"
#include "sa_api.h"
#include "../debug.h"

extern struct ath12k_smart_ant_ops *g_sa_ops;
extern atomic_t ag_sa_init;

static inline int sa_core_get_txdefaultantenna(u8 radio_id, u32 *antenna)
{
	int ret;

	if (!g_sa_ops || !g_sa_ops->sa_get_txdefaultantenna || !antenna) {
		ath12k_err(NULL, "sa_core: get_txdefaultantenna invalid ops/args\n");
		return -1;
	}

	ret = g_sa_ops->sa_get_txdefaultantenna(radio_id, antenna);
	if (ret)
		ath12k_err(NULL, "sa_core: get_txdefaultantenna op failed (%d)\n", ret);

	return ret;
}

static inline int sa_core_get_rxantenna(u8 radio_id, u32 *rx_antenna)
{
	int ret;

	if (!g_sa_ops || !g_sa_ops->sa_get_rxantenna || !rx_antenna) {
		ath12k_err(NULL, "sa_core: get_rxantenna invalid ops/args\n");
		return -1;
	}

	ret = g_sa_ops->sa_get_rxantenna(radio_id, rx_antenna);
	if (ret)
		ath12k_err(NULL, "sa_core: get_rxantenna op failed (%d)\n", ret);

	return ret;
}

static inline
int sa_core_get_gpio_config(u8 radio_id, struct ath12k_sa_gpio_config *gpio_cfg)
{
	int ret;

	if (!g_sa_ops || !g_sa_ops->sa_get_gpio_config || !gpio_cfg) {
		ath12k_err(NULL, "sa_core: get_gpio_config invalid ops/args\n");
		return -1;
	}

	ret = g_sa_ops->sa_get_gpio_config(radio_id, gpio_cfg);
	if (ret)
		ath12k_err(NULL, "sa_core: get_gpio_config op failed (%d)\n", ret);

	return ret;
}

void sa_get_cfg_flags(struct ath12k_base *ab, u8 *sa_en, u8 *sa_sw_test)
{
	struct sa_base_extn *sa_base;

	if (ab) {
		if (sa_en)
			*sa_en = !!ath12k_cfg_get(ab, ATH12K_CFG_SA_ENABLE);
		if (sa_sw_test)
			*sa_sw_test = ath12k_cfg_get(ab, ATH12K_CFG_SA_SW_TEST);
		sa_base = &ab->ath12k_base_extn.sa_base;
		sa_base->sa_mode_overwrite =
					ath12k_cfg_get(ab, ATH12K_CFG_SA_MODE);
	}
}

int sa_core_init(struct ath12k *ar,
		 struct ath12k_link_vif *arvif, u8 init_flags)
{
	struct sa_base_extn *sa_base;
	struct sa_ar_extn *sa_ar;
	struct ath12k_base *ab;
	struct ath12k_vif *ahvif;
	struct ath12k_sa_config init_cfg = {0};
	struct ieee80211_channel *chan;
	int sa_mode;
	u32 rx_antenna, tx_defaultant;

	if (!g_sa_ops || !ar || !arvif || !ar->ab)
		return -1;

	if (!g_sa_ops->sa_algo_init)
		return -1;

	ab = ar->ab;
	sa_base = &ab->ath12k_base_extn.sa_base;
	if (!sa_base->sa_enable)
		return -1;

	sa_ar = &ar->ar_extn.sa_ar;
	if (sa_ar->state != SA_INIT_DONE_STATE)
		atomic_inc(&ag_sa_init);
	else
		return -1;

	ath12k_dbg(ab, ATH12K_DBG_ANY,
		   "SA Core Init Called with init_flag = %u\n", init_flags);
	ahvif = arvif->ahvif;
	if ((ahvif->vdev_type != WMI_VDEV_TYPE_AP) &&
	    (ahvif->vdev_type != WMI_VDEV_TYPE_STA)) {
		atomic_dec(&ag_sa_init);
		return -1;
	}

	init_cfg.radio_id = ar->radio_idx;
	init_cfg.nss = ar->num_tx_chains;
	init_cfg.bss_mode = (ahvif->vdev_type == WMI_VDEV_TYPE_AP)?
			    SA_BSS_MODE_AP: SA_BSS_MODE_STA;
	init_cfg.txrx_chainmask =
		(ar->cfg_tx_chainmask | (ar->cfg_rx_chainmask << 4));
	init_cfg.txrx_feedback = SA_TX_FEEDBACK_MASK | SA_RX_FEEDBACK_MASK;
	chan = arvif->chanctx.def.chan;
	if (chan) {
		init_cfg.band = chan->band;
		init_cfg.chan_num = chan->hw_value;
	}
	sa_mode = g_sa_ops->sa_algo_init(&init_cfg, init_flags);
	if (sa_mode < 0) {
		ath12k_err(ab, "SA Init error (%d)\n", sa_mode);
		atomic_dec(&ag_sa_init);
		return -1;
	}
	if (sa_base->sa_mode_overwrite != -1) {
		sa_mode = sa_base->sa_mode_overwrite;
		ath12k_dbg(ab, ATH12K_DBG_ANY,
			   "SA Mode Overwritten to %d\n", sa_mode);
	} else {
		ath12k_dbg(ab, ATH12K_DBG_ANY, "SA Mode %d\n", sa_mode);
	}
	sa_ar->sa_mode = sa_mode;

	sa_core_get_txdefaultantenna(ar->radio_idx, &tx_defaultant);
	sa_core_get_rxantenna(ar->radio_idx, &rx_antenna);
	/* Create smart antenna enabled and Tx/Rx feedback subscription state
	 * bitmap. bit 0 for smart antenna enabled/disabled, bit 4 for TX
	 * subscription state and bit 5 for RX subscription state.
	 */
	sa_ar->enable_mask = SA_ENABLE_MASK | (init_cfg.txrx_feedback &
			(SA_TX_FEEDBACK_MASK | SA_RX_FEEDBACK_MASK));
	if (init_flags & SA_NEW_CONFIG) {
		sa_ar->state = SA_INIT_DONE_STATE;
	}

	return 0;
}

int sa_core_deinit(struct ath12k *ar, u8 init_flags)
{
	struct sa_ar_extn *sa_ar;
	struct ath12k_base *ab;

	if (!g_sa_ops || !ar || !ar->ab)
		return -1;

	ab = ar->ab;

	sa_ar = &ar->ar_extn.sa_ar;
	if (!(sa_ar->enable_mask & SA_ENABLE_MASK))
		return -1;

	if (sa_ar->state == SA_DEINIT_DONE_STATE) {
		ath12k_dbg(ab, ATH12K_DBG_ANY, "SA Core Deinit already done!\n");
		return -1;
	}
	if (init_flags) {
		atomic_dec(&ag_sa_init);
		if (g_sa_ops->sa_algo_deinit)
			g_sa_ops->sa_algo_deinit(ar->radio_idx);

		sa_ar->enable_mask = 0;
		sa_ar->state = SA_DEINIT_DONE_STATE;
	}

	return 0;
}
