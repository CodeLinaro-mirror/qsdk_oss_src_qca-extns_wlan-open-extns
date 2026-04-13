/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/crc32.h>
#include <net/mac80211.h>
#include "../ieee80211_i.h"
#include "../trace.h"
#include "../driver-ops.h"
#include "../debugfs_sta.h"
#include "../debugfs_netdev.h"
#include "../../wireless/core.h"

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
#define SUPPORTED_RATES_LEN 8

unsigned int mmeshsim = 1;
EXPORT_SYMBOL(mmeshsim);
module_param_named(mmeshsim, mmeshsim, uint, 0644);
MODULE_PARM_DESC(mmeshsim, "mmeshsim");

u8 supported_rates_5g6g[SUPPORTED_RATES_LEN] = {0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c};
u8 supported_rates_2g[SUPPORTED_RATES_LEN] = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */

struct ieee80211_240mhz_vendor_oper_extn*
drv_get_240mhz_cap_extn(struct ieee80211_local *local,
			struct ieee80211_vif *vif,
			struct ieee80211_link_sta *link_sta)
{
	if (local->ops_extn && local->ops_extn->get_240mhz_cap_extn)
		return local->ops_extn->get_240mhz_cap_extn(vif, link_sta);

	return NULL;
}

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
void ieee80211_mark_mmesh_frame(struct ieee80211_rx_status *status)
{
	status->rx_flags |= IEEE80211_RX_MHDR;
}
EXPORT_SYMBOL(ieee80211_mark_mmesh_frame);

int
drv_mesh_peer_update_caps_extn(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata,
			       struct sta_info *sta,
			       struct link_sta_info *link_sta)
{
	int ret = -EOPNOTSUPP;
	might_sleep();

	if (local->ops_extn->mesh_peer_update_caps_extn) {
		ret = local->ops_extn->mesh_peer_update_caps_extn(&local->hw, &sdata->vif,
							     &sta->sta, link_sta->pub);
	}

	return ret;
}

/**
 * ieee80211_populate_mesh_peer_default rates: Populates default rates for mesh peer being added
 *
 * @params: Link station parameters
 * @band: NL80211 band ID where the peer entry is being added
 *
 * Return void
 */
static void ieee80211_populate_mesh_peer_default_rates(struct link_station_parameters *params,
					       enum nl80211_band band)
{
	if (params == NULL)
		return;

	if (band == NL80211_BAND_2GHZ)
		params->supported_rates = supported_rates_2g;
	else
		params->supported_rates = supported_rates_5g6g;

	params->supported_rates_len = SUPPORTED_RATES_LEN;
}

/**
 * ieee80211_mesh_ap_config_save: Saves the capability information passed on from
 * 	cfg80211 layer in the bss_conf struct. These capabilities will be later used to
 * 	populate default capabilities for statically added mesh peers.
 *
 * @params: AP configuration parameters from cfg80211 layer
 * @link_conf: Pointer to BSS conf
 *
 * Return: 0 on success, -ve on failure
 */
int ieee80211_mesh_ap_config_save(struct cfg80211_ap_settings *params,
				  struct ieee80211_bss_conf *link_conf)
{
	struct mesh_bss_conf *mesh_conf;
	struct ieee80211_ht_cap *mesh_ht_cap;
	struct ieee80211_vht_cap *mesh_vht_cap;
	struct ieee80211_he_cap_elem *mesh_he_cap;
	struct ieee80211_eht_cap_elem *mesh_eht_cap;

	link_conf->bss_conf_extn = kzalloc(sizeof(struct ieee80211_bss_conf_extn),
					   GFP_KERNEL);
	if (!link_conf->bss_conf_extn) {
		printk(KERN_ERR "AP config save failed for mesh vap due to no memory\n");
		return -ENOMEM;
	}

	mesh_conf = &link_conf->bss_conf_extn->mesh_bss_caps;

	if (params->ht_cap) {
		mesh_ht_cap = kzalloc(sizeof(struct ieee80211_ht_cap), GFP_KERNEL);
		if (!mesh_ht_cap)
			return -ENOMEM;

		memcpy(mesh_ht_cap, params->ht_cap, sizeof(struct ieee80211_ht_cap));
		mesh_conf->ht_cap = mesh_ht_cap;
	}

	if (params->vht_cap) {
		mesh_vht_cap = kzalloc(sizeof(struct ieee80211_vht_cap), GFP_KERNEL);
		if (!mesh_vht_cap)
			return -ENOMEM;

		memcpy(mesh_vht_cap, params->vht_cap, sizeof(struct ieee80211_vht_cap));
		mesh_conf->vht_cap = mesh_vht_cap;
	}

	if (params->he_cap) {
		mesh_he_cap = kzalloc(sizeof(struct ieee80211_he_cap_elem), GFP_KERNEL);
		if (!mesh_he_cap)
			return -ENOMEM;

		memcpy(mesh_he_cap, params->he_cap, sizeof(struct ieee80211_he_cap_elem));
		mesh_conf->he_cap = mesh_he_cap;
	}

	if (params->eht_cap) {
		mesh_eht_cap = kzalloc(sizeof(struct ieee80211_eht_cap_elem), GFP_KERNEL);
		if (!mesh_eht_cap)
			return -ENOMEM;

		memcpy(mesh_eht_cap, params->eht_cap,
		       sizeof(struct ieee80211_eht_cap_elem));
		mesh_conf->eht_cap = mesh_eht_cap;
	}

	return 0;
}

/**
 * ieee80211_free_mesh_vap_caps: Free the dynamically allocated memory for mesh AP
 * 	capabilities info
 *
 * @wdev: Pointer to wireless dev
 * @bss_conf_extn: Pointer to BSS conf
 *
 * Return: void
 */
void ieee80211_free_mesh_vap_caps(struct ieee80211_bss_conf *link_conf)
{
	struct mesh_bss_conf *mesh_conf;

	if (!link_conf->bss_conf_extn)
		return;

	mesh_conf = &link_conf->bss_conf_extn->mesh_bss_caps;

	kfree(mesh_conf->ht_cap);
	kfree(mesh_conf->vht_cap);
	kfree(mesh_conf->he_cap);
	kfree(mesh_conf->eht_cap);
	kfree(link_conf->bss_conf_extn);
	mesh_conf->ht_cap = NULL;
	mesh_conf->vht_cap = NULL;
	mesh_conf->he_cap = NULL;
	mesh_conf->eht_cap = NULL;
	link_conf->bss_conf_extn = NULL;
}

/**
 * ieee80211_mesh_ap_setup: Mesh AP setup
 * 	Called in ieee80211_start_ap() when bringing up mesh interface
 *
 * @wdev: Wireless dev pointer
 * @sdata: SDATA pointer
 * @params: AP capabilities info
 * @link_conf: Pointer to BSS conf
 *
 * Return: void
 */
void ieee80211_mesh_ap_setup(struct wireless_dev *wdev,
			     struct ieee80211_sub_if_data *sdata,
			     struct cfg80211_ap_settings *params,
			     struct ieee80211_bss_conf *link_conf)
{
	if (!wdev || wdev->vap_submode != IEEE80211_EXTN_VAP_SUBMODE_MESH) {
		printk(KERN_DEBUG "Mesh AP setup skipped\n");
		return;
	}

	if (!sdata || !params || !link_conf) {
		printk(KERN_DEBUG "Mesh AP setup skipped, invalid params\n");
		return;
	}

	memset(sdata->u.ap.if_ap_extn.mesh_peer_aid, 0,
	       sizeof(sdata->u.ap.if_ap_extn.mesh_peer_aid));

	sdata->u.ap.if_ap_extn.mesh_peer_timeout_cnt = MESH_PEER_TIMEOUT_CNT;

	if (ieee80211_mesh_ap_config_save(params, link_conf) < 0) {
		printk(KERN_ERR "AP config save failed");
		return;
	}

	timer_setup(&sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer,
		    ieee80211_mesh_peer_timeout_check, 0);
	/* Timer is enabled by default */
	sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer_enabled = true;
}
EXPORT_SYMBOL(ieee80211_mesh_ap_setup);

void ieee80211_mesh_ap_cleanup(struct wireless_dev *wdev,
			       struct ieee80211_sub_if_data *sdata,
			       struct ieee80211_bss_conf *link_conf)
{
	if (!wdev || wdev->vap_submode != IEEE80211_EXTN_VAP_SUBMODE_MESH) {
		printk(KERN_DEBUG "Mesh AP cleanup skipped\n");
		return;
	}

	if (!sdata || !link_conf) {
		printk(KERN_DEBUG "Mesh AP cleanup skipped, invalid params\n");
		return;
	}

	ieee80211_free_mesh_vap_caps(link_conf);
	del_timer_sync(&sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer);
}
EXPORT_SYMBOL(ieee80211_mesh_ap_cleanup);

/**
 * ieee80211_mesh_get_aid - Get unique AID for mesh peer
 * @sdata: Interface data
 * @aid_out: Pointer to sta aid
 *
 * Replicates hostapd_get_aid() logic for mesh peers.
 * Returns 0 on success, -1 on failure (no available AIDs)
 */
static int ieee80211_mesh_get_aid(struct ieee80211_sub_if_data *sdata, u16 *aid_out)
{
	int i, j = 32;
	u16 aid;

	if (!sdata || !aid_out)
		return -1;

	/* Find first available AID */
	for (i = 0; i < MESH_AID_WORDS; i++) {
		if (sdata->u.ap.if_ap_extn.mesh_peer_aid[i] == (u32) -1)
			continue;  /* All AIDs in this word are used */

		for (j = 0; j < 32; j++) {
			if (!(sdata->u.ap.if_ap_extn.mesh_peer_aid[i] & BIT(j)))
				break;  /* Found available AID */
		}

		if (j < 32)
			break;
	}

	if (j == 32)
		return -1;  /* No available AIDs */

	aid = i * 32 + j + 1;  /* AID starts from 1, not 0 */

	if (aid > IEEE80211_MAX_AID)
		return -1;  /* AID out of valid range */

	/* Mark AID as used */
	sdata->u.ap.if_ap_extn.mesh_peer_aid[i] |= BIT(j);

	*aid_out = aid;
	printk("Assigned mesh peer AID %d\n", aid);

	return 0;
}

/**
 * ieee80211_mesh_free_aid - Free AID when mesh peer is removed
 * @sdata: Interface data
 * @aid: AID to free
 */
static void ieee80211_mesh_free_aid(struct ieee80211_sub_if_data *sdata,
				    u16 aid)
{
	int i, j;

	if (!sdata || aid == 0 || aid > 2007)
		return;

	/* Convert AID back to bitmap position */
	aid--;  /* AID starts from 1, array index from 0 */
	i = aid / 32;
	j = aid % 32;

	/* Clear the bit */
	sdata->u.ap.if_ap_extn.mesh_peer_aid[i] &= ~BIT(j);

	pr_debug("Freed mesh peer AID %d\n", aid + 1);
}

/**
 * ieee80211_mesh_peer_copy_bss_cap: Populate default capability information for the
 * 	mesh peer being added from the AP capabilities
 *
 * @bss_conf: BSS conf pointer
 * @station_params: Station config parameters
 * @band: NL80211 band ID
 *
 * Return 0 on success and -ve on failure
 */
static int ieee80211_mesh_peer_copy_bss_cap(struct ieee80211_bss_conf *bss_conf,
					    struct station_parameters *station_params,
					    enum nl80211_band band)
{
	struct ieee80211_bss_conf_extn *bss_conf_extn;
	struct mesh_bss_conf *mesh_conf;
	struct ieee80211_ht_cap *mesh_ht_cap;
	struct ieee80211_vht_cap *mesh_vht_cap;
	struct ieee80211_he_cap_elem *mesh_he_cap;
	struct ieee80211_eht_cap_elem *mesh_eht_cap;

	bss_conf_extn = bss_conf->bss_conf_extn;
	if (!bss_conf_extn)
		return -EINVAL;

	mesh_conf = &bss_conf_extn->mesh_bss_caps;

	/* Populate the HT and VHT capbility for the peer */
	if (band != NL80211_BAND_6GHZ) {
		if (mesh_conf->ht_cap) {
			mesh_ht_cap = kzalloc(sizeof(struct ieee80211_ht_cap), GFP_KERNEL);
			if (!mesh_ht_cap)
				return -ENOMEM;

			memcpy(mesh_ht_cap, mesh_conf->ht_cap,
			       sizeof(struct ieee80211_ht_cap));
			station_params->link_sta_params.ht_capa = mesh_ht_cap;
		}

		if (mesh_conf->vht_cap) {
			mesh_vht_cap = kzalloc(sizeof(struct ieee80211_vht_cap), GFP_KERNEL);
			if (!mesh_vht_cap)
				return -ENOMEM;

			memcpy(mesh_vht_cap, mesh_conf->vht_cap,
			       sizeof(struct ieee80211_vht_cap));
			station_params->link_sta_params.vht_capa = mesh_vht_cap;
		}
	}

	/* Populate the HE capbility for the peer */
	if (mesh_conf->he_cap) {
		/* Allocate space for fixed HE cap elem + MCS/NSS map written by
		 * ieee80211_mesh_peer_he_mcsnssmap_update() immediately after the
		 * fixed portion.
		 */
		mesh_he_cap = kzalloc(sizeof(struct ieee80211_he_cap_elem) +
				      sizeof(struct ieee80211_he_mcs_nss_supp),
				      GFP_KERNEL);
		if (!mesh_he_cap)
			return -ENOMEM;

		memcpy(mesh_he_cap, mesh_conf->he_cap,
		       sizeof(struct ieee80211_he_cap_elem));
		station_params->link_sta_params.he_capa = mesh_he_cap;
	}

	/* Populate the EHT capbility for the peer */
	if (mesh_conf->eht_cap) {
		/* Allocate space for fixed EHT cap elem + the full optional
		 * MCS/NSS support field (ieee80211_eht_mcs_nss_supp covers both
		 * the 20-MHz-only case and the worst-case bw._80/_160/_320 case).
		 */
		mesh_eht_cap = kzalloc(sizeof(struct ieee80211_eht_cap_elem_fixed) +
				       sizeof(struct ieee80211_eht_mcs_nss_supp),
				       GFP_KERNEL);
		if (!mesh_eht_cap)
			return -ENOMEM;

		memcpy(mesh_eht_cap, mesh_conf->eht_cap,
		       sizeof(struct ieee80211_eht_cap_elem_fixed));
		station_params->link_sta_params.eht_capa = mesh_eht_cap;
	}

	return 0;
}

/**
 * ieee80211_mesh_peer_ht_vht_mcsnssmap_update: Updates the peer HT and VHT MCS and NSS
 * 	capability based on the configurations
 *
 * @ht_cap: Pointer to peer's HT capabilities
 * @vht_cap: Pointer to peer's VHT capabilities
 * @nss: Peer NSS configuration
 * @mode: Peer mode configuration
 *
 * Return 0 on success and -ve on failure
 */
static int ieee80211_mesh_peer_ht_vht_mcsnssmap_update(struct ieee80211_ht_cap *ht_cap,
						       struct ieee80211_vht_cap *vht_cap,
						       u8 nss, u8 mode)
{
	int iter;

	if (!ht_cap)
	    return -EINVAL;

	for (iter = 0; iter < nss; iter++)
		ht_cap->mcs.rx_mask[iter] = 0xFF;
	ht_cap->mcs.tx_params |= IEEE80211_HT_MCS_TX_DEFINED;

	if (mode < MESH_PREAMBLE_VHT)
	    return 0;

	if (!vht_cap)
	    return -EINVAL;

	for (iter = 0; iter < 8; iter++) {
		if (iter < nss) {
			vht_cap->supp_mcs.rx_mcs_map |=
				(IEEE80211_VHT_MCS_SUPPORT_0_9 << (iter * 2));
			vht_cap->supp_mcs.tx_mcs_map |=
				(IEEE80211_VHT_MCS_SUPPORT_0_9 << (iter * 2));
		} else {
			vht_cap->supp_mcs.rx_mcs_map |=
				(IEEE80211_VHT_MCS_NOT_SUPPORTED << (iter * 2));
			vht_cap->supp_mcs.tx_mcs_map |=
				(IEEE80211_VHT_MCS_NOT_SUPPORTED << (iter * 2));
		}
	}
	return 0;
}

/**
 * ieee80211_mesh_peer_he_mcsnssmap_update: Updates the peer HE MCS and NSS
 * 	capability based on the configurations
 *
 * @he_cap: Pointer to peer's HE capabilities
 * @nss: Peer NSS configuration
 * @bw: Peer BW configuration
 * @he_cap_len: Pointer to return updated HE cap length
 *
 * Return 0 on success and -ve on failure
 */
static int ieee80211_mesh_peer_he_mcsnssmap_update(struct ieee80211_he_cap_elem *he_cap,
						   u8 nss, u8 bw, u8 *he_cap_len)
{
	int iter;
	struct ieee80211_he_mcs_nss_supp *mcsnss_map;
	u8 *he_cap_ie;
	uint16_t calc_mcs_nss = 0;

	if (!he_cap)
		return -EINVAL;

	he_cap->phy_cap_info[6] &=
		~IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT;
	he_cap_ie = (u8 *)he_cap;
	*he_cap_len += sizeof(struct ieee80211_he_cap_elem);

	mcsnss_map = (struct ieee80211_he_mcs_nss_supp *)
			(he_cap_ie + sizeof(struct ieee80211_he_cap_elem));
	for (iter = 0; iter < 8; iter++) {
		if (iter < nss)
			calc_mcs_nss |=
			    (IEEE80211_HE_MCS_SUPPORT_0_11 << (iter * 2));
		else
			calc_mcs_nss |=
			    (IEEE80211_HE_MCS_NOT_SUPPORTED << (iter * 2));
	}

	switch (bw) {
	case MESH_BW_320:
		fallthrough;
	case MESH_BW_160:
		mcsnss_map->rx_mcs_160 = calc_mcs_nss;
		mcsnss_map->tx_mcs_160 = calc_mcs_nss;
		*he_cap_len += 4;
		fallthrough;
	case MESH_BW_80_80:
		mcsnss_map->rx_mcs_80p80 = calc_mcs_nss;
		mcsnss_map->tx_mcs_80p80 = calc_mcs_nss;
		*he_cap_len += 4;
		fallthrough;
	default:
		mcsnss_map->rx_mcs_80 = calc_mcs_nss;
		mcsnss_map->tx_mcs_80 = calc_mcs_nss;
		*he_cap_len += 4;
		break;
	}

	return 0;
}

/**
 * ieee80211_mesh_peer_eht_mcsnssmap_update: Updates the peer EHT MCS and NSS
 * 	capability based on the configurations
 *
 * @eht_cap: Pointer to peer's EHT capabilities
 * @nss: Peer NSS configuration
 * @bw: Peer BW configuration
 * @he_cap_len: Pointer to return updated EHT cap length
 *
 * Return 0 on success and -ve on failure
 */
static int ieee80211_mesh_peer_eht_mcsnssmap_update(struct ieee80211_eht_cap_elem *eht_cap,
						    u8 nss, u8 bw, u8 *eht_cap_len)
{
	struct ieee80211_eht_mcs_nss_supp_20mhz_only *mcsnss_20mhz;
	struct ieee80211_eht_mcs_nss_supp_bw *mcsnss_map;

	if (!eht_cap)
		return -EINVAL;

	eht_cap->fixed.phy_cap_info[5] &=
		~IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT;
	*eht_cap_len += sizeof(struct ieee80211_eht_cap_elem_fixed);

	mcsnss_map = (struct ieee80211_eht_mcs_nss_supp_bw *)eht_cap->optional;
	switch (bw) {
	case MESH_BW_20:
		mcsnss_20mhz = (struct ieee80211_eht_mcs_nss_supp_20mhz_only *)
				eht_cap->optional;
		memset(mcsnss_20mhz->rx_tx_max_nss, nss, 4);
		*eht_cap_len += sizeof(struct ieee80211_eht_mcs_nss_supp_20mhz_only);
		break;
	case MESH_BW_320:
		memset(mcsnss_map->rx_tx_max_nss, nss, 3);
		*eht_cap_len += sizeof(struct ieee80211_eht_mcs_nss_supp_bw);
		mcsnss_map = (struct ieee80211_eht_mcs_nss_supp_bw *)
			((u8 *)mcsnss_map +
			sizeof(struct ieee80211_eht_mcs_nss_supp_bw));
		fallthrough;
	case MESH_BW_160:
		fallthrough;
	case MESH_BW_80_80:
		memset(mcsnss_map->rx_tx_max_nss, nss, 3);
		*eht_cap_len += sizeof(struct ieee80211_eht_mcs_nss_supp_bw);
		mcsnss_map = (struct ieee80211_eht_mcs_nss_supp_bw *)
			((u8 *)mcsnss_map +
			sizeof(struct ieee80211_eht_mcs_nss_supp_bw));
		fallthrough;
	default:
		memset(mcsnss_map->rx_tx_max_nss, nss, 3);
		*eht_cap_len += sizeof(struct ieee80211_eht_mcs_nss_supp_bw);
		mcsnss_map = (struct ieee80211_eht_mcs_nss_supp_bw *)
			((u8 *)mcsnss_map +
			sizeof(struct ieee80211_eht_mcs_nss_supp_bw));
		break;
	}
	return 0;
}

/**
 * ieee80211_mesh_peer_update_cap: Update the peer's PHY capability information based on
 * 	the configurations.
 *
 * @params: Pointer to station parameters
 * @bw: Peer BW configuration
 * @nss: Peer NSS configuration
 * @mode: Peer mode configuration
 * @band: NL80211 band ID
 *
 * Return 0 on success and -ve on failure
 */
static int ieee80211_mesh_peer_update_cap(struct station_parameters *params,
					  u8 bw, u8 nss, u8 mode, enum nl80211_band band)
{
	struct ieee80211_ht_cap *mesh_ht_cap;
	struct ieee80211_vht_cap *mesh_vht_cap;
	struct ieee80211_he_cap_elem *mesh_he_cap;
	struct ieee80211_eht_cap_elem *mesh_eht_cap;
	int ret = 0;

	if (!params)
		return -EINVAL;

	mesh_ht_cap = (struct ieee80211_ht_cap *)params->link_sta_params.ht_capa;
	mesh_vht_cap = (struct ieee80211_vht_cap *)params->link_sta_params.vht_capa;
	mesh_he_cap = (struct ieee80211_he_cap_elem *)params->link_sta_params.he_capa;
	mesh_eht_cap = (struct ieee80211_eht_cap_elem *)params->link_sta_params.eht_capa;

	if (mesh_eht_cap)
		mesh_eht_cap->fixed.phy_cap_info[0] &=
			~IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ;

	if (mesh_he_cap) {
		mesh_he_cap->phy_cap_info[0] &= ~(
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G |
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G);

		if (band == NL80211_BAND_2GHZ) {
			mesh_he_cap->phy_cap_info[0] &=
				~IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
		}
        }

	/* Update the PHY capabilities based on the BW configured */
	switch (bw) {
	case MESH_BW_320:
		if ((band == NL80211_BAND_6GHZ) && mesh_eht_cap)
			mesh_eht_cap->fixed.phy_cap_info[0] |=
				IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ;
		fallthrough;
	case MESH_BW_160:
		if (mesh_he_cap)
			mesh_he_cap->phy_cap_info[0] |=
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;
		fallthrough;
	case MESH_BW_80_80:
		if (mesh_he_cap)
			mesh_he_cap->phy_cap_info[0] |=
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G;
		fallthrough;
	case MESH_BW_80:
		if (mesh_he_cap)
			mesh_he_cap->phy_cap_info[0] |=
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
		fallthrough;
	case MESH_BW_40:
		if (band != NL80211_BAND_6GHZ) {
			if (mesh_ht_cap)
				mesh_ht_cap->cap_info |=
					IEEE80211_HT_CAP_SUP_WIDTH_20_40;
			if (mesh_vht_cap) {
				mesh_vht_cap->vht_cap_info |=
					IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
				mesh_vht_cap->vht_cap_info &=
					~IEEE80211_VHT_CAP_EXT_NSS_BW_MASK;
			}
		}
		if ((band == NL80211_BAND_2GHZ) &&
				mesh_he_cap)
			mesh_he_cap->phy_cap_info[0] |=
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
		break;
	case MESH_BW_20:
		if (mesh_ht_cap)
			mesh_ht_cap->cap_info &=
				~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		if (mesh_vht_cap) {
			mesh_vht_cap->vht_cap_info |=
				IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
			mesh_vht_cap->vht_cap_info &=
				~IEEE80211_VHT_CAP_EXT_NSS_BW_MASK;
		}
		break;
	default:
		return -EINVAL;
		break;
	}

	/* Update the MCS and NSS capabilities based on the MCS NSS configured */
	if ((band != NL80211_BAND_6GHZ) && (mode >= MESH_PREAMBLE_HT)) {
		ret = ieee80211_mesh_peer_ht_vht_mcsnssmap_update(mesh_ht_cap,
								  mesh_vht_cap, nss,
								  mode);

		if (ret < 0)
			return ret;
	}

	if (mode < MESH_PREAMBLE_HE)
	    return 0;

	ret = ieee80211_mesh_peer_he_mcsnssmap_update(mesh_he_cap, nss, bw,
						      &params->link_sta_params.he_capa_len);
	if (ret < 0)
		return ret;

	if (mode == MESH_PREAMBLE_EHT) {
		ret = ieee80211_mesh_peer_eht_mcsnssmap_update(mesh_eht_cap,
							       nss, bw,
							       &params->link_sta_params.eht_capa_len);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/**
 * ieee80211_mesh_station_param_update: Populates default capability and updates specific
 * 	capabilities based on the configuration parameters when adding mesh peer.
 *
 * @sdata: SDATA pointer
 * @mesh_params: Mesh configuration parameters
 * @station_params: Station parameters pointer
 *
 * Return 0 on success and -ve on failure
 */
static int ieee80211_mesh_station_param_update(struct ieee80211_sub_if_data *sdata,
					       struct ieee80211_mesh_config_params_extn *mesh_params,
					       struct station_parameters *station_params)
{
	u8 bw, mode, nss;
	u32 short_preamble;
	struct ieee80211_link_data *link;
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_vif *vif;
	int ret = 0;
	u64 caps;
	enum nl80211_band band = NUM_NL80211_BANDS;

	if (!sdata || !mesh_params || !station_params || !mesh_params->num_links)
		return -EINVAL;

	vif = &sdata->vif;

	link = sdata_dereference(sdata->link[mesh_params->peers[0].link_id], sdata);
	if (!link)
		return -EINVAL;

	link_conf = link->conf;
	if (!link_conf || (link_conf->chanreq.oper.chan == NULL))
		return -EINVAL;

	caps = mesh_params->peers[0].caps;

	bw   = (caps >> MESH_CAPS_BW_OFFSET) & MESH_CAPS_NIBBLE_MASK;
	nss  = (caps >> MESH_CAPS_NSS_OFFSET) & MESH_CAPS_NIBBLE_MASK;
	mode = (caps >> MESH_CAPS_MODE_OFFSET) & MESH_CAPS_NIBBLE_MASK;
	short_preamble = caps & MESH_CAPS_SHORT_PREAMBLE;

	if ((nss >= MESH_CAPS_MAX_NSS) || (mode >= MESH_PREAMBLE_MAX)
	    || (bw >= MESH_BW_MAX))
		return -EINVAL;

	band = link_conf->chanreq.oper.chan->band;

	station_params->sta_flags_mask = BIT(NL80211_STA_FLAG_WME) |
				BIT(NL80211_STA_FLAG_AUTHENTICATED) |
				BIT(NL80211_STA_FLAG_ASSOCIATED);

	if (short_preamble)
		station_params->sta_flags_mask |= BIT(NL80211_STA_FLAG_SHORT_PREAMBLE);

	/* Since we want to set all the flags to 1, sta_flags_set bitmap
	 * will look the same as sta_flags_mask bitmap.
	 */
	station_params->sta_flags_set = station_params->sta_flags_mask;
	/* Listen interval 0 indicates the mesh peer will
	 * never go to power save */
	station_params->listen_interval = 0;

	if (mode < MESH_PREAMBLE_EHT)
		station_params->link_sta_params.link_id = -1;
	else
		station_params->link_sta_params.link_id =
			vif->bss_conf.link_id;

	station_params->link_sta_params.mld_mac =
			mesh_params->peer_mld_mac;

	station_params->link_sta_params.link_mac =
			mesh_params->peers[0].peer_link_mac;

	/* Update the supported rates for the mesh peer */
	ieee80211_populate_mesh_peer_default_rates(&station_params->link_sta_params,
						   band);

	station_params->link_sta_params.punctured =
				mesh_params->peers[0].puncture_bitmaps;

	ret = ieee80211_mesh_peer_copy_bss_cap(link_conf, station_params, band);
	if (ret < 0)
		return ret;

	/* Update the protocol capabilities based on the BW, NSS and mode
	 * programmed by the user for the mesh peer.
	 */
	ret = ieee80211_mesh_peer_update_cap(station_params, bw, nss, mode, band);

	return ret;
}

/**
 * ieee80211_vendor_mesh_add_local_peer: Mesh peer addition handler.
 *
 * @wiphy: WIPHY pointer
 * @vif: VIF pointer
 * @mesh_params: Mesh peer parameters essential for adding the peer
 *
 * Return 0 on success and -ve on failure
 */
int ieee80211_vendor_mesh_add_local_peer(struct wiphy *wiphy,
					 struct ieee80211_vif *vif,
					 struct ieee80211_mesh_config_params_extn *mesh_params)
{
	struct station_parameters params = {0};
	u8 *peer_addr = NULL;
	struct ieee80211_sta *sta;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct ieee80211_sub_if_data *sdata;
	struct net_device *dev;
	int ret;

	if (!vif || !mesh_params)
		return -EINVAL;

	sdata = vif_to_sdata(vif);
	if (!sdata)
		return -EINVAL;

	dev = sdata->dev;
	if (!dev)
		return -EINVAL;

	if (!rdev || !rdev->ops || !rdev->ops->add_station)
		return -EOPNOTSUPP;

	peer_addr = mesh_params->peers[0].peer_link_mac;
	/* Validate MAC address */
	if (!is_valid_ether_addr(peer_addr))
		return -EINVAL;

	/* Check if peer already exists */
	rcu_read_lock();
	sta = ieee80211_find_sta(vif, peer_addr);
	rcu_read_unlock();

	if (sta)
		return -EEXIST;

	/* Translate capabilities */
	ret = ieee80211_mesh_station_param_update(sdata, mesh_params, &params);
	if (ret)
		goto cleanup;

	ret = ieee80211_mesh_get_aid(sdata, &params.aid);
	if (ret) {
		pr_err("Failed to assign AID for mesh peer, no available AIDs\n");
		ret = -ENOSPC;
		goto cleanup;
	}

	/* Call mac80211's add_station via cfg80211_ops */
	ret = rdev->ops->add_station(wiphy, dev, peer_addr, &params);
	if (ret) {
		ieee80211_mesh_free_aid(sdata, params.aid);
		goto cleanup;
	}

	/* Get the created station */
	rcu_read_lock();
	sta = ieee80211_find_sta(vif, peer_addr);
	if (!sta) {
		rcu_read_unlock();
		ieee80211_mesh_free_aid(sdata, params.aid);
		ret = -ENOENT;
		goto cleanup;
	}

	/* Start timer to track peers who are not sending beacons. Start this
	 * timer right after the 1st peer is added, only if timer is enabled.
	 */
	if (sdata->u.ap.if_ap_extn.num_mesh_peers == 0 &&
	    sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer_enabled)
		mod_timer(&sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer,
			  jiffies + msecs_to_jiffies(MESH_PEER_INACTIVITY_PERIOD));

	sdata->u.ap.if_ap_extn.num_mesh_peers++;
	sta->sta_extn.peer_info.is_mesh_peer = true;
	rcu_read_unlock();

	ret = 0;

cleanup:
	/* Free allocated capability structures after add_station has copied what it needs
	 * These were allocated in ieee80211_mesh_peer_copy_bss_cap()
	 */
	kfree((void *)params.link_sta_params.ht_capa);
	kfree((void *)params.link_sta_params.vht_capa);
	kfree((void *)params.link_sta_params.he_capa);
	kfree((void *)params.link_sta_params.eht_capa);

	return ret;

}
EXPORT_SYMBOL(ieee80211_vendor_mesh_add_local_peer);

/**
 * ieee80211_vendor_mesh_authorize_peer: Mesh peer authorize handler.
 *
 * @wiphy: WIPHY pointer
 * @vif: VIF pointer
 * @peer_addr: MAC address of mesh peer to be authorized
 *
 * Return 0 on success and -ve on failure
 */
int ieee80211_vendor_mesh_authorize_peer(struct wiphy *wiphy, struct ieee80211_vif *vif,
					 const u8 *peer_addr)
{
	struct ieee80211_sta *sta;
	struct station_parameters params;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct ieee80211_sub_if_data *sdata;
	struct net_device *dev;
	int ret;

	if (!vif || !peer_addr)
		return -EINVAL;

	sdata = vif_to_sdata(vif);
	if (!sdata)
		return -EINVAL;

	dev = sdata->dev;
	if (!dev)
		return -EINVAL;

	if (!rdev || !rdev->ops || !rdev->ops->change_station)
		return -EOPNOTSUPP;

	/* Find the station */
	rcu_read_lock();
	sta = ieee80211_find_sta(vif, peer_addr);
	if (!sta) {
		rcu_read_unlock();
		return -ENOENT;
	}

	/* Verify this is a local mesh peer */
	if (!sta->sta_extn.peer_info.is_mesh_peer) {
		rcu_read_unlock();
		return -EINVAL;
	}

	rcu_read_unlock();

	memset(&params, 0, sizeof(params));

	/* AID and listen_interval properties can be set only for unassociated
	 * station. They will be checked in cfg80211_check_station_change().
	 */
	params.aid = 0;
	params.listen_interval = -1;
	params.support_p2p_ps = -1;

	/* Build station parameters to set AUTHORIZED flag */
	params.sta_flags_mask = BIT(NL80211_STA_FLAG_AUTHORIZED);
	params.sta_flags_set = BIT(NL80211_STA_FLAG_AUTHORIZED);

	/* Call mac80211's change_station to set the flag */
	ret = rdev->ops->change_station(wiphy, dev, peer_addr, &params);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(ieee80211_vendor_mesh_authorize_peer);

/**
 * ieee80211_vendor_mesh_delete_local_peer: Mesh peer delete handler.
 *
 * @wiphy: WIPHY pointer
 * @vif: VIF pointer
 * @peer_addr: MAC address of mesh peer to be deleted
 *
 * Return 0 on success and -ve on failure
 */
int ieee80211_vendor_mesh_delete_local_peer(struct wiphy *wiphy,
					    struct ieee80211_vif *vif,
					    const u8 *peer_addr)
{
	struct ieee80211_sta *sta;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct ieee80211_sub_if_data *sdata;
	struct net_device *dev;
	struct station_del_parameters params;
	int ret;
	u16 sta_aid = 0;

	/* Validate input parameters */
	if (!vif || !peer_addr)
		return -EINVAL;

	/* Get sdata and netdev */
	sdata = vif_to_sdata(vif);
	if (!sdata)
		return -EINVAL;

	dev = sdata->dev;
	if (!dev)
		return -EINVAL;

	if (!is_valid_ether_addr(peer_addr))
		return -EINVAL;

	/* Check if del_station operation is available */
	if (!rdev || !rdev->ops || !rdev->ops->del_station)
		return -EOPNOTSUPP;

	/* Find the station */
	rcu_read_lock();
	sta = ieee80211_find_sta(vif, peer_addr);
	if (!sta) {
		rcu_read_unlock();
		return -ENOENT;
	}

	/* Verify this is a local mesh peer */
	if (!sta->sta_extn.peer_info.is_mesh_peer) {
		rcu_read_unlock();
		return -EINVAL;
	}
	sta_aid = sta->aid;
	rcu_read_unlock();

	/* Prepare deletion parameters */
	memset(&params, 0, sizeof(params));
	params.mac = peer_addr;
	params.link_id = -1;
	params.subtype = IEEE80211_STYPE_DEAUTH;
	params.reason_code = WLAN_REASON_DEAUTH_LEAVING;

	/* Call mac80211's del_station to remove the peer */
	ret = rdev->ops->del_station(wiphy, dev, &params);

	if (!ret)
	    ieee80211_mesh_free_aid(sdata, sta_aid);

	return ret;
}
EXPORT_SYMBOL(ieee80211_vendor_mesh_delete_local_peer);

/**
 * ieee80211_vendor_mesh_set_peer_timeout_cnt - Set mesh peer inactivity timeout count.
 *
 * @wiphy: WIPHY pointer
 * @vif: VIF pointer
 * @timeout_cnt: New peer inactivity timeout threshold (number of iterations of
 *               peer timeout where the peer was found to have not sent beacon)
 *
 * Configures the number of iterations of peer timeout where the peer was found
 * to have not sent beacon before a mesh peer is considered inactive and cleaned
 * up. Replaces the compile-time constant MESH_PEER_TIMEOUT_CNT with a
 * runtime-configurable value per interface.
 *
 * Return 0 on success and -ve on failure
 */
int ieee80211_vendor_mesh_set_peer_timeout_cnt(struct wiphy *wiphy,
					       struct ieee80211_vif *vif,
					       u16 timeout_cnt)
{
	struct ieee80211_sub_if_data *sdata;

	if (!vif)
		return -EINVAL;

	if (timeout_cnt == 0) {
		wiphy_err(wiphy, "mesh_peer_timeout_cnt must be greater than 0\n");
		return -EINVAL;
	}

	sdata = vif_to_sdata(vif);
	if (!sdata)
		return -EINVAL;

	if (sdata->wdev.vap_submode != IEEE80211_EXTN_VAP_SUBMODE_MESH) {
		wiphy_err(wiphy, "ieee80211_vendor_mesh_set_peer_timeout_cnt: not a mesh VAP\n");
		return -EINVAL;
	}

	sdata->u.ap.if_ap_extn.mesh_peer_timeout_cnt = timeout_cnt;
	wiphy_dbg(wiphy, "mesh_peer_timeout_cnt set to %u\n", timeout_cnt);

	return 0;
}
EXPORT_SYMBOL(ieee80211_vendor_mesh_set_peer_timeout_cnt);

/**
 * ieee80211_calc_beacon_ie_checksum_raw - Calculate checksum of critical IEs
 *
 * @mgmt: Pointer to beacon management frame
 * @len: Length of beacon frame
 *
 * Calculates CRC32 checksum of critical IEs to detect capability changes.
 * Return: CRC32 checksum value
 */
static u32 ieee80211_calc_beacon_ie_checksum_raw(const struct ieee80211_mgmt *mgmt,
						 size_t len)
{
	const u8 *ies;
	size_t ies_len;
	u32 checksum = 0;
	const u8 *pos, *end;

	/* Skip to IEs (after fixed beacon fields) */
	ies = mgmt->u.beacon.variable;
	ies_len = len - offsetof(struct ieee80211_mgmt, u.beacon.variable);

	pos = ies;
	end = ies + ies_len;

	/* Calculate checksum only for critical IEs */
	while (pos + 2 <= end) {
		u8 id = pos[0];
		u8 ie_len = pos[1];

		if (pos + 2 + ie_len > end)
			break;

		/* Include only capability-related IEs in checksum */
		switch (id) {
		case WLAN_EID_SUPP_RATES:
		case WLAN_EID_EXT_SUPP_RATES:
		case WLAN_EID_HT_CAPABILITY:
		case WLAN_EID_VHT_CAPABILITY:
		case WLAN_EID_VHT_OPERATION:
			checksum = crc32(checksum, pos, 2 + ie_len);
			break;
		case WLAN_EID_EXTENSION:
			/* Check for HE/EHT extension IEs */
			if (ie_len > 0) {
				u8 ext_id = pos[2];
				if (ext_id == WLAN_EID_EXT_HE_CAPABILITY ||
				    ext_id == WLAN_EID_EXT_HE_OPERATION ||
				    ext_id == WLAN_EID_EXT_EHT_CAPABILITY)
					checksum = crc32(checksum, pos, 2 + ie_len);
			}
			break;
		default:
			break;
		}

		pos += 2 + ie_len;
	}

	return checksum;
}

/**
 * ieee80211_update_mesh_peer_caps_work: Deferred work to update peer capabilities
 * 	after beacon intersect and send peer assoc WMI to FW.
 *
 * @wiphy: WIPHY pointer
 * @work: WIPHY work pointer
 *
 * Return: void
 */
void
ieee80211_update_mesh_peer_caps_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct sta_info *sta = container_of(work, struct sta_info,
					    sta.sta_extn.update_cap_wk);
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct link_sta_info *link_sta;
	int ret;

	lockdep_assert_wiphy(local->hw.wiphy);

	/* TODO: Currently only SLO support is enabled, link_sta extraction to be updated
	 * when MLO support is enabled */
	link_sta = &sta->deflink;

	/* Recalculate NSS */
	ieee80211_sta_init_nss(link_sta);

	/* Notify driver to update peer assoc */
	ret = drv_mesh_peer_update_caps_extn(local, sdata, sta, link_sta);
	if (ret < 0) {
		printk(KERN_ERR
		       "Mesh peer update failed for peer AID %d\n", sta->sta.aid);
		sta->sta.sta_extn.peer_info.intersect_failures++;
        } else {
		sta->sta.sta_extn.peer_info.intersection_done = true;
		sta->sta.sta_extn.peer_info.intersect_successes++;
        }
}

/**
 * ieee80211_timeout_mesh_peer_work: Deferred work to delete timed out mesh peers
 *
 * @wiphy: WIPHY pointer
 * @work: WIPHY work pointer
 *
 * Return: void
 */
void
ieee80211_timeout_mesh_peer_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct sta_info *sta_info = container_of(work, struct sta_info,
						 sta.sta_extn.timeout_wk);
	struct ieee80211_sub_if_data *sdata = sta_info->sdata;
	int ret;

	ret = ieee80211_vendor_mesh_delete_local_peer(wiphy, &sdata->vif, sta_info->addr);

	if (ret < 0)
	    printk("Failure to delete timed out peer[AID]: %d", sta_info->sta.aid);
}

static bool ieee80211_mesh_peer_bw_update(struct link_sta_info *link_sta,
					  struct ieee802_11_elems *elems,
					  enum nl80211_band band)
{
	enum ieee80211_sta_rx_bandwidth new_bw;
	enum ieee80211_sta_rx_bandwidth old_bw = link_sta->pub->bandwidth;
	enum ieee80211_sta_rx_bandwidth peer_oper_bw = IEEE80211_STA_RX_BW_320;
	bool cap_changed = false;

	if (elems->eht_operation) {
		const struct ieee80211_eht_operation *eht_oper =
			(struct ieee80211_eht_operation *)elems->eht_operation;
		const struct ieee80211_eht_operation_info *eht_oper_info =
			(const void *)eht_oper->optional;

		if (eht_oper->params & IEEE80211_EHT_OPER_INFO_PRESENT) {
			u8 ch_width = eht_oper_info->control &
					IEEE80211_EHT_OPER_CHAN_WIDTH;

			switch (ch_width) {
			case IEEE80211_EHT_OPER_CHAN_WIDTH_320MHZ:
				peer_oper_bw = IEEE80211_STA_RX_BW_320;
				break;
			case IEEE80211_EHT_OPER_CHAN_WIDTH_160MHZ:
				peer_oper_bw = IEEE80211_STA_RX_BW_160;
				break;
			case IEEE80211_EHT_OPER_CHAN_WIDTH_80MHZ:
				peer_oper_bw = IEEE80211_STA_RX_BW_80;
				break;
			case IEEE80211_EHT_OPER_CHAN_WIDTH_40MHZ:
				peer_oper_bw = IEEE80211_STA_RX_BW_40;
				break;
			case IEEE80211_EHT_OPER_CHAN_WIDTH_20MHZ:
			default:
				peer_oper_bw = IEEE80211_STA_RX_BW_20;
				break;
			}
		} else {
			/* EHT operation Info not present, fall back based on band:
			 * 6GHz - Use HE 6GHz operation info
			 * 5GHz/2.4GHz - Use VHT/HT operation info
			 */
			if (band == NL80211_BAND_6GHZ && elems->he_operation)
				goto parse_he_6ghz_operation;
			else if (elems->vht_operation)
				goto parse_vht_operation;
			else if (elems->ht_operation)
				goto parse_ht_operation;
		}
	} else if (elems->he_operation) {
parse_he_6ghz_operation:
		const struct ieee80211_he_operation *he_oper = elems->he_operation;

		if (band == NL80211_BAND_6GHZ) {
			if (he_oper->he_oper_params & IEEE80211_HE_OPERATION_6GHZ_OP_INFO) {
				/* Calculate offset to retrieve HE 6GHz oper information
				 *
				 * Skip past mandatory HE operation fields:
				 * HE operation parameters:	 3 bytes
				 * HE BSS color information:	 1 byte
				 * HE basic MCS-NSS information: 2 bytes
				 *                             = 6 bytes
				 */
				const u8 *oper_6g_info = (const u8 *)he_oper + 6;
				u8 control, ch_width;

				/* Skip 3bytes if VHT Operation information is present */
				if (he_oper->he_oper_params &
				    IEEE80211_HE_OPERATION_VHT_OPER_INFO)
					oper_6g_info += 3;

				/* Skip 1byte if Co-Hosted BSS information is present */
				if (he_oper->he_oper_params &
				    IEEE80211_HE_OPERATION_CO_HOSTED_BSS)
					oper_6g_info += 1;
				/* 6GHz Operation Info structure (5 bytes):
			 	 * Byte 0: Primary Channel
			 	 * Byte 1: Control (bits 0-1 = Channel Width)
			 	 * Byte 2: Center Freq Seg 0
			 	 * Byte 3: Center Freq Seg 1
			 	 * Byte 4: Minimum Rate
			 	 */
				control = oper_6g_info[1];
				ch_width = control & IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH;

				switch (ch_width) {
				case IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_160MHZ:
					peer_oper_bw = IEEE80211_STA_RX_BW_160;
					break;
				case IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_80MHZ:
					peer_oper_bw = IEEE80211_STA_RX_BW_80;
					break;
				case IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_40MHZ:
					peer_oper_bw = IEEE80211_STA_RX_BW_40;
					break;
				case IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_20MHZ:
					peer_oper_bw = IEEE80211_STA_RX_BW_20;
					break;
				}
			} else {
				peer_oper_bw = IEEE80211_STA_RX_BW_20;
			}
		} else {
			/* Non-6GHz HE operation, fall back to VHT or HT */
			if (elems->vht_operation)
				goto parse_vht_operation;
			else if (elems->ht_operation)
				goto parse_ht_operation;
		}
	} else if (elems->vht_operation) {
parse_vht_operation:
		const struct ieee80211_vht_operation *vht_oper = elems->vht_operation;

		switch (vht_oper->chan_width) {
		case IEEE80211_VHT_CHANWIDTH_160MHZ:
		case IEEE80211_VHT_CHANWIDTH_80P80MHZ:
			peer_oper_bw = IEEE80211_STA_RX_BW_160;
			break;
		case IEEE80211_VHT_CHANWIDTH_80MHZ:
			peer_oper_bw = IEEE80211_STA_RX_BW_80;
			break;
		case IEEE80211_VHT_CHANWIDTH_USE_HT:
		default:
			/* fall back to HT operation */
			if (elems->ht_operation)
				goto parse_ht_operation;
			else
				peer_oper_bw = IEEE80211_STA_RX_BW_20;
			break;
		}
	} else if (elems->ht_operation) {
parse_ht_operation:
		const struct ieee80211_ht_operation *ht_oper = elems->ht_operation;

		if (ht_oper->ht_param & IEEE80211_HT_PARAM_CHAN_WIDTH_ANY)
			peer_oper_bw = IEEE80211_STA_RX_BW_40;
		else
			peer_oper_bw = IEEE80211_STA_RX_BW_20;
	}
	link_sta->cur_max_bandwidth = peer_oper_bw;

	new_bw = _ieee80211_sta_cur_vht_bw(link_sta, NULL);

	if (new_bw != old_bw) {
		link_sta->pub->bandwidth = new_bw;
		cap_changed = true;
		printk("Mesh peer bandwidth updated: %d -> %d (peer_oper=%d)\n",
		       old_bw, new_bw, peer_oper_bw);
	}

	return cap_changed;
}

/**
 * ieee80211_mesh_beacon_intersect - Perform capability intersection
 *
 * @sdata: Interface data
 * @sta: Station info for mesh peer
 * @elems: Parsed beacon IEs
 * @mgmt: Beacon management frame
 * @band: NL80211 band ID
 *
 * Return: 0 on success, -ve on failure
 */
static int ieee80211_mesh_beacon_intersect(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_sta *pubsta,
					   struct ieee802_11_elems *elems,
					   struct ieee80211_mgmt *mgmt,
					   enum nl80211_band band)
{
	struct wiphy *wiphy = NULL;
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	struct link_sta_info *link_sta;
	struct ieee80211_supported_band *sband;
	bool cap_changed = false;

	if (!sdata->local)
		return -EINVAL;

	wiphy = sdata->local->hw.wiphy;

	lockdep_assert_wiphy(wiphy);

	link_sta = &sta->deflink;
	sband = wiphy->bands[band];

	if (!sband)
		return -EINVAL;

	/* Validate basic requirements */
	if (!elems->supp_rates) {
		pubsta->sta_extn.peer_info.intersect_failures++;
		return -EINVAL;
	}

	/* Update HT capabilities */
	if (elems->ht_cap_elem) {
		struct ieee80211_sta_ht_cap old_ht_cap = link_sta->pub->ht_cap;

		ieee80211_ht_cap_ie_to_sta_ht_cap(sdata, sband, elems->ht_cap_elem,
						  link_sta);

		if (memcmp(&old_ht_cap, &link_sta->pub->ht_cap, sizeof(old_ht_cap)))
			cap_changed = true;
	}

	/* Update VHT capabilities */
	if (elems->vht_cap_elem) {
		struct ieee80211_sta_vht_cap old_vht_cap = link_sta->pub->vht_cap;

		ieee80211_vht_cap_ie_to_sta_vht_cap(sdata, sband, elems->vht_cap_elem,
						    NULL, link_sta);

		if (memcmp(&old_vht_cap, &link_sta->pub->vht_cap, sizeof(old_vht_cap)))
			cap_changed = true;
	}

	/* Update HE capabilities */
	if (elems->he_cap && elems->he_cap_len) {
		struct ieee80211_sta_he_cap old_he_cap = link_sta->pub->he_cap;

		ieee80211_he_cap_ie_to_sta_he_cap(sdata, sband, elems->he_cap,
						  elems->he_cap_len, elems->he_6ghz_capa,
						  link_sta);

		if (memcmp(&old_he_cap, &link_sta->pub->he_cap, sizeof(old_he_cap)))
			cap_changed = true;
	}

	/* Update EHT capabilities */
	if (elems->eht_cap && elems->eht_cap_len) {
		struct ieee80211_sta_eht_cap old_eht_cap = link_sta->pub->eht_cap;

		ieee80211_eht_cap_ie_to_sta_eht_cap(sdata, sband, elems->he_cap,
						    elems->he_cap_len, elems->eht_cap,
						    elems->eht_cap_len,	link_sta);

		if (memcmp(&old_eht_cap, &link_sta->pub->eht_cap, sizeof(old_eht_cap)))
			cap_changed = true;
	}

	/* Update Operating BW for the peer */
	if (ieee80211_mesh_peer_bw_update(link_sta, elems, band))
		cap_changed = true;

	/* Schedule deferred work queue to send the peer assoc WMI with updated caps */
	if (cap_changed)
		wiphy_work_queue(wiphy, &sta->sta.sta_extn.update_cap_wk);

	return 0;
}

int ieee80211_extn_get_key_material(struct ieee80211_vif *vif,
				    int link_id,
				    const u8 *peer_mac,
				    bool pairwise,
				    u8 key_idx,
				    u8 *out_key, u8 out_key_max,
				    u8 *out_key_len)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_key *key;
	u8 len;

	if (!vif || !out_key || !out_key_len || !out_key_max)
		return -EINVAL;

	if (pairwise && !peer_mac)
		return -EINVAL;

	sdata = vif_to_sdata(vif);
	if (!sdata)
		return -EINVAL;

	rcu_read_lock();

	key = ieee80211_lookup_key(sdata, link_id, key_idx, pairwise,
				   pairwise ? peer_mac : NULL);
	if (!key) {
		rcu_read_unlock();
		return -ENOENT;
	}

	len = min_t(u8, key->conf.keylen, out_key_max);
	memcpy(out_key, key->conf.key, len);
	*out_key_len = len;

	rcu_read_unlock();
	return 0;
}
EXPORT_SYMBOL(ieee80211_extn_get_key_material);

/**
 * ieee80211_process_mesh_peer_beacon - Process beacon from mesh peer
 *	Called from ieee80211_rx_h_mgmt_check() when a beacon is received in
 *	mesh mode. Performs capability intersection if needed.
 *
 * @rx: RX data containing beacon frame
 *
 * Return: RX_CONTINUE to continue processing
 */
ieee80211_rx_result
ieee80211_process_mesh_peer_beacon(struct ieee80211_rx_data *rx)
{
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct ieee80211_mgmt *mgmt = (void *)rx->skb->data;
	struct ieee80211_sta *sta = NULL;
	struct ieee802_11_elems *elems = NULL;
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(rx->skb);
	u32 new_checksum;
	int ret;
	ieee80211_rx_result result = RX_CONTINUE;

	if (rx->sdata->vif.type != NL80211_IFTYPE_AP ||
	    rx->sdata->wdev.vap_submode != IEEE80211_EXTN_VAP_SUBMODE_MESH)
		return RX_CONTINUE;

	if (!ieee80211_is_beacon(mgmt->frame_control) && !ieee80211_is_action(mgmt->frame_control))
		return RX_DROP_MONITOR;

	rcu_read_lock();
	sta = ieee80211_find_sta(&rx->sdata->vif, mgmt->sa);
	if (!sta || !sta->sta_extn.peer_info.is_mesh_peer) {
		rcu_read_unlock();
		return RX_CONTINUE;
	}

	/* Parse beacon IEs */
	elems = ieee802_11_parse_elems(mgmt->u.beacon.variable,
				       rx->skb->len - offsetof(struct ieee80211_mgmt,
							       u.beacon.variable),
				       IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON,
				       NULL);
	if (!elems) {
		rcu_read_unlock();
		return RX_CONTINUE;
	}

	/* Check for MBSSID - skip nontransmitted BSSIDs */
	if (elems->bssid_index) {
		u8 mbssid_index = elems->bssid_index->bssid_index;
		if (mbssid_index != 0)
			/* Nontransmitted BSSID - not applicable for mesh */
			goto exit;
	}

	/* Update beacon reception timestamp and reset miss count */
	sta->sta_extn.peer_info.last_beacon_time = jiffies;

	/* Calculate IE checksum from raw beacon */
	new_checksum = ieee80211_calc_beacon_ie_checksum_raw(mgmt, rx->skb->len);

	/* Check if intersection needed */
	if (sta->sta_extn.peer_info.intersection_done &&
	    new_checksum == sta->sta_extn.peer_info.beacon_ie_checksum) {
		/* No capability change, skip intersection */
		goto exit;
	}

	/* Perform intersection */
	sta->sta_extn.peer_info.intersect_attempts++;

	ret = ieee80211_mesh_beacon_intersect(sdata, sta, elems, mgmt, rx_status->band);

	if (ret == 0)
		/* Update checksum on success */
		sta->sta_extn.peer_info.beacon_ie_checksum = new_checksum;

exit:
	rcu_read_unlock();
	kfree(elems);
	return result;
}
EXPORT_SYMBOL(ieee80211_process_mesh_peer_beacon);

bool ieee80211_skip_pn_check(struct ieee80211_rx_data *rx)
{
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct ieee80211_mgmt *mgmt = (void *)rx->skb->data;

	if ((sdata->wdev.vap_submode == IEEE80211_EXTN_VAP_SUBMODE_MESH) &&
	    ieee80211_is_beacon(mgmt->frame_control))
		return true;

	return false;
}
EXPORT_SYMBOL(ieee80211_skip_pn_check);

void ieee80211_mesh_init_peer_work(struct ieee80211_sub_if_data *sdata,
				   struct sta_info *sta_info)
{
	if (!sdata || !sta_info)
		return;

	if (sdata->wdev.vap_submode == IEEE80211_EXTN_VAP_SUBMODE_MESH) {
		wiphy_work_init(&sta_info->sta.sta_extn.update_cap_wk,
				ieee80211_update_mesh_peer_caps_work);
		wiphy_work_init(&sta_info->sta.sta_extn.timeout_wk,
				ieee80211_timeout_mesh_peer_work);
	}
}
EXPORT_SYMBOL(ieee80211_mesh_init_peer_work);

void ieee80211_mesh_peer_cleanup(struct wiphy *wiphy,
				 struct ieee80211_sub_if_data *sdata,
				 struct sta_info *sta_info)
{
	if (!wiphy || !sdata || !sta_info)
		return;

	if (sta_info->sta.sta_extn.peer_info.is_mesh_peer) {
		wiphy_work_cancel(wiphy, &sta_info->sta.sta_extn.update_cap_wk);
		wiphy_work_cancel(wiphy, &sta_info->sta.sta_extn.timeout_wk);
		sdata->u.ap.if_ap_extn.num_mesh_peers--;
		if (!sdata->u.ap.if_ap_extn.num_mesh_peers) {
			del_timer(&sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer);
		}
	}
}
EXPORT_SYMBOL(ieee80211_mesh_peer_cleanup);

/**
 * ieee80211_mesh_peer_timeout_check - Check for mesh peer timeouts
 *
 * @sdata: SDATA pointer
 *
 * Called periodically to check if mesh peers have stopped sending beacons.
 */
void ieee80211_mesh_peer_timeout_check(struct timer_list *t)
{
	struct sta_info *sta = NULL;
	struct ieee80211_sub_if_data *sdata =
		from_timer(sdata, t, u.ap.if_ap_extn.mesh_peer_cleanup_timer);;

	if (sdata->wdev.vap_submode != IEEE80211_EXTN_VAP_SUBMODE_MESH)
		return;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	list_for_each_entry(sta, &sdata->local->sta_list, list) {
		if (sta->sdata != sdata)
			continue;

		if (!sta->sta.sta_extn.peer_info.is_mesh_peer)
			continue;

		unsigned long now = jiffies;

		if (time_after(now, sta->sta.sta_extn.peer_info.last_beacon_time +
			       msecs_to_jiffies(MESH_PEER_INACTIVITY_PERIOD)))
			sta->sta.sta_extn.peer_info.beacon_miss_count++;
		else
			sta->sta.sta_extn.peer_info.beacon_miss_count = 0;

		if (sta->sta.sta_extn.peer_info.beacon_miss_count >=
		    sdata->u.ap.if_ap_extn.mesh_peer_timeout_cnt) {
			/* Peer timeout - trigger cleanup */
			wiphy_work_queue(sdata->local->hw.wiphy,
					 &sta->sta.sta_extn.timeout_wk);
		}
	}
	/* Re-arm the timer only if it is still enabled */
	if (sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer_enabled)
	    mod_timer(&sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer,
		      jiffies + msecs_to_jiffies(MESH_PEER_INACTIVITY_PERIOD));
}
EXPORT_SYMBOL(ieee80211_mesh_peer_timeout_check);

/**
 * ieee80211_vendor_mesh_peer_timeout_en - Enable or disable the mesh peer
 *                                            cleanup timer
 * @wiphy: wiphy pointer
 * @vif: virtual interface pointer
 * @enable: true to enable the timer, false to disable it
 *
 * When enabling, sets the enabled flag and arms the timer immediately via
 * mod_timer() if there are active mesh peers.  When disabling, clears the
 * flag and stops the timer via del_timer().
 *
 * Return: 0 on success, negative error code on failure
 */
int ieee80211_vendor_mesh_peer_timeout_en(struct wiphy *wiphy,
					  struct ieee80211_vif *vif,
					  bool enable)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	if (!sdata)
		return -EINVAL;

	if (sdata->wdev.vap_submode != IEEE80211_EXTN_VAP_SUBMODE_MESH) {
		printk(KERN_ERR "mesh_peer_cleanup_timer_config: not a mesh VAP\n");
		return -EINVAL;
	}

	if (enable) {
		sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer_enabled = true;
		/* If there are active peers, start the timer immediately */
		if (sdata->u.ap.if_ap_extn.num_mesh_peers > 0)
			mod_timer(&sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer,
				  jiffies + msecs_to_jiffies(MESH_PEER_INACTIVITY_PERIOD));
		printk(KERN_DEBUG "Mesh peer cleanup timer enabled\n");
	} else {
		sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer_enabled = false;
		del_timer(&sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer);
		printk(KERN_DEBUG "Mesh peer cleanup timer disabled\n");
	}

	return 0;
}
EXPORT_SYMBOL(ieee80211_vendor_mesh_peer_timeout_en);

/**
 * Vendor mesh operations structure - provides function pointers
 * for mesh peer management operations
 */
static const struct ieee80211_vendor_mesh_ops vendor_mesh_ops = {
       .add_local_peer = ieee80211_vendor_mesh_add_local_peer,
       .authorize_peer = ieee80211_vendor_mesh_authorize_peer,
       .delete_local_peer = ieee80211_vendor_mesh_delete_local_peer,
};

/**
 * ieee80211_get_vendor_mesh_ops - Get vendor mesh operations
 *
 * Returns a pointer to the structure containing function pointers
 * for vendor mesh operations.
 *
 * Return: Pointer to ieee80211_vendor_mesh_ops structure
 */
const struct ieee80211_vendor_mesh_ops *ieee80211_get_vendor_mesh_ops(void)
{
       return &vendor_mesh_ops;
}
EXPORT_SYMBOL(ieee80211_get_vendor_mesh_ops);
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */
