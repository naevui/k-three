/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/kernel/sched.c
 *
 * Preemptive Task Scheduler & Context Switching for K-Three Linux.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vm.h"
#include "arch/k3/include/asm/processor.h"
#include "arch/k3/include/asm/ptrace.h"
#include "arch/k3/include/asm/page.h"

#define K3_MAX_TASKS        8

#define TASK_RUNNING        0
#define TASK_INTERRUPTIBLE  1
#define TASK_STOPPED        2
#define TASK_ZOMBIE         3

struct task_struct {
    int pid;
    int ppid;
    char comm[16];
    int state;
    uint64_t utime;
    uint64_t stime;
    int priority;
    int timeslice;

    /* Architectural Context */
    bt_word regs[K3_VM_REGISTER_COUNT];
    uint16_t pc;
    uint8_t mode;
};

static struct task_struct s_tasks[K3_MAX_TASKS];
static int s_current_pid = 0;
static uint64_t s_jiffies = 0;

void k3_init_scheduler(void)
{
    memset(s_tasks, 0, sizeof(s_tasks));

    /* Task 0: swapper (idle daemon) */
    s_tasks[0].pid = 0;
    s_tasks[0].ppid = 0;
    strncpy(s_tasks[0].comm, "swapper", sizeof(s_tasks[0].comm) - 1);
    s_tasks[0].state = TASK_RUNNING;
    s_tasks[0].priority = 20;
    s_tasks[0].timeslice = 5;

    /* Task 1: /sbin/init */
    s_tasks[1].pid = 1;
    s_tasks[1].ppid = 0;
    strncpy(s_tasks[1].comm, "init", sizeof(s_tasks[1].comm) - 1);
    s_tasks[1].state = TASK_RUNNING;
    s_tasks[1].priority = 20;
    s_tasks[1].timeslice = 5;

    s_current_pid = 1; /* Start with init */
    s_jiffies = 0;
}

int k3_get_current_pid(void)
{
    return s_current_pid;
}

uint64_t k3_get_jiffies(void)
{
    return s_jiffies;
}

const char *k3_get_task_comm(int pid)
{
    if (pid >= 0 && pid < K3_MAX_TASKS) {
        return s_tasks[pid].comm;
    }
    return "unknown";
}

int k3_create_task(const char *comm, uint16_t entry_pc, uint16_t stack_ptr)
{
    for (int i = 2; i < K3_MAX_TASKS; ++i) {
        if (s_tasks[i].state == TASK_ZOMBIE || s_tasks[i].pid == 0) {
            s_tasks[i].pid = i;
            s_tasks[i].ppid = s_current_pid;
            strncpy(s_tasks[i].comm, comm, sizeof(s_tasks[i].comm) - 1);
            s_tasks[i].comm[sizeof(s_tasks[i].comm) - 1] = '\0';
            s_tasks[i].state = TASK_RUNNING;
            s_tasks[i].priority = 20;
            s_tasks[i].timeslice = 5;
            s_tasks[i].pc = entry_pc;
            s_tasks[i].mode = K3_MODE_USER;

            /* Zero registers except stack pointer r7 */
            for (int r = 0; r < K3_VM_REGISTER_COUNT; ++r) {
                bt_word_zero(&s_tasks[i].regs[r]);
            }
            bt_word sp;
            bt_word_from_i64((int64_t)stack_ptr, &sp);
            s_tasks[i].regs[7] = sp;

            return i;
        }
    }
    return -1; /* Out of task slots */
}

/*
 * Low-level context switch between two K-Three processes.
 */
static void do_context_switch(k3_vm *vm, int prev_pid, int next_pid)
{
    if (prev_pid == next_pid) return;

    struct task_struct *prev = &s_tasks[prev_pid];
    struct task_struct *next = &s_tasks[next_pid];

    /* 1. Save architecture state of current task */
    for (int i = 0; i < K3_VM_REGISTER_COUNT; ++i) {
        prev->regs[i] = vm->regs[i];
    }
    prev->pc = vm->pc;
    prev->mode = (uint8_t)k3_vm_get_mode(vm);

    /* 2. Restore architecture state of next task */
    for (int i = 0; i < K3_VM_REGISTER_COUNT; ++i) {
        vm->regs[i] = next->regs[i];
    }
    vm->pc = next->pc;
    k3_vm_set_mode(vm, (k3_vm_mode)next->mode);

    s_current_pid = next_pid;
}

/*
 * Round-robin preemptive scheduler.
 */
void k3_schedule(k3_vm *vm)
{
    if (vm == NULL) return;

    int next_pid = -1;

    /* Scan task table starting from current + 1 */
    for (int offset = 1; offset <= K3_MAX_TASKS; ++offset) {
        int candidate = (s_current_pid + offset) % K3_MAX_TASKS;
        if (s_tasks[candidate].pid > 0 && s_tasks[candidate].state == TASK_RUNNING) {
            next_pid = candidate;
            break;
        }
    }

    if (next_pid < 0) {
        /* Default to idle task (0) */
        next_pid = 0;
    }

    if (next_pid != s_current_pid) {
        do_context_switch(vm, s_current_pid, next_pid);
    }
}

/*
 * Hardware Timer IRQ handler: updates jiffies and decrements timeslice.
 */
void k3_timer_irq_tick(k3_vm *vm)
{
    s_jiffies++;

    struct task_struct *cur = &s_tasks[s_current_pid];
    if (cur->state == TASK_RUNNING) {
        if (k3_vm_get_mode(vm) == K3_MODE_USER) {
            cur->utime++;
        } else {
            cur->stime++;
        }

        if (--cur->timeslice <= 0) {
            cur->timeslice = 5; /* Reset timeslice */
            k3_schedule(vm);    /* Preempt and switch task! */
        }
    }
}

void k3_exit_current_task(k3_vm *vm, int exit_code)
{
    (void)exit_code;
    s_tasks[s_current_pid].state = TASK_ZOMBIE;
    k3_schedule(vm);
}
