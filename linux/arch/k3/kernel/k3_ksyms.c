/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/kernel/k3_ksyms.c
 *
 * Exported architecture symbols for K-Three modules.
 */

#include <linux/export.h>
#include <linux/string.h>
#include <asm/checksum.h>
#include <asm/uaccess.h>

/* Export core string and memory primitives */
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strcmp);
