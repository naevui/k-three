/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/irqflags.h
 *
 * Hardware interrupt enable, disable, and flag saving for K-Three Linux.
 */

#ifndef _ASM_K3_IRQFLAGS_H
#define _ASM_K3_IRQFLAGS_H

#include <stdbool.h>
#include <stdint.h>

#include "vm.h"

static inline void arch_local_irq_enable(k3_vm *vm)
{
    k3_vm_enable_irq(vm, true);
}

static inline void arch_local_irq_disable(k3_vm *vm)
{
    k3_vm_enable_irq(vm, false);
}

static inline unsigned long arch_local_irq_save(k3_vm *vm)
{
    unsigned long flags = k3_vm_irq_enabled(vm) ? 1UL : 0UL;
    k3_vm_enable_irq(vm, false);
    return flags;
}

static inline void arch_local_irq_restore(k3_vm *vm, unsigned long flags)
{
    k3_vm_enable_irq(vm, (flags & 1UL) != 0);
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
    return (flags & 1UL) == 0;
}

#endif /* _ASM_K3_IRQFLAGS_H */
