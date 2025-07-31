/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef VENDOR_EXTN_H
#define VENDOR_EXTN_H

#include "../vendor.h"

extern const struct nla_policy
ath12k_240mhz_sta_info_policy[QCA_WLAN_VENDOR_ATTR_240MHZ_MAX + 1];


int ath12k_vendor_get_sta_240mhz_info(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data,
				      int data_len);

#endif /* VENDOR_EXTN_H */
