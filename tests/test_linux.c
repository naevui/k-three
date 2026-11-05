/* SPDX-License-Identifier: GPL-2.0 */
/*
 * tests/test_linux.c - Test Suite for K-Three Balanced Ternary Linux Architecture
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"
#include "io.h"
#include "syscall.h"
#include "arch/k3/include/asm/unistd.h"
#include "arch/k3/include/asm/processor.h"
#include "arch/k3/include/asm/uaccess.h"
#include "arch/k3/include/asm/atomic.h"
#include "arch/k3/include/asm/bitops.h"
#include "arch/k3/include/asm/io.h"
#include "arch/k3/include/asm/irq.h"
#include "arch/k3/include/asm/irqflags.h"
#include "arch/k3/include/asm/signal.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(expr) do { \
    g_checks++; \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while (0)

#define CHECK_EQ_I64(a, b) do { \
    g_checks++; \
    int64_t _a = (int64_t)(a); \
    int64_t _b = (int64_t)(b); \
    if (_a != _b) { \
        fprintf(stderr, "%s:%d: check failed: %lld == %lld (%s == %s)\n", \
                __FILE__, __LINE__, (long long)_a, (long long)_b, #a, #b); \
        g_failures++; \
    } \
} while (0)

/* External architecture functions */
extern void k3_init_scheduler(void);
extern int k3_get_current_pid(void);
extern const char *k3_get_task_comm(int pid);
extern int k3_create_task(const char *comm, uint16_t entry_pc, uint16_t stack_ptr);
extern void k3_schedule(k3_vm *vm);
extern void k3_timer_irq_tick(k3_vm *vm);

extern int64_t k3_dispatch_linux_syscall(k3_vm *vm, int64_t nr, int64_t a0, int64_t a1, int64_t a2);

extern int k3_proc_cpuinfo(char *buf, size_t max_len);
extern int k3_proc_meminfo(char *buf, size_t max_len);
extern int k3_proc_version(char *buf, size_t max_len);
extern int k3_proc_uptime(char *buf, size_t max_len);
extern int k3_proc_interrupts(char *buf, size_t max_len);

extern int k3_trap_to_signal(k3_trap_reason reason);
extern struct thread_struct *k3_get_init_thread(void);

extern void init_IRQ(void);
typedef int (*irq_handler_t)(int irq, void *dev_id);
extern int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags, const char *name, void *dev_id);
extern void free_irq(unsigned int irq, void *dev_id);
extern void do_IRQ(k3_vm *vm, unsigned int irq, struct pt_regs *regs);
extern unsigned long k3_get_irq_count(unsigned int irq);
extern const char *k3_get_irq_name(unsigned int irq);

extern void init_signals(void);
extern int k3_do_sigaction(int pid, int sig, const struct sigaction *act, struct sigaction *oldact);
extern int k3_send_signal(k3_vm *vm, int pid, int sig);
extern bool k3_has_pending_signal(int pid, int *out_sig);

static int s_test_sig_handled = 0;
static void test_sig_handler(int sig)
{
    s_test_sig_handled = sig;
}

static void test_linux_signals(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);
    init_signals();

    struct sigaction act = {
        .sa_handler = test_sig_handler,
        .sa_flags = 0,
        .sa_mask = 0
    };
    struct sigaction oldact;

    /* Register SIGINT handler for PID 1 */
    CHECK_EQ_I64(k3_do_sigaction(1, SIGINT, &act, &oldact), 0);

    /* SIGKILL cannot be caught (-EINVAL = -22) */
    CHECK_EQ_I64(k3_do_sigaction(1, SIGKILL, &act, &oldact), -22);

    /* Send SIGINT to PID 1 */
    CHECK_EQ_I64(k3_send_signal(&vm, 1, SIGINT), 0);

    /* Check pending signal */
    int sig_received = 0;
    CHECK(k3_has_pending_signal(1, &sig_received));
    CHECK_EQ_I64(sig_received, SIGINT);

    /* No more pending signals */
    CHECK(!k3_has_pending_signal(1, &sig_received));
}

static int s_test_irq_fired = 0;
static int test_irq_handler(int irq, void *dev_id)
{
    (void)dev_id;
    s_test_irq_fired = irq + 10;
    return 0;
}

static void test_linux_irq_subsystem(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);
    init_IRQ();

    int dev_token = 123;
    CHECK_EQ_I64(request_irq(K3_IRQ_TIMER, test_irq_handler, 0, "test_timer", &dev_token), 0);
    CHECK(strcmp(k3_get_irq_name(K3_IRQ_TIMER), "test_timer") == 0);

    /* Dispatch IRQ 0 */
    s_test_irq_fired = 0;
    do_IRQ(&vm, K3_IRQ_TIMER, NULL);
    CHECK_EQ_I64(s_test_irq_fired, 10);
    CHECK_EQ_I64(k3_get_irq_count(K3_IRQ_TIMER), 1);

    /* Free IRQ */
    free_irq(K3_IRQ_TIMER, &dev_token);

    /* irqflags primitives */
    arch_local_irq_enable(&vm);
    CHECK(k3_vm_irq_enabled(&vm));
    unsigned long flags = arch_local_irq_save(&vm);
    CHECK(!k3_vm_irq_enabled(&vm));
    CHECK(!arch_irqs_disabled_flags(flags));
    arch_local_irq_restore(&vm, flags);
    CHECK(k3_vm_irq_enabled(&vm));
}

static void test_linux_traps_and_atomics(void)
{
    /* 1. Linux signal mapping for K-Three hardware traps */
    CHECK_EQ_I64(k3_trap_to_signal(K3_TRAP_OVERFLOW), 8);  /* SIGFPE */
    CHECK_EQ_I64(k3_trap_to_signal(K3_TRAP_SHIFT), 8);     /* SIGFPE */
    CHECK_EQ_I64(k3_trap_to_signal(K3_TRAP_MEMORY), 11);   /* SIGSEGV */
    CHECK_EQ_I64(k3_trap_to_signal(K3_TRAP_ILLEGAL), 4);   /* SIGILL */
    CHECK_EQ_I64(k3_trap_to_signal(K3_TRAP_EXPLICIT), 5);  /* SIGTRAP */

    /* 2. Initial thread context */
    struct thread_struct *thr = k3_get_init_thread();
    CHECK(thr != NULL);
    CHECK_EQ_I64(thr->ksp, 6560);
    CHECK_EQ_I64(thr->mode, K3_MODE_KERNEL);

    /* 3. Atomics */
    atomic_t val = ATOMIC_INIT(10);
    CHECK_EQ_I64(atomic_read(&val), 10);
    atomic_add(5, &val);
    CHECK_EQ_I64(atomic_read(&val), 15);
    atomic_sub(3, &val);
    CHECK_EQ_I64(atomic_read(&val), 12);
    atomic_inc(&val);
    CHECK_EQ_I64(atomic_read(&val), 13);
    atomic_dec(&val);
    CHECK_EQ_I64(atomic_read(&val), 12);
    CHECK_EQ_I64(atomic_cmpxchg(&val, 12, 100), 12);
    CHECK_EQ_I64(atomic_read(&val), 100);

    /* 4. Bitops */
    unsigned long bitmap = 0;
    set_bit(3, &bitmap);
    CHECK(test_bit(3, &bitmap));
    CHECK(!test_bit(4, &bitmap));
    clear_bit(3, &bitmap);
    CHECK(!test_bit(3, &bitmap));
    change_bit(5, &bitmap);
    CHECK(test_bit(5, &bitmap));
}

static void test_linux_scheduler(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);
    k3_init_scheduler();

    /* Initial state: PID 1 (kinit) is running */
    CHECK_EQ_I64(k3_get_current_pid(), 1);
    CHECK(strcmp(k3_get_task_comm(1), "kinit") == 0);

    /* Create background task (PID 2: shell) at PC 2000, SP 6000 */
    int pid2 = k3_create_task("sh", 2000, 6000);
    CHECK_EQ_I64(pid2, 2);
    CHECK(strcmp(k3_get_task_comm(2), "sh") == 0);

    /* Preempt task 1 via schedule(): switches to PID 2 */
    k3_schedule(&vm);
    CHECK_EQ_I64(k3_get_current_pid(), 2);
    CHECK_EQ_I64(vm.pc, 2000);

    /* Timer tick decrements timeslice and triggers preemption */
    for (int t = 0; t < 5; ++t) {
        k3_timer_irq_tick(&vm);
    }
    /* Switched back to PID 1! */
    CHECK_EQ_I64(k3_get_current_pid(), 1);
}

static void test_linux_syscalls(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);
    k3_init_scheduler();

    /* 1. sys_getpid(), sys_gettid(), sys_getuid(), sys_geteuid(), sys_getegid(), sys_getppid(), sys_getpgrp(), sys_getpgid(), sys_setpgid(), sys_setsid() */
    int64_t pid = k3_dispatch_linux_syscall(&vm, __NR_getpid, 0, 0, 0);
    CHECK_EQ_I64(pid, 1);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_gettid, 0, 0, 0), 1);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_getppid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_getpgrp, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_getpgid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_setpgid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_setsid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_getuid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_getgid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_geteuid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_getegid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_setreuid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_setregid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_getgroups, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_setgroups, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_setresuid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_getresuid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_setresgid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_getresgid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_setfsuid, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_setfsgid, 0, 0, 0), 0);

    /* 2. sys_uname() */
    int64_t ret = k3_dispatch_linux_syscall(&vm, __NR_uname, 3000, 0, 0);
    CHECK_EQ_I64(ret, 0);

    /* 3. sys_sysinfo() */
    ret = k3_dispatch_linux_syscall(&vm, __NR_sysinfo, 3100, 0, 0);
    CHECK_EQ_I64(ret, 0);
    bt_word w;
    int64_t mem_words = 0;
    CHECK(k3_vm_get_mem(&vm, 3101, &w));
    CHECK(bt_word_to_i64(&w, &mem_words));
    CHECK_EQ_I64(mem_words, K3_VM_MEMORY_WORDS);

    /* 4. sys_k3_uptime() */
    int64_t up = k3_dispatch_linux_syscall(&vm, __NR_k3_uptime, 0, 0, 0);
    CHECK(up >= 0);

    /* 5. sys_k3_entropy() */
    int64_t r = k3_dispatch_linux_syscall(&vm, __NR_k3_entropy, 0, 0, 0);
    CHECK(r >= -K3_WORD_MAX && r <= K3_WORD_MAX);

    /* 6. sys_k3_disk_dma() */
    int64_t stat = k3_dispatch_linux_syscall(&vm, __NR_k3_disk_dma, 0, 2500, 1);
    CHECK_EQ_I64(stat, 0);

    /* 7. sys_nanosleep(), sys_reboot(), sys_gettimeofday(), sys_execve(), sys_sync(), sys_k3_halt(), sys_prctl(), sys_getpriority(), sys_setpriority(), sys_sched_priority */
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_nanosleep, 1, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_reboot, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_gettimeofday, 3200, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_execve, 3300, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_sync, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_readahead, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_get_mempolicy, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_set_mempolicy, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_mbind, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_getcpu, 3400, 0, 0), 0);
    bt_word cpu_w;
    int64_t cpu_val = -1;
    CHECK(k3_vm_get_mem(&vm, 3400, &cpu_w));
    CHECK(bt_word_to_i64(&cpu_w, &cpu_val));
    CHECK_EQ_I64(cpu_val, 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_kill, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_tkill, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_tgkill, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_k3_halt, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_prctl, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_getpriority, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_setpriority, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_sched_get_priority_max, 0, 0, 0), 99);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_sched_get_priority_min, 0, 0, 0), 1);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_sched_getscheduler, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_sched_setscheduler, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_sched_getparam, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_sched_setparam, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_sched_rr_get_interval, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_ioprio_set, 0, 0, 0), 0);
    CHECK_EQ_I64(k3_dispatch_linux_syscall(&vm, __NR_ioprio_get, 0, 0, 0), 0);
}

static void test_linux_procfs(void)
{
    char buf[1024];

    /* /proc/cpuinfo */
    int n = k3_proc_cpuinfo(buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "K-Three 9-Trit Balanced Ternary Processor") != NULL);
    CHECK(strstr(buf, "radix\t\t: 3") != NULL);
    CHECK(strstr(buf, "bogomips") != NULL);

    /* /proc/meminfo */
    n = k3_proc_meminfo(buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "6561 words") != NULL);

    /* /proc/version */
    n = k3_proc_version(buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "Linux version 0.1-k3") != NULL);

    /* /proc/interrupts */
    n = k3_proc_interrupts(buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "K3-TIMER") != NULL);
}

static void test_linux_uaccess(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);

    /* Configure user bounds: 1458..6560 */
    k3_vm_set_user_bounds(&vm, 1458, 6560);

    /* Access check */
    CHECK(access_ok(&vm, 2000, 10));     /* Inside user space: OK */
    CHECK(!access_ok(&vm, 500, 10));      /* Kernel space [0..1457]: DENIED! */
    CHECK(!access_ok(&vm, 6555, 10));     /* Beyond 6560: DENIED! */

    /* Copy to/from user */
    bt_word kbuf[4];
    for (int i = 0; i < 4; ++i) {
        bt_word_from_i64(700 + i, &kbuf[i]);
    }

    /* Copy to user space at 2000: success */
    CHECK_EQ_I64(copy_to_user(&vm, 2000, kbuf, 4), 0);

    /* Copy from user space at 2000: success */
    bt_word dest[4];
    CHECK_EQ_I64(copy_from_user(&vm, dest, 2000, 4), 0);
    for (int i = 0; i < 4; ++i) {
        int64_t val = 0;
        bt_word_to_i64(&dest[i], &val);
        CHECK_EQ_I64(val, 700 + i);
    }

    /* Attempting to copy to kernel space at 100: returns -EFAULT */
    CHECK_EQ_I64(copy_to_user(&vm, 100, kbuf, 4), -EFAULT);
    CHECK_EQ_I64(copy_from_user(&vm, dest, 100, 4), -EFAULT);
}

int main(void)
{
    test_linux_scheduler();
    test_linux_syscalls();
    test_linux_procfs();
    test_linux_uaccess();
    test_linux_traps_and_atomics();
    test_linux_irq_subsystem();
    test_linux_signals();

    printf("test_linux: %d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
