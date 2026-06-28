/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef DFS_EXTN_H
#define DFS_EXTN_H

#include "cfg80211_dfs_extn.h"

struct ieee80211_link_data;
struct ieee80211_sub_if_data;
struct cfg80211_ap_settings;
struct ieee80211_chanctx;

#ifndef CPTCFG_QCN_EXTN

static inline void
ieee80211_bootup_cac_start_timer_extn(struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_link_data *link,
				      const struct cfg80211_chan_def *chandef,
				      u32 residual_cac_ms)
{
}

static inline void
ieee80211_bootup_cac_mesh_vdev_up_extn(struct ieee80211_chanctx *ctx)
{
}

#else /* CPTCFG_QCN_EXTN */

/**
 * ieee80211_bootup_cac_start_timer_extn - start boot-up CAC timer for a
 * 5G DFS channel, called from both AP (start_ap) and mesh (join_mesh) paths.
 * @sdata: the sub-interface data
 * @link: the link data for the link being started
 * @chandef: the channel definition for the link
 * @residual_cac_ms: residual CAC time in milliseconds, or 0 for full CAC
 *
 * If this is the first BSS being started on a 5G DFS channel, starts the
 * per-chanctx dfs_cac_timer with the residual CAC time when valid, otherwise
 * with the regulatory CAC time. Subsequent calls on the same chanctx inherit
 * the timing state.
 */
void ieee80211_bootup_cac_start_timer_extn(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_link_data *link,
					   const struct cfg80211_chan_def *chandef,
					   u32 residual_cac_ms);

/**
 * ieee80211_bootup_cac_mesh_vdev_up_extn - trigger vdev_up for mesh vdevs
 * sharing a chanctx after boot-up CAC completes.
 * @ctx:     the chanctx whose CAC timer just fired
 *
 * The CAC timer fires once per chanctx and cfg80211_cac_event() is sent only
 * for the first link found.  For any mesh vdev on the same chanctx that has
 * already joined (enable_beacon=true), re-fire BSS_CHANGED_BEACON_ENABLED so
 * that ath12k_control_beaconing() is called again.  At this point the channel
 * is DFS_AVAILABLE, so ath12k_control_beaconing_bootup_cac_check_extn()
 * returns false and vdev_up is issued.
 */
void ieee80211_bootup_cac_mesh_vdev_up_extn(struct ieee80211_chanctx *ctx);

#endif /* CPTCFG_QCN_EXTN */

#endif /* DFS_EXTN_H */
