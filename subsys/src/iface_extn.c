// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <net/mac80211.h>
#include <linux/if_arp.h>
#include "../ieee80211_i.h"
#include "../driver-ops.h"


void ieee80211_scan_radio_do_open_extn(struct ieee80211_sub_if_data *sdata,
				       struct wireless_dev *wdev,
				       struct net_device *dev,
				       u32 *hw_reconf_flags)
{
	struct ieee80211_local *local = sdata->local;

	if (!wdev || !wdev_is_scan_radio(wdev))
		return;

	sdata->bss = &sdata->u.ap;
	local->monitors++;
	if (local->monitors == 1) {
		local->hw.conf.flags |= IEEE80211_CONF_MONITOR;
		*hw_reconf_flags |= IEEE80211_CONF_CHANGE_MONITOR;
	}

	ieee80211_adjust_monitor_flags(sdata, 1);
	ieee80211_configure_filter(local);
	ieee80211_recalc_offload(local);
	ieee80211_recalc_idle(local);

	if (dev)
		netif_carrier_on(dev);

	INIT_LIST_HEAD(&sdata->u.mntr.list);
	list_add_tail_rcu(&sdata->u.mntr.list, &local->mon_list);
	if (sdata->dev)
		sdata->dev->type = ARPHRD_IEEE80211_RADIOTAP;
}

void ieee80211_scan_radio_do_stop_extn(struct ieee80211_sub_if_data *sdata,
				       u32 *hw_reconf_flags)
{
	struct ieee80211_local *local = sdata->local;
	struct wireless_dev *wdev = &sdata->wdev;

	if (!wdev || !wdev_is_scan_radio(wdev) || local->monitors == 0)
		return;

	local->monitors--;
	if (local->monitors == 0) {
		local->hw.conf.flags &= ~IEEE80211_CONF_MONITOR;
		*hw_reconf_flags |= IEEE80211_CONF_CHANGE_MONITOR;
	}

	ieee80211_configure_filter(local);
	ieee80211_recalc_offload(local);
	ieee80211_recalc_idle(local);
	list_del_rcu(&sdata->u.mntr.list);
}



