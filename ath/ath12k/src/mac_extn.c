// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include "../core.h"
#include "../debug.h"
#include "ini.h"

static enum ath12k_band_id ath12k_mac_band_id_of_extn(struct ath12k_base *ab, u8 idx)
{
	struct ath12k_pdev *pdev = &ab->pdevs[idx];
	struct ath12k_wmi_hal_reg_capabilities_ext_arg *hal_reg_cap;

	hal_reg_cap = &ab->hal_reg_cap[idx];
	switch (pdev->cap.supported_bands) {
	case WMI_HOST_WLAN_2GHZ_5GHZ_CAP:
		return ATH12K_BAND_ID_2GHZ_5GHZ;
	case WMI_HOST_WLAN_2GHZ_CAP:
		return ATH12K_BAND_ID_2GHZ;
	case WMI_HOST_WLAN_5GHZ_CAP:
		if (hal_reg_cap->high_5ghz_chan <= ATH12K_MIN_5G_HIGH_BAND_FREQ)
			return ATH12K_BAND_ID_5GHZ_LOW;
		if (hal_reg_cap->low_5ghz_chan >= ATH12K_MAX_5G_LOW_BAND_FREQ &&
		    hal_reg_cap->high_5ghz_chan <= ATH12K_MIN_6GHZ_FREQ)
			return ATH12K_BAND_ID_5GHZ_HIGH;
		if (hal_reg_cap->low_5ghz_chan >= ATH12K_MIN_5GHZ_FREQ &&
		    hal_reg_cap->high_5ghz_chan <= ATH12K_MAX_5GHZ_FREQ)
			return ATH12K_BAND_ID_5GHZ;
		if (hal_reg_cap->low_5ghz_chan >= ATH12K_MIN_6GHZ_FREQ &&
		    hal_reg_cap->high_5ghz_chan <= ATH12K_MAX_6G_LOW_BAND_FREQ)
			return ATH12K_BAND_ID_6GHZ_LOW;
		if (hal_reg_cap->low_5ghz_chan >= ATH12K_MAX_6G_LOW_BAND_FREQ &&
		    hal_reg_cap->high_5ghz_chan <= ATH12K_MAX_6GHZ_FREQ)
			return ATH12K_BAND_ID_6GHZ_HIGH;
		if (hal_reg_cap->low_5ghz_chan >= ATH12K_MIN_6GHZ_FREQ &&
		    hal_reg_cap->high_5ghz_chan <= ATH12K_MAX_6GHZ_FREQ)
			return ATH12K_BAND_ID_6GHZ;
		return ATH12K_BAND_ID_5GHZ;
	default:
		return ATH12K_BAND_ID_INVALID;
	}
}

void ath12k_mac_build_radio_order_extn(struct ath12k_base *ab)
{
	struct ath12k_base_extn *abe = &ab->ath12k_base_extn;
	u8 serving_idx[MAX_RADIOS];
	u8 chosen_idx[MAX_RADIOS];
	u8 n_serving = 0;
	u8 placed_idx = 0;
	int i, k;

	abe->radio_order_raw = ath12k_cfg_get(ab, ATH12K_CFG_RADIO_ORDER);
	abe->radio_order_valid = false;

	if (!abe->radio_order_raw)
		return;

	/* seed identity; scan-radio slots are never permuted below */
	for (i = 0; i < ab->num_radios; i++)
		abe->radio_order[i] = i;

	for (i = 0; i < ab->num_radios; i++) {
		if (!ath12k_scan_radio_supported(&ab->pdevs[i]))
			serving_idx[n_serving++] = i;
	}

	if (n_serving < 2)
		return;

	for (k = 0; k < n_serving; k++) {
		u32 shift = k * ATH12K_RADIO_ORDER_SLOT_SHIFT;
		u32 band_id = u32_get_bits(abe->radio_order_raw >> shift,
					   ATH12K_RADIO_ORDER_SLOT_MASK);
		int match = -1;

		if (!band_id) {
			ath12k_warn(ab,
				    "radio_order: slot %d has no band id (raw=0x%x), reverting to default order\n",
				    k, abe->radio_order_raw);
			return;
		}

		for (i = 0; i < n_serving; i++) {
			if (ath12k_mac_band_id_of_extn(ab, serving_idx[i]) ==
			    band_id) {
				match = serving_idx[i];
				break;
			}
		}

		if (match < 0 || (placed_idx & BIT(match))) {
			ath12k_warn(ab,
				    "radio_order: invalid/duplicate band id %u at slot %d (raw=0x%x), reverting to default order\n",
				    band_id, k, abe->radio_order_raw);
			return;
		}

		placed_idx |= BIT(match);
		chosen_idx[k] = match;
	}

	if (hweight8(placed_idx) != n_serving) {
		ath12k_warn(ab,
			    "radio_order: only %u of %u serving radios placed (raw=0x%x), reverting to default order\n",
			    hweight8(placed_idx), n_serving, abe->radio_order_raw);
		return;
	}

	for (k = 0; k < n_serving; k++)
		abe->radio_order[serving_idx[k]] = chosen_idx[k];

	abe->radio_order_valid = true;
}

u8 ath12k_mac_get_radio_idx_extn(struct ath12k_base *ab, u8 local_idx)
{
	struct ath12k_base_extn *abe = &ab->ath12k_base_extn;

	if (!abe->radio_order_valid || local_idx >= ab->num_radios)
		return local_idx;

	return abe->radio_order[local_idx];
}
