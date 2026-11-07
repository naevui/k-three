/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/pgtable.h
 *
 * Page table definitions for K-Three (NOMMU / Flat Linear Architecture).
 */

#ifndef _ASM_K3_PGTABLE_H
#define _ASM_K3_PGTABLE_H

#include <asm-generic/pgtable-nopud.h>
#include <asm-generic/pgtable-nop4d.h>

#define PAGE_NONE       __pgprot(0)
#define PAGE_SHARED     __pgprot(0)
#define PAGE_COPY       __pgprot(0)
#define PAGE_READONLY   __pgprot(0)
#define PAGE_KERNEL     __pgprot(0)

#define pgprot_noncached(prot) (prot)

typedef struct { unsigned long pgprot; } pgprot_t;
#define pgprot_val(x)   ((x).pgprot)
#define __pgprot(x)     ((pgprot_t) { (x) })

#define swapper_pg_dir  ((pgd_t *)0)

#include <asm-generic/pgtable.h>

#endif /* _ASM_K3_PGTABLE_H */
