// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/nl80211.h>
#include <net/cfg80211.h>
#include <net/genetlink.h>
#include "core.h"
#include "cfg80211_scan_radio_extn.h"

void cfg80211_scan_radio_inject_power_mode(struct cfg80211_registered_device *rdev,
					   struct wireless_dev *wdev,
					   struct genl_info *info,
					   struct cfg80211_scan_radio_pwr_nla *attr_buf)
{
	struct ieee80211_channel *chan;
	struct ieee80211_supported_band *sband6;
	u32 freq_khz;
	u8 mode;
	int i;
	static const u8 scan_radio_pwr_prio[] = {
		NL80211_REG_AP_SP,
		NL80211_REG_AP_LPI,
		NL80211_REG_AP_VLP,
	};

	if (!wdev_is_scan_radio(wdev))
		return;

	if (!info->attrs[NL80211_ATTR_WIPHY_FREQ])
		return;

	if (info->attrs[NL80211_ATTR_6G_REG_POWER_MODE])
		return;

	sband6 = rdev->wiphy.bands[NL80211_BAND_6GHZ];
	if (!sband6)
		return;

	freq_khz = MHZ_TO_KHZ(nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]));
	if (info->attrs[NL80211_ATTR_WIPHY_FREQ_OFFSET])
		freq_khz += nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ_OFFSET]);

	/* Pass 1: SP → LPI → VLP, require no DISABLED and no NO_IR */
	for (i = 0; i < ARRAY_SIZE(scan_radio_pwr_prio); i++) {

		mode = scan_radio_pwr_prio[i];

		if (!sband6->chan_6g[mode])
			continue;

		chan = ieee80211_get_6g_channel_khz(&rdev->wiphy, freq_khz, mode);
		if (chan &&
		    !(chan->flags & (IEEE80211_CHAN_DISABLED | IEEE80211_CHAN_NO_IR)))
			goto inject;
	}

	/* Pass 2: fall back to SP, accept NO_IR but not DISABLED */
	mode = NL80211_REG_AP_SP;
	chan = ieee80211_get_6g_channel_khz(&rdev->wiphy, freq_khz, mode);
	if (!chan || (chan->flags & IEEE80211_CHAN_DISABLED))
		return;

inject:
	attr_buf->hdr.nla_len  = nla_attr_size(sizeof(u8));
	attr_buf->hdr.nla_type = NL80211_ATTR_6G_REG_POWER_MODE;
	attr_buf->mode = mode;
	info->attrs[NL80211_ATTR_6G_REG_POWER_MODE] = &attr_buf->hdr;
}
