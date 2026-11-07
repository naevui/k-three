/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/timex.h
 *
 * Architecture-specific clock and cycle counter support for K-Three.
 */

#ifndef _ASM_K3_TIMEX_H
#define _ASM_K3_TIMEX_H

#include <asm/processor.h>

#define CLOCK_TICK_RATE 100 /* 100 Hz timer tick */

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
    /* Return monotonic ticks count */
    return 0;
}

#endif /* _ASM_K3_TIMEX_H */
