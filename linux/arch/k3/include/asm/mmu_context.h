/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/mmu_context.h
 *
 * NOMMU MMU context management operations for K-Three.
 */

#ifndef _ASM_K3_MMU_CONTEXT_H
#define _ASM_K3_MMU_CONTEXT_H

#include <linux/mm_types.h>
#include <asm-generic/mm_hooks.h>

#define init_new_context(tsk, mm)   0
#define destroy_context(mm)         do { } while (0)
#define activate_mm(prev, next)     do { } while (0)
#define switch_mm(prev, next, tsk)  do { } while (0)
#define deactivate_mm(tsk, mm)      do { } while (0)
#define enter_lazy_tlb(mm, tsk)     do { } while (0)

#endif /* _ASM_K3_MMU_CONTEXT_H */
