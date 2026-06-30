// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/nl80211.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include "../ieee80211_i.h"
#include "../../wireless/core.h"
#include "dfs_extn.h"

/**
 * ieee80211_bootup_cac_start_timer_extn - start boot-up CAC timer for a
 * 5G DFS channel, called from both AP (start_ap) and mesh (join_mesh) paths.
 */
void ieee80211_bootup_cac_start_timer_extn(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_link_data *link,
					   const struct cfg80211_chan_def *chandef,
					   u32 residual_cac_ms)
{
	struct ieee80211_local *local = sdata->local;
	unsigned int link_id = link->link_id;
	struct ieee80211_chanctx *chanctx;

	if (!cfg80211_bootup_cac_is_5g_dfs_chan_extn(local->hw.wiphy,
						     chandef))
		return;

	chanctx = ieee80211_link_get_chanctx(link);
	if (!chanctx)
		return;

	if (!hrtimer_is_queued(&chanctx->dfs_cac_timer)) {
		unsigned int cac_time_ms, full_cac_time_ms;

		full_cac_time_ms = cfg80211_chandef_dfs_cac_time(local->hw.wiphy,
								 chandef,
								 false, false);
		if (WARN_ON(!full_cac_time_ms))
			full_cac_time_ms = IEEE80211_DFS_MIN_CAC_TIME_MS;

		cac_time_ms = full_cac_time_ms;
		/* Dependent repeater FH AP residual CAC: userspace passes
		 * remaining CAC time; root AP passes 0 and uses full CAC.
		 */
		if (residual_cac_ms && residual_cac_ms < full_cac_time_ms)
			cac_time_ms = residual_cac_ms;

		pr_debug("mac80211: boot-up CAC: starting %ums timer on chanctx for %s "
			 "link %u full_cac_ms=%u residual_cac_ms=%u\n",
			 cac_time_ms, sdata->name, link_id, full_cac_time_ms,
			 residual_cac_ms);
		hrtimer_start(&chanctx->dfs_cac_timer,
			      ms_to_ktime(cac_time_ms),
			      HRTIMER_MODE_REL);

		sdata->wdev.links[link_id].cac_started = true;
		sdata->wdev.links[link_id].cac_start_time = jiffies;
		sdata->wdev.links[link_id].cac_time_ms = cac_time_ms;
	} else {
		struct ieee80211_link_data *first_link = NULL;
		struct ieee80211_link_data *iter_link;

		list_for_each_entry(iter_link, &chanctx->assigned_links,
				    assigned_chanctx_list) {
			/* Skip the current link: it was just prepended to the
			 * list head by ieee80211_link_use_channel() before this
			 * call, so its cac_start_time/cac_time_ms are not yet
			 * initialized. */
			if (iter_link == link)
				continue;

			if (iter_link->sdata->vif.type == NL80211_IFTYPE_AP ||
			    iter_link->sdata->vif.type == NL80211_IFTYPE_MESH_POINT) {
				first_link = iter_link;
				break;
			}
		}

		sdata->wdev.links[link_id].cac_started = true;
		if (first_link) {
			unsigned int first_link_id = first_link->link_id;
			struct wireless_dev *first_wdev =
				&first_link->sdata->wdev;

			sdata->wdev.links[link_id].cac_start_time =
				first_wdev->links[first_link_id].cac_start_time;
			sdata->wdev.links[link_id].cac_time_ms =
				first_wdev->links[first_link_id].cac_time_ms;
		} else {
			sdata->wdev.links[link_id].cac_start_time = jiffies;
			sdata->wdev.links[link_id].cac_time_ms =
				IEEE80211_DFS_MIN_CAC_TIME_MS;
		}
	}
}

/**
 * ieee80211_bootup_cac_mesh_vdev_up_extn - trigger vdev_up for mesh vdevs
 * after boot-up CAC completes on a shared chanctx.
 */
void ieee80211_bootup_cac_mesh_vdev_up_extn(struct ieee80211_chanctx *ctx)
{
	struct ieee80211_link_data *iter_link;

	list_for_each_entry(iter_link, &ctx->assigned_links,
			    assigned_chanctx_list) {
		if (iter_link->sdata->vif.type != NL80211_IFTYPE_MESH_POINT)
			continue;
		/*
		 * enable_beacon is set by ieee80211_start_mesh() when the mesh
		 * joined during boot-up CAC.  Use it as the guard instead of
		 * cac_started, which cfg80211_clear_cac_started() has already
		 * cleared for all wdevs on this channel by the time we get here.
		 */
		if (!iter_link->sdata->vif.bss_conf.enable_beacon)
			continue;
		ieee80211_link_info_change_notify(iter_link->sdata, iter_link,
						  BSS_CHANGED_BEACON_ENABLED);
	}
}

/**
 * ieee80211_bootup_cac_handle_skip_extn - handle skip_cac flag during boot-up CAC.
 *
 * When @skip_cac is true, the user space has requested that CAC be skipped on
 * this DFS channel.  Instead of starting the per-chanctx CAC timer we mark
 * cac_started on the wdev link and immediately send a synthetic
 * NL80211_RADAR_CAC_FINISHED event.
 *
 * When @skip_cac is false, the normal boot-up CAC timer path is taken via
 * ieee80211_bootup_cac_start_timer_extn().
 */
void ieee80211_bootup_cac_handle_skip_extn(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_link_data *link,
					   const struct cfg80211_chan_def *chandef,
					   u32 residual_cac_ms, bool skip_cac)
{
	unsigned int link_id = link->link_id;

	if (!skip_cac) {
		ieee80211_bootup_cac_start_timer_extn(sdata, link, chandef,
						      residual_cac_ms);
		return;
	}

	pr_debug("mac80211: boot-up CAC: skip_cac set for %s link %u, sending dummy CAC_FINISHED\n",
		 sdata->name, link_id);

	sdata->wdev.links[link_id].cac_started = true;
	sdata->wdev.links[link_id].cac_start_time = jiffies;
	sdata->wdev.links[link_id].cac_time_ms = 0;

	cfg80211_cac_event(sdata->dev, chandef,
			   NL80211_RADAR_CAC_FINISHED,
			   GFP_KERNEL, link_id);
}
