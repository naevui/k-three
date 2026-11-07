/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/bug.h
 *
 * Architecture BUG() and WARN() diagnostics for K-Three.
 */

#ifndef _ASM_K3_BUG_H
#define _ASM_K3_BUG_H

#include <linux/compiler.h>

#define BUG() do { \
    __builtin_trap(); \
    unreachable(); \
} while (0)

#define HAVE_ARCH_BUG

#include <asm-generic/bug.h>

#endif /* _ASM_K3_BUG_H */
