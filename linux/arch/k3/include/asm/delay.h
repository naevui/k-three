/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/delay.h
 *
 * Architecture-specific busy-wait delay routines for K-Three.
 */

#ifndef _ASM_K3_DELAY_H
#define _ASM_K3_DELAY_H

static inline void __delay(unsigned long loops)
{
    while (loops--) {
        __asm__ __volatile__("" : : : "memory");
    }
}

static inline void udelay(unsigned long usecs)
{
    __delay(usecs * 10);
}

static inline void mdelay(unsigned long msecs)
{
    while (msecs--) {
        udelay(1000);
    }
}

#endif /* _ASM_K3_DELAY_H */
