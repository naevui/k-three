/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/uaccess.h
 *
 * User space memory access and hardware memory protection validation.
 */

#ifndef _ASM_K3_UACCESS_H
#define _ASM_K3_UACCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vm.h"
#include "arch/k3/include/asm/page.h"

#define EFAULT          14

/*
 * Check if a user address range [addr .. addr + len - 1] is valid
 * within the configured hardware user space bounds.
 */
static inline bool access_ok(const k3_vm *vm, uint16_t addr, size_t len)
{
    if (vm == NULL) return false;
    uint16_t base = 0, limit = K3_VM_MAX_ADDRESS;
    k3_vm_get_user_bounds(vm, &base, &limit);

    if (addr < base) return false;
    if ((size_t)addr + len > (size_t)limit + 1) return false;
    return true;
}

/*
 * Copy data from user space memory to kernel memory.
 * Returns 0 on success, or -EFAULT on memory boundary violation.
 */
static inline long copy_from_user(k3_vm *vm, void *to, uint16_t from_user_addr, size_t words)
{
    if (!access_ok(vm, from_user_addr, words)) {
        return -EFAULT;
    }
    for (size_t i = 0; i < words; ++i) {
        bt_word w;
        if (!k3_vm_get_mem(vm, (uint16_t)(from_user_addr + i), &w)) {
            return -EFAULT;
        }
        ((bt_word *)to)[i] = w;
    }
    return 0;
}

/*
 * Copy data from kernel memory to user space memory.
 * Returns 0 on success, or -EFAULT on memory boundary violation.
 */
static inline long copy_to_user(k3_vm *vm, uint16_t to_user_addr, const void *from, size_t words)
{
    if (!access_ok(vm, to_user_addr, words)) {
        return -EFAULT;
    }
    for (size_t i = 0; i < words; ++i) {
        const bt_word *w = &((const bt_word *)from)[i];
        if (!k3_vm_set_mem(vm, (uint16_t)(to_user_addr + i), w)) {
            return -EFAULT;
        }
    }
    return 0;
}

#endif /* _ASM_K3_UACCESS_H */
