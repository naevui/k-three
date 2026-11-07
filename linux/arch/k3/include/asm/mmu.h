/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/mmu.h
 *
 * NOMMU memory context definition for K-Three.
 */

#ifndef _ASM_K3_MMU_H
#define _ASM_K3_MMU_H

typedef struct {
    unsigned long end_brk;
} mm_context_t;

#endif /* _ASM_K3_MMU_H */
