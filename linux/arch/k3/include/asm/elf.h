/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/elf.h
 *
 * ELF binary format definitions and register mapping for K-Three.
 */

#ifndef _ASM_K3_ELF_H
#define _ASM_K3_ELF_H

#include <asm/ptrace.h>

#define EM_K3               0x3333  /* K-Three ELF machine type */

typedef unsigned long elf_greg_t;
#define ELF_NGREG           (sizeof(struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

#define elf_check_arch(x)   ((x)->e_machine == EM_K3)

#define ELF_CLASS           ELFCLASS32
#define ELF_DATA            ELFDATA2LSB
#define ELF_ARCH            EM_K3

#define ELF_PLATFORM        ("k3")

#endif /* _ASM_K3_ELF_H */
