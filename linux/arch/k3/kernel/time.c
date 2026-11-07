/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/kernel/time.c
 *
 * Architecture-specific clocksource and timer initialization for K-Three.
 */

#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched_clock.h>

void __init time_init(void)
{
    /* Initialize 100 Hz K-Three MMIO hardware timer */
}
