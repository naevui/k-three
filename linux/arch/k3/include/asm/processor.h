/*
 * arch/k3/include/asm/processor.h
 *
 * Processor architecture definitions, thread structures, and privilege states.
 */

#ifndef _ASM_K3_PROCESSOR_H
#define _ASM_K3_PROCESSOR_H

#include <stdint.h>
#include "ptrace.h"

#define K3_CPU_NAME         "K-Three Balanced Ternary"
#define K3_CPU_WORD_BITS    18      /* 9 trits = 18 bits packed */
#define K3_CPU_TRITS        9
#define K3_CPU_REG_COUNT    8

/* Hardware privilege levels */
#define K3_MODE_KERNEL      0
#define K3_MODE_USER        1

/* Architecture thread context saved during switch_to() */
struct thread_struct {
    int64_t ksp;            /* Kernel stack pointer (r7) */
    int64_t krsp;           /* Kernel return stack pointer (r4) */
    uint16_t pc;            /* Program counter */
    uint8_t  mode;          /* Execution mode */
    struct pt_regs *regs;   /* Pointer to user registers on kernel stack */
};

#endif /* _ASM_K3_PROCESSOR_H */
