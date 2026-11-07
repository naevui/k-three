/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/futex.h
 *
 * Fast Userspace Locking (futex) architecture support for K-Three.
 */

#ifndef _ASM_K3_FUTEX_H
#define _ASM_K3_FUTEX_H

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

static inline int arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval,
                                              u32 __user *uaddr)
{
    int oldval = 0, ret = 0;

    (void)op;
    (void)oparg;
    (void)uaddr;
    *oval = oldval;
    return ret;
}

static inline int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
                                                u32 oldval, u32 newval)
{
    (void)uaddr;
    (void)newval;
    *uval = oldval;
    return 0;
}

#endif /* _ASM_K3_FUTEX_H */
