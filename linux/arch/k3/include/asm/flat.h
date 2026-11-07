/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/flat.h
 *
 * uClinux BFLT (Binary Flat Format) binary execution support for K-Three.
 */

#ifndef _ASM_K3_FLAT_H
#define _ASM_K3_FLAT_H

#define flat_argvp_envp_on_stack()              1
#define flat_old_ram_flag(flags)                (flags)
#define flat_reloc_valid(reloc, size)           ((reloc) <= (size))
#define flat_set_persistent(relval, p)          0
#define flat_get_relocate_addr(rel)             (rel)
#define flat_get_addr_from_rp(rp, relval, flags, p) get_unaligned(rp)
#define flat_put_addr_at_rp(rp, addr, relval)   put_unaligned((addr), (rp))

#endif /* _ASM_K3_FLAT_H */
