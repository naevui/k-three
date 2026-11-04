/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/arch/k3/kernel/signal.c
 *
 * Signal dispatch and user frame delivery for K-Three Linux.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vm.h"
#include "arch/k3/include/asm/signal.h"
#include "arch/k3/include/asm/ptrace.h"

#define K3_MAX_TASKS        8

static struct sigaction s_task_signals[K3_MAX_TASKS][_NSIG];
static uint32_t s_pending_signals[K3_MAX_TASKS];

extern int k3_get_current_pid(void);
extern void k3_exit_current_task(k3_vm *vm, int exit_code);

void init_signals(void)
{
    memset(s_task_signals, 0, sizeof(s_task_signals));
    memset(s_pending_signals, 0, sizeof(s_pending_signals));
}

int k3_do_sigaction(int pid, int sig, const struct sigaction *act, struct sigaction *oldact)
{
    if (pid < 0 || pid >= K3_MAX_TASKS || sig <= 0 || sig >= _NSIG) {
        return -22; /* -EINVAL */
    }
    if (sig == SIGKILL || sig == SIGSTOP) {
        return -22; /* Cannot catch SIGKILL or SIGSTOP */
    }

    if (oldact != NULL) {
        *oldact = s_task_signals[pid][sig];
    }
    if (act != NULL) {
        s_task_signals[pid][sig] = *act;
    }
    return 0;
}

int k3_send_signal(k3_vm *vm, int pid, int sig)
{
    if (pid < 0 || pid >= K3_MAX_TASKS || sig <= 0 || sig >= _NSIG) {
        return -22; /* -EINVAL */
    }

    /* Record pending signal */
    s_pending_signals[pid] |= (1U << sig);

    /* Default action for SIGKILL / SIGTERM */
    if (s_task_signals[pid][sig].sa_handler == SIG_DFL) {
        if (sig == SIGKILL || sig == SIGTERM || sig == SIGSEGV) {
            k3_exit_current_task(vm, 128 + sig);
            return 0;
        }
    }
    return 0;
}

bool k3_has_pending_signal(int pid, int *out_sig)
{
    if (pid >= 0 && pid < K3_MAX_TASKS) {
        for (int s = 1; s < _NSIG; ++s) {
            if (s_pending_signals[pid] & (1U << s)) {
                s_pending_signals[pid] &= ~(1U << s);
                if (out_sig) *out_sig = s;
                return true;
            }
        }
    }
    return false;
}
