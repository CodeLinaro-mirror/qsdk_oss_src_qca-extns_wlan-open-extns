// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>

#include "../../pci.h"
#include "pci.h"


static int pci_err;

static int ath12k_wifi6_init(void)
{
	pci_err = ath12k_wifi6_pci_init();
	if (pci_err)
		pr_warn("Failed to initialize ath12k WiFi6 PCI device: %d\n",
			pci_err);

	/* Return PCI initialization result */
	return pci_err;
}

static void ath12k_wifi6_exit(void)
{
	if (!pci_err)
		ath12k_wifi6_pci_exit();
}

module_init(ath12k_wifi6_init);
module_exit(ath12k_wifi6_exit);

MODULE_DESCRIPTION("Driver support for Qualcomm Technologies 802.11ax WLAN devices");
MODULE_LICENSE("Dual BSD/GPL");
