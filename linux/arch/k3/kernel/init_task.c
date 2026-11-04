/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/kernel/init_task.c
 *
 * Initial task (Swapper/Idle PID 0) definition for K-Three Linux.
 */

#include "arch/k3/include/asm/processor.h"
#include "arch/k3/include/asm/thread_info.h"

/*
 * Static initial task structure conforming to Linux kernel standard.
 */
static struct thread_struct init_thread = {
    .ksp = 6560,
    .krsp = 6000,
    .pc = 0x0000,
    .mode = K3_MODE_KERNEL,
    .regs = (void *)0,
};

static struct thread_info init_thread_info = {
    .task = (void *)0,
    .flags = 0,
    .preempt_count = 1,
    .cpu = 0,
};

struct thread_struct *k3_get_init_thread(void)
{
    return &init_thread;
}

struct thread_info *k3_get_init_thread_info(void)
{
    return &init_thread_info;
}
