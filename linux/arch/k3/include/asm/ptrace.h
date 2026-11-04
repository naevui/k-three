/*
 * arch/k3/include/asm/ptrace.h
 *
 * User and kernel register context for the K-Three balanced ternary processor.
 */

#ifndef _ASM_K3_PTRACE_H
#define _ASM_K3_PTRACE_H

#include <stdint.h>

/*
 * Saved processor registers during trap / interrupt entry.
 * Conforms to the K-Three calling convention and context switch frame.
 */
struct pt_regs {
    int64_t r0;     /* Arg 0 / Return value / Syscall number */
    int64_t r1;     /* Arg 1 / Secondary return */
    int64_t r2;     /* Arg 2 */
    int64_t r3;     /* Scratch / Local */
    int64_t r4;     /* Scratch / Thread Return Stack Pointer */
    int64_t r5;     /* Scratch / Call Target Register */
    int64_t r6;     /* Link Register / Return Address */
    int64_t r7;     /* Data Stack Pointer */

    uint16_t epc;       /* Exception Program Counter (saved_pc) */
    uint16_t status;    /* CPU status flags */
    uint8_t  mode;      /* Saved privilege mode (0 = Kernel, 1 = User) */
    int32_t  orig_r0;   /* Original r0 before syscall dispatch */
};

#endif /* _ASM_K3_PTRACE_H */
