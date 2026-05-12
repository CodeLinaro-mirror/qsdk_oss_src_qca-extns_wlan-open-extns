// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "core.h"
#include "debug.h"
#include "ath12k_cmn_extn.h"
#include "cbs_extn.h"

static int ath12k_cbs_submit_scan(struct ath12k *ar, struct ath12k_cbs *cbs);

static const char *ath12k_cbs_state_str(enum ath12k_cbs_state state)
{
	switch (state) {
	case ATH12K_CBS_STATE_INIT:
		return "INIT";
	case ATH12K_CBS_STATE_SCAN:
		return "SCAN";
	case ATH12K_CBS_STATE_REST:
		return "REST";
	case ATH12K_CBS_STATE_WAIT:
		return "WAIT";
	}

	return "UNKNOWN";
}

static const char *ath12k_cbs_event_str(enum ath12k_cbs_event event)
{
	switch (event) {
	case ATH12K_CBS_EVENT_SCAN_START:
		return "SCAN_START";
	case ATH12K_CBS_EVENT_SCAN_NEXT:
		return "SCAN_NEXT";
	case ATH12K_CBS_EVENT_SCAN_CONTINUE:
		return "SCAN_CONTINUE";
	case ATH12K_CBS_EVENT_DWELL_SPLIT:
		return "DWELL_SPLIT";
	case ATH12K_CBS_EVENT_SCAN_CANCEL:
		return "SCAN_CANCEL";
	case ATH12K_CBS_EVENT_SCAN_COMPLETE:
		return "SCAN_COMPLETE";
	case ATH12K_CBS_EVENT_SCAN_RESTART:
		return "SCAN_RESTART";
	}

	return "UNKNOWN";
}

void ath12k_cbs_init_dwell_params(struct ath12k_cbs *cbs)
{
	u32 idx;

	if (!cbs)
		return;

	memset(cbs->scan_offset, 0, sizeof(cbs->scan_offset));
	memset(cbs->scan_dwell_rest, 0, sizeof(cbs->scan_dwell_rest));
	cbs->max_arr_size_used = 0;
	cbs->max_dwell_split_cnt = 0;
	cbs->dwell_split_cnt = 0;

	switch (cbs->dwell_split_time) {
	case ATH12K_CBS_DWELL_TIME_20MS:
		switch (cbs->cbs_total_dwell_time) {
		case ATH12K_CBS_TOTAL_DWELL_TIME_20MS:
			cbs->max_arr_size_used = 1;
			cbs->scan_offset[0] = 0;
			break;
		case ATH12K_CBS_TOTAL_DWELL_TIME_200MS:
			cbs->max_arr_size_used = 10;
			for (idx = 0; idx < cbs->max_arr_size_used; idx++)
				cbs->scan_offset[idx] = idx * 10;
			break;
		default:
			return;
		}
		break;
	case ATH12K_CBS_DWELL_TIME_25MS:
		switch (cbs->cbs_total_dwell_time) {
		case ATH12K_CBS_TOTAL_DWELL_TIME_25MS:
			cbs->max_arr_size_used = 1;
			cbs->scan_offset[0] = 0;
			break;
		case ATH12K_CBS_TOTAL_DWELL_TIME_100MS:
			cbs->max_arr_size_used = 4;
			cbs->scan_offset[0] = 0;
			cbs->scan_offset[1] = 25;
			cbs->scan_offset[2] = 50;
			cbs->scan_offset[3] = 75;
			break;
		case ATH12K_CBS_TOTAL_DWELL_TIME_200MS:
			cbs->max_arr_size_used = 8;
			cbs->scan_offset[0] = 0;
			cbs->scan_offset[1] = 25;
			cbs->scan_offset[2] = 50;
			cbs->scan_offset[3] = 75;
			cbs->scan_offset[4] = 0;
			cbs->scan_offset[5] = 25;
			cbs->scan_offset[6] = 50;
			cbs->scan_offset[7] = 75;
			break;
		default:
			return;
		}
		break;
	case ATH12K_CBS_DWELL_TIME_50MS:
		switch (cbs->cbs_total_dwell_time) {
		case ATH12K_CBS_TOTAL_DWELL_TIME_50MS:
			cbs->max_arr_size_used = 1;
			cbs->scan_offset[0] = 0;
			break;
		case ATH12K_CBS_TOTAL_DWELL_TIME_100MS:
			cbs->max_arr_size_used = 2;
			cbs->scan_offset[0] = 0;
			cbs->scan_offset[1] = 50;
			break;
		case ATH12K_CBS_TOTAL_DWELL_TIME_200MS:
			cbs->max_arr_size_used = 4;
			cbs->scan_offset[0] = 0;
			cbs->scan_offset[1] = 50;
			cbs->scan_offset[2] = 0;
			cbs->scan_offset[3] = 50;
			break;
		default:
			return;
		}
		break;
	default:
		return;
	}

	cbs->dwell_split_cnt = cbs->max_arr_size_used ? cbs->max_arr_size_used - 1 : 0;
	cbs->max_dwell_split_cnt = cbs->dwell_split_cnt;
	/* Initialize scan_dwell_rest array.
	 * The first scan (idx=0) should NOT have a rest period before it.
	 * Rest periods apply AFTER each scan completes, not before.
	 */
	for (idx = 0; idx < ATH12K_CBS_SCAN_OFFSET_ARRAY_SIZE; idx++) {
		if (idx == 0)
			/* No rest before the first scan */
			cbs->scan_dwell_rest[idx] = 0;
		else if (idx < cbs->max_arr_size_used)
			/* Rest after each subsequent scan */
			cbs->scan_dwell_rest[idx] = cbs->rest_time;
		else
			cbs->scan_dwell_rest[idx] = 0;
	}
}

static void ath12k_cbs_set_state(struct ath12k_cbs *cbs,
				 enum ath12k_cbs_state state)
{
	struct ath12k *ar = cbs->ar;
	enum ath12k_cbs_state old_state = cbs->state;

	cbs->state = state;

	if (ar && old_state != state)
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "CBS: state %s -> %s\n",
			   ath12k_cbs_state_str(old_state),
			   ath12k_cbs_state_str(state));
}

static void ath12k_cbs_timer(struct timer_list *timer)
{
	struct ath12k_cbs *cbs = from_timer(cbs, timer, timer);
	enum ath12k_cbs_event event = 0;

	switch (cbs->state) {
	case ATH12K_CBS_STATE_SCAN:
		event = ATH12K_CBS_EVENT_DWELL_SPLIT;
		break;
	case ATH12K_CBS_STATE_REST:
		if (cbs->dwell_split_cnt < cbs->max_dwell_split_cnt)
			event = ATH12K_CBS_EVENT_DWELL_SPLIT;
		else
			event = ATH12K_CBS_EVENT_SCAN_NEXT;
		break;
	case ATH12K_CBS_STATE_WAIT:
		event = ATH12K_CBS_EVENT_SCAN_START;
		break;
	default:
		return;
	}

	ath12k_cbs_post_event(cbs, event);
}

static bool ath12k_cbs_is_urgent_event(enum ath12k_cbs_event event)
{
	return event == ATH12K_CBS_EVENT_SCAN_CANCEL;
}

static void ath12k_cbs_cancel(struct ath12k_cbs *cbs, bool restart)
{
	struct ath12k *ar = cbs->ar;

	del_timer_sync(&cbs->timer);
	cbs->ar = NULL;
	cbs->scan_id = 0;
	cbs->chan_list_idx = 0;
	cbs->dwell_split_cnt = cbs->max_dwell_split_cnt;
	cbs->pending_event = 0;

	if (!restart)
		ath12k_cbs_set_state(cbs, ATH12K_CBS_STATE_INIT);

	if (ar)
		memset(&ar->ar_extn.cbs_params, 0,
		       sizeof(ar->ar_extn.cbs_params));
}

static void ath12k_cbs_complete(struct ath12k_cbs *cbs)
{
	cbs->complete_count++;
	ath12k_cbs_set_state(cbs, ATH12K_CBS_STATE_INIT);
}

static void ath12k_cbs_work(struct work_struct *work)
{
	struct ath12k_cbs *cbs = container_of(work, struct ath12k_cbs, work);
	enum ath12k_cbs_event event;

	spin_lock_bh(&cbs->lock);
	event = cbs->pending_event;
	cbs->pending_event = 0;
	spin_unlock_bh(&cbs->lock);

	if (!event)
		return;

	ath12k_cbs_event_handler(cbs, event);
}

int ath12k_cbs_init(struct ath12k *ar)
{
	struct ath12k_cbs *cbs;

	if (!ar)
		return -EINVAL;

	cbs = &ar->ar_extn.cbs;
	memset(cbs, 0, sizeof(*cbs));

	cbs->ar = ar;
	cbs->state = ATH12K_CBS_STATE_INIT;
	cbs->dwell_time = ATH12K_CBS_DEFAULT_DWELL_TIME;
	cbs->rest_time = ATH12K_CBS_DEFAULT_RESTTIME;
	cbs->wait_time = ATH12K_CBS_DEFAULT_WAIT_TIME;
	cbs->dwell_split_time = ATH12K_CBS_DEFAULT_DWELL_SPLIT_TIME;
	cbs->min_dwell_rest_time = ATH12K_CBS_DEFAULT_DWELL_REST_TIME;
	cbs->cbs_total_dwell_time = ATH12K_CBS_DEFAULT_TOTAL_DWELL_TIME;
	cbs->dwell_split_cnt = 0;
	cbs->max_dwell_split_cnt = ATH12K_CBS_DEFAULT_TOTAL_DWELL_TIME / ATH12K_CBS_DEFAULT_DWELL_SPLIT_TIME;

	spin_lock_init(&cbs->lock);
	timer_setup(&cbs->timer, ath12k_cbs_timer, 0);
	INIT_WORK(&cbs->work, ath12k_cbs_work);

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "CBS: initialized\n");

	return 0;
}
EXPORT_SYMBOL_GPL(ath12k_cbs_init);

void ath12k_cbs_deinit(struct ath12k *ar)
{
	struct ath12k_cbs *cbs;

	if (!ar)
		return;

	cbs = &ar->ar_extn.cbs;
	ath12k_cbs_stop(ar);
	cancel_work_sync(&cbs->work);
	del_timer_sync(&cbs->timer);
}
EXPORT_SYMBOL_GPL(ath12k_cbs_deinit);

int ath12k_cbs_start(struct ath12k *ar,
		     const struct ath12k_cbs_scan_params *scan_params)
{
	struct ath12k_cbs *cbs;
	u32 chan_idx;

	if (!ar || !scan_params)
		return -EINVAL;

	cbs = &ar->ar_extn.cbs;

	spin_lock_bh(&cbs->lock);
	if (!cbs->ar)
		cbs->ar = ar;

	if (scan_params->arvif)
		cbs->arvif = scan_params->arvif;

	cbs->cbs_enable = scan_params->enable;
	cbs->rest_time = scan_params->resttime;
	cbs->min_dwell_rest_time = scan_params->dwellrest;
	cbs->wait_time = scan_params->waittime;
	cbs->dwell_split_time = scan_params->dwellsplit;
	cbs->cbs_total_dwell_time = scan_params->totaldwell;
	cbs->nchans = scan_params->num_freqs;
	cbs->chan_list_idx = scan_params->link_id;
	if (scan_params->freq_list && scan_params->num_freqs) {
			for (chan_idx = 0; chan_idx < scan_params->num_freqs && chan_idx < ATH12K_CBS_MAX_CHANNELS; chan_idx++)
			cbs->chan_list[chan_idx] = scan_params->freq_list[chan_idx];
	}

	if (!scan_params || !cbs->cbs_enable) {
		spin_unlock_bh(&cbs->lock);
		ath12k_cbs_stop(ar);
		return 0;
	}

	spin_unlock_bh(&cbs->lock);

	ath12k_cbs_post_event(cbs, ATH12K_CBS_EVENT_SCAN_START);

	return 0;
}
EXPORT_SYMBOL_GPL(ath12k_cbs_start);

void ath12k_cbs_stop(struct ath12k *ar)
{
	struct ath12k_wmi_scan_cancel_arg arg = {0};
	struct ath12k_cbs *cbs;
	int ret;

	if (!ar)
		return;

	cbs = &ar->ar_extn.cbs;

	if (cbs->state != ATH12K_CBS_STATE_INIT) {
		arg.req_type = WLAN_SCAN_CANCEL_SINGLE;
		arg.scan_id = cbs->scan_id ? cbs->scan_id : ATH12K_SCAN_ID;
		arg.pdev_id = ar->pdev ? ar->pdev->pdev_id : 0;
		del_timer_sync(&cbs->timer);
		spin_lock_bh(&cbs->lock);
		cbs->cbs_enable = 0;
		cbs->pending_event = ATH12K_CBS_EVENT_SCAN_CANCEL;
		spin_unlock_bh(&cbs->lock);
		ret = ath12k_wmi_send_scan_stop_cmd(ar, &arg);
		if (ret)
			ath12k_warn(ar->ab, "CBS: failed to stop active scan: %d\n", ret);
		ath12k_cbs_cancel(cbs, false);
	}
}

void ath12k_cbs_stop_async(struct ath12k *ar)
{
	struct ath12k_cbs *cbs;

	if (!ar)
		return;

	cbs = &ar->ar_extn.cbs;
	if (cbs->state != ATH12K_CBS_STATE_INIT)
		ath12k_cbs_stop(ar);
}

void ath12k_cbs_post_event(struct ath12k_cbs *cbs, enum ath12k_cbs_event event)
{
	struct ath12k *ar;

	if (!cbs || !event)
		return;

	ar = cbs->ar;
	spin_lock_bh(&cbs->lock);
	if (cbs->pending_event) {
		if (ar) {
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
				   "CBS: drop event %s while %s pending\n",
				   ath12k_cbs_event_str(event),
				   ath12k_cbs_event_str(cbs->pending_event));
		}
		spin_unlock_bh(&cbs->lock);
		return;
	}

	if (!cbs->ar && !ath12k_cbs_is_urgent_event(event)) {
		if (ar)
			ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
				   "CBS: ignore event %s without vif\n",
				   ath12k_cbs_event_str(event));
		spin_unlock_bh(&cbs->lock);
		return;
	}

	cbs->pending_event = event;
	schedule_work(&cbs->work);
	spin_unlock_bh(&cbs->lock);
}

int ath12k_cbs_event_handler(struct ath12k_cbs *cbs,
			     enum ath12k_cbs_event event)
{
	struct ath12k *ar;
	enum ath12k_cbs_state cur_state;
	int ret = 0;

	if (!cbs)
		return -EINVAL;

	ar = cbs->ar;
	cur_state = cbs->state;

	if (ar)
		ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "CBS: state %s event %s\n",
			   ath12k_cbs_state_str(cur_state),
			   ath12k_cbs_event_str(event));

	switch (event) {
	case ATH12K_CBS_EVENT_SCAN_START:
		switch (cur_state) {
		case ATH12K_CBS_STATE_INIT:
		case ATH12K_CBS_STATE_WAIT:
			cbs->chan_list_idx = 0;
			ath12k_cbs_init_dwell_params(cbs);
			if (!cbs->max_arr_size_used) {
				ret = -EINVAL;
				break;
			}
			cbs->scan_offset_idx = 0;
			ath12k_cbs_set_state(cbs, ATH12K_CBS_STATE_SCAN);
			cbs->scan_count++;
			cbs->dwell_split_cnt = 0;
			cbs->scan_offset_idx = 0;
			if (ar)
				ret = ath12k_cbs_submit_scan(ar, cbs);
			break;
		default:
			ret = -EALREADY;
			break;
		}
		break;
	case ATH12K_CBS_EVENT_SCAN_NEXT:
		switch (cur_state) {
		case ATH12K_CBS_STATE_SCAN:
			cbs->chan_list_idx++;
			cbs->dwell_split_cnt = 0;
			cbs->scan_offset_idx = 0;
			if (cbs->chan_list_idx >= cbs->nchans) {
				ath12k_cbs_post_event(cbs, ATH12K_CBS_EVENT_SCAN_COMPLETE);
				break;
			}
			if (ar)
				ret = ath12k_cbs_submit_scan(ar, cbs);
			break;
		case ATH12K_CBS_STATE_REST:
			cbs->chan_list_idx++;
			cbs->dwell_split_cnt = 0;
 			cbs->scan_offset_idx = 0;
 			if (cbs->chan_list_idx >= cbs->nchans) {
				ath12k_cbs_post_event(cbs, ATH12K_CBS_EVENT_SCAN_COMPLETE);
				break;
			}
			ath12k_cbs_set_state(cbs, ATH12K_CBS_STATE_SCAN);
			if (ar)
				ret = ath12k_cbs_submit_scan(ar, cbs);
			break;
		default:
			break;
		}
		break;
	case ATH12K_CBS_EVENT_DWELL_SPLIT:
		switch (cur_state) {
		case ATH12K_CBS_STATE_SCAN:
			if (cbs->scan_offset_idx >= cbs->max_arr_size_used)
				cbs->scan_offset_idx = 0;
			if (cbs->scan_offset_idx >= ATH12K_CBS_SCAN_OFFSET_ARRAY_SIZE) {
				ret = -EINVAL;
				break;
			}
			if (cbs->scan_dwell_rest[cbs->scan_offset_idx]) {
				ath12k_cbs_set_state(cbs, ATH12K_CBS_STATE_REST);
				mod_timer(&cbs->timer,
					  jiffies + msecs_to_jiffies(cbs->scan_dwell_rest[cbs->scan_offset_idx]));
				break;
			}
			if (cbs->dwell_split_cnt < cbs->max_dwell_split_cnt)
				cbs->dwell_split_cnt++;
			cbs->scan_offset_idx++;
			if (cbs->scan_offset_idx >= cbs->max_arr_size_used) {
				cbs->scan_offset_idx = 0;
				if (cbs->dwell_split_cnt >= cbs->max_dwell_split_cnt) {
					ath12k_cbs_post_event(cbs, ATH12K_CBS_EVENT_SCAN_NEXT);
					break;
				}
			}
			if (ar)
				ret = ath12k_cbs_submit_scan(ar, cbs);
			break;
		case ATH12K_CBS_STATE_REST:
			ath12k_cbs_set_state(cbs, ATH12K_CBS_STATE_SCAN);
			cbs->scan_offset_idx++;
			if (cbs->scan_offset_idx >= cbs->max_arr_size_used)
				cbs->scan_offset_idx = 0;
			if (cbs->dwell_split_cnt < cbs->max_dwell_split_cnt)
				cbs->dwell_split_cnt++;
			if (ar)
				ret = ath12k_cbs_submit_scan(ar, cbs);
			break;
		default:
			break;
		}
		break;
	case ATH12K_CBS_EVENT_SCAN_CONTINUE:
		switch (cur_state) {
		case ATH12K_CBS_STATE_REST:
			ath12k_cbs_set_state(cbs, ATH12K_CBS_STATE_SCAN);
			cbs->scan_offset_idx = 0;
			if (ar)
				ret = ath12k_cbs_submit_scan(ar, cbs);
			break;
		default:
			break;
		}
		break;
	case ATH12K_CBS_EVENT_SCAN_CANCEL:
		if (cur_state != ATH12K_CBS_STATE_INIT) {
			cbs->cancel_count++;
			ath12k_cbs_cancel(cbs, false);
		}
		break;
	case ATH12K_CBS_EVENT_SCAN_COMPLETE:
		ath12k_cbs_complete(cbs);
		break;
	case ATH12K_CBS_EVENT_SCAN_RESTART:
		if (cur_state != ATH12K_CBS_STATE_INIT) {
			ath12k_cbs_cancel(cbs, true);
			ath12k_cbs_set_state(cbs, ATH12K_CBS_STATE_WAIT);
			mod_timer(&cbs->timer,
				  jiffies + msecs_to_jiffies(ATH12K_CBS_RESTART_TIME));
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

void ath12k_cbs_scan_event_handler(struct ath12k *ar, u32 scan_id,
				   u32 event_type)
{
	struct ath12k_cbs *cbs;

	if (!ar)
		return;

	cbs = &ar->ar_extn.cbs;
	if (scan_id && cbs->scan_id && scan_id != cbs->scan_id)
		return;

	switch (event_type) {
	case WMI_SCAN_EVENT_COMPLETED:
		if (cbs->state == ATH12K_CBS_STATE_SCAN && cbs->dwell_split_cnt < cbs->max_dwell_split_cnt) {
			if (cbs->dwell_split_time) {
				mod_timer(&cbs->timer,
					  jiffies + msecs_to_jiffies(cbs->dwell_split_time));
			} else {
				ath12k_cbs_post_event(cbs, ATH12K_CBS_EVENT_DWELL_SPLIT);
			}
		} else if (cbs->state == ATH12K_CBS_STATE_SCAN) {
			ath12k_cbs_set_state(cbs, ATH12K_CBS_STATE_REST);
			mod_timer(&cbs->timer,
				  jiffies + msecs_to_jiffies(cbs->rest_time));
		}
		break;
	case WMI_SCAN_EVENT_DEQUEUED:
		ath12k_cbs_post_event(cbs, ATH12K_CBS_EVENT_SCAN_RESTART);
		break;
	default:
		break;
	}
}

static int ath12k_cbs_submit_scan(struct ath12k *ar, struct ath12k_cbs *cbs)
{
	struct ath12k_wmi_scan_req_arg *scan_arg;
	struct ath12k_link_vif *arvif;
	struct ieee80211_vif *vif;
	int ret;

	if (!ar || !cbs || !cbs->arvif)
		return -EINVAL;

	if (!cbs->max_arr_size_used)
		cbs->max_arr_size_used = 1;

	if (cbs->scan_offset_idx >= cbs->max_arr_size_used)
		return -EINVAL;

	if (!cbs->nchans || cbs->chan_list_idx >= cbs->nchans)
		return -EINVAL;

	arvif = cbs->arvif;
	if (!arvif->ahvif || !arvif->ahvif->vif)
		return -EINVAL;

	vif = arvif->ahvif->vif;
	if (!vif)
		return -EINVAL;

	scan_arg = kzalloc(sizeof(*scan_arg), GFP_KERNEL);
	if (!scan_arg)
		return -ENOMEM;

	ath12k_wmi_start_scan_init(ar, scan_arg, vif->type);
	scan_arg->scan_id = ATH12K_CBS_SCAN_ID;
	scan_arg->scan_req_id = ATH12K_CBS_SCAN_ID;
	scan_arg->vdev_id = arvif->vdev_id;
	scan_arg->pdev_id = ar->pdev ? ar->pdev->pdev_id : 0;
	scan_arg->notify_scan_events = WMI_SCAN_EVENT_STARTED | WMI_SCAN_EVENT_COMPLETED;
	scan_arg->min_rest_time = cbs->rest_time;
	scan_arg->max_rest_time = cbs->min_dwell_rest_time;
	scan_arg->idle_time = cbs->wait_time;
	scan_arg->burst_duration = cbs->cbs_total_dwell_time;
	scan_arg->extn.scan_start_offset = cbs->scan_offset[cbs->scan_offset_idx];
	scan_arg->chan_list.num_chan = 1;
	scan_arg->chan_list.chan = kcalloc(1, sizeof(struct chan_info), GFP_KERNEL);
	if (!scan_arg->chan_list.chan) {
		ret = -ENOMEM;
		goto out_free;
	}

	scan_arg->chan_list.chan[0].freq = cbs->chan_list[cbs->chan_list_idx];
	if (!scan_arg->chan_list.chan[0].freq)
		scan_arg->chan_list.chan[0].freq = 2412;
	scan_arg->chan_list.chan[0].phymode = MODE_11BE_EHT20;

	ret = ath12k_wmi_send_scan_start_cmd(ar, scan_arg);

out_free:
	kfree(scan_arg->chan_list.chan);
	kfree(scan_arg);
	return ret;
}
