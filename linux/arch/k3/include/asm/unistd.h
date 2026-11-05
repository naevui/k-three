/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/unistd.h
 *
 * System call numbers for the K-Three balanced ternary Linux architecture.
 */

#ifndef _ASM_K3_UNISTD_H
#define _ASM_K3_UNISTD_H

/* Standard Linux POSIX System Call Numbers */
#define __NR_restart_syscall    0
#define __NR_exit               1
#define __NR_fork               2
#define __NR_read               3
#define __NR_write              4
#define __NR_open               5
#define __NR_close              6
#define __NR_waitpid            7
#define __NR_execve             11
#define __NR_getpid             20
#define __NR_pause              29
#define __NR_sync               36
#define __NR_kill               37
#define __NR_pipe               42
#define __NR_brk                45
#define __NR_getppid            64
#define __NR_getpgrp            65
#define __NR_setsid             66
#define __NR_getpgid            67
#define __NR_setpgid            68
#define __NR_setreuid           70
#define __NR_setregid           71
#define __NR_getgroups          79
#define __NR_setgroups          80
#define __NR_setresuid          81
#define __NR_getresuid          82
#define __NR_setresgid          83
#define __NR_getresgid          84
#define __NR_getuid             102
#define __NR_getgid             104
#define __NR_geteuid            107
#define __NR_getegid            108
#define __NR_gettimeofday       110
#define __NR_sysinfo            116
#define __NR_clone              120
#define __NR_uname              122
#define __NR_setfsuid           138
#define __NR_setfsgid           139
#define __NR_prctl              157
#define __NR_sched_yield        158
#define __NR_getpriority        140
#define __NR_setpriority        141
#define __NR_nanosleep          162
#define __NR_reboot             169
#define __NR_k3_halt            199

/* K-Three Balanced Ternary Hardware Extensions */
#define __NR_k3_uptime          200   /* Monotonic hardware timer ms */
#define __NR_k3_entropy         201   /* True hardware balanced ternary random word */
#define __NR_k3_leds            202   /* Read / write front-panel diagnostic LEDs */
#define __NR_k3_disk_dma        203   /* Direct sector block storage DMA transfer */
#define __NR_k3_vram_flush      204   /* Flush text video RAM to terminal display */

#define __NR_syscalls           256

#endif /* _ASM_K3_UNISTD_H */
