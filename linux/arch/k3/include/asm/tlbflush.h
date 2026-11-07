/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/tlbflush.h
 *
 * TLB flushing routines for K-Three.
 *
 * In flat NOMMU K-Three, hardware uses physical addresses; TLB flushes are no-ops.
 */

#ifndef _ASM_K3_TLBFLUSH_H
#define _ASM_K3_TLBFLUSH_H

#define flush_tlb()                     do { } while (0)
#define flush_tlb_all()                 do { } while (0)
#define flush_tlb_mm(mm)                do { } while (0)
#define flush_tlb_range(vma, start, end) do { } while (0)
#define flush_tlb_page(vma, vmaddr)     do { } while (0)
#define flush_tlb_kernel_range(start, end) do { } while (0)

#endif /* _ASM_K3_TLBFLUSH_H */
