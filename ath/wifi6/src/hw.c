// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>

#include "../../debug.h"
#include "../../core.h"
#include "../../ce.h"
#include "ce.h"
#include "../../hw.h"
#include "hw.h"
#include "../../qcn_extns/ath12k_cmn_extn.h"
#include "../../mhi.h"
#include "mhi.h"
#include "dp_rx.h"
#include "wmi.h"
#include "../../wow.h"
#include "../../debugfs_sta.h"
#include "../../debugfs.h"
#include "../../testmode.h"
#include "../../dp_peer.h"
#include "../../dp_tx.h"
#include "dp_tx.h"
#include "hal_qcn9074.h"
#include "../../cfr.h"
#include "../../dp_stats.h"


static u8 ath12k_wifi6_hw_qcn9074_mac_from_pdev_id(int pdev_idx)
{
	return pdev_idx;
}

static int
ath12k_wifi6_hw_mac_id_to_pdev_id_qcn9074(const struct ath12k_hw_params *hw,
					  int mac_id)
{
	return mac_id;
}

static int
ath12k_wifi6_hw_mac_id_to_srng_id_qcn9074(const struct ath12k_hw_params *hw,
					  int mac_id)
{
	return 0;
}

static u8 ath12k_wifi6_hw_get_ring_selector_qcn9074(struct sk_buff *skb)
{
	return smp_processor_id();
}

static bool ath12k_wifi6_dp_srng_is_comp_ring_qcn9074(int ring_num)
{
	if (ring_num < 3 || ring_num == 4)
		return true;

	return false;
}


void ath12k_hw_qcn9074_fill_cfr_hdr_info(struct ath12k *ar,
					 struct ath12k_csi_cfr_header *header,
					 struct ath12k_cfr_peer_tx_param *params)
{
	header->start_magic_num = ATH12K_CFR_START_MAGIC;
	header->vendorid = VENDOR_QCA;
	header->pltform_type = PLATFORM_TYPE_ARM;
	header->cfr_metadata_len = sizeof(struct cfr_enh_metadata);
	header->cfr_data_version = ATH12K_CFR_DATA_VERSION_1;
	header->host_real_ts = ktime_to_ns(ktime_get_real());

	header->cfr_metadata_version = ATH12K_CFR_META_VERSION_9;
	if (ar->ab->hw_rev == ATH12K_HW_QCN6432_HW10)
		header->chip_type = ATH12K_CFR_RADIO_QCN6432;
	else if (ar->ab->hw_rev == ATH12K_HW_IPQ5424_HW10)
		header->chip_type = ATH12K_CFR_RADIO_IPQ5424;
	else if (ar->ab->hw_rev == ATH12K_HW_IPQ5332_HW10)
		header->chip_type = ATH12K_CFR_RADIO_IPQ5332;
	else
		header->chip_type = ATH12K_CFR_RADIO_QCN9274;

	header->u.meta_enh.status = FIELD_GET(WMI_CFR_PEER_CAPTURE_STATUS,
					      params->status);
	header->u.meta_enh.capture_bw = params->bandwidth;
	header->u.meta_enh.phy_mode = params->phy_mode;
	header->u.meta_enh.prim20_chan = params->primary_20mhz_chan;
	header->u.meta_enh.center_freq1 = params->band_center_freq1;
	header->u.meta_enh.center_freq2 = params->band_center_freq2;
	header->u.meta_enh.capture_mode = params->bandwidth ?
		ATH12K_CFR_CAPTURE_DUP_LEGACY_ACK : ATH12K_CFR_CAPTURE_LEGACY_ACK;
	header->u.meta_enh.capture_type = params->capture_method;
	header->u.meta_enh.num_rx_chain = ar->cfg_rx_chainmask;
	header->u.meta_enh.sts_count = params->spatial_streams;
	header->u.meta_enh.timestamp = params->timestamp_us;
	header->u.meta_enh.rx_start_ts = params->rx_start_ts;
	header->u.meta_enh.cfo_measurement = params->cfo_measurement;
	header->u.meta_enh.mcs_rate = params->mcs_rate;
	header->u.meta_enh.gi_type = params->gi_type;

	memcpy(header->u.meta_enh.peer_addr.su_peer_addr,
	       params->peer_mac_addr, ETH_ALEN);
	memcpy(header->u.meta_enh.chain_rssi, params->chain_rssi,
	       sizeof(params->chain_rssi));
	memcpy(header->u.meta_enh.chain_phase, params->chain_phase,
	       sizeof(params->chain_phase));
	memcpy(header->u.meta_enh.agc_gain, params->agc_gain,
	       sizeof(params->agc_gain));
	memcpy(header->u.meta_enh.agc_gain_tbl_index, params->agc_gain_tbl_index,
	       sizeof(params->agc_gain_tbl_index));
}

static const struct ath12k_hw_ops qcn9074_ops = {
	.get_hw_mac_from_pdev_id = ath12k_wifi6_hw_qcn9074_mac_from_pdev_id,
	.mac_id_to_pdev_id = ath12k_wifi6_hw_mac_id_to_pdev_id_qcn9074,
	.mac_id_to_srng_id = ath12k_wifi6_hw_mac_id_to_srng_id_qcn9074,
	.rxdma_ring_sel_config = ath12k_wifi6_dp_rxdma_ring_sel_config_qcn9274,
	.get_ring_selector = ath12k_wifi6_hw_get_ring_selector_qcn9074,
	.dp_srng_is_tx_comp_ring = ath12k_wifi6_dp_srng_is_comp_ring_qcn9074,
	.fill_cfr_hdr_info = ath12k_hw_qcn9074_fill_cfr_hdr_info,
};


#define ATH12K_TX_RING_MASK_0 0x1
#define ATH12K_TX_RING_MASK_1 0x2
#define ATH12K_TX_RING_MASK_2 0x4
#define ATH12K_TX_RING_MASK_3 0x8
#define ATH12K_TX_RING_MASK_4 0x10

#define ATH12K_RX_RING_MASK_0 0x1
#define ATH12K_RX_RING_MASK_1 0x2
#define ATH12K_RX_RING_MASK_2 0x4
#define ATH12K_RX_RING_MASK_3 0x8

#define ATH12K_RX_ERR_RING_MASK_0 0x1

#define ATH12K_RX_WBM_REL_RING_MASK_0 0x1

#define ATH12K_REO_STATUS_RING_MASK_0 0x1

#define ATH12K_HOST2RXDMA_RING_MASK_0 0x1
#define ATH12K_HOST2RXDMA_RING_MASK_1 0x2
#define ATH12K_HOST2RXDMA_RING_MASK_2 0x4

#define	ATH12K_HOST2RXMON_RING_MASK_0	0x1

#define ATH12K_RX_MON_RING_MASK_0 0x1
#define ATH12K_RX_MON_RING_MASK_1 0x2
#define ATH12K_RX_MON_RING_MASK_2 0x4

#define ATH12K_TX_MON_RING_MASK_0 0x1
#define ATH12K_TX_MON_RING_MASK_1 0x2
#define ATH12K_UMAC_RESET_INTR_MASK_0   0x1

#define ATH12K_PPE2TCL_RING_MASK_0 0x1
#define ATH12K_REO2PPE_RING_MASK_0 0x1
#define ATH12K_PPE_WBM2SW_RELEASE_RING_MASK_0 0x1

/* To support 8 MSI DP grouping */
static struct ath12k_hw_ring_mask ath12k_wifi6_hw_ring_mask_qcn9074_msi8 = {
	.tx  = {
		ATH12K_TX_RING_MASK_0,
		ATH12K_TX_RING_MASK_1,
		ATH12K_TX_RING_MASK_2 | ATH12K_TX_RING_MASK_3,
		0, 0, 0, 0, 0
	},
	.host2rxmon = {
		0, 0, 0,
		ATH12K_HOST2RXMON_RING_MASK_0,
	},
	.rx_mon_dest = {
		0, 0,
		ATH12K_RX_MON_RING_MASK_0,
		ATH12K_RX_MON_RING_MASK_1,
		ATH12K_RX_MON_RING_MASK_2,
		0, 0, 0
	},
	.rx = {
		0, 0, 0,
		ATH12K_RX_RING_MASK_0,
		ATH12K_RX_RING_MASK_1,
		ATH12K_RX_RING_MASK_2 | ATH12K_RX_RING_MASK_3,
		0, 0
	},
	.rx_err = {
		0, 0,
		ATH12K_RX_ERR_RING_MASK_0,
		0, 0, 0, 0
	},
	.rx_wbm_rel = {
		0, 0,
		ATH12K_RX_WBM_REL_RING_MASK_0,
		0, 0, 0, 0
	},
	.reo_status = {
		0, 0,
		ATH12K_REO_STATUS_RING_MASK_0,
		0, 0, 0, 0
	},
	.host2rxdma = {
		0, 0,
		ATH12K_HOST2RXDMA_RING_MASK_0,
		0, 0, 0, 0
	},
	.tx_mon_dest = {
		ATH12K_TX_MON_RING_MASK_0,
		ATH12K_TX_MON_RING_MASK_1,
		0, 0, 0, 0, 0, 0
	},
#ifdef CPTCFG_ATH12K_PPE_DS_SUPPORT
	.ppe2tcl = {
		0, 0, 0, 0, 0, 0,
		ATH12K_PPE2TCL_RING_MASK_0,
		0
	},
	.reo2ppe = {
		0, 0, 0, 0, 0,
		ATH12K_REO2PPE_RING_MASK_0,
		0, 0
	},
	.wbm2sw6_ppeds_tx_cmpln = {
		ATH12K_PPE_WBM2SW_RELEASE_RING_MASK_0,
		0, 0, 0, 0, 0, 0, 0
	},
#endif
	.umac_dp_reset = {
		0, 0, 0, 0, 0, 0, 0,
		ATH12K_UMAC_RESET_INTR_MASK_0
	},
};

#define ATH12K_RX_MON_STATUS_RING_MASK_0 0x1
#define ATH12K_RX_MON_STATUS_RING_MASK_1 0x2
#define ATH12K_RX_MON_STATUS_RING_MASK_2 0x4

static struct ath12k_hw_ring_mask ath12k_wifi6_hw_ring_mask_qcn9074 = {
	.tx  = {
		ATH12K_TX_RING_MASK_0,
		ATH12K_TX_RING_MASK_1,
		ATH12K_TX_RING_MASK_2,
	},
	.rx_mon_status = {
		0, 0, 0,
		ATH12K_RX_MON_STATUS_RING_MASK_0,
		ATH12K_RX_MON_STATUS_RING_MASK_1,
		ATH12K_RX_MON_STATUS_RING_MASK_2,
	},
	.rx = {
		0, 0, 0, 0,
		ATH12K_RX_RING_MASK_0,
		ATH12K_RX_RING_MASK_1,
		ATH12K_RX_RING_MASK_2,
		ATH12K_RX_RING_MASK_3,
	},
	.rx_err = {
		0, 0, 0,
		ATH12K_RX_ERR_RING_MASK_0,
	},
	.rx_wbm_rel = {
		0, 0, 0,
		ATH12K_RX_WBM_REL_RING_MASK_0,
	},
	.reo_status = {
		0, 0, 0,
		ATH12K_REO_STATUS_RING_MASK_0,
	},
	.host2rxdma = {
		0, 0, 0,
		ATH12K_HOST2RXDMA_RING_MASK_0,
	},
};

static struct ath12k_hw_params ath12k_wifi6_hw_params[] = {
	{
		.name = "qcn9074 hw1.0",
		.hw_rev = ATH12K_HW_QCN9074_HW10,
		.fw = {
			.dir = "QCN9074/hw1.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
			.m3_loader = ath12k_m3_fw_loader_driver,
		},
		.max_radios = 1,
		.single_pdev_only = false,
		.qmi_service_ins_id = ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_QCN9074,
		.internal_sleep_clock = false,

		.hw_ops = &qcn9074_ops,
		.ring_mask = &ath12k_wifi6_hw_ring_mask_qcn9074,

		.host_ce_config = ath12k_wifi6_host_ce_config_qcn9074,
		.ce_count = 6,
		.target_ce_config = ath12k_wifi6_target_ce_config_wlan_qcn9074,
		.target_ce_count = 9,
		.svc_to_ce_map =
			ath12k_wifi6_target_service_to_ce_map_wlan_qcn9074,
		.svc_to_ce_map_len = 18,

		.rxdma1_enable = true,
		.num_rxdma_per_pdev = 1,
		.num_rxdma_dst_ring = 0,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP) |
					BIT(NL80211_IFTYPE_MESH_POINT),
		.supports_monitor = true,

		.idle_ps = false,
		.cold_boot_calib = false,
		.download_calib = true,
		.supports_suspend = false,
		.reoq_lut_support = false,
		.supports_shadow_regs = false,

		.num_tcl_banks = 48,
		.max_tx_ring = 4,

		.mhi_config = &ath12k_wifi6_mhi_config_qcn9074,

		.wmi_init = ath12k_wifi6_wmi_init_qcn9074,

		.hal_ops = &hal_qcn9074_ops,

		.qmi_cnss_feature_bitmap = BIT(CNSS_QDSS_CFG_MISS_V01),

		.rfkill_pin = 0,
		.rfkill_cfg = 0,
		.rfkill_on_level = 0,

		.rddm_size = 0x600000,

		.def_num_link = 0,
		.max_mlo_peer = 256,

		.otp_board_id_register = QCN9274_QFPROM_RAW_RFA_PDET_ROW13_LSB,

		.supports_sta_ps = false,

		.acpi_guid = NULL,
		.supports_dynamic_smps_6ghz = false,

		.iova_mask = 0,

		.supports_aspm = false,

		.current_cc_support = false,

		.ce_ie_addr = NULL,
		.ce_remap = NULL,
		.handle_beacon_miss = true,
		.en_qdsslog = false,
		.support_fse = false,
		.alloc_cacheable_memory = true,
		.spectral = {
			.fft_sz = 2,
			.fft_pad_sz = 0,
			.summary_pad_sz = 16,
			.fft_hdr_len = 24,
			.max_fft_bins = 1024,
			.fragment_160mhz = false,
		},
		.supports_ap_ps = true,
		.ftm_responder = true,
		.credit_flow = false,
		.is_plink_preferable = true,
		.support_umac_reset = false,
		.umac_irq_line_reset = false,
		.umac_reset_ipc = 0,
		.ds_support = false,
		.cfr_support = true,
		.cfr_dma_hdr_size = sizeof(struct ath12k_cfir_enh_dma_hdr),
		.cfr_num_stream_bufs = 255,
		/* sizeof (ath12k_csi_cfr_header) + max cfr header(200 bytes) +
		 * max cfr payload(16384 bytes)
		 */
		.cfr_stream_buf_size = 16716,
		.mlo_3_link_tx_support = false,
		.board_magic = "QCA-ATH11K-BOARD",
	},
};

void ath12k_wifi6_ieee80211_free_txskb(struct ieee80211_hw *hw,
				       struct sk_buff *skb,
				       struct ath12k_dp_vif *dp_vif,
				       enum ath12k_dp_tx_enq_error drop_reason,
				       u8 ring_id,
				       bool dev_free)
{
	if (unlikely(drop_reason > DP_TX_ENQ_ERR_MAX))
		DP_STATS_INC(dp_vif, tx_i.drop[DP_TX_ENQ_DROP_MISC], 1, ring_id);
	else
		DP_STATS_INC(dp_vif, tx_i.drop[drop_reason], 1, ring_id);

	if (dev_free)
		dev_kfree_skb_any(skb);
	else
		ieee80211_free_txskb(hw, skb);
}

static inline bool
ath12k_wifi6_check_err_code_debug_logging(enum ath12k_dp_tx_enq_error err)
{
	if ((err == DP_TX_ENQ_DROP_SW_DESC_NA) || (err == DP_TX_ENQ_DROP_EXT_DESC_NA) ||
	    (err == DP_TX_ENQ_DROP_TCL_DESC_NA))
		return true;

	return false;
}

/* Note: called under rcu_read_lock() */
static void ath12k_wifi6_mac_op_tx(struct ieee80211_hw *hw,
				   struct ieee80211_tx_control *control,
				   struct sk_buff *skb)
{
	struct ath12k_skb_cb *skb_cb = ATH12K_SKB_CB(skb);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif = &ahvif->deflink;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ath12k_mgmt_frame_stats *mgmt_stats = &ahvif->mgmt_stats;
	struct ieee80211_sta *sta = control->sta;
	struct ath12k_link_sta *arsta = NULL;
	struct ath12k_link_vif *tmp_arvif;
	struct ath12k_sta *ahsta = NULL;
	u32 info_flags = info->flags;
	struct ieee80211_mgmt *mgmt;
	struct sk_buff *msdu_copied;
	struct ath12k *ar, *tmp_ar;
	struct ath12k_pdev_dp *dp_pdev, *tmp_dp_pdev;
	struct ath12k_dp_link_peer *peer = NULL;
	struct ath12k_dp_vif *dp_vif = &ahvif->dp_vif;
	struct ath12k_dp *dp = NULL;
	unsigned long links_map;
	bool is_mcast = false, is_eth = false;
	bool is_dvlan = false;
	struct ethhdr *eth;
	bool is_prb_rsp;
	u32 qos_nw_delay = info->sawf.nw_delay;
	u16 frm_type = 0;
	u16 mcbc_gsn;
	u8 link_id, tid;
	int ret;
	u8 qos_tag;
	enum ath12k_dp_tx_enq_error err;
	u8 ring_id = 0, ring_selector = 0;

	if (ahvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ath12k_wifi6_ieee80211_free_txskb(hw, skb, dp_vif,
						  DP_TX_ENQ_DROP_VIF_TYPE_MON,
						  ring_id, false);
		return;
	}

	link_id = u32_get_bits(info->control.flags, IEEE80211_TX_CTRL_MLO_LINK);
	if (unlikely(!(skb->fast_xmit &&
		     ((skb->mark & ATH12K_MLO_METADATA_MLO_ASSIST_TAG_MASK) ==
		      ATH12K_MLO_METADATA_MLO_ASSIST_TAG)) || !hw->perf_mode)) {
		memset(skb_cb, 0, sizeof(*skb_cb));
		skb_cb->vif = vif;

		if (key) {
			skb_cb->cipher = key->cipher;
			skb_cb->flags |= ATH12K_SKB_CIPHER_SET;
		}
	}

	/* handle only for MLO case, use deflink for non MLO case */
#ifdef CPTCFG_MAC80211_SFE_SUPPORT
	if (likely(skb->fast_xmit &&
		   ((skb->mark & ATH12K_MLO_METADATA_MLO_ASSIST_TAG_MASK) ==
		    ATH12K_MLO_METADATA_MLO_ASSIST_TAG))) {
		link_id =  u32_get_bits(skb->mark, ATH12K_MLO_METADATA_LINKID_MASK);
		skb_cb->link_id = link_id;

		arvif = rcu_dereference(ahvif->link[link_id]);

		if (unlikely(!arvif || !arvif->ar)) {
			ath12k_wifi6_ieee80211_free_txskb(hw, skb, dp_vif,
							  DP_TX_ENQ_DROP_INV_ARVIF_FAST,
							  ring_id, false);

			return;
		}

		ar = arvif->ar;

		dp_pdev = ath12k_dp_to_dp_pdev(ar->ab->dp, ar->pdev_idx);
		if (unlikely(!dp_pdev)) {
			ath12k_wifi6_ieee80211_free_txskb(hw, skb, dp_vif,
							  DP_TX_ENQ_DROP_INV_PDEV_FAST,
							  ring_id, false);

			return;
		}

		ret = ath12k_mac_tx_check_max_limit(dp_pdev, skb);
		if (unlikely(ret)) {
			enum ath12k_dp_tx_enq_error err_code;

			ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
				   "failed due to limit check pdev idx %d\n",
				   ar->pdev_idx);
			err_code = DP_TX_ENQ_DROP_MAX_TX_LIMIT_FAST;
			ath12k_wifi6_ieee80211_free_txskb(hw, skb, dp_vif,
							  err_code,
							  ring_id, false);

			return;
		}

		switch (ahvif->dp_vif.tx_encap_type) {
		case ATH12K_HW_TXRX_ETHERNET:
			skb_cb->flags |= ATH12K_SKB_HW_80211_ENCAP;
			err = ath12k_wifi6_dp_tx_fast(dp_pdev, arvif,
						      skb,
						      qos_nw_delay);
			break;
		case ATH12K_HW_TXRX_NATIVE_WIFI:
			ath12k_dp_tx_encap_nwifi(skb);
			err = ath12k_wifi6_dp_tx_fast(dp_pdev, arvif,
						      skb,
						      qos_nw_delay);
			break;
		case ATH12K_HW_TXRX_RAW:
		default:
			err = DP_TX_ENQ_DROP_INV_ENCAP_FAST;
		}
		if (unlikely(err)) {
			ring_id = smp_processor_id();

			if (ath12k_wifi6_check_err_code_debug_logging(err))
				ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
					   "failed to transmit frame %d\n", err);
			else
				ath12k_warn(ar->ab, "failed to transmit frame %d\n", err);

			ath12k_wifi6_ieee80211_free_txskb(ar->ah->hw, skb, dp_vif,
							  err, ring_id, false);

			return;
		}
		if (unlikely(ath12k_dp_stats_enabled(dp_pdev) &&
			     ath12k_tid_stats_enabled(dp_pdev))) {
			tid = skb->priority &
			      IEEE80211_QOS_CTL_TID_MASK;
			ath12k_tid_tx_stats(ahvif, tid, skb->len,
					    ATH_TX_SFE_PKTS);
		}

		return;
	} else if (ieee80211_vif_is_mld(vif)) {
#else
	if (ieee80211_vif_is_mld(vif)) {
#endif
		link_id = ath12k_mac_get_tx_link(sta, vif, link_id, skb, info_flags);
		if (link_id >= ATH12K_NUM_MAX_LINKS ||
		    (ATH12K_SCAN_LINKS_MASK & BIT(link_id))) {
			ath12k_wifi6_ieee80211_free_txskb(hw, skb, dp_vif,
							  DP_TX_ENQ_DROP_INV_LINK,
							  ring_id, false);
			return;
		}
	} else {
		link_id = 0;
	}

	arvif = rcu_dereference(ahvif->link[link_id]);
	if (!arvif || !arvif->ar) {
		ath12k_wifi6_ieee80211_free_txskb(hw, skb, dp_vif,
						  DP_TX_ENQ_DROP_INV_ARVIF,
						  ring_id, false);
		return;
	}

	ar = arvif->ar;
	skb_cb->link_id = link_id;

	if (unlikely(test_bit(ATH12K_FLAG_RECOVERY, &ar->ab->dev_flags))) {
		ieee80211_free_txskb(hw, skb);
		return;
	}

	if (sta) {
		ahsta = ath12k_sta_to_ahsta(sta);
		qos_tag = u32_get_bits(skb->mark, QOS_TAG_MASK);
		if (ahsta->use_4addr_set || qos_tag)
			arsta = rcu_dereference(ahsta->link[link_id]);
	}

	/* as skb_cb is common currently for dp and mgmt tx processing
	 * set this in the common mac op tx function.
	 */
	skb_cb->u.ar = ar;
	is_prb_rsp = ieee80211_is_probe_resp(hdr->frame_control);

	if (info_flags & IEEE80211_TX_CTL_HW_80211_ENCAP) {
		eth = (struct ethhdr *)skb->data;
		is_mcast = is_multicast_ether_addr(eth->h_dest);
		is_eth = true;
		skb_cb->flags |= ATH12K_SKB_HW_80211_ENCAP;
	} else if (ieee80211_is_mgmt(hdr->frame_control)) {
		if (is_prb_rsp && arvif->tbtt_offset) {
			u64 adjusted_tsf;

			mgmt = (struct ieee80211_mgmt *)skb->data;
			adjusted_tsf = cpu_to_le64(0ULL - arvif->tbtt_offset);
			memcpy(&mgmt->u.probe_resp.timestamp, &adjusted_tsf,
			       sizeof(adjusted_tsf));
		}

		if (ath12k_mac_is_bridge_vdev(arvif)) {
			ath12k_wifi6_ieee80211_free_txskb(hw, skb, dp_vif,
							  DP_TX_ENQ_DROP_BRIDGE_VDEV,
							  ring_id, false);
			return;
		}

		frm_type = FIELD_GET(IEEE80211_FCTL_STYPE, hdr->frame_control);
		ret = ath12k_mac_mgmt_tx(ar, skb, is_prb_rsp);
		if (ret) {
			if (ret != -EBUSY)
				ath12k_warn(ar->ab,
					    "failed to queue mgmt stype 0x%x frame %d\n",
					    frm_type, ret);
			ath12k_wifi6_ieee80211_free_txskb(hw, skb, dp_vif,
							  DP_TX_ENQ_DROP_MGMT_FRAME,
							  ring_id, false);
			spin_lock_bh(&ar->data_lock);
			mgmt_stats->tx_fail_cnt[frm_type]++;
			spin_unlock_bh(&ar->data_lock);
		} else {
			spin_lock_bh(&ar->data_lock);
			mgmt_stats->tx_succ_cnt[frm_type]++;
			spin_unlock_bh(&ar->data_lock);
		}
		return;
	}

	if (!(info_flags & IEEE80211_TX_CTL_HW_80211_ENCAP))
		is_mcast = is_multicast_ether_addr(hdr->addr1);

	/* This is case only for P2P_GO */
	if (vif->type == NL80211_IFTYPE_AP && vif->p2p)
		ath12k_mac_add_p2p_noa_ie(ar, vif, skb, is_prb_rsp);

	dp_pdev = ath12k_dp_to_dp_pdev(ar->ab->dp, ar->pdev_idx);
	if (!dp_pdev) {
		ath12k_wifi6_ieee80211_free_txskb(hw, skb, dp_vif,
						  DP_TX_ENQ_DROP_INV_PDEV,
						  ring_id, false);
		return;
	}

	dp = dp_pdev->dp;
	ring_selector = dp->hw_params->hw_ops->get_ring_selector(skb);
	ring_id = ring_selector % dp->hw_params->max_tx_ring;

	/* Checking if it is a DVLAN frame */
	if (!test_bit(ATH12K_GROUP_FLAG_HW_CRYPTO_DISABLED, &ar->ab->ag->flags) &&
	    !(skb_cb->flags & ATH12K_SKB_HW_80211_ENCAP) &&
	    !(skb_cb->flags & ATH12K_SKB_CIPHER_SET) &&
	    ieee80211_has_protected(hdr->frame_control))
		is_dvlan = true;

	if (!vif->valid_links || !is_mcast || is_dvlan ||
	    (is_eth && (!is_mcast || sta)) ||
	    test_bit(ATH12K_GROUP_FLAG_RAW_MODE, &ar->ab->ag->flags)) {
		ret = ath12k_mac_tx_check_max_limit(dp_pdev, skb);
		if (ret) {
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
				   "failed due to limit check pdev idx %d\n",
				   ar->pdev_idx);
			ath12k_wifi6_ieee80211_free_txskb(hw, skb, dp_vif,
							  DP_TX_ENQ_DROP_MAX_TX_LIMIT,
							  ring_id, false);
			return;
		}

		err = ath12k_wifi6_dp_tx(dp_pdev, arvif, skb, false, 0, is_mcast,
					 arsta, ring_id, qos_nw_delay);
		if (unlikely(err)) {
			if (ath12k_wifi6_check_err_code_debug_logging(err))
				ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
					   "failed to transmit frame %d\n", err);
			else
				ath12k_warn(ar->ab, "failed to transmit frame %d\n", err);

			ath12k_wifi6_ieee80211_free_txskb(ar->ah->hw, skb, dp_vif,
							  err, ring_id, false);
			return;
		}
	} else {
		mcbc_gsn = atomic_inc_return(&ahvif->dp_vif.mcbc_gsn) & 0xfff;

		links_map = ahvif->links_map;
		for_each_set_bit(link_id, &links_map,
				 IEEE80211_MLD_MAX_NUM_LINKS) {
			tmp_arvif = rcu_dereference(ahvif->link[link_id]);
			if (!tmp_arvif || !tmp_arvif->is_up)
				continue;

			tmp_ar = tmp_arvif->ar;
			tmp_dp_pdev = ath12k_dp_to_dp_pdev(tmp_ar->ab->dp,
							   tmp_ar->pdev_idx);
			if (!tmp_dp_pdev)
				continue;

			ret = ath12k_mac_tx_check_max_limit(tmp_dp_pdev, skb);
			if (ret) {
				ath12k_dbg(tmp_ar->ab, ATH12K_DBG_MAC,
					   "failed mcast tx due to limit check pdev idx %d\n",
					    tmp_ar->pdev_idx);
				continue;
			}

			if (is_eth) {
				msdu_copied = skb_clone(skb, GFP_ATOMIC);
				if (!msdu_copied) {
					ath12k_err(ar->ab,
						   "skb clone failure link_id 0x%X vdevid 0x%X\n",
						   link_id, tmp_arvif->vdev_id);
					continue;
				}
				skb_cb = ATH12K_SKB_CB(msdu_copied);
				goto skip_nwifi;
			}
			msdu_copied = skb_copy(skb, GFP_ATOMIC);
			if (!msdu_copied) {
				ath12k_err(ar->ab,
					   "skb copy failure link_id 0x%X vdevid 0x%X\n",
					   link_id, tmp_arvif->vdev_id);
				continue;
			}

			ath12k_mlo_mcast_update_tx_link_address(vif, link_id,
								msdu_copied,
								info_flags);

			skb_cb = ATH12K_SKB_CB(msdu_copied);
skip_nwifi:
			skb_cb->link_id = link_id;
			skb_cb->vif = vif;
			skb_cb->u.ar = tmp_ar;

			if (ahsta && ahsta->use_4addr_set)
				arsta = rcu_dereference(ahsta->link[link_id]);

			/* For open mode, skip peer find logic */
			if (unlikely(arvif->key_cipher == WMI_CIPHER_NONE))
				goto skip_peer_find;

			spin_lock_bh(&tmp_ar->ab->dp->dp_lock);
			peer = ath12k_dp_link_peer_find_by_addr(tmp_ar->ab->dp,
								tmp_arvif->bssid);
			if (!peer) {
				spin_unlock_bh(&tmp_ar->ab->dp->dp_lock);
				ath12k_warn(tmp_ar->ab,
					    "failed to find peer for vdev_id 0x%X addr %pM link_map 0x%X\n",
					    tmp_arvif->vdev_id, tmp_arvif->bssid,
					    ahvif->links_map);
				ath12k_wifi6_ieee80211_free_txskb(hw, msdu_copied,
								  dp_vif,
								  DP_TX_ENQ_DROP_INV_PEER,
								  ring_id, true);

				continue;
			}

			key = peer->dp_peer->keys[peer->dp_peer->mcast_keyidx];
			if (key) {
				skb_cb->cipher = key->cipher;
				skb_cb->flags |= ATH12K_SKB_CIPHER_SET;

				if (!is_eth) {
					hdr = (struct ieee80211_hdr *)msdu_copied->data;
					if (!ieee80211_has_protected(hdr->frame_control))
						hdr->frame_control |=
						cpu_to_le16(IEEE80211_FCTL_PROTECTED);
				}
			}
			spin_unlock_bh(&tmp_ar->ab->dp->dp_lock);

skip_peer_find:
			err = ath12k_wifi6_dp_tx(tmp_dp_pdev, tmp_arvif,
						 msdu_copied, true, mcbc_gsn,
						 is_mcast, arsta, ring_id,
						 qos_nw_delay);
			if (unlikely(err)) {
				if (ath12k_wifi6_check_err_code_debug_logging(err))
					ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
						   "failed to transmit frame %d\n", err);
				else
					ath12k_warn(ar->ab,
						    "failed to transmit frame %d\n", err);

				ath12k_wifi6_ieee80211_free_txskb(hw, msdu_copied,
								  dp_vif, err,
								  ring_id, true);
			}
		}
		ieee80211_free_txskb(ar->ah->hw, skb);
	}
}

static const struct ieee80211_ops ath12k_ops_wifi6 = {
	.tx				= ath12k_wifi6_mac_op_tx,
	.wake_tx_queue			= ieee80211_handle_wake_tx_queue,
	.start                          = ath12k_mac_op_start,
	.stop                           = ath12k_mac_op_stop,
	.reconfig_complete              = ath12k_mac_op_reconfig_complete,
	.add_interface                  = ath12k_mac_op_add_interface,
	.remove_interface		= ath12k_mac_op_remove_interface,
	.update_vif_offload		= ath12k_mac_op_update_vif_offload,
	.config                         = ath12k_mac_op_config,
	.sta_set_4addr			= ath12k_mac_op_sta_set_4addr,
	.link_info_changed              = ath12k_mac_op_link_info_changed,
	.vif_cfg_changed		= ath12k_mac_op_vif_cfg_changed,
	.change_vif_links               = ath12k_mac_op_change_vif_links,
	.configure_filter		= ath12k_mac_op_configure_filter,
	.hw_scan                        = ath12k_mac_op_hw_scan,
	.cancel_hw_scan                 = ath12k_mac_op_cancel_hw_scan,
	.set_key                        = ath12k_mac_op_set_key,
	.set_rekey_data	                = ath12k_mac_op_set_rekey_data,
	.sta_state                      = ath12k_mac_op_sta_state,
	.sta_set_txpwr			= ath12k_mac_op_sta_set_txpwr,
	.link_sta_rc_update		= ath12k_mac_op_link_sta_rc_update,
	.conf_tx                        = ath12k_mac_op_conf_tx,
	.set_antenna			= ath12k_mac_op_set_antenna,
	.get_antenna			= ath12k_mac_op_get_antenna,
	.ampdu_action			= ath12k_mac_op_ampdu_action,
	.add_chanctx			= ath12k_mac_op_add_chanctx,
	.remove_chanctx			= ath12k_mac_op_remove_chanctx,
	.change_chanctx			= ath12k_mac_op_change_chanctx,
	.assign_vif_chanctx		= ath12k_mac_op_assign_vif_chanctx,
	.unassign_vif_chanctx		= ath12k_mac_op_unassign_vif_chanctx,
	.switch_vif_chanctx		= ath12k_mac_op_switch_vif_chanctx,
	.get_txpower			= ath12k_mac_op_get_txpower,
	.set_rts_threshold		= ath12k_mac_op_set_rts_threshold,
	.set_frag_threshold		= ath12k_mac_op_set_frag_threshold,
	.set_bitrate_mask		= ath12k_mac_op_set_bitrate_mask,
	.get_survey			= ath12k_mac_op_get_survey,
	.flush				= ath12k_mac_op_flush,
	.sta_statistics			= ath12k_mac_op_sta_statistics,
	.link_sta_statistics		= ath12k_mac_op_link_sta_statistics,
	.remain_on_channel              = ath12k_mac_op_remain_on_channel,
	.cancel_remain_on_channel       = ath12k_mac_op_cancel_remain_on_channel,
	.change_sta_links               = ath12k_mac_op_change_sta_links,
	.can_activate_links             = ath12k_mac_op_can_activate_links,
	.set_dscp_tid                   = ath12k_mac_op_set_dscp_tid,
#ifdef CONFIG_PM
	.suspend			= ath12k_wow_op_suspend,
	.resume				= ath12k_wow_op_resume,
	.set_wakeup			= ath12k_wow_op_set_wakeup,
#endif
#ifdef CPTCFG_ATH12K_DEBUGFS
	.vif_add_debugfs                = ath12k_debugfs_op_vif_add,
#endif
	CFG80211_TESTMODE_CMD(ath12k_tm_cmd)
#ifdef CPTCFG_ATH12K_DEBUGFS
	.sta_add_debugfs                = ath12k_debugfs_sta_op_add,
	.link_sta_add_debugfs           = ath12k_debugfs_link_sta_op_add,
#endif
	.link_reconfig_remove           = ath12k_mac_op_link_reconfig_remove,
	.removed_link_is_primary        = ath12k_mac_op_removed_link_is_primary,
#ifdef CPTCFG_ATH12K_PPE_DS_SUPPORT
	.change_mtu			= ath12k_mac_op_set_mtu,
#endif
	.can_neg_ttlm			= ath12k_mac_op_can_neg_ttlm,
	.apply_neg_ttlm_per_client	= ath12k_mac_op_apply_neg_ttlm_per_client,
	.set_radar_background           = ath12k_mac_op_set_radar_background,
	.erp                            = ath12k_mac_op_erp,
	.qos_mgmt_cfg                   = ath12k_mac_op_qos_mgmt_cfg,
	.get_afc_eirp_pwr               = ath12k_mac_op_get_afc_eirp_pwr,
	.get_6ghz_dev_deployment_type	= ath12k_mac_op_get_6ghz_dev_deployment_type,
};

int ath12k_wifi6_hw_init(struct ath12k_base *ab)
{
	struct ath12k_hw_params *hw_params = NULL;
	struct ath12k_hw_params *hw_params_msi8 = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(ath12k_wifi6_hw_params); i++) {
		hw_params = &ath12k_wifi6_hw_params[i];

		if (hw_params->hw_rev == ab->hw_rev)
			break;
	}

	if (i == ARRAY_SIZE(ath12k_wifi6_hw_params)) {
		ath12k_err(ab, "Unsupported WiFi6 hardware version: 0x%x\n",
			   ab->hw_rev);
		return -EINVAL;
	}

	if ((ab->hif.bus == ATH12K_BUS_PCI &&
	    ab->msi.config->total_vectors == ATH12K_MSI_16)) {
		hw_params_msi8 = kzalloc(sizeof(struct ath12k_hw_params), GFP_KERNEL);
		if (!hw_params_msi8)
			return -ENOMEM;
		memcpy(hw_params_msi8, hw_params, sizeof(struct ath12k_hw_params));
		/*
		 * Include it when PPEDS patch are rebased
		 * hw_params_msi8->ext_irq_grp_num_max = 6;
		 */
		hw_params_msi8->ring_mask = &ath12k_wifi6_hw_ring_mask_qcn9074_msi8;
		ab->hw_params = hw_params_msi8;
	} else {
		ab->hw_params = hw_params;
	}
	ab->ath12k_ops = &ath12k_ops_wifi6;

	ath12k_info(ab, "WiFi6 Hardware name: %s\n", ab->hw_params->name);

	return 0;
}
