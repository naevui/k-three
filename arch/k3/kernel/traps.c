/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/kernel/traps.c
 *
 * Architecture trap and exception handler, register dump, and signal dispatch.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "vm.h"
#include "arch/k3/include/asm/ptrace.h"

#define SIGILL          4
#define SIGTRAP         5
#define SIGFPE          8
#define SIGSEGV         11

void k3_show_registers(const struct pt_regs *regs)
{
    printf("CPU Register State:\n");
    printf("  r0: %04lld  r1: %04lld  r2: %04lld  r3: %04lld\n",
           (long long)regs->r0, (long long)regs->r1,
           (long long)regs->r2, (long long)regs->r3);
    printf("  r4: %04lld  r5: %04lld  r6: %04lld  r7: %04lld\n",
           (long long)regs->r4, (long long)regs->r5,
           (long long)regs->r6, (long long)regs->r7);
    printf("  EPC: 0x%04x  Status: 0x%04x  Mode: %s\n",
           (unsigned)regs->epc, (unsigned)regs->status,
           regs->mode == 0 ? "KERNEL" : "USER");
    fflush(stdout);
}

int k3_trap_to_signal(k3_trap_reason reason)
{
    switch (reason) {
    case K3_TRAP_OVERFLOW:
    case K3_TRAP_SHIFT:
        return SIGFPE;
    case K3_TRAP_MEMORY:
        return SIGSEGV;
    case K3_TRAP_ILLEGAL:
        return SIGILL;
    case K3_TRAP_EXPLICIT:
        return SIGTRAP;
    default:
        return SIGSEGV;
    }
}

void k3_kernel_die(const char *str, const struct pt_regs *regs, long err)
{
    printf("\n\033[1;31m[kernel panic] %s (error code: %ld)\033[0m\n", str, err);
    if (regs != NULL) {
        k3_show_registers(regs);
    }
    fflush(stdout);
}
