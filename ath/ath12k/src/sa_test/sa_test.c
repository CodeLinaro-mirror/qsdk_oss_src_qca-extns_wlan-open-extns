/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/if_ether.h>
#include "sa_test.h"

/*
 * These extern declarations are intentional: sa_test is a standalone
 * loadable module and must not include ath12k's internal headers.
 * The symbols are provided by the ath12k module at runtime.
 */
extern int ath12k_register_smart_ant_ops(struct ath12k_smart_ant_ops *ops);
extern int ath12k_unregister_smart_ant_ops(void);

static int __init sa_test_init(void)
{
	pr_info("SA Test module Init");
	return 0;
}

static void __exit sa_test_exit(void)
{
	pr_info("SA Test module Exit");
}

module_init(sa_test_init);
module_exit(sa_test_exit);

MODULE_DESCRIPTION("Test module for Qualcomm Technologies Smart Antenna feature");
MODULE_LICENSE("Dual BSD/GPL");
