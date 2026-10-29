/*
 * arch/k3/include/asm/page.h
 *
 * Ternary page definitions for K-Three Linux / NOMMU memory model.
 */

#ifndef _ASM_K3_PAGE_H
#define _ASM_K3_PAGE_H

/*
 * Ternary Page Size: 3^6 = 729 words.
 *
 * In 2187-word memory:
 *   Page 0: 0000..0728 (Kernel Code & Drivers)
 *   Page 1: 0729..1457 (Kernel Data & Kernel Stack)
 *   Page 2: 1458..2186 (Userspace Process Space)
 */
#define K3_PAGE_SHIFT       6
#define K3_PAGE_SIZE        729     /* 3^6 words */
#define K3_PAGE_MASK        (~(K3_PAGE_SIZE - 1))

#define K3_TOTAL_PAGES      (K3_VM_MEMORY_WORDS / K3_PAGE_SIZE) /* 3 pages */

#define PAGE_ALIGN(addr)    (((addr) + K3_PAGE_SIZE - 1) & K3_PAGE_MASK)

#endif /* _ASM_K3_PAGE_H */
