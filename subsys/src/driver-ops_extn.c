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
#include "cmn_extn.h"

#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
#define SUPPORTED_RATES_LEN 8

unsigned int mmeshsim = 1;
EXPORT_SYMBOL(mmeshsim);
module_param_named(mmeshsim, mmeshsim, uint, 0644);
MODULE_PARM_DESC(mmeshsim, "mmeshsim");

u8 supported_rates_5g6g[SUPPORTED_RATES_LEN] = {0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c};
u8 supported_rates_2g[SUPPORTED_RATES_LEN] = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};

/* AKM/cipher pair preference order for managed mesh beacon intersection.
 * PSK-SHA256/CCMP-256 is preferred; PSK/CCMP is the fallback.
 */
static const struct mesh_akm_cipher_pref mesh_combo_pref[] = {
	{ WLAN_AKM_SUITE_PSK_SHA256, WLAN_CIPHER_SUITE_CCMP_256 },
	{ WLAN_AKM_SUITE_PSK,        WLAN_CIPHER_SUITE_CCMP     },
};
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */

void ieee80211_set_vif_diagnostic_sta_extn(struct ieee80211_vif *vif, bool diagnostic_sta)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;

	lockdep_assert_wiphy(local->hw.wiphy);

	wiphy_dbg(local->hw.wiphy,
		  "DEBUG_DIAG_STA: set_vif_diagnostic_sta called vif=%s type=%d diagnostic_sta=%d old=%d\n",
		  sdata->name, vif->type, diagnostic_sta, vif->diagnostic_sta);

	if (WARN_ON(vif->type != NL80211_IFTYPE_STATION))
		return;

	vif->diagnostic_sta = diagnostic_sta;

	sdata_info(sdata, "vif %s CSA isolation\n",
		   diagnostic_sta ? "marked" : "unmarked");
}
EXPORT_SYMBOL(ieee80211_set_vif_diagnostic_sta_extn);

struct ieee80211_240mhz_vendor_oper_extn*
drv_get_240mhz_cap_extn(struct ieee80211_local *local,
			struct ieee80211_vif *vif,
			struct ieee80211_link_sta *link_sta)
{
	if (local->ops_extn && local->ops_extn->get_240mhz_cap_extn)
		return local->ops_extn->get_240mhz_cap_extn(vif, link_sta);

	return NULL;
}

int ieee80211_validate_6ghz_chandef_extn(struct ieee80211_local *local,
					 struct ieee80211_vif *vif,
					 unsigned int link_id,
					 const struct cfg80211_chan_def *chandef,
					 enum ieee80211_ap_reg_power power_type)
{
	int ret;

	if (!chandef || !chandef->chan ||
	    chandef->chan->band != NL80211_BAND_6GHZ)
		return 0;

	if (!local->ops_extn || !local->ops_extn->validate_6ghz_chandef_extn)
		return 0;

	ret = local->ops_extn->validate_6ghz_chandef_extn(
		&local->hw, vif, link_id, chandef, power_type);
	if (ret)
		wiphy_dbg(local->hw.wiphy,
			  "HWBL: 6 GHz chandef blocked (freq %u MHz, bw %d, power_type %d)\n",
			  chandef->chan->center_freq,
			  nl80211_chan_width_to_mhz(chandef->width),
			  power_type);
	return ret;
}

/**
 * ieee80211_nol_regdom_change_locked_extn - notify driver of a regulatory domain update
 * @local: mac80211 local state
 *
 * Called from cfg80211_nol_regdom_update_locked_extn() after handle_band_custom()
 * has finished updating the 5 GHz channel table.  Delegates to the driver
 * callback which iterates all radios on the ieee80211_hw and restores each
 * radio's NOL from its dump file if the new country code matches.
 */
void ieee80211_nol_regdom_change_locked_extn(struct ieee80211_local *local)
{
	if (!local->ops_extn || !local->ops_extn->nol_regdom_change_locked_extn)
		return;

	local->ops_extn->nol_regdom_change_locked_extn(&local->hw);
}
/*
 * For a diagnostic-STA vif (vif->diagnostic_sta),
 * clamp a scan request to whatever channel the co-channel-context AP vif
 * on the scan's own radio is currently operating on.
 * The diagnostic-STA vif is supposed to scan only on AP operating
 * channel, so added change in mac80211 to find the subset of AP’s
 * channel in scan channel list and overwrite the channel list with
 * AP’s channel and update channel count to 1
 *
 * If the co-channel-context AP vif has a channel switch in progress
 * (link_conf->csa_active), ctx->conf.def still reflects the OLD channel --
 * the new one isn't applied until ieee80211_csa_finalize() runs. Scanning/
 * reconnecting to the old channel while the AP is mid-switch races the
 * finalize and has been observed to crash firmware. Returns -EBUSY in
 * that case so the caller rejects the scan outright instead of targeting
 * a channel the AP is already leaving; the isolated STA vif will naturally
 * get a fresh CTRL-EVENT-DISCONNECTED-driven reconnect attempt once
 * AP-CSA-FINISHED clears csa_active.
 */
int ieee80211_clamp_scan_to_ap_channel_extn(struct ieee80211_sub_if_data *sdata,
					    struct cfg80211_scan_request *req)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *ctx;
	struct ieee80211_channel *ap_chan = NULL;
	bool ap_csa_active = false;
	u32 radio_mask = 0;
	u32 i, kept = 0, orig_n_channels = req->n_channels;

	lockdep_assert_wiphy(local->hw.wiphy);

	for (i = 0; i < req->n_channels; i++) {
		int radio_idx = cfg80211_get_hw_idx_by_chan(local->hw.wiphy,
							    req->channels[i]);
		if (radio_idx >= 0)
			radio_mask |= BIT(radio_idx);
	}

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		struct ieee80211_link_data *link;

		/*
		 * radio_mask == 0 means cfg80211_get_hw_idx_by_chan()
		 * couldn't resolve a radio for any requested channel (e.g.
		 * wiphy->n_radio == 0, a single implicit radio) -- don't
		 * filter by radio at all in that case. Likewise, a chanctx
		 * with radio_idx < 0 is never excluded, since -1 means "no
		 * specific radio" on this wiphy too.
		 */
		if (radio_mask && ctx->conf.radio_idx >= 0 &&
		    !(radio_mask & BIT(ctx->conf.radio_idx)))
			continue;

		list_for_each_entry(link, &ctx->assigned_links,
				    assigned_chanctx_list) {
			if (link->sdata->vif.type == NL80211_IFTYPE_AP ||
			    link->sdata->vif.type == NL80211_IFTYPE_MONITOR
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
			    || link->sdata->wdev.vap_submode ==
			       IEEE80211_EXTN_VAP_SUBMODE_MESH
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */
			   ) {
				ap_chan = ctx->conf.def.chan;
				ap_csa_active = link->conf->csa_active;
				break;
			}
		}
		if (ap_chan)
			break;
	}

	wiphy_dbg(local->hw.wiphy,
		  "DEBUG_DIAG_STA: clamp_scan_to_ap_channel: requested_n_channels=%u radio_mask=0x%x ap_chan=%d MHz ap_csa_active=%d\n",
		  orig_n_channels, radio_mask, ap_chan ? ap_chan->center_freq : -1,
		  ap_csa_active);

	if (!ap_chan) {
		wiphy_dbg(local->hw.wiphy,
			  "DEBUG_DIAG_STA: clamp_scan_to_ap_channel: ap_chan not present, go for full scan");
		return 0;
	}

	if (ap_csa_active) {
		wiphy_dbg(local->hw.wiphy,
			  "DEBUG_DIAG_STA: clamp_scan_to_ap_channel: AP CSA in progress, rejecting scan\n");
		return -EBUSY;
	}


	for (i = 0; i < req->n_channels; i++) {
		if (req->channels[i]->center_freq == ap_chan->center_freq &&
		    req->channels[i]->freq_offset == ap_chan->freq_offset)
			req->channels[kept++] = req->channels[i];
	}
	if (!kept) {
		wiphy_dbg(local->hw.wiphy,
			  "DEBUG_DIAG_STA: clamp_scan_to_ap_channel:"
			  "AP channel not in scan list, rejecting scan\n");

		return -EINVAL;
	}
	req->n_channels = kept;

	wiphy_dbg(local->hw.wiphy,
		  "DEBUG_DIAG_STA: clamp_scan_to_ap_channel: kept=%u of %u channels\n",
		  kept, orig_n_channels);

	return 0;
}
EXPORT_SYMBOL(ieee80211_clamp_scan_to_ap_channel_extn);

/*
 * There is a possibility for STA to do Auth/Assoc against a stale
 * scan cached BSS entry. Hence validate the STA’s Auth/Assoc channel
 * with the co-located AP’s channel, if different reject the Auth/Assoc
 * frame and abort the connection
 */
bool ieee80211_diagnostic_sta_blocks_bss_extn(struct ieee80211_sub_if_data *sdata,
					      struct cfg80211_bss *bss)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx *ctx;
	int bss_radio_idx;

	if (!sdata->vif.diagnostic_sta)
		return false;

	bss_radio_idx = cfg80211_get_hw_idx_by_chan(local->hw.wiphy, bss->channel);

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		struct ieee80211_link_data *link;

		/*
		 * bss_radio_idx < 0 means cfg80211_get_hw_idx_by_chan()
		 * couldn't resolve a radio for @bss's channel (e.g.
		 * wiphy->n_radio == 0, a single implicit radio) -- don't
		 * filter by radio at all in that case. Likewise, a chanctx
		 * with radio_idx < 0 is never excluded, since -1 means "no
		 * specific radio" on this wiphy too.
		 */
		if (bss_radio_idx >= 0 && ctx->conf.radio_idx >= 0 &&
		    bss_radio_idx != ctx->conf.radio_idx)
			continue;

		list_for_each_entry(link, &ctx->assigned_links,
				    assigned_chanctx_list) {
			struct ieee80211_channel *ap_chan;

			if (link->sdata->vif.type != NL80211_IFTYPE_AP &&
			    link->sdata->vif.type != NL80211_IFTYPE_MONITOR
#ifdef CPTCFG_QCN_EXTN_MESH_SUPPORT
			    && link->sdata->wdev.vap_submode !=
			       IEEE80211_EXTN_VAP_SUBMODE_MESH
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */
			   )
				continue;

			if (link->conf->csa_active) {
				wiphy_dbg(local->hw.wiphy,
					  "DEBUG_DIAG_STA: diagnostic_sta_blocks_bss: vif=%s co-vif=%s csa_active=1, blocking\n",
					  sdata->name, link->sdata->name);
				return true;
			}

			ap_chan = ctx->conf.def.chan;
			if (ap_chan && bss->channel &&
			    (bss->channel->center_freq != ap_chan->center_freq ||
			     bss->channel->freq_offset != ap_chan->freq_offset)) {
				wiphy_dbg(local->hw.wiphy,
					  "DEBUG_DIAG_STA: diagnostic_sta_blocks_bss: vif=%s co-vif=%s bss_chan=%d MHz ap_chan=%d MHz mismatch, blocking\n",
					  sdata->name, link->sdata->name,
					  bss->channel->center_freq,
					  ap_chan->center_freq);
				return true;
			}
		}
	}

	return false;
}
EXPORT_SYMBOL(ieee80211_diagnostic_sta_blocks_bss_extn);

static const char *
ieee80211_diagnostic_sta_disconnect_reason_str(enum ieee80211_diagnostic_sta_disconnect_reason reason)
{
	switch (reason) {
	case IEEE80211_DIAGNOSTIC_STA_DISCONNECT_CSA:
		return "co-channel-context CSA";
	case IEEE80211_DIAGNOSTIC_STA_DISCONNECT_RADAR:
		return "co-channel-context DFS radar detection";
	case IEEE80211_DIAGNOSTIC_STA_DISCONNECT_AWGN:
		return "co-channel-context AWGN/CW interference";
	case IEEE80211_DIAGNOSTIC_STA_DISCONNECT_USERSPACE_CHANGE:
		return "co-channel-context userspace channel change";
	default:
		return "co-channel-context channel change";
	}
}

/*
 * Synchronously drop any diagnostic-STA vif (vif->diagnostic_sta, see
 * mac80211.h) currently assigned to @ctx, notifying the driver of @reason
 * so it can log/report it (see &enum ieee80211_diagnostic_sta_disconnect_reason).
 * There is no single "initiator" link to exclude here: any isolated STA vif
 * sharing @ctx is disconnected. This covers the co-located-AP CSA case too
 * (the initiator link is never itself DIAGNOSTIC_STA -- that flag is only
 * ever set on STA-type vifs -- so it is already excluded by the flag check
 * below without needing a separate initiator check).
 */
void ieee80211_disconnect_diagnostic_sta_on_ctx_extn(struct ieee80211_local *local,
						     struct ieee80211_chanctx *ctx,
						     enum ieee80211_diagnostic_sta_disconnect_reason reason)
{
	struct ieee80211_link_data *link, *tmp;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (!ctx)
		return;

	list_for_each_entry_safe(link, tmp, &ctx->assigned_links,
				 assigned_chanctx_list) {
		if (!link->sdata->vif.diagnostic_sta)
			continue;

		sdata_info(link->sdata,
			   "%s; disconnecting diagnostic-STA\n",
			   ieee80211_diagnostic_sta_disconnect_reason_str(reason));
		wiphy_work_queue(local->hw.wiphy, &link->sdata->u.mgd.csa_connection_drop_work);
		ieee80211_link_release_channel(link);
		drv_notify_diagnostic_sta_disconnect(local, link->sdata,
						     link->link_id, reason);
	}
}
EXPORT_SYMBOL(ieee80211_disconnect_diagnostic_sta_on_ctx_extn);

/*
 * mac80211's own copy of this helper (formerly a static inline in
 * driver-ops.h) lived alongside drv_unassign_vif_chanctx() and friends;
 * moved here since notify_diagnostic_sta_disconnect is itself a
 * CPTCFG_QCN_EXTN-only driver op.
 */
void drv_notify_diagnostic_sta_disconnect(struct ieee80211_local *local,
					struct ieee80211_sub_if_data *sdata,
					unsigned int link_id,
					enum ieee80211_diagnostic_sta_disconnect_reason reason)
{
	if (!sdata || !local->ops->notify_diagnostic_sta_disconnect)
		return;

	lockdep_assert_wiphy(local->hw.wiphy);

	local->ops->notify_diagnostic_sta_disconnect(&local->hw, &sdata->vif,
						   link_id, reason);
}
EXPORT_SYMBOL(drv_notify_diagnostic_sta_disconnect);

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
 * ieee80211_mesh_parse_local_rsn_caps - Extract RSN Capabilities from an RSN IE body.
 * Returns 0 if the capabilities field is not reachable.
 *
 * @rsn_body: pointer to the RSN IE body (after tag+datalen bytes)
 * @body_len: length of the body (element::datalen)
 */
static u16 ieee80211_mesh_parse_local_rsn_caps(const u8 *rsn_body, u8 body_len)
{
	const u8 *pos = rsn_body;
	u16 rem = body_len;
	u16 n;

	/* Skip RSN Version */
	if (rem < RSN_VERSION_LEN)
		return 0;
	pos += RSN_VERSION_LEN;
	rem -= RSN_VERSION_LEN;

	/* Skip Group Cipher Suite */
	if (rem < RSN_SUITE_LEN)
		return 0;
	pos += RSN_SUITE_LEN;
	rem -= RSN_SUITE_LEN;

	/* Skip Pairwise Cipher Suite list */
	if (rem < RSN_SUITE_COUNT_LEN)
		return 0;
	n = get_unaligned_le16(pos);
	pos += RSN_SUITE_COUNT_LEN;
	rem -= RSN_SUITE_COUNT_LEN;
	if ((u32)n * RSN_SUITE_LEN > rem)
		return 0;
	pos += n * RSN_SUITE_LEN;
	rem -= n * RSN_SUITE_LEN;

	/* Skip AKM Suite list */
	if (rem < RSN_SUITE_COUNT_LEN)
		return 0;
	n = get_unaligned_le16(pos);
	pos += RSN_SUITE_COUNT_LEN;
	rem -= RSN_SUITE_COUNT_LEN;
	if ((u32)n * RSN_SUITE_LEN > rem)
		return 0;
	pos += n * RSN_SUITE_LEN;
	rem -= n * RSN_SUITE_LEN;

	/* RSN Capabilities */
	if (rem < RSN_CAP_LEN)
		return 0;
	return get_unaligned_le16(pos);
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

	pr_debug_ratelimited("managed_mesh_dbg ap_config_save: wpa_versions=0x%x beacon_tail_len=%zu\n",
			    params->crypto.wpa_versions, params->beacon.tail_len);

	link_conf->bss_conf_extn = kzalloc(sizeof(struct ieee80211_bss_conf_extn),
					   GFP_KERNEL);
	if (!link_conf->bss_conf_extn) {
		pr_err("managed_mesh_dbg ap_config_save: failed to alloc bss_conf_extn\n");
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

	/* Allocate and populate RSN/WPA security config */
	mesh_conf->rsn_conf = kzalloc(sizeof(struct mesh_rsn_conf), GFP_KERNEL);
	if (!mesh_conf->rsn_conf)
		return -ENOMEM;

	mesh_conf->rsn_conf->wpa_versions = params->crypto.wpa_versions;

	if (params->crypto.n_ciphers_pairwise > 0) {
		mesh_conf->rsn_conf->n_pairwise_ciphers =
			min_t(u8, params->crypto.n_ciphers_pairwise,
			      NL80211_MAX_NR_CIPHER_SUITES);
		memcpy(mesh_conf->rsn_conf->pairwise_ciphers,
		       params->crypto.ciphers_pairwise,
		       mesh_conf->rsn_conf->n_pairwise_ciphers * sizeof(u32));
	}

	if (params->crypto.n_akm_suites > 0) {
		mesh_conf->rsn_conf->n_akm_suites =
			min_t(u8, params->crypto.n_akm_suites,
			      NL80211_MAX_NR_AKM_SUITES);
		memcpy(mesh_conf->rsn_conf->akm_suites,
		       params->crypto.akm_suites,
		       mesh_conf->rsn_conf->n_akm_suites * sizeof(u32));
	}

	/* Save a copy of the local AP's RSN IE from the beacon tail and
	 * pre-parse the RSN Capabilities for MFP cross-check. */
	if (params->beacon.tail && params->beacon.tail_len > 0) {
		const struct element *elem =
			(const struct element *)cfg80211_find_ie(WLAN_EID_RSN,
								 params->beacon.tail,
								 params->beacon.tail_len);
		if (elem) {
			u16 total_len = sizeof(*elem) + elem->datalen;

			mesh_conf->rsn_conf->rsn_ie = kmemdup(elem, total_len, GFP_KERNEL);
			if (!mesh_conf->rsn_conf->rsn_ie) {
				kfree(mesh_conf->rsn_conf);
				mesh_conf->rsn_conf = NULL;
				return -ENOMEM;
			}
			mesh_conf->rsn_conf->rsn_ie_len = total_len;
			mesh_conf->rsn_conf->rsn_caps =
				ieee80211_mesh_parse_local_rsn_caps(elem->data,
								    elem->datalen);
		}
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

	if (!link_conf || !link_conf->bss_conf_extn)
		return;

	mesh_conf = &link_conf->bss_conf_extn->mesh_bss_caps;

	kfree(mesh_conf->ht_cap);
	kfree(mesh_conf->vht_cap);
	kfree(mesh_conf->he_cap);
	kfree(mesh_conf->eht_cap);
	if (mesh_conf->rsn_conf) {
		kfree(mesh_conf->rsn_conf->rsn_ie);
		kfree(mesh_conf->rsn_conf);
	}
	kfree(link_conf->bss_conf_extn);
	link_conf->bss_conf_extn = NULL;
}

enum ieee80211_encrypt {
        ENCRYPT_NO,
        ENCRYPT_MGMT,
        ENCRYPT_DATA,
};
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
	if (!wdev || !IEEE80211_EXTN_VAP_IS_MESH_MODE(wdev->vap_submode)) {
		pr_debug_ratelimited("managed_mesh_dbg ap_setup: skipped (vap_submode=%u)\n",
				     wdev ? wdev->vap_submode : 0xff);
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

	/* For non-beaconing BSS, peer lifecycle is managed externally
	 * via wlanconfig addlocalpeer/dellocalpeer. Disable the inactivity
	 * timer so peers are not auto-deleted due to missing beacons. For
	 * other BSS, enable peer cleanup timer by default.
	 */
	if (wdev->non_beacon_bss)
		sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer_enabled = false;
	else
		sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer_enabled = true;
}
EXPORT_SYMBOL(ieee80211_mesh_ap_setup);

void ieee80211_mesh_ap_cleanup(struct wireless_dev *wdev,
			       struct ieee80211_sub_if_data *sdata,
			       struct ieee80211_bss_conf *link_conf)
{
	if (!wdev) {
		pr_debug("managed_mesh_dbg ap_cleanup: skipped (wdev is NULL)\n");
		return;
	}

	if (!sdata || !link_conf) {
		printk(KERN_DEBUG "Mesh AP cleanup skipped, invalid params\n");
		return;
	}

	ieee80211_free_mesh_vap_caps(link_conf);
	if (sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer_enabled)
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
						       u8 nss, u8 mode,
						       enum nl80211_band band)
{
	int iter;

	if (!ht_cap)
	    return -EINVAL;

	for (iter = 0; iter < nss; iter++)
		ht_cap->mcs.rx_mask[iter] = 0xFF;
	ht_cap->mcs.tx_params |= IEEE80211_HT_MCS_TX_DEFINED;

	if (mode < MESH_PREAMBLE_VHT)
	    return 0;

	/* VHT is optional on 2.4 GHz and not supported on 6 GHz; skip VHT
	 * MCS/NSS update for these bands. HE/EHT caps are updated separately
	 * by the caller.
	 */
	if (!vht_cap) {
		if (band == NL80211_BAND_2GHZ)
			return 0;
		return -EINVAL;
	}

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
								  mode, band);

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
		station_params->link_sta_params.link_id =
			ieee80211_vif_is_mld(vif) ?
			(int)mesh_params->peers[0].link_id : -1;
	else
		station_params->link_sta_params.link_id =
			link_conf->link_id;

	station_params->link_sta_params.mld_mac =
		is_zero_ether_addr(mesh_params->peer_mld_mac) ?
		NULL : mesh_params->peer_mld_mac;

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

static bool ieee80211_mesh_bss_is_secured(struct ieee80211_bss_conf *bss_conf)
{
	return bss_conf &&
	       bss_conf->bss_conf_extn &&
	       bss_conf->bss_conf_extn->mesh_bss_caps.rsn_conf &&
	       (bss_conf->bss_conf_extn->mesh_bss_caps.rsn_conf->wpa_versions &
		(NL80211_WPA_VERSION_2 | NL80211_WPA_VERSION_3));
}

/**
 * ieee80211_mesh_local_has_suite - Check if a suite is in the local
 * VAP's configured suite array.
 * @list:  Local suite array (akm_suites[] or pairwise_ciphers[])
 * @count: Number of valid entries in @list
 * @suite: Suite selector to look for
 */
static bool
ieee80211_mesh_local_has_suite(const u32 *list, u8 count, u32 suite)
{
	int i;

	for (i = 0; i < count; i++)
		if (list[i] == suite)
			return true;
	return false;
}

/**
 * ieee80211_mesh_peer_has_suite - Check if a suite is in the peer's
 * flat RSN IE suite list.
 * @peer_list:  Peer's flat suite list (array of 4-byte OUI+type entries)
 * @peer_count: Number of entries in @peer_list
 * @suite:      Suite selector to look for
 */
static bool
ieee80211_mesh_peer_has_suite(const u8 *peer_list, u16 peer_count, u32 suite)
{
	int i;

	if (!peer_list)
		return false;

	for (i = 0; i < peer_count; i++)
		if (get_unaligned_be32(peer_list + i * RSN_SUITE_LEN) == suite)
			return true;
	return false;
}

/**
 * ieee80211_mesh_negotiate_combo - Select an (AKM, cipher) pair using
 * the fixed preference order.
 * @rsn_conf:          Local VAP RSN configuration
 * @peer_akm_list:     Peer's flat AKM suite list (from RSN IE)
 * @peer_akm_count:    Number of entries in @peer_akm_list
 * @peer_cipher_list:  Peer's flat pairwise cipher suite list (from RSN IE)
 * @peer_cipher_count: Number of entries in @peer_cipher_list
 * @out_akm:           Set to the selected AKM suite on success
 * @out_cipher:        Set to the selected cipher suite on success
 * Iterates mesh_combo_pref[] in order.
 * Returns true and fills *out_akm or *out_cipher on success, false if no
 * common combination exists.
 */
static bool
ieee80211_mesh_negotiate_combo(const struct mesh_rsn_conf *rsn_conf,
			       const u8 *peer_akm_list, u16 peer_akm_count,
			       const u8 *peer_cipher_list, u16 peer_cipher_count,
			       u32 *out_akm, u32 *out_cipher)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mesh_combo_pref); i++) {
		u32 akm    = mesh_combo_pref[i].akm;
		u32 cipher = mesh_combo_pref[i].cipher;

		if (!ieee80211_mesh_local_has_suite(rsn_conf->akm_suites,
						    rsn_conf->n_akm_suites, akm) ||
		    !ieee80211_mesh_local_has_suite(rsn_conf->pairwise_ciphers,
						    rsn_conf->n_pairwise_ciphers, cipher))
			continue;
		if (!ieee80211_mesh_peer_has_suite(peer_akm_list, peer_akm_count, akm) ||
		    !ieee80211_mesh_peer_has_suite(peer_cipher_list, peer_cipher_count, cipher))
			continue;

		*out_akm    = akm;
		*out_cipher = cipher;
		return true;
	}
	return false;
}

/**
 * ieee80211_mesh_peer_clear_crypto - Reset a peer's crypto/MFP state.
 * @peer: Peer info to clear
 *
 * Handles primitive mesh RSN handling cases by clearing the peer crypto
 * accordingly
 */
static void
ieee80211_mesh_peer_clear_crypto(struct mesh_peer_info *peer)
{
	peer->peer_ucast_cipher  = 0;
	peer->peer_mcast_cipher  = 0;
	peer->peer_akm_suite     = 0;
	peer->peer_wpa_versions  = 0;
	peer->peer_mfp_capable   = false;
	peer->peer_mfp_required  = false;
	peer->peer_rsn_ie_len    = 0;
	peer->peer_ht_restricted = false;
}

/**
 * ieee80211_mesh_rsn_locate_list - Navigate to a suite list within an RSN IE body.
 * @rsn_body:     RSN IE body (starting at the version field)
 * @rsn_body_len: Length of @rsn_body
 * @list_index:   0 = pairwise cipher list, 1 = AKM suite list
 * @count_out:    Returns the number of suites in the located list
 *
 * Returns a pointer to the first 4-byte suite entry in the requested list,
 * or NULL if the IE is too short or malformed.
 */
static const u8 *
ieee80211_mesh_rsn_locate_list(const u8 *rsn_body, u8 rsn_body_len,
			       int list_index, u16 *count_out)
{
	const u8 *pos = rsn_body;
	u16 ie_len = rsn_body_len;
	u16 count;
	int idx;

	/* Skip RSN version (2) + group cipher (4) */
	if (ie_len < RSN_VERSION_LEN + RSN_SUITE_LEN)
		return NULL;
	pos   += RSN_VERSION_LEN + RSN_SUITE_LEN;
	ie_len -= RSN_VERSION_LEN + RSN_SUITE_LEN;

	for (idx = 0; idx <= list_index; idx++) {
		if (ie_len < RSN_SUITE_COUNT_LEN)
			return NULL;
		count  = get_unaligned_le16(pos);
		pos   += RSN_SUITE_COUNT_LEN;
		ie_len -= RSN_SUITE_COUNT_LEN;
		if ((u32)count * RSN_SUITE_LEN > ie_len)
			return NULL;
		if (idx == list_index) {
			*count_out = count;
			return pos;
		}
		pos   += count * RSN_SUITE_LEN;
		ie_len -= count * RSN_SUITE_LEN;
	}
	/* Unreachable: the loop always returns when idx == list_index. */
	return NULL;
}

/**
 * ieee80211_mesh_select_akm_cipher - Select an (AKM, cipher) pair for
 * the peer using ieee80211_mesh_negotiate_combo(); handles the legacy
 * zero-count RSN fallback to PSK/CCMP
 * @mesh_caps: Local mesh BSS capabilities (may be NULL)
 * @elems:     Parsed peer beacon IEs
 * @peer:      Peer info; peer_akm_suite/peer_ucast_cipher set on success
 *
 * Returns true if a pair was selected, false if none found.
 */
static bool
ieee80211_mesh_select_akm_cipher(struct mesh_bss_conf *mesh_caps,
				 struct ieee802_11_elems *elems,
				 struct mesh_peer_info *peer)
{
	const u8 *peer_akm_list, *peer_cipher_list;
	u16 peer_akm_count = 0, peer_cipher_count = 0;
	u32 sel_akm = 0, sel_cipher = 0;

	if (!mesh_caps || !mesh_caps->rsn_conf)
		return true;  /* nothing configured locally — leave peer fields as-is */

	peer_cipher_list = ieee80211_mesh_rsn_locate_list(
			elems->rsn, elems->rsn_len, 0, &peer_cipher_count);
	peer_akm_list = ieee80211_mesh_rsn_locate_list(
			elems->rsn, elems->rsn_len, 1, &peer_akm_count);

	if (peer_cipher_count == 0 && peer_akm_count == 0) {
		if (ieee80211_mesh_local_has_suite(mesh_caps->rsn_conf->akm_suites,
						   mesh_caps->rsn_conf->n_akm_suites,
						   WLAN_AKM_SUITE_PSK) &&
		    ieee80211_mesh_local_has_suite(mesh_caps->rsn_conf->pairwise_ciphers,
						   mesh_caps->rsn_conf->n_pairwise_ciphers,
						   WLAN_CIPHER_SUITE_CCMP)) {
			sel_cipher = WLAN_CIPHER_SUITE_CCMP;
			sel_akm    = WLAN_AKM_SUITE_PSK;
		} else if (ieee80211_mesh_local_has_suite(mesh_caps->rsn_conf->akm_suites,
							  mesh_caps->rsn_conf->n_akm_suites,
							  WLAN_AKM_SUITE_PSK_SHA256) &&
			   ieee80211_mesh_local_has_suite(mesh_caps->rsn_conf->pairwise_ciphers,
							  mesh_caps->rsn_conf->n_pairwise_ciphers,
							  WLAN_CIPHER_SUITE_CCMP_256)) {
			sel_cipher = WLAN_CIPHER_SUITE_CCMP_256;
			sel_akm    = WLAN_AKM_SUITE_PSK_SHA256;
		} else {
			peer->cnt_no_common_combo++;
			return false;
		}
		peer->cnt_legacy_zero_rsn++;
	} else if (ieee80211_mesh_negotiate_combo(mesh_caps->rsn_conf,
						  peer_akm_list, peer_akm_count,
						  peer_cipher_list, peer_cipher_count,
						  &sel_akm, &sel_cipher)) {
		if (sel_akm == WLAN_AKM_SUITE_PSK_SHA256)
			peer->cnt_combo_sha256_ccmp256++;
		else
			peer->cnt_combo_psk_ccmp++;
	} else {
		peer->cnt_no_common_combo++;
		return false;
	}

	peer->peer_akm_suite    = sel_akm;
	peer->peer_ucast_cipher = sel_cipher;
	return true;
}

/**
 * ieee80211_mesh_vap_gtk_installed - Check all required GTK source key slots
 * are installed for the configured AKM/cipher combos.
 * @sdata:    sub-interface data
 * @link_id:  link ID to check (from addlocalpeer command)
 * @rsn_conf: local VAP RSN configuration
 *
 * For PSK-SHA256/CCMP-256: slots 0 and 1 must both be present.
 * For PSK/CCMP: slots 2 and 3 must both be present.
 * Returns true only when all required slots for the configured combos are
 * populated, false otherwise.
 */
static bool ieee80211_mesh_vap_gtk_installed(struct ieee80211_sub_if_data *sdata,
					     int link_id,
					     const struct mesh_rsn_conf *rsn_conf)
{
	struct ieee80211_link_data *link;
	bool need_ccmp256, need_ccmp;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	link = wiphy_dereference(sdata->local->hw.wiphy, sdata->link[link_id]);
	if (!link)
		return false;

	need_ccmp256 =
		ieee80211_mesh_local_has_suite(rsn_conf->akm_suites,
					       rsn_conf->n_akm_suites,
					       WLAN_AKM_SUITE_PSK_SHA256) &&
		ieee80211_mesh_local_has_suite(rsn_conf->pairwise_ciphers,
					       rsn_conf->n_pairwise_ciphers,
					       WLAN_CIPHER_SUITE_CCMP_256);

	need_ccmp =
		ieee80211_mesh_local_has_suite(rsn_conf->akm_suites,
					       rsn_conf->n_akm_suites,
					       WLAN_AKM_SUITE_PSK) &&
		ieee80211_mesh_local_has_suite(rsn_conf->pairwise_ciphers,
					       rsn_conf->n_pairwise_ciphers,
					       WLAN_CIPHER_SUITE_CCMP);

	if (need_ccmp256 &&
	    (!wiphy_dereference(sdata->local->hw.wiphy, link->gtk[0]) ||
	     !wiphy_dereference(sdata->local->hw.wiphy, link->gtk[1])))
		return false;

	if (need_ccmp &&
	    (!wiphy_dereference(sdata->local->hw.wiphy, link->gtk[2]) ||
	     !wiphy_dereference(sdata->local->hw.wiphy, link->gtk[3])))
		return false;

	return need_ccmp256 || need_ccmp;
}

/**
 * ieee80211_mesh_install_gtks_for_peer - Re-install link GTKs per-mesh-peer
 * after add.
 * @sdata:   sub-interface data
 * @sta_info: newly added sta_info for the mesh peer
 * @link_id: link ID the peer was added on
 *
 */
static void ieee80211_mesh_install_gtks_for_peer(struct ieee80211_sub_if_data *sdata,
						 struct sta_info *sta_info,
						 u8 link_id)
{
	struct ieee80211_link_data *link;
	struct ieee80211_key *key;
	int i;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return;

	link = wiphy_dereference(sdata->local->hw.wiphy, sdata->link[link_id]);
	if (!link)
		return;

	for (i = 0; i < NUM_DEFAULT_KEYS; i++) {
		key = wiphy_dereference(sdata->local->hw.wiphy, link->gtk[i]);
		if (!key)
			continue;
		drv_set_key(sdata->local, SET_KEY, sdata, &sta_info->sta,
			    &key->conf);
	}
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
	struct sta_info *sta_info;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct ieee80211_sub_if_data *sdata;
	struct net_device *dev;
	struct ieee80211_bss_conf *local_mesh_bss_conf;
	u8 link_id;
	int ret;

	if (!vif || !mesh_params)
		return -EINVAL;

	sdata = vif_to_sdata(vif);
	if (!sdata)
		return -EINVAL;

	dev = sdata->dev;
	if (!dev)
		return -EINVAL;

	link_id = ieee80211_vif_is_mld(&sdata->vif) ?
		  mesh_params->peers[0].link_id : sdata->deflink.link_id;
	if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return -EINVAL;

	local_mesh_bss_conf = wiphy_dereference(wiphy,
		sdata->vif.link_conf[link_id]);

	/* Reject peer addition for WPA2/WPA3 VAPs when no GTK source key is
	 * installed. Open-BSS VAPs are exempt — they authorize the peer
	 * directly without key installation. */
	if (ieee80211_mesh_bss_is_secured(local_mesh_bss_conf) &&
	    !ieee80211_mesh_vap_gtk_installed(sdata, link_id,
					      local_mesh_bss_conf->bss_conf_extn->mesh_bss_caps.rsn_conf)) {
		pr_err("%s: mesh peer add rejected - no GTK installed on %s\n",
		       __func__, sdata->name);
		return -EINVAL;
	}

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
	sta->sta_extn.peer_info.parse_beacon_install_key =
		!!(mesh_params->peers[0].caps & MESH_CAPS_SKIP_KEY_INSTALL);
	sta->sta_extn.peer_info.key_installed = false;
	rcu_read_unlock();

	/* Re-install VAP GTKs per-peer */
	sta_info = sta_info_get(sdata, peer_addr);
	if (sta_info && ieee80211_mesh_bss_is_secured(local_mesh_bss_conf))
		ieee80211_mesh_install_gtks_for_peer(sdata, sta_info, link_id);

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

	params.link_sta_params.link_id = -1;

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
 * ieee80211_mesh_send_deauth - send a deauth frame to a mesh peer, encrypting
 * it when an MFP PTKSA is established.
 *
 * ieee80211_send_deauth_disassoc() unconditionally sets DONT_ENCRYPT for
 * AP-type interfaces.  For a managed-mesh BSS with ieee80211w=2, deauth to
 * an MFP peer with an installed pairwise key must be protected.  This helper
 * calls the function with send_frame=false to populate @frame_buf, then
 * builds and transmits the skb, omitting DONT_ENCRYPT when @peer_mfp and
 * @key_installed are both true so the TX path encrypts with the PTK.
 */
static void ieee80211_mesh_send_deauth(struct ieee80211_sub_if_data *sdata,
				       const u8 *peer_addr,
				       const u8 *bssid,
				       u16 reason,
				       bool peer_mfp,
				       bool key_installed,
				       u8 *frame_buf)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;

	ieee80211_send_deauth_disassoc(sdata, peer_addr, bssid,
				       IEEE80211_STYPE_DEAUTH, reason,
				       false, frame_buf);

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + IEEE80211_DEAUTH_FRAME_LEN);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);
	skb_put_data(skb, frame_buf, IEEE80211_DEAUTH_FRAME_LEN);

	if (!peer_mfp || !key_installed)
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;

	ieee80211_tx_skb(sdata, skb);
}

/*
 * struct mesh_peer_del_ctx - Context for deferred mesh peer deletion.
 *
 * Heap-allocated before the deauth is queued.  The WMI management TX work and
 * this deletion work are both serialised by the same wiphy workqueue (FIFO),
 * so the deauth is guaranteed to reach firmware before del_station tears down
 * the peer entry — regardless of whether the deauth is MFP-encrypted or open.
 * This mirrors the natural ordering in regular AP mode where the application
 * sends the deauth and calls del_station in separate, sequential steps.
 */
struct mesh_peer_del_ctx {
	struct wiphy_work work;
	struct ieee80211_sub_if_data *sdata;
	u8 peer_addr[ETH_ALEN];
	u16 sta_aid;
};

static void ieee80211_mesh_peer_del_work(struct wiphy *wiphy,
					 struct wiphy_work *work)
{
	struct mesh_peer_del_ctx *ctx =
		container_of(work, struct mesh_peer_del_ctx, work);
	struct ieee80211_sub_if_data *sdata = ctx->sdata;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct station_del_parameters params;
	struct ieee80211_sta *sta;
	int ret;

	/* Bail out if VAP was torn down or driver unloaded since we were queued */
	if (!ieee80211_sdata_running(sdata) ||
	    !rdev || !rdev->ops || !rdev->ops->del_station) {
		ieee80211_mesh_free_aid(sdata, ctx->sta_aid);
		kfree(ctx);
		return;
	}

	/* Peer may have been removed by another path (e.g. beacon timeout)
	 * between the deauth send and now — free AID to avoid leak.
	 */
	rcu_read_lock();
	sta = ieee80211_find_sta(&sdata->vif, ctx->peer_addr);
	rcu_read_unlock();
	if (!sta) {
		ieee80211_mesh_free_aid(sdata, ctx->sta_aid);
		kfree(ctx);
		return;
	}

	/* Prepare deletion parameters */
	memset(&params, 0, sizeof(params));
	params.mac = ctx->peer_addr;
	params.link_id = -1;
	params.subtype = IEEE80211_STYPE_DEAUTH;
	params.reason_code = WLAN_REASON_DEAUTH_LEAVING;

	/* Call mac80211's del_station to remove the peer */
	ret = rdev->ops->del_station(wiphy, sdata->dev, &params);
	/* Free AID only on success — if deletion failed the station still exists
	 * with this AID, freeing it could cause an AID collision.
	 */
	if (!ret)
		ieee80211_mesh_free_aid(sdata, ctx->sta_aid);

	kfree(ctx);
}

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
	struct mesh_peer_del_ctx *del_ctx;
	bool mfp_capable;
	bool key_installed;
	int ret;
	u16 sta_aid = 0;
	u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

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
	sta_aid     = sta->aid;
	mfp_capable = sta->sta_extn.peer_info.peer_mfp_capable;
	key_installed = sta->sta_extn.peer_info.key_installed;
	rcu_read_unlock();

	/* Send deauth OTA before tearing down the station's TX path. */
	ieee80211_mesh_send_deauth(sdata, peer_addr, sdata->vif.addr,
				   WLAN_REASON_DEAUTH_LEAVING,
				   mfp_capable, key_installed, frame_buf);

	/* Defer del_station so the ath12k WMI management TX work (queued ahead
	 * of us in the same wiphy workqueue) processes the deauth first, while
	 * the PTK is still installed in firmware.  See ieee80211_mesh_peer_del_work.
	 */
	del_ctx = kzalloc(sizeof(*del_ctx), GFP_KERNEL);
	if (del_ctx) {
		del_ctx->sdata = sdata;
		ether_addr_copy(del_ctx->peer_addr, peer_addr);
		del_ctx->sta_aid = sta_aid;
		wiphy_work_init(&del_ctx->work, ieee80211_mesh_peer_del_work);
		wiphy_work_queue(sdata->local->hw.wiphy, &del_ctx->work);
		return 0;
	}

	/* Allocation failure — fall back to immediate deletion. */

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

	if (!IEEE80211_EXTN_VAP_IS_MESH_MODE(sdata->wdev.vap_submode)) {
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
 * ieee80211_mesh_install_peer_key - Install pairwise key (or authorize
 *	directly for an open BSS) and authorize a mesh peer.
 *
 * Called from ieee80211_update_mesh_peer_caps_work after the WMI peer assoc so
 * the firmware peer entry already has is_pmf_enabled=true when the PTK arrives.
 * Retries silently on subsequent beacons if the GTK is not yet available.
 *
 * @wiphy: WIPHY pointer
 * @sta:   Station whose key needs installing
 */
static void ieee80211_mesh_install_peer_key(struct wiphy *wiphy,
					    struct sta_info *sta)
{
	struct mesh_peer_info *peer = &sta->sta.sta_extn.peer_info;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_bss_conf *link_conf = NULL;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct net_device *dev = sdata->dev;
	struct key_params kp = {};
	u8 key_buf[WLAN_MAX_KEY_LEN];
	u8 key_len = 0;
	int ret = -ENOENT;
	unsigned int i;

	if (peer->key_installed)
		return;

	/* Open BSS: no pairwise key to install — authorize directly */
	if (!peer->peer_ucast_cipher) {
		ret = ieee80211_vendor_mesh_authorize_peer(wiphy, &sdata->vif, sta->sta.addr);
		if (ret) {
			pr_warn_ratelimited("managed_mesh_dbg install_peer_key: authorize failed for %pM (ret=%d)\n",
					    sta->sta.addr, ret);
			return;
		}
		peer->key_installed = true;
		return;
	}

	for (i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++) {
		struct ieee80211_bss_conf *lc =
			wiphy_dereference(wiphy, sdata->vif.link_conf[i]);

		if (lc && lc->bss_conf_extn) {
			link_conf = lc;
			break;
		}
	}

	if (!link_conf) {
		pr_warn_ratelimited("managed_mesh_dbg install_peer_key: bss_conf_extn NULL for peer %pM\n",
				    sta->sta.addr);
		return;
	}

	/* Step 1: Retrieve the VAP's installed GTK */
	ret = ieee80211_extn_get_key_material(&sdata->vif,
					      link_conf->link_id,
					      NULL,
					      false,
					      MANAGED_MESH_GTK_KEY_IDX,
					      key_buf, sizeof(key_buf), &key_len);
	if (ret) {
		pr_debug_ratelimited("managed_mesh_dbg install_peer_key: no VAP GTK at idx=%d for peer %pM\n",
				     MANAGED_MESH_GTK_KEY_IDX, sta->sta.addr);
		memzero_explicit(key_buf, sizeof(key_buf));
		return;
	}

	/* Step 2: Install as pairwise key */
	if (WARN_ON_ONCE(!peer->peer_ucast_cipher)) {
		memzero_explicit(key_buf, sizeof(key_buf));
		return;
	}
	kp.key     = key_buf;
	kp.key_len = key_len;
	kp.cipher  = peer->peer_ucast_cipher;

	ret = rdev->ops->add_key(wiphy, dev,
				 MANAGED_MESH_SET_KEY_DEFAULT_LINK_ID,
				 MANAGED_MESH_PTK_KEY_IDX,
				 true,
				 sta->sta.addr,
				 &kp);
	memzero_explicit(key_buf, sizeof(key_buf));
	if (ret) {
		pr_warn_ratelimited("managed_mesh_dbg install_peer_key: add_key failed for peer %pM (ret=%d)\n",
				    sta->sta.addr, ret);
		return;
	}

	/* Step 3: Authorize peer */
	ret = ieee80211_vendor_mesh_authorize_peer(wiphy, &sdata->vif, sta->sta.addr);
	if (ret) {
		pr_warn_ratelimited("managed_mesh_dbg install_peer_key: authorize failed for %pM (ret=%d)\n",
				    sta->sta.addr, ret);
		return;
	}

	peer->key_installed = true;
	pr_debug_ratelimited("managed_mesh_dbg install_peer_key: key installed and peer %pM authorized (cipher=0x%x akm=0x%x mfp=%d)\n",
			     sta->sta.addr, peer->peer_ucast_cipher,
			     peer->peer_akm_suite, peer->peer_mfp_capable);
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

	/* TODO: Currently only SLO support is enabled, link_sta extraction to be
	 * updated when MLO support is enabled */
	link_sta = &sta->deflink;

	/* Recalculate NSS */
	ieee80211_sta_init_nss(link_sta);

	/* Notify driver to update peer assoc */
	ret = drv_mesh_peer_update_caps_extn(local, sdata, sta, link_sta);
	if (ret < 0) {
		printk(KERN_ERR "Mesh peer update failed for peer AID %d\n", sta->sta.aid);
		sta->sta.sta_extn.peer_info.cap_update_failures++;
	} else {
		sta->sta.sta_extn.peer_info.intersection_done = true;
		sta->sta.sta_extn.peer_info.intersect_successes++;
	}

	/* Install PTK after the peer assoc; firmware peer entry has PMF enabled */
	ieee80211_mesh_install_peer_key(wiphy, sta);
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

/* RSN/WPA security gate: validates peer's RSN IE against local VAP policy,
 * extracts cipher/AKM/MFP params, and sets peer_ht_restricted when the
 * pairwise cipher is TKIP. Must run before capability intersection since
 * ieee80211_mesh_beacon_intersect uses peer_ht_restricted to suppress HT. */
static bool ieee80211_mesh_rsn_ie_check(struct ieee80211_sub_if_data *sdata,
					struct ieee80211_bss_conf *link_conf,
					struct ieee80211_sta *sta,
					struct ieee802_11_elems *elems,
					struct ieee80211_mgmt *mgmt);

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
					   struct ieee80211_bss_conf *link_conf,
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
	bool rsn_check_passed = false;

	if (!sdata->local)
		return -EINVAL;

	wiphy = sdata->local->hw.wiphy;

	lockdep_assert_wiphy(wiphy);

	/* RSN/WPA security gating — must pass before capability intersection */
	if (ieee80211_mesh_rsn_ie_check(sdata, link_conf, pubsta, elems, mgmt))
		return -EACCES;
	rsn_check_passed = true;

	link_sta = &sta->deflink;
	sband = wiphy->bands[band];

	if (!sband)
		return -EINVAL;

	/* Validate basic requirements */
	if (!elems->supp_rates) {
		pubsta->sta_extn.peer_info.cap_update_failures++;
		return -EINVAL;
	}

	/* Update HT capabilities.
	 * Per 802.11-2020 §10.3.3, HT rates must not be used when the pairwise
	 * cipher is TKIP.  When peer_ht_restricted is set (TKIP detected during
	 * RSN IE check), suppress HT caps to force legacy rates.  The peer is
	 * not rejected — this preserves connectivity with legacy hardware. */
	if (elems->ht_cap_elem) {
		if (sta->sta.sta_extn.peer_info.peer_ht_restricted) {
			memset(&link_sta->pub->ht_cap, 0, sizeof(link_sta->pub->ht_cap));
			cap_changed = true;
		} else {
			struct ieee80211_sta_ht_cap old_ht_cap = link_sta->pub->ht_cap;

			ieee80211_ht_cap_ie_to_sta_ht_cap(sdata, sband, elems->ht_cap_elem,
							  link_sta);

			if (memcmp(&old_ht_cap, &link_sta->pub->ht_cap, sizeof(old_ht_cap)))
				cap_changed = true;
		}
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

	/* Schedule update_cap_wk if capabilities changed, or if RSN check
	 * passed and the peer still needs key install (secure BSS) or
	 * authorization (open BSS) — skipped when parse_beacon_install_key
	 * is set, since the consumer manages both in that mode. */
	if (cap_changed || (rsn_check_passed &&
			    !sta->sta.sta_extn.peer_info.parse_beacon_install_key &&
			    !sta->sta.sta_extn.peer_info.key_installed))
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
 * ieee80211_mesh_parse_rsn_ie - Extract cipher/AKM/MFP params from a peer's RSN IE
 *
 * Parses the RSN IE pointed to by @rsn_ie and fills the security-related
 * fields of @peer_info.  Returns 0 on success, -EINVAL if the IE is
 * malformed or carries an unsupported RSN version.
 */
static int
ieee80211_mesh_parse_rsn_ie(const u8 *rsn_body, u8 rsn_body_len,
			    struct mesh_peer_info *peer_info, bool log_verbose)
{
	const u8 *pos;
	u16 ie_len;  /* u16 to avoid truncation in count*4 bounds checks */
	u16 rsn_ver, count, rsn_caps = 0;

	if (!rsn_body || rsn_body_len < RSN_VERSION_LEN) {
		pr_warn_ratelimited("managed_mesh_dbg rsn_parse: IE body null or too short (len=%u)\n",
				    rsn_body_len);
		return -EINVAL;
	}

	ie_len = rsn_body_len;
	if (log_verbose)
		pr_debug_ratelimited("managed_mesh_dbg rsn_parse: IE body_len=%u raw: %*ph\n",
				     ie_len, min_t(int, ie_len, 64), rsn_body);
	pos = rsn_body;

	rsn_ver = get_unaligned_le16(pos);
	if (rsn_ver != 1) {
		pr_warn_ratelimited("managed_mesh_dbg rsn_parse: bad RSN version %u\n", rsn_ver);
		return -EINVAL;
	}
	pos += RSN_VERSION_LEN; ie_len -= RSN_VERSION_LEN;
	peer_info->peer_wpa_versions = NL80211_WPA_VERSION_2;

	if (ie_len < RSN_SUITE_LEN)
		goto caps_done;

	/* Group/multicast cipher suite: 3-byte OUI + 1-byte type, read as BE32 */
	peer_info->peer_mcast_cipher = get_unaligned_be32(pos);
	if (log_verbose)
		pr_debug_ratelimited("managed_mesh_dbg rsn_parse: group_cipher=0x%08x\n",
				     peer_info->peer_mcast_cipher);
	pos += RSN_SUITE_LEN; ie_len -= RSN_SUITE_LEN;

	if (ie_len < RSN_SUITE_COUNT_LEN)
		goto caps_done;

	/* Pairwise (unicast) cipher suites */
	count = get_unaligned_le16(pos); pos += RSN_SUITE_COUNT_LEN; ie_len -= RSN_SUITE_COUNT_LEN;
	if (log_verbose)
		pr_debug_ratelimited("managed_mesh_dbg rsn_parse: pairwise count=%u ie_len_remaining=%u\n",
				     count, ie_len);
	if (count * RSN_SUITE_LEN > ie_len) {
		pr_warn_ratelimited("managed_mesh_dbg rsn_parse: pairwise count %u overflows ie_len %u\n",
				    count, ie_len);
		return -EINVAL;
	}
	if (count > 0)
		peer_info->peer_ucast_cipher = get_unaligned_be32(pos);
	if (log_verbose)
		pr_debug_ratelimited("managed_mesh_dbg rsn_parse: ucast_cipher=0x%08x\n",
				     peer_info->peer_ucast_cipher);
	pos += count * RSN_SUITE_LEN; ie_len -= count * RSN_SUITE_LEN;

	if (ie_len < RSN_SUITE_COUNT_LEN)
		goto caps_done;

	/* AKM suites — store first for diagnostics/key installation, log all */
	count = get_unaligned_le16(pos); pos += RSN_SUITE_COUNT_LEN; ie_len -= RSN_SUITE_COUNT_LEN;
	if (log_verbose)
		pr_debug_ratelimited("managed_mesh_dbg rsn_parse: AKM count=%u ie_len_remaining=%u\n",
				     count, ie_len);
	if (count * RSN_SUITE_LEN > ie_len) {
		pr_warn_ratelimited("managed_mesh_dbg rsn_parse: AKM count %u overflows ie_len %u\n",
				    count, ie_len);
		return -EINVAL;
	}
	if (count > 0) {
		int i;

		/* Store first AKM suite for key installation cipher matching.
		 * peer_wpa_versions stays at WPA_VERSION_2 (RSN IE present =
		 * WPA2 minimum); security level alignment is enforced by the
		 * presence check and local_wpa_versions, not by AKM value. */
		peer_info->peer_akm_suite = get_unaligned_be32(pos);
		if (log_verbose) {
			for (i = 0; i < count; i++)
				pr_debug_ratelimited("managed_mesh_dbg rsn_parse: AKM[%d]=0x%08x\n",
						     i, get_unaligned_be32(pos + i * RSN_SUITE_LEN));
		}
	}
	pos += count * RSN_SUITE_LEN; ie_len -= count * RSN_SUITE_LEN;

	if (ie_len >= RSN_CAP_LEN)
		rsn_caps = get_unaligned_le16(pos);

	if (log_verbose)
		pr_debug_ratelimited("managed_mesh_dbg rsn_parse: rsn_caps=0x%04x ie_len_after_caps=%d\n",
				     rsn_caps, (int)ie_len - 2);

caps_done:
	peer_info->peer_mfp_capable  = !!(rsn_caps & RSN_CAP_MFP_CAPABLE);
	peer_info->peer_mfp_required = !!(rsn_caps & RSN_CAP_MFP_REQUIRED);
	if (log_verbose)
		pr_debug_ratelimited("managed_mesh_dbg rsn_parse: mfp_capable=%d mfp_required=%d wpa_versions=0x%x akm=0x%08x\n",
				     peer_info->peer_mfp_capable, peer_info->peer_mfp_required,
				     peer_info->peer_wpa_versions, peer_info->peer_akm_suite);
	return 0;
}

/**
 * ieee80211_mesh_rsn_ie_check - RSN/WPA security gating and crypto param extraction
 *
 * Called from ieee80211_process_mesh_peer_beacon() under rcu_read_lock().
 *
 * Steps performed:
 *  1. Presence gating: rejects peers whose RSN IE presence/absence is
 *     inconsistent with the local VAP's wpa_versions policy.
 *  2. RSN IE parsing: extracts pairwise cipher, group cipher, AKM suite,
 *     and MFP caps from the peer's RSN IE into mesh_peer_info.
 *  3. PHY mode cipher checks:
 *     - TKIP + VHT: hard reject per 802.11ac §11.5.3.
 *     - TKIP + HT: sets peer_ht_restricted to suppress HT caps at
 *       beacon intersection, preserving connectivity at legacy rates.
 *  4. MFP cross-check: rejects if the local VAP requires MFP but the
 *     peer does not advertise MFP capability.
 *  5. Saves raw peer RSN IE into peer_info.peer_rsn_ie.
 *  6. Key install trigger: if parse_beacon_install_key is set and keys
 *     have not yet been installed, queues update_cap_wk.
 *
 * Returns true if the peer is rejected (caller should goto exit), false if OK.
 */

static bool
ieee80211_mesh_rsn_ie_check(struct ieee80211_sub_if_data *sdata,
			    struct ieee80211_bss_conf *link_conf,
			    struct ieee80211_sta *sta,
			    struct ieee802_11_elems *elems,
			    struct ieee80211_mgmt *mgmt)
{
	struct sta_info *sta_info = container_of(sta, struct sta_info, sta);
	struct mesh_peer_info *peer = &sta_info->sta.sta_extn.peer_info;
	struct mesh_bss_conf *mesh_caps = NULL;
	u32 local_wpa_versions = 0;
	bool local_requires_rsn, peer_has_rsn;

	if (link_conf->bss_conf_extn) {
		mesh_caps = &link_conf->bss_conf_extn->mesh_bss_caps;
		if (mesh_caps->rsn_conf)
			local_wpa_versions = mesh_caps->rsn_conf->wpa_versions;
	}

	/* WPA2 and WPA3 both produce a standard RSN IE (WLAN_EID_RSN = 0x30) */
	local_requires_rsn = !!(local_wpa_versions &
				(NL80211_WPA_VERSION_2 | NL80211_WPA_VERSION_3));
	peer_has_rsn       = (elems->rsn != NULL);

	/* Step 1: Presence gating.
	 * Both open: accept, no keys.
	 * Primitive Mesh (secured local, open peer): accept, assume PSK/CCMP.
	 * Primitive Mesh (open local, secured peer): accept, no keys.
	 * Both secured: fall through to Step 2/3.
	 */
	if (!local_requires_rsn && !peer_has_rsn) {
		/* Both open — clear any stale crypto state */
		ieee80211_mesh_peer_clear_crypto(peer);
		return false;
	}

	if (local_requires_rsn && !peer_has_rsn) {
		ieee80211_mesh_peer_clear_crypto(peer);
		if (ieee80211_mesh_local_has_suite(mesh_caps->rsn_conf->akm_suites,
						   mesh_caps->rsn_conf->n_akm_suites,
						   WLAN_AKM_SUITE_PSK) &&
		    ieee80211_mesh_local_has_suite(mesh_caps->rsn_conf->pairwise_ciphers,
						   mesh_caps->rsn_conf->n_pairwise_ciphers,
						   WLAN_CIPHER_SUITE_CCMP)) {
			peer->peer_ucast_cipher = WLAN_CIPHER_SUITE_CCMP;
			peer->peer_akm_suite    = WLAN_AKM_SUITE_PSK;
		} else if (ieee80211_mesh_local_has_suite(mesh_caps->rsn_conf->akm_suites,
							  mesh_caps->rsn_conf->n_akm_suites,
							  WLAN_AKM_SUITE_PSK_SHA256) &&
			   ieee80211_mesh_local_has_suite(mesh_caps->rsn_conf->pairwise_ciphers,
							  mesh_caps->rsn_conf->n_pairwise_ciphers,
							  WLAN_CIPHER_SUITE_CCMP_256)) {
			peer->peer_ucast_cipher = WLAN_CIPHER_SUITE_CCMP_256;
			peer->peer_akm_suite    = WLAN_AKM_SUITE_PSK_SHA256;
		} else {
			goto reject;
		}
		peer->peer_wpa_versions = NL80211_WPA_VERSION_2;
		return false;
	}

	if (!local_requires_rsn && peer_has_rsn) {
		ieee80211_mesh_peer_clear_crypto(peer);
		return false;
	}

	/* Step 2: Parse peer RSN IE — log verbose only on first parse per peer */
	if (ieee80211_mesh_parse_rsn_ie(elems->rsn, elems->rsn_len, peer,
					(!peer->intersection_done &&
					 !peer->rsn_parse_failures)) < 0) {
		goto reject;
	}

	/* Step 3: Select an (AKM, cipher) pair for this peer. */
	if (!ieee80211_mesh_select_akm_cipher(mesh_caps, elems, peer))
		goto reject;

	/* Step 4: PHY mode cipher checks */
	if (peer->peer_ucast_cipher == WLAN_CIPHER_SUITE_TKIP) {
		if (elems->vht_cap_elem) {
			/* TKIP is explicitly prohibited for VHT per 802.11ac §11.5.3 */
			goto reject;
		}
		if (elems->ht_cap_elem) {
			/* TKIP + HT: suppress HT caps at beacon intersection,
			 * fall back to legacy rates (per 802.11-2020 §10.3.3) */
			peer->peer_ht_restricted = true;
		}
	} else {
		peer->peer_ht_restricted = false;
	}

	/* Step 5: MFP cross-check using RSN Capabilities pre-parsed at AP setup */
	if (mesh_caps && mesh_caps->rsn_conf &&
	    (mesh_caps->rsn_conf->rsn_caps & RSN_CAP_MFP_REQUIRED) &&
	    !peer->peer_mfp_capable) {
		goto reject;
	}

	/* Step 6: Save raw peer RSN IE body */
	if (elems->rsn_len <= (int)sizeof(peer->peer_rsn_ie)) {
		memcpy(peer->peer_rsn_ie, elems->rsn, elems->rsn_len);
		peer->peer_rsn_ie_len = elems->rsn_len;
	}

	/* Set WLAN_STA_MFP once at RSN capability discovery so the subsequent
	 * WMI peer assoc in update_cap_wk carries is_pmf_enabled=true before
	 * the PTK is installed.
	 */
	if (peer->peer_mfp_capable && !test_sta_flag(sta_info, WLAN_STA_MFP)) {
		sta_info->sta.mfp = true;
		set_sta_flag(sta_info, WLAN_STA_MFP);
	}

	/* Step 7: Key install is deferred to ieee80211_mesh_beacon_intersect
	 * which queues update_cap_wk after RSN check passes. */

	return false;  /* All security checks passed */

reject:
	/* Intersection failed — non-fatal.  Peer stays alive; intersection
	 * is retried on the next beacon.  No deauth is sent here; the driver
	 * only sends deauth when the host explicitly deletes the peer. */
	peer->rsn_parse_failures++;
	return true;
}

/**
 * ieee80211_mesh_peer_deauth_rx_work - deferred peer deletion triggered by
 * receiving a deauth or disassoc frame from a mesh peer.
 *
 * Runs under wiphy mutex. Does not send deauth OTA — the peer initiated
 * the disconnection, no notification is needed.
 */
static void
ieee80211_mesh_peer_deauth_rx_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct sta_info *sta_info = container_of(work, struct sta_info,
						 sta.sta_extn.deauth_rx_wk);
	struct ieee80211_sub_if_data *sdata = sta_info->sdata;

	sdata_info(sdata, "Mesh peer %pM: removing after RX deauth/disassoc\n",
		   sta_info->addr);

	sta_info_destroy_addr(sdata, sta_info->addr);
}

/**
 * ieee80211_process_mesh_peer_deauth_disassoc - process a received deauth or
 * disassoc frame on a vap_submode=MESH BSS.
 *
 * Called directly from ieee80211_rx_h_mgmt_check() in the RX path.
 * Defers peer deletion to wiphy work context since sta_info_destroy_addr()
 * requires the wiphy mutex.
 *
 * Returns RX_DROP_MONITOR if the frame was consumed.
 */
ieee80211_rx_result
ieee80211_process_mesh_peer_deauth_disassoc(struct ieee80211_rx_data *rx)
{
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct ieee80211_mgmt *mgmt = (void *)rx->skb->data;
	struct ieee80211_sta *sta;
	u16 reason;

	if (!IEEE80211_EXTN_VAP_IS_MESH_MODE(sdata->wdev.vap_submode))
		return RX_CONTINUE;

	if (!ieee80211_is_deauth(mgmt->frame_control) &&
	    !ieee80211_is_disassoc(mgmt->frame_control))
		return RX_CONTINUE;

	/* Drop group-addressed frames (except broadcast) */
	if (is_multicast_ether_addr(mgmt->da) &&
	    !is_broadcast_ether_addr(mgmt->da))
		return RX_DROP_MONITOR;

	/* Must be addressed to our BSS */
	if (!ether_addr_equal(mgmt->da, sdata->vif.addr) &&
	    !is_broadcast_ether_addr(mgmt->da))
		return RX_DROP_MONITOR;

	if (rx->skb->len < IEEE80211_DEAUTH_FRAME_LEN)
		return RX_DROP_MONITOR;

	reason = le16_to_cpu(mgmt->u.deauth.reason_code);

	rcu_read_lock();
	sta = ieee80211_find_sta(&sdata->vif, mgmt->sa);
	if (!sta || !sta->sta_extn.peer_info.is_mesh_peer) {
		rcu_read_unlock();
		return RX_DROP_MONITOR;
	}

	/* Populate rx->sta if absent (e.g. MLO link-steering) so the MFP
	 * enforcement check below can evaluate the peer's WLAN_STA_MFP flag.
	 */
	if (!rx->sta) {
		struct sta_info *si = container_of(sta, struct sta_info, sta);

		rx->sta      = si;
		rx->link_sta = &si->deflink;
	}

	/* Drop unprotected deauth/disassoc from an established MFP peer.
	 * A unicast robust management frame must be protected when the peer
	 * has WLAN_STA_MFP set; an unprotected frame indicates a spoofing
	 * attempt and must be silently discarded.
	 */
	if (rx->sta &&
	    test_sta_flag(rx->sta, WLAN_STA_MFP) &&
	    !ieee80211_has_protected(mgmt->frame_control)) {
		rcu_read_unlock();
		return RX_DROP_U_UNPROT_UCAST_MGMT;
	}

	sdata_info(sdata, "RX %s from mesh peer %pM (reason: %d)\n",
		   ieee80211_is_deauth(mgmt->frame_control) ?
		   "DeAuth" : "DisAssoc", mgmt->sa, reason);

	wiphy_work_queue(sdata->local->hw.wiphy, &sta->sta_extn.deauth_rx_wk);
	rcu_read_unlock();

	return RX_DROP_MONITOR;
}
EXPORT_SYMBOL(ieee80211_process_mesh_peer_deauth_disassoc);

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
	struct ieee80211_bss_conf *link_conf;
	u32 new_checksum;
	int ret;
	ieee80211_rx_result result = RX_CONTINUE;

	if (rx->sdata->vif.type != NL80211_IFTYPE_AP ||
	    !IEEE80211_EXTN_VAP_IS_MESH_MODE(rx->sdata->wdev.vap_submode))
		return RX_CONTINUE;

	/* Beacons are allowed to be processed further.
	 * Action frames pass to the normal mac80211 handler.
	 * All other non-beacon frames are dropped. */
	if (!ieee80211_is_beacon(mgmt->frame_control))
		return ieee80211_is_action(mgmt->frame_control) ?
		       RX_CONTINUE : RX_DROP_MONITOR;

	rcu_read_lock();

	/* Use the link_conf for the specific MLD link on which this beacon
	 * was received. For MLD AP, params->beacon.link_id during start_ap
	 * may be non-zero, so bss_conf_extn is on link[link_id]->conf, not
	 * sdata->vif.bss_conf (link[0]->conf). */
	link_conf = (rx->link_id >= 0 && rx->link_id < IEEE80211_MLD_MAX_NUM_LINKS) ?
		    rcu_dereference(sdata->vif.link_conf[rx->link_id]) :
		    &sdata->vif.bss_conf;
	if (!link_conf)
		link_conf = &sdata->vif.bss_conf;

	sta = ieee80211_find_sta(&rx->sdata->vif, mgmt->sa);
	if (!sta || !sta->sta_extn.peer_info.is_mesh_peer) {
		rcu_read_unlock();
		return RX_CONTINUE;
	}

	/* Skip IE parsing and intersection if beacon content is unchanged */
	new_checksum = ieee80211_calc_beacon_ie_checksum_raw(mgmt, rx->skb->len);
	if (sta->sta_extn.peer_info.intersection_done &&
	    new_checksum == sta->sta_extn.peer_info.beacon_ie_checksum) {
		sta->sta_extn.peer_info.last_beacon_time = jiffies;
		goto exit;
	}

	/* Beacon changed or first beacon — full IE parse and security check */
	elems = ieee802_11_parse_elems(mgmt->u.beacon.variable,
				       rx->skb->len - offsetof(struct ieee80211_mgmt,
							       u.beacon.variable),
				       IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON,
				       NULL);
	if (!elems) {
		if (net_ratelimit())
			sdata_info(sdata,
				   "managed_mesh_dbg beacon: IE parse failed for peer %pM\n",
				   mgmt->sa);
		rcu_read_unlock();
		return RX_CONTINUE;
	}

	/* Check for MBSSID - skip nontransmitted BSSIDs */
	if (elems->bssid_index) {
		u8 mbssid_index = elems->bssid_index->bssid_index;
		if (mbssid_index != 0)
			goto exit;
	}

	ret = ieee80211_mesh_beacon_intersect(sdata, link_conf, sta, elems, mgmt, rx_status->band);

	/* Update beacon reception timestamp on every received beacon regardless
	 * of intersection result — prevents inactivity timeout from firing
	 * while intersection is being retried on subsequent beacons. */
	sta->sta_extn.peer_info.last_beacon_time = jiffies;

	/* Count every full intersection attempt regardless of outcome. */
	sta->sta_extn.peer_info.intersect_attempts++;

	if (ret == 0) {
		/* Update checksum on success — reuses new_checksum computed above,
		 * no second calculation needed. */
		sta->sta_extn.peer_info.beacon_ie_checksum = new_checksum;
	}

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

	if (IEEE80211_EXTN_VAP_IS_MESH_MODE(sdata->wdev.vap_submode) &&
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

	if (IEEE80211_EXTN_VAP_IS_MESH_MODE(sdata->wdev.vap_submode)) {
		wiphy_work_init(&sta_info->sta.sta_extn.update_cap_wk,
				ieee80211_update_mesh_peer_caps_work);
		wiphy_work_init(&sta_info->sta.sta_extn.timeout_wk,
				ieee80211_timeout_mesh_peer_work);
		wiphy_work_init(&sta_info->sta.sta_extn.deauth_rx_wk,
				ieee80211_mesh_peer_deauth_rx_work);
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
		wiphy_work_cancel(wiphy, &sta_info->sta.sta_extn.deauth_rx_wk);
		sdata->u.ap.if_ap_extn.num_mesh_peers--;
		if (!sdata->u.ap.if_ap_extn.num_mesh_peers) {
			del_timer_sync(&sdata->u.ap.if_ap_extn.mesh_peer_cleanup_timer);
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
		from_timer(sdata, t, u.ap.if_ap_extn.mesh_peer_cleanup_timer);
	unsigned long now;

	if (!IEEE80211_EXTN_VAP_IS_MESH_MODE(sdata->wdev.vap_submode))
		return;

	if (!ieee80211_sdata_running(sdata))
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &sdata->local->sta_list, list) {
		if (sta->sdata != sdata)
			continue;

		if (!sta->sta.sta_extn.peer_info.is_mesh_peer)
			continue;

		now = jiffies;

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
	rcu_read_unlock();

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

	if (!IEEE80211_EXTN_VAP_IS_MESH_MODE(sdata->wdev.vap_submode)) {
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
 * ieee80211_vendor_mesh_get_key - Retrieve key material for a mesh peer.
 *
 * Looks up the key identified by @params->key_idx, @params->link_id and
 * @params->peer_mac using ieee80211_lookup_key (already exported by mac80211)
 * and fills in @key_info with the cipher, raw key bytes and TX sequence number.
 *
 * Caller must hold the wiphy mutex (lockdep_assert_wiphy is enforced).
 *
 * @wiphy:    Wiphy instance
 * @vif:      Virtual interface
 * @params:   Request parameters (key index, link ID, peer MAC)
 * @key_info: Output: filled with cipher / key / seq on success
 *
 * Return: 0 on success, -EINVAL for bad args, -ENOENT if key not found.
 */
int ieee80211_vendor_mesh_get_key(struct wiphy *wiphy,
				  struct ieee80211_vif *vif,
				  const struct ieee80211_mesh_getkey_params *params,
				  struct ieee80211_mesh_key_info *key_info)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_key *key;
	bool pairwise;
	int link_id;
	u64 pn;

	if (!vif || !params || !key_info)
		return -EINVAL;

	sdata = vif_to_sdata(vif);
	if (!sdata)
		return -EINVAL;

	lockdep_assert_wiphy(wiphy);

	pairwise = params->pairwise;
	link_id  = (params->link_id >= 0) ? (int)params->link_id : -1;

	rcu_read_lock();

	key = ieee80211_lookup_key(sdata, link_id, params->key_idx,
				   pairwise, pairwise ? params->peer_mac : NULL);
	if (!key) {
		rcu_read_unlock();
		return -ENOENT;
	}

	memset(key_info, 0, sizeof(*key_info));
	key_info->cipher  = key->conf.cipher;
	key_info->key_len = key->conf.keylen;
	if (key_info->key_len > sizeof(key_info->key))
		key_info->key_len = sizeof(key_info->key);
	memcpy(key_info->key, key->conf.key, key_info->key_len);

	/* All supported ciphers store a 48-bit PN/TSC in tx_pn, LSB first. */
	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
	case WLAN_CIPHER_SUITE_TKIP:
		pn = atomic64_read(&key->conf.tx_pn);
		key_info->seq[0] = (u8)(pn);
		key_info->seq[1] = (u8)(pn >> 8);
		key_info->seq[2] = (u8)(pn >> 16);
		key_info->seq[3] = (u8)(pn >> 24);
		key_info->seq[4] = (u8)(pn >> 32);
		key_info->seq[5] = (u8)(pn >> 40);
		key_info->seq_len = 6;
		break;
	default:
		break;
	}

	rcu_read_unlock();
	return 0;
}
EXPORT_SYMBOL(ieee80211_vendor_mesh_get_key);

/**
 * Vendor mesh operations structure - provides function pointers
 * for mesh peer management operations
 */
static const struct ieee80211_vendor_mesh_ops vendor_mesh_ops = {
       .add_local_peer = ieee80211_vendor_mesh_add_local_peer,
       .authorize_peer = ieee80211_vendor_mesh_authorize_peer,
       .delete_local_peer = ieee80211_vendor_mesh_delete_local_peer,
       .get_key = ieee80211_vendor_mesh_get_key,
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

/**
 * ieee80211_rawmesh_encap_eth_to_dot11 - encap an Ethernet II frame
 *	into an 802.11 MPDU, in place
 * @sdata: sub interface the frame is being sent on
 * @skb: skb holding a plain Ethernet-II frame (dst(6) + src(6) +
 *	ethertype/len(2) + payload)
 * @sta_out: on success, set to the resolved destination STA (or NULL
 *	for a multicast/broadcast RA, or when no STA is known for a
 *	unicast RA)
 * @key_out: on success, set to the resolved TX key (or NULL for an
 *	unencrypted BSS)
 *
 * Used for VAPs configured with %IEEE80211_EXTN_VAP_SUBMODE_RAW_MODE_MESH.
 * mac80211's normal Ethernet->802.11 conversion (ieee80211_build_hdr())
 * and TX handler processing are entirely bypassed for these VAPs (see
 * ieee80211_rawmesh_tx()), so this function does the minimal
 * equivalent of it directly: it resolves the destination STA and TX
 * key the same way ieee80211_tx_h_select_key() does, strips the
 * Ethernet header, inserts an RFC1042/bridge-tunnel LLC/SNAP header
 * carrying the original ethertype, and prepends a 3-address 802.11
 * header (QoS-Data for unicast, legacy Data for multicast) addressed
 * according to the VAP's own operating mode (AP/AP_VLAN or managed
 * station). The Protected bit is set, and IV/PN space reserved ahead
 * of the LLC/SNAP header, only when a key that actually needs it was
 * resolved -- matching ieee80211_check_fast_xmit()'s handling of
 * IEEE80211_KEY_FLAG_GENERATE_IV / IEEE80211_KEY_FLAG_PUT_IV_SPACE.
 *
 * This is the mac80211-native equivalent of the proprietary rawsim
 * module's encap_eth_to_dot11(): unlike that function, it does not
 * perform A-MSDU aggregation or 4-address mesh addressing -- those
 * are out of scope here and remain the responsibility of a lower
 * layer/driver if ever needed.
 *
 * Return: the (possibly reallocated) skb on success. On failure the
 *	skb is freed and NULL is returned.
 */
struct sk_buff *
ieee80211_rawmesh_encap_eth_to_dot11(struct ieee80211_sub_if_data *sdata,
				     struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_hdr hdr;
	struct sta_info *sta;
	struct ieee80211_key *key;
	__le16 fc;
	u16 ethertype, hdrlen;
	const u8 *encaps_data;
	int encaps_len, skip_header_bytes;
	int head_need;
	unsigned int iv_len = 0;
	bool gen_iv = false;

	if (unlikely(skb->len < ETH_HLEN)) {
		sdata_err(sdata,
			  "ieee80211_rawmesh_encap_eth_to_dot11: skb too short (%u)\n",
			  skb->len);
		kfree_skb(skb);
		return NULL;
	}

	if (ieee80211_lookup_ra_sta(sdata, skb, &sta)) {
		kfree_skb(skb);
		return NULL;
	}
	if (IS_ERR(sta))
		sta = NULL;

	ethertype = (skb->data[12] << 8) | skb->data[13];

	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA);
	memset(&hdr, 0, sizeof(hdr));

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_STATION:
		fc |= cpu_to_le16(IEEE80211_FCTL_TODS);
		/* BSSID SA DA */
		memcpy(hdr.addr1, sdata->vif.cfg.ap_addr, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, skb->data, ETH_ALEN);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_MESH_POINT:
		/* DA BSSID SA
		 *
		 * NOTE: sdata->vif.addr is the MLD-level address, which for
		 * this driver differs from the actual over-the-air/netdev
		 * address once the link is grouped into an MLD (e.g. vif.addr
		 * 00:03:7f:04:38:11 vs. the real link address
		 * 02:03:7f:04:38:11 reported by ifconfig/iw -- confirmed vif
		 * hex dump analysis: TX frames carried addr2=00:.. while the
		 * peer only knows/authorizes 02:..). Peers only know and
		 * authorize the real link address, so addr2 (the
		 * transmitter/BSSID address that the peer's RX path matches
		 * against via sta_info_get_bss()) MUST be the per-link
		 * address (sdata->deflink.conf->addr), not the MLD address,
		 * or every frame we send will have an unrecognized TA and be
		 * silently dropped by the peer. Fall back to vif.addr if the
		 * link conf isn't available yet (shouldn't happen once the
		 * netdev is up and passing traffic).
		 */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		if (sdata->deflink.conf)
			memcpy(hdr.addr3, sdata->deflink.conf->addr, ETH_ALEN);
		else
			memcpy(hdr.addr3, sdata->vif.addr, ETH_ALEN);
		break;
	default:
		sdata_err(sdata,
			  "ieee80211_rawmesh_encap_eth_to_dot11: unsupported vif type %d\n",
			  sdata->vif.type);
		kfree_skb(skb);
		return NULL;
	}

	/* Resolve the TX key with the same precedence
	 * ieee80211_tx_h_select_key() uses: per-STA PTK, then GTK for a
	 * multicast RA, then the default_unicast_key fallback.
	 *
	 * For the multicast/GTK case, mirror ieee80211_select_link_key():
	 * on an MLD vif the default_multicast_key promoted by
	 * ieee80211_config_default_key() lives on sdata->link[link_id], not
	 * on &sdata->deflink -- deflink and link[0] are distinct structs
	 * once the vif has real (even single) MLO links assigned. This
	 * path has no per-skb MLO link selection (no IEEE80211_TX_CTRL_MLO_LINK
	 * in control.flags), so derive link_id from vif.active_links
	 * (falling back to valid_links), same as
	 * ieee80211_mlo_multicast_tx()'s single-active-link case. On a
	 * plain non-MLO vif valid_links is 0 and this stays on deflink.
	 */
	if (sta && (key = rcu_dereference(sta->ptk[sta->ptk_idx])))
		;
	else if (is_multicast_ether_addr(hdr.addr1)) {
		struct ieee80211_link_data *link = &sdata->deflink;
		u16 mlo_links = sdata->vif.active_links ?
				sdata->vif.active_links : sdata->vif.valid_links;

		if (mlo_links) {
			struct ieee80211_link_data *mlo_link =
				rcu_dereference(sdata->link[__ffs(mlo_links)]);

			if (mlo_link)
				link = mlo_link;
		}

		key = rcu_dereference(link->default_multicast_key);
	} else
		key = rcu_dereference(sdata->default_unicast_key);

	if (key) {
		bool put_iv_spc;

		gen_iv = key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV;
		put_iv_spc = key->conf.flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE;

		if (key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE &&
		    !(key->flags & KEY_FLAG_TAINTED)) {
			switch (key->conf.cipher) {
			case WLAN_CIPHER_SUITE_CCMP:
			case WLAN_CIPHER_SUITE_CCMP_256:
				if (gen_iv || put_iv_spc)
					iv_len = IEEE80211_CCMP_HDR_LEN;
				break;
			case WLAN_CIPHER_SUITE_GCMP:
			case WLAN_CIPHER_SUITE_GCMP_256:
				if (gen_iv || put_iv_spc)
					iv_len = IEEE80211_GCMP_HDR_LEN;
				break;
			case WLAN_CIPHER_SUITE_TKIP:
				gen_iv = false;
				if (put_iv_spc)
					iv_len = IEEE80211_TKIP_IV_LEN;
				break;
			case WLAN_CIPHER_SUITE_WEP40:
			case WLAN_CIPHER_SUITE_WEP104:
				gen_iv = false;
				if (put_iv_spc)
					iv_len = IEEE80211_WEP_IV_LEN;
				break;
			default:
				gen_iv = false;
				break;
			}
			fc |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		} else {
			key = NULL;
			gen_iv = false;
		}
	}

	/* Always send QoS frames (QoS Control field present) for unicast.
	 * IV/PN space ahead of the LLC/SNAP header is reserved only when
	 * the resolved key's driver actually needs it (mirrors
	 * ieee80211_check_fast_xmit()) -- HW crypto in the common
	 * (non-RAW) ath12k mode writes PN/MIC transparently and needs no
	 * extra space at all.
	 */

	if (is_broadcast_ether_addr(hdr.addr1)) {
		hdrlen = 24;
	} else {
		fc |= cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
		hdrlen = 24 + 2;
	}
	hdr.frame_control = fc;
	hdr.duration_id = 0;
	hdr.seq_ctrl = 0;

	/*
	 * If the skb is shared (e.g. cloned for a TX-status callback or an
	 * AF_PACKET tap) we must not mutate it in place below via
	 * skb_pull()/skb_push() -- obtain our own copy first, matching
	 * ieee80211_build_hdr() and __ieee80211_xmit_fast().
	 */
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		return NULL;

	skip_header_bytes = ETH_HLEN;
	if (ethertype == ETH_P_AARP || ethertype == ETH_P_IPX) {
		encaps_data = bridge_tunnel_header;
		encaps_len = sizeof(bridge_tunnel_header);
		skip_header_bytes -= 2;
	} else if (ethertype >= ETH_P_802_3_MIN) {
		encaps_data = rfc1042_header;
		encaps_len = sizeof(rfc1042_header);
		skip_header_bytes -= 2;
	} else {
		encaps_data = NULL;
		encaps_len = 0;
	}

	skb_pull(skb, skip_header_bytes);

	head_need = hdrlen + iv_len + encaps_len - skb_headroom(skb);
	head_need += IEEE80211_ENCRYPT_HEADROOM;
	head_need += local->tx_headroom;
	head_need = max_t(int, 0, head_need);

	if (ieee80211_skb_resize(sdata, skb, head_need, ENCRYPT_DATA)) {
		sdata_err(sdata,
			  "ieee80211_rawmesh_encap_eth_to_dot11: skb resize failed\n");
		kfree_skb(skb);
		return NULL;
	}

	if (encaps_data)
		memcpy(skb_push(skb, encaps_len), encaps_data, encaps_len);

	if (iv_len) {
		u8 *crypto_hdr = skb_push(skb, iv_len);

		memset(crypto_hdr, 0, iv_len);
		if (gen_iv) {
			u64 pn = atomic64_inc_return(&key->conf.tx_pn);

			crypto_hdr[0] = pn;
			crypto_hdr[1] = pn >> 8;
			crypto_hdr[3] = 0x20 | (key->conf.keyidx << 6);
			crypto_hdr[4] = pn >> 16;
			crypto_hdr[5] = pn >> 24;
			crypto_hdr[6] = pn >> 32;
			crypto_hdr[7] = pn >> 40;
		}
	}

	/*
	 * Maybe we could actually set some fields here, for now just
	 * initialise to zero to indicate no special operation.
	 */
	*(__le16 *)skb_push(skb, 2) = 0;
	memcpy(skb_push(skb, hdrlen - 2), &hdr, hdrlen - 2);

	skb_reset_mac_header(skb);

	return skb;
}

/**
 * ieee80211_rawmesh_tx - transmit a pre-built 802.11 frame directly
 * @sdata: sub interface the frame is being sent on
 * @skb: skb containing a fully-formed 802.11 frame (header + payload)
 * @sta: destination STA resolved by
 *	ieee80211_rawmesh_encap_eth_to_dot11(), or NULL for a
 *	multicast/broadcast RA or an unknown unicast RA
 * @key: TX key resolved by ieee80211_rawmesh_encap_eth_to_dot11(), or
 *	NULL for an unencrypted BSS
 *
 * Used for VAPs configured with %IEEE80211_EXTN_VAP_SUBMODE_RAW_MODE_MESH.
 * By the time this is called, @skb has already been converted from
 * Ethernet-II to a full 802.11 frame by
 * ieee80211_rawmesh_encap_eth_to_dot11(), so no further TX handler
 * processing (crypto, rate control, queue-stop/pending bookkeeping, etc.)
 * is required here. This mirrors the hardware-encap "fast_xmit" fast path
 * in __ieee80211_subif_start_xmit_8023() -- build a minimal
 * ieee80211_tx_info/ieee80211_tx_control and hand the skb straight to
 * drv_tx(), since the frame is already fully formed and the driver
 * resolves addressing directly from the 802.11 header.
 */
static void ieee80211_rawmesh_tx(struct ieee80211_sub_if_data *sdata,
                                   struct sk_buff *skb,
                                   struct sta_info *sta,
                                   struct ieee80211_key *key)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_control control = {};
	struct ieee80211_hdr *hdr;

	memset(info, 0, sizeof(*info));
	prefetch(local->hw.priv);
	info->control.vif = &sdata->vif;
	info->control.flags = u32_encode_bits(IEEE80211_LINK_UNSPECIFIED,
					      IEEE80211_TX_CTRL_MLO_LINK);
	info->hw_queue = sdata->vif.hw_queue[skb_get_queue_mapping(skb)];
	if (key && key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE)
		info->control.hw_key = &key->conf;

	hdr = (struct ieee80211_hdr *)skb->data;
	pr_debug("rawmesh_tx: sdata=%s vif_type=%d fc=0x%04x a1=%pM a2=%pM a3=%pM len=%u hw_queue=%d q_map=%d\n",
		  sdata->name, sdata->vif.type, le16_to_cpu(hdr->frame_control),
		  hdr->addr1, hdr->addr2, hdr->addr3, skb->len, info->hw_queue,
		  skb_get_queue_mapping(skb));

	control.sta = sta ? &sta->sta : NULL;
	drv_tx(local, &control, skb);
}

/**
 * ieee80211_subif_start_xmit_raw_mesh - Send raw 80211 frame to ath12k driver
 * @sdata: sub interface the frame is being sent on
 * @skb: SKB buffer
 *
 * Used for sending the raw 80211 frame to ath12k driver in case of
 * RAW_MODE_MESH
 *
 * Return: NETDEV_TX_OK in case of success.
 */
int ieee80211_subif_start_xmit_raw_mesh(struct ieee80211_sub_if_data *sdata,
					struct sk_buff *skb)
{
	u16 mlo_links;
	struct sta_info *sta;
	struct ieee80211_key *key;
	struct ieee80211_hdr *hdr;
	struct ieee80211_link_data *link;
	struct ieee80211_link_data *mlo_link;
	u8 *da;

	if (unlikely(!ieee80211_sdata_running(sdata))) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	rcu_read_lock();

	if (mmeshsim)
		skb = ieee80211_rawmesh_encap_eth_to_dot11(sdata, skb);
	else
		skb_pull(skb, sdata->vif.mhdr_len);

	if (!skb) {
		rcu_read_unlock();
		return NETDEV_TX_OK;
	}

	hdr = (struct ieee80211_hdr *)skb->data;
	da = ieee80211_get_DA(hdr);

	if (is_multicast_ether_addr(da))
		sta = ERR_PTR(-ENOENT);
	else
		sta = sta_info_get_bss(sdata, da);

	if (IS_ERR(sta))
		sta = NULL;

	if (sta) {
		key = rcu_dereference(sta->ptk[sta->ptk_idx]);
	} else if (is_multicast_ether_addr(da)) {
		link = &sdata->deflink;
		mlo_links = sdata->vif.active_links ? sdata->vif.active_links :
				sdata->vif.valid_links;

		if (mlo_links) {
			mlo_link = rcu_dereference(sdata->link[__ffs(mlo_links)]);
			if (mlo_link)
				link = mlo_link;
		}

		key = rcu_dereference(link->default_multicast_key);
	} else {
		key = rcu_dereference(sdata->default_unicast_key);
	}

	ieee80211_rawmesh_tx(sdata, skb, sta, key);
	rcu_read_unlock();

	return NETDEV_TX_OK;
}
EXPORT_SYMBOL(ieee80211_subif_start_xmit_raw_mesh);
#endif /* CPTCFG_QCN_EXTN_MESH_SUPPORT */
