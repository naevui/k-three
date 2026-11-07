/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/cmpxchg.h
 *
 * Atomic compare-and-swap (cmpxchg) primitives for K-Three.
 */

#ifndef _ASM_K3_CMPXCHG_H
#define _ASM_K3_CMPXCHG_H

#include <asm/irqflags.h>

static inline unsigned long __k3_cmpxchg(volatile void *ptr, unsigned long old,
                                        unsigned long new, int size)
{
    unsigned long flags;
    unsigned long prev;

    (void)size;
    local_irq_save(flags);
    prev = *(volatile unsigned long *)ptr;
    if (prev == old) {
        *(volatile unsigned long *)ptr = new;
    }
    local_irq_restore(flags);
    return prev;
}

#define cmpxchg(ptr, o, n) \
    __k3_cmpxchg((ptr), (unsigned long)(o), (unsigned long)(n), sizeof(*(ptr)))

#endif /* _ASM_K3_CMPXCHG_H */
