/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_K3_SIGINFO_H
#define _ASM_K3_SIGINFO_H
typedef struct siginfo_k3 { int si_signo; int si_errno; int si_code; int si_pid; } siginfo_t;
#endif
