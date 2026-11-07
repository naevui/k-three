/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/syscall.h
 *
 * System call tracing, invocation, and register query helpers for K-Three.
 */

#ifndef _ASM_K3_SYSCALL_H
#define _ASM_K3_SYSCALL_H

#include <uapi/linux/audit.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <asm/ptrace.h>

static inline int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
    (void)task;
    return regs->r0;
}

static inline void syscall_rollback(struct task_struct *task, struct pt_regs *regs)
{
    (void)task;
    (void)regs;
}

static inline long syscall_get_error(struct task_struct *task, struct pt_regs *regs)
{
    (void)task;
    return IS_ERR_VALUE((unsigned long)regs->r0) ? regs->r0 : 0;
}

static inline long syscall_get_return_value(struct task_struct *task, struct pt_regs *regs)
{
    (void)task;
    return regs->r0;
}

static inline void syscall_set_return_value(struct task_struct *task,
                                           struct pt_regs *regs,
                                           int error, long val)
{
    (void)task;
    regs->r0 = (error ? error : val);
}

#endif /* _ASM_K3_SYSCALL_H */
