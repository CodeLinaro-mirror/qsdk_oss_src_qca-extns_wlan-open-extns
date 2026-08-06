/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_ipa.h"
#include "../../../qcn_extns/ipa/dp_ipa.h"
#include "../../hal.h"
#include "../../dp.h"

/* Read IPA2TCL ring HP from MMIO — IPA advances this register each time it
 * enqueues a TX descriptor into the PPE2TCL ring.
 */
static u32 ath12k_ipa_cc_read_ipa2tcl_hp(struct ath12k_base *ab)
{
	return ath12k_hif_read32(ab,
				 HAL_SEQ_WCSS_UMAC_TCL_REG +
				 HAL_TCL_PPE2TCL1_RING_HP);
}

/* Read TQM2IPA ring HP from the doorbell shadow.
 * TQM DMA-writes an updated HP here on each TX completion.
 * Returns 0 if the doorbell has not been mapped yet.
 */
static u32 ath12k_ipa_cc_read_tqm2ipa_hp(struct ath12k_ipa *ipa_ctx)
{
	if (!ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_vaddr)
		return 0;
	return READ_ONCE(*ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_vaddr);
}

/* Ring-pointer delta with wraparound (ring_size is the srng ring_size field). */
static u32 ath12k_ipa_cc_ring_delta(u32 new_val, u32 old_val, u32 ring_size)
{
	return (new_val - old_val + ring_size) % ring_size;
}

/* Convert a raw srng offset delta to a buffer entry count. */
static u32 ath12k_ipa_cc_to_entries(u32 raw_delta, u32 entry_size)
{
	return entry_size ? raw_delta / entry_size : 0;
}

/**
 * ath12k_ipa_cc_check - IPA TX buffer congestion check
 * @ab: ath12k base structure
 *
 * Called from ath12k_wifi8_dp_tx_congestion_recovery_handler() on every
 * 100 ms tick.  Samples IPA2TCL HP and TQM2IPA HP, updates the running
 * in-flight buffer count, and halts or resumes PPE2TCL ring based on
 * the 80% buffer-utilisation threshold.
 *
 * Safe to call in softirq/timer context.
 *
 * Returns: number of buffers to be dropped from MSDU/MPDU queue.
 */
u32 ath12k_ipa_cc_check(struct ath12k_base *ab)
{
	struct ath12k_ipa *ipa_ctx = IPA_CTX(ab);
	struct ath12k_ipa_cc *cc;
	u32 ipa2tcl_hp, tqm2ipa_hp;
	u32 d_sends, d_returns;
	u32 used;
	u32 target_drop = 0;

	if (!ipa_ctx)
		return 0;

	cc = &ipa_ctx->cc;

	/* Skip until pipes are up and doorbell is mapped */
	if (!atomic_read(&ipa_ctx->ipa_pipes_enabled) ||
	    !ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_vaddr)
		return 0;

	ipa2tcl_hp = ath12k_ipa_cc_read_ipa2tcl_hp(ab);
	tqm2ipa_hp = ath12k_ipa_cc_read_tqm2ipa_hp(ipa_ctx);

	/* Compute buffer-count deltas since the previous tick */
	d_sends   = ath12k_ipa_cc_to_entries(
			ath12k_ipa_cc_ring_delta(ipa2tcl_hp, cc->ipa2tcl_hp_prev,
						 cc->ipa2tcl_ring_size),
			cc->ipa2tcl_entry_size);

	d_returns = ath12k_ipa_cc_to_entries(
			ath12k_ipa_cc_ring_delta(tqm2ipa_hp, cc->tqm2ipa_hp_prev,
						 cc->tqm2ipa_ring_size),
			cc->tqm2ipa_entry_size);

	cc->ipa2tcl_hp_prev = ipa2tcl_hp;
	cc->tqm2ipa_hp_prev = tqm2ipa_hp;

	/*
	 * Idle detection: if both HPs haven't moved, no traffic is flowing.
	 * After ATH12K_IPA_CC_IDLE_RESET_TICKS consecutive idle ticks the
	 * pipeline must be drained — reset inflight to zero to correct any
	 * accumulated delta drift that would otherwise leave the counter
	 * stuck above threshold forever.
	 */
	if (d_sends == 0 && d_returns == 0) {
		cc->idle_ticks++;
		if (cc->idle_ticks >= ATH12K_IPA_CC_IDLE_RESET_TICKS &&
		    cc->inflight > 0) {
			ath12k_ipa_dbg(ab,
				       "IPA CC: idle reset inflight %d->0 after %u ticks",
				       cc->inflight, cc->idle_ticks);
			cc->inflight = 0;
			cc->idle_resets++;
		}
	} else {
		cc->idle_ticks = 0;
	}

	/* Update running in-flight count; clamp to [0, total] */
	cc->inflight += (s32)(d_sends - d_returns);
	cc->inflight  = clamp_t(s32, cc->inflight, 0, (s32)cc->total_tx_bufs);

	used = (u32)cc->inflight;

	if (used > cc->threshold)
		target_drop = used - cc->threshold;
	else
		target_drop = 0;

	if (!atomic_read(&cc->congested)) {
		if (used > cc->threshold) {
			atomic_set(&cc->congested, 1);
			cc->congestion_events++;
			ath12k_ipa_dbg(ab,
				       "IPA CC: congestion inflight=%u/%u events=%llu",
				       used, cc->total_tx_bufs,
				       cc->congestion_events);
		}
	} else {
		if (used <= cc->threshold) {
			atomic_set(&cc->congested, 0);
			cc->recovery_events++;
			ath12k_ipa_dbg(ab,
				       "IPA CC: recovered inflight=%u/%u events=%llu",
				       used, cc->total_tx_bufs,
				       cc->recovery_events);
		}
	}

	return target_drop;
}

/**
 * ath12k_ipa_cc_init - initialise IPA TX congestion control
 * @ab: ath12k base structure
 *
 * Called after ath12k_wifi8_dp_ipa_tx_buf_alloc() sets alloc_tx_buf_cnt.
 * Snapshots ring parameters from the HAL srng structures, computes the
 * 80% inflight threshold, and registers ath12k_ipa_cc_check() as the
 * IPA callback inside the existing congestion-control timer struct.
 *
 * If the TX descriptor congestion-control timer is not running
 * (ath12k_congestion_ctrl module param not set), the registration is
 * skipped and IPA CC will be a no-op for this session.
 */
void ath12k_ipa_cc_init(struct ath12k_base *ab)
{
	struct ath12k_ipa *ipa_ctx = IPA_CTX(ab);
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_wifi8 *dp_wifi8;
	struct ath12k_dp_wifi8_ipa *dp_wifi8_ipa;
	struct ath12k_dp_hw_group_wifi8 *dp_hw_grp_wifi8;
	struct ath12k_ipa_cc *cc;
	struct hal_srng *ipa2tcl_srng, *tqm2ipa_srng;
	u32 total;

	if (!ipa_ctx || !dp)
		return;

	dp_wifi8 = ath12k_get_dp_wifi8(dp);
	if (!dp_wifi8)
		return;

	dp_wifi8_ipa = ath12k_wifi8_get_ipa_dp(dp_wifi8);
	if (!dp_wifi8_ipa)
		return;

	total = ipa_ctx->ipa_uc_tx_rsc.alloc_tx_buf_cnt;
	if (!total) {
		ath12k_ipa_err(ab, "IPA CC: no TX buffers allocated");
		return;
	}

	cc = &ipa_ctx->cc;
	memset(cc, 0, sizeof(*cc));

	/* Snapshot ring parameters from HAL srng */
	cc->ipa2tcl_ring_id   = dp_wifi8_ipa->ipa2tcl_ring.ring_id;
	ipa2tcl_srng          = &ab->hal.srng_list[cc->ipa2tcl_ring_id];
	cc->ipa2tcl_entry_size = ipa2tcl_srng->entry_size;
	cc->ipa2tcl_ring_size  = ipa2tcl_srng->ring_size;

	tqm2ipa_srng          = &ab->hal.srng_list[dp_wifi8_ipa->tqm2ipa_ring.ring_id];
	cc->tqm2ipa_entry_size = tqm2ipa_srng->entry_size;
	cc->tqm2ipa_ring_size  = tqm2ipa_srng->ring_size;

	cc->total_tx_bufs = total;
	cc->threshold     = total * ATH12K_IPA_CC_BUF_USED_THRESHOLD_PCT / 100;
	atomic_set(&cc->congested, 0);

	/*
	 * Register the IPA check callback inside the existing
	 * congestion-control timer.  If the timer is not initialised
	 * (ath12k_congestion_ctrl = 0), skip — IPA CC requires the
	 * congestion timer to be running.
	 */
	dp_hw_grp_wifi8 = ath12k_get_dp_hw_group_wifi8(dp->dp_hw_grp);
	if (dp_hw_grp_wifi8 && dp_hw_grp_wifi8->congstn.init) {
		dp_hw_grp_wifi8->congstn.ipa_cc_check = ath12k_ipa_cc_check;
		ath12k_ipa_info(ab,
				"IPA CC: registered total=%u threshold=%u(%d%%)",
				total, cc->threshold,
				ATH12K_IPA_CC_BUF_USED_THRESHOLD_PCT);
	} else {
		ath12k_ipa_warn(ab,
				"IPA CC: congstn timer not running — "
				"IPA CC disabled (enable ath12k_congestion_ctrl)");
	}
}

/**
 * ath12k_ipa_cc_deinit - tear down IPA TX congestion control
 * @ab: ath12k base structure
 *
 * Clears the IPA callback from the congestion-control timer struct and
 * ensures PPE2TCL is left un-halted.
 */
void ath12k_ipa_cc_deinit(struct ath12k_base *ab)
{
	struct ath12k_ipa *ipa_ctx = IPA_CTX(ab);
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_hw_group_wifi8 *dp_hw_grp_wifi8;
	struct ath12k_ipa_cc *cc;

	if (!ipa_ctx)
		return;

	cc = &ipa_ctx->cc;

	/* Unregister callback before touching the ring */
	if (dp) {
		dp_hw_grp_wifi8 = ath12k_get_dp_hw_group_wifi8(dp->dp_hw_grp);
		if (dp_hw_grp_wifi8)
			dp_hw_grp_wifi8->congstn.ipa_cc_check = NULL;
	}

	/* Leave ring un-halted on teardown */
	if (atomic_read(&cc->congested)) {
		ath12k_wifi8_hal_tx_ppe2tcl_ring_halt_reset(ab);
		atomic_set(&cc->congested, 0);
	}

	ath12k_ipa_info(ab,
			"IPA CC: deinit congestion=%llu recovery=%llu",
			cc->congestion_events, cc->recovery_events);
}

/**
 * ath12k_ipa_cc_dump_stats - dump IPA TX congestion control stats
 * @ab: ath12k base structure
 * @buf: output buffer
 * @size: remaining bytes in buffer
 *
 * Appends IPA buffer pool and congestion control statistics to the
 * congestion_ctrl_stats debugfs output.  Reads live ring HPs so the
 * numbers reflect the state at the moment the file is read.
 *
 * Returns: number of characters written.
 */
ssize_t ath12k_ipa_cc_dump_stats(struct ath12k_base *ab, char *buf, int size)
{
	struct ath12k_ipa *ipa_ctx = IPA_CTX(ab);
	struct ath12k_ipa_cc *cc;
	u32 ipa2tcl_hp, tqm2ipa_hp;
	u32 ipa2tcl_entries, tqm2ipa_entries;
	int len = 0;

	if (!ipa_ctx)
		return 0;

	cc = &ipa_ctx->cc;

	if (!cc->total_tx_bufs) {
		len += scnprintf(buf + len, size - len,
				 "\nIPA Congestion Control: not initialised\n");
		return len;
	}

	/* Read live HP values */
	ipa2tcl_hp = ath12k_ipa_cc_read_ipa2tcl_hp(ab);
	tqm2ipa_hp = ath12k_ipa_cc_read_tqm2ipa_hp(ipa_ctx);

	/* Convert ring sizes from bytes to entry counts */
	ipa2tcl_entries = cc->ipa2tcl_entry_size ?
			  cc->ipa2tcl_ring_size / cc->ipa2tcl_entry_size : 0;
	tqm2ipa_entries = cc->tqm2ipa_entry_size ?
			  cc->tqm2ipa_ring_size / cc->tqm2ipa_entry_size : 0;

	len += scnprintf(buf + len, size - len,
			 "\nIPA Congestion Control:\n");
	len += scnprintf(buf + len, size - len,
			 "  total_tx_bufs:           %u\n",
			 cc->total_tx_bufs);
	len += scnprintf(buf + len, size - len,
			 "  threshold (%u%%):         %u\n",
			 ATH12K_IPA_CC_BUF_USED_THRESHOLD_PCT,
			 cc->threshold);
	len += scnprintf(buf + len, size - len,
			 "  inflight:                %d\n",
			 cc->inflight);
	len += scnprintf(buf + len, size - len,
			 "  congested:               %s\n",
			 atomic_read(&cc->congested) ? "YES" : "no");
	len += scnprintf(buf + len, size - len,
			 "  congestion_events:       %llu\n",
			 cc->congestion_events);
	len += scnprintf(buf + len, size - len,
			 "  recovery_events:         %llu\n",
			 cc->recovery_events);
	len += scnprintf(buf + len, size - len,
			 "  idle_ticks:              %u\n",
			 cc->idle_ticks);
	len += scnprintf(buf + len, size - len,
			 "  idle_resets:             %llu\n",
			 cc->idle_resets);

	len += scnprintf(buf + len, size - len,
			 "  IPA2TCL (PPE2TCL) ring:\n");
	len += scnprintf(buf + len, size - len,
			 "    ring_id:               %u\n",
			 cc->ipa2tcl_ring_id);
	len += scnprintf(buf + len, size - len,
			 "    ring_entries:           %u\n",
			 ipa2tcl_entries);
	len += scnprintf(buf + len, size - len,
			 "    hp (live):             0x%08x\n",
			 ipa2tcl_hp);
	len += scnprintf(buf + len, size - len,
			 "    hp (last tick):        0x%08x\n",
			 cc->ipa2tcl_hp_prev);

	len += scnprintf(buf + len, size - len,
			 "  TQM2IPA (TQM2PPE) ring:\n");
	len += scnprintf(buf + len, size - len,
			 "    ring_entries:           %u\n",
			 tqm2ipa_entries);
	len += scnprintf(buf + len, size - len,
			 "    hp (live):             0x%08x\n",
			 tqm2ipa_hp);
	len += scnprintf(buf + len, size - len,
			 "    hp (last tick):        0x%08x\n",
			 cc->tqm2ipa_hp_prev);

	len += scnprintf(buf + len, size - len,
			 "  pipes_enabled:           %s\n",
			 atomic_read(&ipa_ctx->ipa_pipes_enabled) ? "yes" : "no");
	len += scnprintf(buf + len, size - len,
			 "  doorbell_mapped:         %s\n",
			 ipa_ctx->ipa_uc_tx_rsc.tx_comp_doorbell_vaddr ?
			 "yes" : "no");
	len += scnprintf(buf + len, size - len,
			 "  ppe2tcl_ring_halted:     %s\n",
			 ath12k_wifi8_hal_tx_ppe2tcl_ring_halt_get(ab) ?
			 "YES" : "no");

	return len;
}
