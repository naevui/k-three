/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/thread_info.h
 *
 * Low-level thread information and scheduler flags for K-Three Linux.
 */

#ifndef _ASM_K3_THREAD_INFO_H
#define _ASM_K3_THREAD_INFO_H

#include <stdint.h>

struct task_struct;

struct thread_info {
    struct task_struct *task;       /* Main task structure */
    unsigned long       flags;      /* Low-level thread flags */
    int                 preempt_count; /* 0 => preemptible, >0 => preemption disabled */
    int                 cpu;        /* Current CPU index */
};

/* Thread Information Flags */
#define TIF_SIGPENDING          0   /* Signal pending for task */
#define TIF_NEED_RESCHED        1   /* Rescheduling required */
#define TIF_NOTIFY_RESUME       2   /* Callback on return to user mode */
#define TIF_MEMDIE              3   /* Process killed by OOM killer */

#define _TIF_SIGPENDING         (1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED       (1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_RESUME      (1 << TIF_NOTIFY_RESUME)

/* Preemption Control */
#define preempt_disable()       do { } while (0)
#define preempt_enable()        do { } while (0)

#endif /* _ASM_K3_THREAD_INFO_H */
