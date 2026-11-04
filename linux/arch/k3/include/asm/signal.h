/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/arch/k3/include/asm/signal.h
 *
 * Signal definitions and signal frame structures for K-Three Linux.
 */

#ifndef _ASM_K3_SIGNAL_H
#define _ASM_K3_SIGNAL_H

#include <stdint.h>

#define _NSIG               32
typedef unsigned long       sigset_t;

/* POSIX Signals */
#define SIGHUP              1
#define SIGINT              2
#define SIGQUIT             3
#define SIGILL              4
#define SIGTRAP             5
#define SIGABRT             6
#define SIGBUS              7
#define SIGFPE              8
#define SIGKILL             9
#define SIGUSR1             10
#define SIGSEGV             11
#define SIGUSR2             12
#define SIGPIPE             13
#define SIGALRM             14
#define SIGTERM             15
#define SIGCHLD             17
#define SIGCONT             18
#define SIGSTOP             19
#define SIGTSTP             20

/* Signal handler flags */
#define SA_NOCLDSTOP        0x00000001
#define SA_NOCLDWAIT        0x00000002
#define SA_SIGINFO          0x00000004
#define SA_ONSTACK          0x08000000
#define SA_RESTART          0x10000000
#define SA_NODEFER          0x40000000
#define SA_RESETHAND        0x80000000

#define SIG_DFL             ((void (*)(int))0)
#define SIG_IGN             ((void (*)(int))1)
#define SIG_ERR             ((void (*)(int))-1)

struct sigaction {
    void (*sa_handler)(int);
    unsigned long sa_flags;
    sigset_t sa_mask;
};

/*
 * Signal delivery context frame placed on user stack during signal dispatch.
 */
struct sigcontext {
    int64_t  regs[8];       /* Saved general-purpose registers r0..r7 */
    uint16_t epc;           /* Exception program counter to return to */
    uint16_t status;        /* Machine status */
    uint8_t  mode;          /* Previous execution mode */
    sigset_t oldmask;       /* Blocked signal mask */
};

#endif /* _ASM_K3_SIGNAL_H */
