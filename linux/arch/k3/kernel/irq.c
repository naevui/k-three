/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/kernel/irq.c
 *
 * Linux interrupt management and dispatch subsystem for K-Three.
 */

#include <stdio.h>
#include <string.h>

#include "vm.h"
#include "arch/k3/include/asm/irq.h"
#include "arch/k3/include/asm/ptrace.h"

typedef int (*irq_handler_t)(int irq, void *dev_id);

struct irq_action {
    irq_handler_t handler;
    void *dev_id;
    const char *name;
    unsigned long count;
    bool active;
};

static struct irq_action s_irq_actions[K3_NR_IRQS];

void init_IRQ(void)
{
    memset(s_irq_actions, 0, sizeof(s_irq_actions));

    /* Pre-register default hardware timer IRQ 0 */
    s_irq_actions[K3_IRQ_TIMER].name = "timer";
    s_irq_actions[K3_IRQ_TIMER].active = true;

    /* Pre-register syscall trap IRQ 1 */
    s_irq_actions[K3_IRQ_SYSCALL].name = "syscall";
    s_irq_actions[K3_IRQ_SYSCALL].active = true;
}

int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
                const char *name, void *dev_id)
{
    (void)flags;
    if (irq >= K3_NR_IRQS || handler == NULL) {
        return -22; /* -EINVAL */
    }

    s_irq_actions[irq].handler = handler;
    s_irq_actions[irq].dev_id = dev_id;
    s_irq_actions[irq].name = name ? name : "unnamed";
    s_irq_actions[irq].active = true;
    return 0;
}

void free_irq(unsigned int irq, void *dev_id)
{
    if (irq < K3_NR_IRQS) {
        if (s_irq_actions[irq].dev_id == dev_id) {
            s_irq_actions[irq].handler = NULL;
            s_irq_actions[irq].dev_id = NULL;
            s_irq_actions[irq].active = false;
        }
    }
}

void do_IRQ(k3_vm *vm, unsigned int irq, struct pt_regs *regs)
{
    (void)vm;
    (void)regs;
    if (irq < K3_NR_IRQS) {
        s_irq_actions[irq].count++;
        if (s_irq_actions[irq].handler != NULL) {
            s_irq_actions[irq].handler((int)irq, s_irq_actions[irq].dev_id);
        }
    }
}

unsigned long k3_get_irq_count(unsigned int irq)
{
    if (irq < K3_NR_IRQS) {
        return s_irq_actions[irq].count;
    }
    return 0;
}

const char *k3_get_irq_name(unsigned int irq)
{
    if (irq < K3_NR_IRQS && s_irq_actions[irq].name != NULL) {
        return s_irq_actions[irq].name;
    }
    return "none";
}
