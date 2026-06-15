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

#ifndef CPTCFG_QCN_EXTN

static inline void
ieee80211_start_ap_bootup_cac_extn(struct ieee80211_sub_if_data *sdata,
				   struct ieee80211_link_data *link,
				   struct cfg80211_ap_settings *params)
{
}

#else /* CPTCFG_QCN_EXTN */

/**
 * ieee80211_start_ap_bootup_cac_extn - start boot-up CAC timer on first
 * start_ap for a 5G DFS channel
 * @sdata: the sub-interface data
 * @link: the link data for the AP link being started
 * @params: the AP configuration parameters
 *
 * If this is the first BSS being started on a 5G DFS channel, starts the
 * per-chanctx dfs_cac_timer with the regulatory CAC time.  Subsequent
 * start_ap calls on the same chanctx inherit the timing state.
 */
void ieee80211_start_ap_bootup_cac_extn(struct ieee80211_sub_if_data *sdata,
					struct ieee80211_link_data *link,
					struct cfg80211_ap_settings *params);

#endif /* CPTCFG_QCN_EXTN */

#endif /* DFS_EXTN_H */
