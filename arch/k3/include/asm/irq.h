/*
 * arch/k3/include/asm/irq.h
 *
 * Hardware interrupts, exceptions, and trap vector definitions.
 */

#ifndef _ASM_K3_IRQ_H
#define _ASM_K3_IRQ_H

#define K3_IRQ_TIMER        0   /* Periodic hardware timer tick */
#define K3_IRQ_SYSCALL      1   /* User syscall trap (TRAP #code) */
#define K3_IRQ_FAULT_MEM    2   /* Memory bounds / fetch fault */
#define K3_IRQ_FAULT_ILL    3   /* Illegal or privileged instruction in user mode */
#define K3_IRQ_FAULT_OVF    4   /* Arithmetic overflow trap */

#define K3_NR_IRQS          8

#endif /* _ASM_K3_IRQ_H */
