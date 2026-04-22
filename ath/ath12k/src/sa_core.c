/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.*/

#include "ini.h"
#include "sa_api.h"
#include <linux/etherdevice.h>
#include "../peer.h"
#include "../debug.h"
#include <linux/notifier.h>
#include "ath12k_notif.h"

struct wmi_peer_ratecode_list_fixed_param {
	struct ath12k_wmi_mac_addr_params macaddr;
	u32 ratecount;
	u32 vdev_id;
	u32 pdev_id;
	u32 ratecount_ext;
} __packed;

struct wmi_peer_cck_ofdm_rate_info {
	u32 ratecode_legacy;
} __packed;

struct wmi_peer_mcs_rate_info {
	u32 rt_code_20;
	u32 rt_code_40;
	u32 rt_code_80;
	u32 rt_code_160;
	u32 rt_code_320;
} __packed;

struct wmi_pdev_set_smart_ant_cmd {
	u32 tlv_header;
	union {
		u32 mac_id;
		u32 pdev_id;
	};
	u32 enable;
	u32 mode;
	u32 rx_antenna;
	u32 tx_default_antenna;
} __packed;

struct wmi_pdev_smart_ant_gpio_handle_cmd {
	u32 tlv_header;
	u32 gpio_pin;
	u32 gpio_func;
	u32 pdev_id;
} __packed;

struct wmi_pdev_set_rx_antenna_cmd {
	u32 tlv_header;
	union {
		u32 mac_id;
		u32 pdev_id;
	};
	u32 rx_antenna;
} __packed;

struct wmi_pdev_set_tx_antenna_cmd {
	u32 tlv_header;
	u32 vdev_id;
	struct ath12k_wmi_mac_addr_params macaddr;
} __packed;

struct wmi_peer_set_smart_tx_ant_series_cmd {
	u32 tlv_header;
	u32 ant_series;
} __packed;

struct wmi_peer_set_smart_ant_train_ant_fixed_param_cmd {
	u32 tlv_header;
	u32 vdev_id;
	struct ath12k_wmi_mac_addr_params macaddr;
	u32 numpkts;
	u32 per_check_bitmap;
} __packed;

struct wmi_peer_set_smart_ant_train_ant_param {
	u32 tlv_header;
	union {
		u32 train_rate_series;
		u32 train_rate_series_lo;
	};
	u32 train_antenna_series;
	u32 rc_flags;
	u32 train_rate_series_hi;
	u32 train_rate_series_ext;
} __packed;

struct wmi_peer_set_smart_ant_node_config_ops_cmd {
	u32 tlv_header;
	u32 vdev_id;
	struct ath12k_wmi_mac_addr_params macaddr;
	u32 cmd_id;
	u32 args_count;
} __packed;

extern struct ath12k_smart_ant_ops *g_sa_ops;
extern atomic_t ag_sa_init;

/* PPDU notifier block for Smart Antenna */
static struct notifier_block sa_ppdu_nb;
static atomic_t sa_ppdu_nb_registered = ATOMIC_INIT(0);

static int sa_core_ppdu_event_notify(struct notifier_block *nb,
                                     unsigned long action, void *data)
{
	struct ath12k_ppdu_event *event = data;

	if (!event || !event->skb)
		return NOTIFY_DONE;

	switch (action) {
	case ATH12K_EVENT_PPDU_RX_COMPLETE:
		/* RX PPDU complete: listeners may parse skb->data as ath12k_ppdu_rx_info */
		return NOTIFY_DONE;
	case ATH12K_EVENT_PPDU_TX_COMPLETE:
		/* TX PPDU complete: listeners may parse skb->data as ath12k_ppdu_tx_info */
		return NOTIFY_DONE;
	default:
		return NOTIFY_DONE;
	}
}

static int sa_core_ppdu_notifier_register(u8 en_mask)
{
	unsigned long mask = 0;
	int ret;

	if (atomic_cmpxchg(&sa_ppdu_nb_registered, 0, 1) != 0)
		return 0;

	sa_ppdu_nb.notifier_call = sa_core_ppdu_event_notify;
	sa_ppdu_nb.priority = 0;

	if (en_mask & SA_TX_FEEDBACK_MASK)
		mask |= (1UL << ATH12K_EVENT_PPDU_TX_COMPLETE);
	if (en_mask & SA_RX_FEEDBACK_MASK)
		mask |= (1UL << ATH12K_EVENT_PPDU_RX_COMPLETE);

	ret = ath12k_register_ppdu_notifier(&sa_ppdu_nb, mask);
	if (ret)
		atomic_set(&sa_ppdu_nb_registered, 0);

	return ret;
}

static int sa_core_ppdu_notifier_unregister(u8 en_mask)
{
	unsigned long mask = 0;
	int ret;

	if (atomic_read(&sa_ppdu_nb_registered) == 0)
		return 0;

	if (en_mask & SA_TX_FEEDBACK_MASK)
		mask |= (1UL << ATH12K_EVENT_PPDU_TX_COMPLETE);
	if (en_mask & SA_RX_FEEDBACK_MASK)
		mask |= (1UL << ATH12K_EVENT_PPDU_RX_COMPLETE);

	ret = ath12k_unregister_ppdu_notifier(&sa_ppdu_nb, mask);
	if (!ret)
		atomic_set(&sa_ppdu_nb_registered, 0);

	return ret;
}

static inline int sa_core_get_txantenna(void *ccp, u32 *antenna_array)
{
	int ret;

	if (!g_sa_ops || !g_sa_ops->sa_get_txantenna || !antenna_array) {
		ath12k_err(NULL, "sa_core: get_txantenna invalid ops/args\n");
		return -1;
	}

	ret = g_sa_ops->sa_get_txantenna(ccp, antenna_array);
	if (ret)
		ath12k_err(NULL, "sa_core: get_txantenna op failed (%d)\n", ret);

	return ret;
}

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

static inline int sa_core_get_traininginfo(void *ccp, u32 *rate_array,
					   u32 *antenna_array, u32 *numpkts)
{
	int ret;

	if (!g_sa_ops || !g_sa_ops->sa_get_traininginfo ||
	    !rate_array || !antenna_array || !numpkts) {
		ath12k_err(NULL, "sa_core: get_traininginfo invalid ops/args\n");
		return -1;
	}

	ret = g_sa_ops->sa_get_traininginfo(ccp, rate_array,
					    antenna_array, numpkts);
	if (ret)
		ath12k_err(NULL, "sa_core: get_traininginfo op failed (%d)\n", ret);

	return ret;
}

static inline int sa_core_get_bcn_txantenna(u8 radio_id, u32 *bcn_txantenna)
{
	int ret;

	if (!g_sa_ops || !g_sa_ops->sa_get_bcn_txantenna || !bcn_txantenna) {
		ath12k_err(NULL, "sa_core: get_bcn_txantenna invalid ops/args\n");
		return -1;
	}

	ret = g_sa_ops->sa_get_bcn_txantenna(radio_id, bcn_txantenna);
	if (ret)
		ath12k_err(NULL, "sa_core: get_bcn_txantenna op failed (%d)\n", ret);

	return ret;
}

static inline int sa_core_get_param(struct ath12k_sa_config_params *params)
{
	int ret;

	if (!g_sa_ops || !g_sa_ops->sa_get_param || !params) {
		ath12k_err(NULL, "sa_core: get_param invalid ops/args\n");
		return -1;
	}

	ret = g_sa_ops->sa_get_param(params);
	if (ret)
		ath12k_err(NULL, "sa_core: get_param op failed (%d)\n", ret);

	return ret;
}

static inline int sa_core_get_node_config(void *ccp, u32 id, u32 *config)
{
	int ret;

	if (!g_sa_ops || !g_sa_ops->sa_get_node_config || !config) {
		ath12k_err(NULL, "sa_core: get_node_config invalid ops/args\n");
		return -1;
	}

	ret = g_sa_ops->sa_get_node_config(ccp, id, config);
	if (ret)
		ath12k_err(NULL, "sa_core: get_node_config op failed (%d)\n", ret);

	return ret;
}

static inline
int sa_core_get_ext_traininginfo(void *ccp, u16 *minpkts, u16 *per_th)
{
	int ret;

	if (!g_sa_ops || !g_sa_ops->sa_get_ext_traininginfo ||
	    !minpkts || !per_th) {
		ath12k_err(NULL, "sa_core: get_ext_traininginfo invalid ops/args\n");
		return -1;
	}

	ret = g_sa_ops->sa_get_ext_traininginfo(ccp, minpkts, per_th);
	if (ret)
		ath12k_err(NULL, "sa_core: get_ext_traininginfo op failed (%d)\n", ret);

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

static void sa_core_peer_connect_notify(struct ath12k_link_sta *arsta,
					struct ath12k_sa_rate_cap *rate_cap)
{
	struct ieee80211_channel *chan;
	struct ieee80211_link_sta *link_sta;
	struct ath12k *ar;
	struct ath12k_link_vif *arvif;
	struct sa_sta_extn *sa_sta;
	struct ath12k_sa_node_info node_info = {0};

	if (!arsta || !arsta->arvif || !arsta->arvif->ar || !rate_cap ||
	    !g_sa_ops || !g_sa_ops->sa_peer_connect)
		return;

	arvif = arsta->arvif;
	ar = arvif->ar;
	if (!ar->ab || !ath12k_smart_ant_is_enable(ar->ab))
		return;

	link_sta = ath12k_mac_get_link_sta(arsta);
	if (!link_sta) {
		ath12k_err(ar->ab, "sa_core: unable to access link_sta\n");
		return;
	}
	sa_sta = &arsta->arsta_extn.sa_sta;
	ether_addr_copy(node_info.mac_addr, arsta->addr);
	node_info.radio_id = ar->radio_idx;
	node_info.max_ch_bw = arsta->bw;
	node_info.nss_tx = arsta->peer_nss;
	node_info.nss_rx = link_sta->rx_nss;
	memcpy(&node_info.rate_cap, rate_cap, sizeof(*rate_cap));

	chan = arvif->chanctx.def.chan;
	if (chan) {
		node_info.band = chan->band;
		node_info.chan_num = chan->hw_value;
	}

	node_info.opmode = SA_OPMODE_CCK_OFDM;
	if (link_sta->uhr_cap.has_uhr)
		node_info.opmode = SA_OPMODE_UHR;
	else if (link_sta->eht_cap.has_eht)
		node_info.opmode = SA_OPMODE_EHT;
	else if (link_sta->he_cap.has_he)
		node_info.opmode = SA_OPMODE_HE;
	else if (link_sta->vht_cap.vht_supported)
		node_info.opmode = SA_OPMODE_VHT;
	else if (link_sta->ht_cap.ht_supported)
		node_info.opmode = SA_OPMODE_HT;

	if (g_sa_ops->sa_peer_connect(&sa_sta->ctx, &node_info))
		ath12k_err(ar->ab, "sa_core: unable to update STA info\n");
}

void sa_core_peer_disconnect_notify(struct ath12k_link_sta *arsta)
{
	struct sa_sta_extn *sa_sta;
	struct ath12k *ar;

	if (!arsta || !arsta->arvif || !arsta->arvif->ar ||
	    !g_sa_ops || !g_sa_ops->sa_peer_disconnect)
		return;

	ar = arsta->arvif->ar;
	if (!ar->ab || !ath12k_smart_ant_is_enable(ar->ab))
		return;

	sa_sta = &arsta->arsta_extn.sa_sta;
	if (sa_sta->ctx)
		g_sa_ops->sa_peer_disconnect(sa_sta->ctx);
	sa_sta->ctx = NULL;
}

static int
sa_core_wmi_send_parallel_mode_gpio_drive(struct ath12k *ar, u32 value)
{
	struct sa_ar_extn *sa_ar;
	int ret = 0;

	if (!ar)
		return -EINVAL;

	sa_ar = &ar->ar_extn.sa_ar;
	if (sa_ar->sa_mode == WMI_SMART_ANT_MODE_PARALLEL) {
		ret = ath12k_wmi_pdev_set_param(ar,
				WMI_PDEV_PARAM_SA_PARALLEL_MODE_GPIO_DRIVE_CFG,
				value, ar->pdev->pdev_id);
	} else {
		ath12k_err(ar->ab, "Operation not permitted in Serial Mode\n");
		ret = -EPERM;
	}

	return ret;
}

static int
sa_core_wmi_send_smart_ant_enable(struct ath12k *ar, u32 tx_ant, u32 rx_ant,
				  struct ath12k_sa_gpio_config *gpio_cfg)
{
	struct ath12k_wmi_pdev *wmi = ar->wmi;
	struct sa_ar_extn *sa_ar = &ar->ar_extn.sa_ar;
	struct wmi_pdev_set_smart_ant_cmd *cmd;
	struct wmi_pdev_smart_ant_gpio_handle_cmd *gpio_param;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	int i, ret, len;
	void *ptr;

	len = sizeof(*cmd) + TLV_HDR_SIZE +
	      WMI_SMART_ANT_HAL_MAX * sizeof(*gpio_param);
	skb = ath12k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	ptr = skb->data;
	cmd = ptr;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG, WMI_TAG_PDEV_SMART_ANT_ENABLE_CMD) |
		FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->pdev_id = ar->pdev->pdev_id;
	cmd->mode = sa_ar->sa_mode;
	cmd->tx_default_antenna = tx_ant;
	cmd->rx_antenna = rx_ant;
	cmd->enable = sa_ar->enable_mask & SA_ENABLE_MASK;

	ptr = skb->data + sizeof(*cmd);
	len = WMI_SMART_ANT_HAL_MAX * sizeof(*gpio_param);
	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_STRUCT) |
		      FIELD_PREP(WMI_TLV_LEN, len);

	ptr += TLV_HDR_SIZE;
	gpio_param = ptr;

	for (i = 0; i < WMI_SMART_ANT_HAL_MAX; i++) {
		gpio_param->tlv_header =
			FIELD_PREP(WMI_TLV_TAG,
				   WMI_TAG_PDEV_SMART_ANT_GPIO_HANDLE) |
			FIELD_PREP(WMI_TLV_LEN,
				   sizeof(*gpio_param) - TLV_HDR_SIZE);
		if (sa_ar->sa_mode == WMI_SMART_ANT_MODE_SERIAL) {
			if (i < WMI_SMART_ANT_MAX_SERIAL_ANT) {
				gpio_param->gpio_pin = gpio_cfg->gpio_pin[i];
				gpio_param->gpio_func = gpio_cfg->gpio_func[i];
			} else {
				gpio_param->gpio_pin = 0;
				gpio_param->gpio_func = 0;
			}
		} else if (sa_ar->sa_mode == WMI_SMART_ANT_MODE_PARALLEL) {
			gpio_param->gpio_pin = gpio_cfg->gpio_pin[i];
			gpio_param->gpio_func = gpio_cfg->gpio_func[i];
		}
		gpio_param->pdev_id = ar->pdev->pdev_id;
		gpio_param++;
	}

	ret = ath12k_wmi_cmd_send(wmi, skb, WMI_PDEV_SMART_ANT_ENABLE_CMDID);
	if (ret) {
		ath12k_warn(ar->ab,
			    "failed to send WMI_PDEV_SMART_ANT_ENABLE_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

static int sa_core_wmi_set_rx_antenna(struct ath12k *ar, u32 rx_antenna)
{
	struct ath12k_wmi_pdev *wmi = ar->wmi;
	struct wmi_pdev_set_rx_antenna_cmd *cmd;
	struct sk_buff *skb;
	int ret, len;

	len = sizeof(*cmd);
	skb = ath12k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_pdev_set_rx_antenna_cmd *) skb->data;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG,
			   WMI_TAG_PDEV_SMART_ANT_SET_RX_ANTENNA_CMD) |
		FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->pdev_id = ar->pdev->pdev_id;
	cmd->rx_antenna = rx_antenna;

	ret = ath12k_wmi_cmd_send(wmi, skb,
				  WMI_PDEV_SMART_ANT_SET_RX_ANTENNA_CMDID);
	if (ret) {
		ath12k_warn(ar->ab,
			    "WMI_PDEV_SMART_ANT_SET_RX_ANTENNA_CMDID failed\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

static int sa_core_wmi_set_tx_antenna(struct ath12k *ar, u32 vdev_id,
				      u8 *macaddr, u32 *tx_ant_series)
{
	struct ath12k_wmi_pdev *wmi = ar->wmi;
	struct wmi_pdev_set_tx_antenna_cmd *cmd;
	struct wmi_peer_set_smart_tx_ant_series_cmd *series;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	void *ptr;
	int i, ret, len, series_len;

	len = sizeof(*cmd) + TLV_HDR_SIZE;
	len += WMI_SMART_ANT_MAX_RATE_SERIES *
	       sizeof(struct wmi_peer_set_smart_tx_ant_series_cmd);

	skb = ath12k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	ptr = skb->data;

	cmd = ptr;
	cmd->tlv_header = FIELD_PREP(WMI_TLV_TAG,
				     WMI_TAG_PEER_SMART_ANT_SET_TX_ANTENNA_CMD) |
			  FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = vdev_id;
	ether_addr_copy(cmd->macaddr.addr, macaddr);

	ptr = skb->data + sizeof(*cmd);

	series_len = WMI_SMART_ANT_MAX_RATE_SERIES *
		     sizeof(struct wmi_peer_set_smart_tx_ant_series_cmd);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_STRUCT) |
		      FIELD_PREP(WMI_TLV_LEN, series_len);
	ptr += TLV_HDR_SIZE;

	series = ptr;
	for (i = 0; i < WMI_SMART_ANT_MAX_RATE_SERIES; i++) {
		series->tlv_header =
			FIELD_PREP(WMI_TLV_TAG,
				   WMI_TAG_PEER_SMART_ANT_SET_TX_ANTENNA_SERIES) |
			FIELD_PREP(WMI_TLV_LEN, sizeof(*series) - TLV_HDR_SIZE);
		series->ant_series = tx_ant_series[i];
		series++;
	}

	ret = ath12k_wmi_cmd_send(wmi, skb,
				  WMI_PEER_SMART_ANT_SET_TX_ANTENNA_CMDID);
	if (ret) {
		ath12k_warn(ar->ab,
			    "failed to send WMI_PEER_SMART_ANT_SET_TX_ANTENNA_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

static int sa_core_wmi_set_train_info(struct ath12k *ar, u8 *macaddr,
				      struct smart_ant_train_info *param)
{
	struct ath12k_wmi_pdev *wmi = ar->wmi;
	struct wmi_peer_set_smart_ant_train_ant_fixed_param_cmd *cmd;
	struct wmi_peer_set_smart_ant_train_ant_param *train_param;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	void *ptr;
	int i, ret, len, itr = 0, series_len;

	len = sizeof(*cmd) + TLV_HDR_SIZE;
	len += WMI_SMART_ANT_MAX_RATE_SERIES *
	       sizeof(struct wmi_peer_set_smart_ant_train_ant_param);

	skb = ath12k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	ptr = skb->data;

	cmd = ptr;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG,
			   WMI_TAG_PEER_SMART_ANT_SET_TRAIN_ANTENNA_CMD) |
		FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = param->vdev_id;
	ether_addr_copy(cmd->macaddr.addr, macaddr);
	cmd->numpkts = param->numpkts;
	if ((param->minpkts != SA_INVALID_PARAM_VALUE) ||
	    (param->per_th != SA_INVALID_PARAM_VALUE)) {
		cmd->per_check_bitmap = 1;
		cmd->per_check_bitmap |= (param->per_th & SA_MASK_PER_TH) << 1;
		cmd->per_check_bitmap |=
				(param->minpkts & SA_MASK_MIN_PKTS) << 9;
	}

	ptr = skb->data + sizeof(*cmd);

	series_len = WMI_SMART_ANT_MAX_RATE_SERIES *
		     sizeof(struct wmi_peer_set_smart_ant_train_ant_param);

	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_STRUCT) |
		      FIELD_PREP(WMI_TLV_LEN, series_len);
	ptr += TLV_HDR_SIZE;

	train_param = ptr;
	for (i = 0; i < WMI_SMART_ANT_MAX_RATE_SERIES; i++) {
		train_param->tlv_header =
			FIELD_PREP(WMI_TLV_TAG,
				   WMI_TAG_PEER_SMART_ANT_SET_TRAIN_ANTENNA_PARAM) |
			FIELD_PREP(WMI_TLV_LEN,
				   sizeof(*train_param) - TLV_HDR_SIZE);
		train_param->train_rate_series_lo =
			((param->rate_array[itr] & SA_MASK_RCODE) |
			 (param->rate_array[itr] & (SA_MASK_RCODE << 16)));
		train_param->train_rate_series_hi =
			((param->rate_array[itr + 1] & SA_MASK_RCODE) |
			 (param->rate_array[itr + 1] & (SA_MASK_RCODE << 16)));
		/* Higher 16-bits are Reserved */
		train_param->train_rate_series_ext =
				param->rate_array[itr + 2] & SA_MASK_RCODE;
		itr += 3;
		train_param->train_antenna_series = param->antenna_array[i];
		train_param->rc_flags = 0;
		train_param++;
	}

	ret = ath12k_wmi_cmd_send(wmi, skb, WMI_PEER_SMART_ANT_SET_TRAIN_INFO_CMDID);
	if (ret) {
		ath12k_warn(ar->ab,
			    "failed to send WMI_PEER_SMART_ANT_SET_TRAIN_INFO_CMDID\n");
		dev_kfree_skb(skb);
	}

	return ret;
}

static int sa_core_wmi_send_node_config_ops(struct ath12k_link_sta *arsta,
					    u8 *macaddr, u32 cmd_id,
					    u16 args_count, u32 *args_arr)
{
	struct ath12k *ar = arsta->arvif->ar;
	struct ath12k_wmi_pdev *wmi = ar->wmi;
	struct wmi_peer_set_smart_ant_node_config_ops_cmd *cmd;
	struct wmi_tlv *tlv;
	struct sk_buff *skb;
	void *ptr;
	int len, ret;
	u32 *cfg_arg;
	u8 i;

	if (!macaddr || !args_count || !args_arr) {
		ath12k_warn(ar->ab, "Cannot send with invalid arguments");
		return -EINVAL;
	}

	len = sizeof(*cmd) + TLV_HDR_SIZE + args_count * sizeof(u32);
	skb = ath12k_wmi_alloc_skb(wmi->wmi_ab, len);
	if (!skb)
		return -ENOMEM;

	ptr = skb->data;
	cmd = ptr;
	cmd->tlv_header =
		FIELD_PREP(WMI_TLV_TAG,
			   WMI_TAG_PEER_SMART_ANT_SET_NODE_CONFIG_OPS_CMD) |
		FIELD_PREP(WMI_TLV_LEN, sizeof(*cmd) - TLV_HDR_SIZE);
	cmd->vdev_id = arsta->arvif->vdev_id;
	ether_addr_copy(cmd->macaddr.addr, macaddr);
	cmd->cmd_id = cmd_id;
	cmd->args_count = args_count;

	ptr = skb->data + sizeof(*cmd);
	tlv = ptr;
	tlv->header = FIELD_PREP(WMI_TLV_TAG, WMI_TAG_ARRAY_UINT32) |
		      FIELD_PREP(WMI_TLV_LEN, args_count * sizeof(u32));
	ptr += TLV_HDR_SIZE;
	cfg_arg = (u32 *)ptr;
	for (i = 0; i < args_count; i++)
		cfg_arg[i] = args_arr[i];

	ret = ath12k_wmi_cmd_send(wmi, skb,
				  WMI_PEER_SMART_ANT_SET_NODE_CONFIG_OPS_CMDID);
	if (ret) {
		ath12k_warn(ar->ab,
			    "failed to send SMART_ANT_SET_NODE_CONFIG_OPS");
		dev_kfree_skb(skb);
	}

	return ret;
}

static void sa_core_extract_sta_legacy_ratecodes(const void *ptr, void *data)
{
	struct wmi_peer_cck_ofdm_rate_info *ofdm_rate =
				(struct wmi_peer_cck_ofdm_rate_info *)ptr;
	struct sa_sta_ratecode_list *param =
				(struct sa_sta_ratecode_list *)data;
	struct ath12k_sa_rate_cap *rate_cap = &param->rate_cap;
	u8 i = param->legacy_index;

	if (i >= (SA_MAX_CCK_OFDM_RATES - 1))
		return;

	rate_cap->rc_legacy[i] = ofdm_rate->ratecode_legacy & SA_MASK_RCODE;
	rate_cap->rc_legacy[i + 1] =
			(ofdm_rate->ratecode_legacy >> 16) & SA_MASK_RCODE;
	param->legacy_index += 2;
}

static void sa_core_extract_sta_mcs_ratecodes(const void *ptr, void *data)
{
	struct wmi_peer_mcs_rate_info *mcs_rate =
				(struct wmi_peer_mcs_rate_info *)ptr;
	struct sa_sta_ratecode_list *param =
				(struct sa_sta_ratecode_list *)data;
	struct ath12k_sa_rate_cap *rate_cap = &param->rate_cap;
	u8 j = param->mcs_index;

	if (j >= (SA_MAX_MCS_RATES - 1))
		return;

	rate_cap->rc_20[j] = mcs_rate->rt_code_20 & SA_MASK_RCODE;
	rate_cap->rc_40[j] = mcs_rate->rt_code_40 & SA_MASK_RCODE;
	rate_cap->rc_80[j] = mcs_rate->rt_code_80 & SA_MASK_RCODE;
	rate_cap->rc_160[j] = mcs_rate->rt_code_160 & SA_MASK_RCODE;
	rate_cap->rc_320[j] = mcs_rate->rt_code_320 & SA_MASK_RCODE;
	rate_cap->rc_20[j + 1] = (mcs_rate->rt_code_20 >> 16) & SA_MASK_RCODE;
	rate_cap->rc_40[j + 1] = (mcs_rate->rt_code_40 >> 16) & SA_MASK_RCODE;
	rate_cap->rc_80[j + 1] = (mcs_rate->rt_code_80 >> 16) & SA_MASK_RCODE;
	rate_cap->rc_160[j + 1] = (mcs_rate->rt_code_160 >> 16) & SA_MASK_RCODE;
	rate_cap->rc_320[j + 1] = (mcs_rate->rt_code_320 >> 16) & SA_MASK_RCODE;
	param->mcs_index += 2;
}

static int
sa_core_wmi_peer_rc_list_subtlv_parser(struct ath12k_base *ab, u16 tag, u16 len,
				       const void *ptr, void *data)
{
	int ret = 0;

	switch (tag) {
	case WMI_TAG_PEER_CCK_OFDM_RATE_INFO:
		sa_core_extract_sta_legacy_ratecodes(ptr, data);
		break;
	case WMI_TAG_PEER_MCS_RATE_INFO:
		sa_core_extract_sta_mcs_ratecodes(ptr, data);
		break;
	default:
		ath12k_warn(ab,
			    "Received invalid tag for peer ratecode in subtlvs\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void sa_core_peer_rc_list_fixed_param_parser(const void *ptr, void *data)
{
	struct wmi_peer_ratecode_list_fixed_param *fixed_param =
			(struct wmi_peer_ratecode_list_fixed_param *)ptr;
	struct sa_sta_ratecode_list *param =
					(struct sa_sta_ratecode_list *)data;
	u8 i;

	ether_addr_copy(param->mac_addr, fixed_param->macaddr.addr);
	param->pdev_id = fixed_param->pdev_id;
	/* Rate Code counts for legacy, 20Mhz, 40Mhz & 80MHz BW */
	for (i = 0; i < SA_BYTES_IN_DWORD; i++)
		param->rate_cap.num_rc[i] =
			(fixed_param->ratecount >> (i*8)) & SA_MASK_BYTE;
	/* Rate Code counts for 160 MHz BW */
	param->rate_cap.num_rc[i] = fixed_param->ratecount_ext & SA_MASK_BYTE;
	/* Rate Code counts for 320 MHz BW */
	param->rate_cap.num_rc[i+1] =
			(fixed_param->ratecount_ext >> 8) & SA_MASK_BYTE;

}

static int sa_core_wmi_peer_rc_list_ev_parser(struct ath12k_base *ab, u16 tag,
					      u16 len, const void *ptr,
					      void *data)
{
	int ret = 0;

	switch (tag) {
	case WMI_TAG_PEER_RATECODE_LIST_EVENT:
		sa_core_peer_rc_list_fixed_param_parser(ptr, data);
		break;
	case WMI_TAG_ARRAY_STRUCT:
		if (len == 0)
			return 0;
		ret = ath12k_wmi_tlv_iter(ab, ptr, len,
					  sa_core_wmi_peer_rc_list_subtlv_parser,
					  data);
		break;
	default:
		ath12k_warn(ab, "Received invalid TAG for peer ratecode list event\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

void sa_core_wmi_peer_rc_list_ev_handler(struct ath12k_base *ab,
					 struct sk_buff *skb)
{
	struct sa_sta_ratecode_list param = {0};
	struct ath12k_link_sta *arsta;
	int ret;

	ret = ath12k_wmi_tlv_iter(ab, skb->data, skb->len,
				  sa_core_wmi_peer_rc_list_ev_parser,
				  &param);
	if (ret) {
		ath12k_warn(ab, "failed to parse SA RateCode List ev %d\n",
			    ret);
		return;
	}

	if (!is_valid_ether_addr(param.mac_addr))
		return;

	ath12k_dbg(ab, ATH12K_DBG_ANY, "Peer ratecode list on pdev: %d for peer: %pM\n",
		   param.pdev_id, param.mac_addr);

	rcu_read_lock();
	arsta = ath12k_link_sta_find_by_addr(ab, param.mac_addr);
	if (!arsta || !arsta->arvif || !arsta->arvif->ar) {
		ath12k_warn(ab, "Peer not found %pM\n", param.mac_addr);
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();
	sa_core_peer_connect_notify(arsta, &param.rate_cap);
}

static int sa_core_trigger_training(struct ath12k_link_sta *arsta, u8 *macaddr)
{
	struct sa_sta_extn *sa_sta = &arsta->arsta_extn.sa_sta;
	struct smart_ant_train_info param = {0};
	int ret = 0;

	param.vdev_id = arsta->arvif->vdev_id;
	if (g_sa_ops->sa_get_traininginfo)
		ret = g_sa_ops->sa_get_traininginfo(sa_sta->ctx,
						    &param.rate_array[0],
						    &param.antenna_array[0],
						    &param.numpkts);
	param.minpkts = SA_INVALID_PARAM_VALUE;
	param.per_th = SA_INVALID_PARAM_VALUE;
	if (g_sa_ops->sa_get_ext_traininginfo)
		g_sa_ops->sa_get_ext_traininginfo(sa_sta->ctx,
						  &param.minpkts,
						  &param.per_th);

	if (!ret)
		ret = sa_core_wmi_set_train_info(arsta->arvif->ar,
						 macaddr, &param);
	else
		ath12k_err(arsta->arvif->ar->ab,
			   "Failed to get training params for STA %pM\n",
			   macaddr);

	return ret;
}

static inline void sa_core_extract_mac_from_params(u32 *params, u8 *mac)
{
	mac[0] = (u8)((params[0] & 0x0000FF00) >> 8);
	mac[1] = (u8)(params[0] & 0x000000FF);
	mac[2] = (u8)((params[1] & 0xFF000000) >> 24);
	mac[3] = (u8)((params[1] & 0x00FF0000) >> 16);
	mac[4] = (u8)((params[1] & 0x0000FF00) >> 8);
	mac[5] = (u8)(params[1] & 0x000000FF);
}

int sa_core_get_configs(struct ath12k *ar, u32 *params)
{
	struct ath12k_sa_config_params sa_params = {0};
	struct sa_sta_extn *sa_sta = NULL;
	struct ath12k_link_sta *arsta = NULL;
	u8 mac[ETH_ALEN] = {0};
	int ret = 0;

	if (!ar || !ar->ab || !params || !g_sa_ops || !g_sa_ops->sa_get_param)
		return -EINVAL;

	if (!ath12k_smart_ant_is_enable(ar->ab))
		return -EPERM;

	sa_params.radio_id = ar->radio_idx;
	sa_params.param_type = (params[0] & SA_PARM_TYPE_MASK) >>
				SA_PARM_TYPE_SHIFT;
	sa_params.param_id = params[2];

	if (sa_params.param_type) {
		sa_core_extract_mac_from_params(params, mac);

		rcu_read_lock();
		arsta = ath12k_link_sta_find_by_addr(ar->ab, mac);
		if (!arsta) {
			ath12k_err(ar->ab, "STA not found %pM\n", mac);
			rcu_read_unlock();
			return -EPERM;
		}
		rcu_read_unlock();
		sa_sta = &arsta->arsta_extn.sa_sta;
		if (!sa_sta->ctx) {
			ath12k_err(ar->ab, "Invalid context for STA %pM\n", mac);
			return -EPERM;
		}
		sa_params.ccp = sa_sta->ctx;
	}
	ret = g_sa_ops->sa_get_param(&sa_params);
	params[3] = sa_params.param_value;

	return ret;
}

int sa_core_set_configs(struct ath12k *ar, u32 *params)
{
	struct ath12k_sa_config_params sa_params = {0};
	struct sa_sta_extn *sa_sta = NULL;
	struct ath12k_link_sta *arsta = NULL;
	u32 value = 0;
	u32 ant_array[SA_MAX_SA_CHAINS] = {0};
	u8 mac[ETH_ALEN] = {0};
	int ret = 0;

	if (!ar || !ar->ab || !params || !g_sa_ops || !g_sa_ops->sa_set_param)
		return -EINVAL;

	if (!ath12k_smart_ant_is_enable(ar->ab))
		return -EPERM;

	sa_params.radio_id = ar->radio_idx;
	sa_params.param_type = (params[0] & SA_PARM_TYPE_MASK) >>
				SA_PARM_TYPE_SHIFT;
	sa_params.param_id = params[2];
	sa_params.param_value = params[3];

	if (sa_params.param_type) {
		sa_core_extract_mac_from_params(params, mac);

		rcu_read_lock();
		arsta = ath12k_link_sta_find_by_addr(ar->ab, mac);
		if (!arsta) {
			ath12k_err(ar->ab, "STA not found %pM\n", mac);
			rcu_read_unlock();
			return -EPERM;
		}
		rcu_read_unlock();
		sa_sta = &arsta->arsta_extn.sa_sta;
		if (!sa_sta->ctx) {
			ath12k_err(ar->ab, "Invalid context for STA %pM\n", mac);
			return -EPERM;
		}
		sa_params.ccp = sa_sta->ctx;
	}
	ret = g_sa_ops->sa_set_param(&sa_params);
	if (ret > 0) {
		if (ret & SA_RX_CONFIG_REQUIRED) {
			sa_core_get_rxantenna(ar->radio_idx, &value);
			sa_core_wmi_set_rx_antenna(ar, value);
		} else if (ret & SA_DEFAULT_TX_CONFIG_REQUIRED) {
			sa_core_get_txdefaultantenna(ar->radio_idx, &value);
			ath12k_wmi_pdev_set_param(ar,
				WMI_PDEV_PARAM_SMART_ANTENNA_DEFAULT_ANTENNA,
				value, ar->pdev->pdev_id);
		} else if (ret & SA_BEACON_ANT_CONFIG_REQUIRED) {
			sa_core_get_bcn_txantenna(ar->radio_idx, &value);
			ath12k_wmi_pdev_set_param(ar,
				WMI_PDEV_PARAM_SMART_ANTENNA_BEACON_ANTENNA,
				value, ar->pdev->pdev_id);
		} else if (ret & SA_PARALLEL_GPIO_CFG_REQUIRED) {
			sa_core_wmi_send_parallel_mode_gpio_drive(ar,
							sa_params.param_value);
		} else if ((ret & SA_TX_CONFIG_REQUIRED) && sa_sta) {
			sa_core_get_txantenna(sa_sta->ctx, &ant_array[0]);
			sa_core_wmi_set_tx_antenna(ar, arsta->arvif->vdev_id,
						   mac, ant_array);
		} else if ((ret & SA_TRAINING_REQUIRED) && sa_sta) {
			sa_core_trigger_training(arsta, mac);
		} else if ((ret & SA_TX_FEEDBACK_CONFIG_REQUIRED) && sa_sta) {
			sa_core_get_node_config(sa_sta->ctx,
						SA_TX_FEEDBACK_CONFIG, &value);
			sa_core_wmi_send_node_config_ops(arsta, mac,
							 SA_TX_FEEDBACK_CONFIG,
							 1, &value);
		}
	}

	return 0;
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
	struct ath12k_sa_gpio_config gpio_cfg = {0};
	int sa_mode;
	u32 rx_antenna, tx_defaultant;
	u32 tx_ant[WMI_SMART_ANT_MAX_RATE_SERIES] = {0};
	u8 i, ret;

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
	sa_core_get_gpio_config(ar->radio_idx, &gpio_cfg);
	/* Create smart antenna enabled and Tx/Rx feedback subscription state
	 * bitmap. bit 0 for smart antenna enabled/disabled, bit 4 for TX
	 * subscription state and bit 5 for RX subscription state.
	 */
	sa_ar->enable_mask = SA_ENABLE_MASK | (init_cfg.txrx_feedback &
			(SA_TX_FEEDBACK_MASK | SA_RX_FEEDBACK_MASK));
	if (init_flags & SA_NEW_CONFIG) {
		sa_ar->state = SA_INIT_DONE_STATE;
		ret = sa_core_wmi_send_smart_ant_enable(ar, tx_defaultant,
							rx_antenna, &gpio_cfg);
		if (ret) {
			ath12k_err(ab, "SA enable command failed for 0x%x\n",
				   sa_ar->enable_mask);
			return ret;
		}
		if (sa_ar->sa_mode == WMI_SMART_ANT_MODE_PARALLEL)
			sa_core_wmi_send_parallel_mode_gpio_drive(ar, ar->cfg_tx_chainmask);
		ret = sa_core_ppdu_notifier_register(sa_ar->enable_mask);
		if (ret)
			ath12k_warn(ab, "SA PPDU registration failed:%d\n",
				    ret);
	} else {
		sa_core_wmi_set_rx_antenna(ar, rx_antenna);
	}

	for (i = 0; i < WMI_SMART_ANT_MAX_RATE_SERIES; i++)
		tx_ant[i] = tx_defaultant;

	sa_core_wmi_set_tx_antenna(ar, arvif->vdev_id,
				   ahvif->vif->addr, tx_ant);

	return 0;
}

int sa_core_deinit(struct ath12k *ar, u8 init_flags)
{
	struct sa_base_extn *sa_base;
	struct sa_ar_extn *sa_ar;
	struct ath12k_base *ab;
	struct ath12k_sa_gpio_config gpio_cfg = {0};
	int ret;

	if (!g_sa_ops || !ar || !ar->ab)
		return -1;

	ab = ar->ab;
	sa_base = &ab->ath12k_base_extn.sa_base;
	if (!sa_base->sa_enable)
		return -1;

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

		ret = sa_core_ppdu_notifier_unregister(sa_ar->enable_mask);
		if (ret)
			ath12k_warn(ab, "SA PPDU unregistration failed:%d\n",
				    ret);
		sa_ar->enable_mask = 0;
		ret = sa_core_wmi_send_smart_ant_enable(ar, 0, 0, &gpio_cfg);
		if (ret)
			ath12k_err(ab, "SA disable failed %d\n", ret);
		if (sa_ar->sa_mode == WMI_SMART_ANT_MODE_PARALLEL)
			sa_core_wmi_send_parallel_mode_gpio_drive(ar, 0);

		sa_ar->state = SA_DEINIT_DONE_STATE;
	}

	return 0;
}
