// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/nl80211.h>
#include <linux/device.h>
#include <net/cfg80211.h>
#include "cfg80211_dfs_extn.h"

/**
 * cfg80211_is_driver_mac80211_hwsim - check if wiphy belongs to mac80211_hwsim
 * @wiphy: the wiphy to check
 *
 * Returns true when the wiphy parent device is driven by the mac80211_hwsim
 * virtual driver, false otherwise.
 */
bool cfg80211_is_driver_mac80211_hwsim(struct wiphy *wiphy)
{
	return wiphy->dev.parent &&
	       !strcmp(dev_driver_string(wiphy->dev.parent), "mac80211_hwsim");
}
EXPORT_SYMBOL(cfg80211_is_driver_mac80211_hwsim);

/**
 * cfg80211_support_bootup_cac - check if boot-up CAC is supported
 * @wiphy: the wiphy to check
 * @radio_idx: physical radio index to check (must be >= 0)
 *
 * Returns false for mac80211_hwsim (virtual driver), when radio_cfg is
 * not allocated, or when radio_idx is out of range.  Returns the value of
 * wiphy->radio_cfg[radio_idx].bootup_cac_feature directly.
 */
bool cfg80211_support_bootup_cac(struct wiphy *wiphy, int radio_idx)
{
	if (cfg80211_is_driver_mac80211_hwsim(wiphy))
		return false;

	if (!wiphy->radio_cfg || radio_idx < 0 || radio_idx >= wiphy->n_radio)
		return false;

	return wiphy->radio_cfg[radio_idx].bootup_cac_feature;
}
EXPORT_SYMBOL(cfg80211_support_bootup_cac);

/**
 * cfg80211_bootup_cac_is_5g_dfs_chan_extn - check if boot-up CAC applies to
 * the given wiphy and channel
 * @wiphy: the wiphy to check boot-up CAC support on
 * @chandef: the channel definition to evaluate (all subchannels are checked)
 *
 * Returns true when boot-up CAC is supported and the chandef covers a 5 GHz
 * channel definition that requires DFS (radar detection) on any subchannel.
 * Used by callers that need to bypass the normal beaconing / CAC-started guards
 * so that all co-located 5G BSS interfaces can be created before CAC completes.
 */
bool cfg80211_bootup_cac_is_5g_dfs_chan_extn(struct wiphy *wiphy,
					      const struct cfg80211_chan_def *chandef)
{
	int radio_idx;

	if (!chandef || !chandef->chan)
		return false;

	if (chandef->chan->band != NL80211_BAND_5GHZ)
		return false;

	radio_idx = cfg80211_get_hw_idx_by_chan(wiphy, chandef->chan);
	if (!cfg80211_support_bootup_cac(wiphy, radio_idx))
		return false;

	return cfg80211_chandef_dfs_required(wiphy, chandef,
					     NL80211_IFTYPE_AP) > 0;
}
EXPORT_SYMBOL(cfg80211_bootup_cac_is_5g_dfs_chan_extn);
