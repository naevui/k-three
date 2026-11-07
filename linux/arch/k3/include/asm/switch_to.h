/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/switch_to.h
 *
 * Process context switching definitions and primitives for K-Three.
 */

#ifndef _ASM_K3_SWITCH_TO_H
#define _ASM_K3_SWITCH_TO_H

struct task_struct;

extern struct task_struct *__switch_to(struct task_struct *prev,
                                       struct task_struct *next);

#define switch_to(prev, next, last) \
    do { \
        ((last) = __switch_to((prev), (next))); \
    } while (0)

#endif /* _ASM_K3_SWITCH_TO_H */
