// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/nl80211.h>
#include <net/cfg80211.h>
#include <net/genetlink.h>
#include "core.h"
#include "rdev-ops.h"
#include "cfg80211_scan_radio_extn.h"

void cfg80211_scan_radio_inject_power_mode(struct cfg80211_registered_device *rdev,
					   struct wireless_dev *wdev,
					   struct genl_info *info,
					   struct cfg80211_scan_radio_pwr_nla *attr_buf)
{
	u32 freq, center_freq;
	int bw, mode;

	if (!wdev_is_scan_radio(wdev))
		return;

	if (!info->attrs[NL80211_ATTR_WIPHY_FREQ])
		return;

	if (info->attrs[NL80211_ATTR_6G_REG_POWER_MODE])
		return;

	if (!rdev->wiphy.bands[NL80211_BAND_6GHZ])
		return;

	freq = MHZ_TO_KHZ(nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]));
	if (info->attrs[NL80211_ATTR_WIPHY_FREQ_OFFSET])
		freq += nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ_OFFSET]);
	freq /= 1000; /* convert to MHz after KHz offset is applied */
	if (!is_freq_6g_band(freq))
		return;

	center_freq = info->attrs[NL80211_ATTR_CENTER_FREQ1]
		? nla_get_u32(info->attrs[NL80211_ATTR_CENTER_FREQ1])
		: freq;

	bw = info->attrs[NL80211_ATTR_CHANNEL_WIDTH]
		? nl80211_chan_width_to_mhz(
			nla_get_u32(info->attrs[NL80211_ATTR_CHANNEL_WIDTH]))
		: 20;
	if (bw < 20)
		return;

	mode = rdev_get_scan_radio_best_6ghz_power_mode(rdev,
							wdev->netdev,
							freq,
							center_freq,
							bw);
	if (mode < 0 || mode >= NL80211_REG_NUM_POWER_MODES)
		return;

	attr_buf->hdr.nla_len  = nla_attr_size(sizeof(u8));
	attr_buf->hdr.nla_type = NL80211_ATTR_6G_REG_POWER_MODE;
	attr_buf->mode = (u8)mode;
	info->attrs[NL80211_ATTR_6G_REG_POWER_MODE] = &attr_buf->hdr;
}
