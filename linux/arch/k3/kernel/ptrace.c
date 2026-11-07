/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/kernel/ptrace.c
 *
 * Process tracing (ptrace) support for K-Three.
 */

#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <asm/ptrace.h>

long arch_ptrace(struct task_struct *child, long request,
                 unsigned long addr, unsigned long data)
{
    (void)child;
    (void)request;
    (void)addr;
    (void)data;
    return -EIO;
}
