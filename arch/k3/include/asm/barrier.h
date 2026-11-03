/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/barrier.h
 *
 * Memory barrier and synchronization primitives for K-Three Linux.
 */

#ifndef _ASM_K3_BARRIER_H
#define _ASM_K3_BARRIER_H

/*
 * The K-Three architecture is strongly ordered: instruction fetch and
 * memory loads/stores execute sequentially in program order with no
 * out-of-order execution or write buffers.
 * Compiler barriers prevent compiler instruction reordering.
 */
#define barrier()               __asm__ __volatile__("" : : : "memory")

#define mb()                    barrier()
#define rmb()                   barrier()
#define wmb()                   barrier()

#define smp_mb()                barrier()
#define smp_rmb()               barrier()
#define smp_wmb()               barrier()

#endif /* _ASM_K3_BARRIER_H */
