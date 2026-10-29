/*
 * arch/k3/kernel/process.c
 *
 * Process management and context switching (switch_to) for K-Three.
 */

#include <stdio.h>
#include <string.h>
#include "vm.h"
#include "arch/k3/include/asm/processor.h"

#define K3_MAX_TASKS 4

struct task_struct {
    int pid;
    char comm[16];
    struct thread_struct thread;
    bt_word regs[K3_VM_REGISTER_COUNT];
    uint16_t pc;
    int state; /* 0 = RUNNING, 1 = SLEEPING, 2 = ZOMBIE */
};

static struct task_struct g_task_table[K3_MAX_TASKS];
static int g_current_pid = 0;

void k3_init_tasks(void)
{
    memset(g_task_table, 0, sizeof(g_task_table));

    /* Task 0: swapper / idle */
    g_task_table[0].pid = 0;
    strncpy(g_task_table[0].comm, "swapper", sizeof(g_task_table[0].comm) - 1);
    g_task_table[0].state = 0;

    /* Task 1: /sbin/init */
    g_task_table[1].pid = 1;
    strncpy(g_task_table[1].comm, "init", sizeof(g_task_table[1].comm) - 1);
    g_task_table[1].state = 0;
}

/*
 * Low-level context switch between two K-Three processes.
 */
void k3_switch_to(k3_vm *vm, int prev_pid, int next_pid)
{
    if (prev_pid < 0 || prev_pid >= K3_MAX_TASKS ||
        next_pid < 0 || next_pid >= K3_MAX_TASKS) {
        return;
    }

    struct task_struct *prev = &g_task_table[prev_pid];
    struct task_struct *next = &g_task_table[next_pid];

    /* 1. Save architecture state of prev task */
    for (int i = 0; i < K3_VM_REGISTER_COUNT; ++i) {
        prev->regs[i] = vm->regs[i];
    }
    prev->pc = vm->pc;

    /* 2. Restore architecture state of next task */
    for (int i = 0; i < K3_VM_REGISTER_COUNT; ++i) {
        vm->regs[i] = next->regs[i];
    }
    vm->pc = next->pc;

    g_current_pid = next_pid;
}
