/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/kernel/sys_k3.c
 *
 * Linux System Call Implementations for K-Three Balanced Ternary Architecture.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vm.h"
#include "io.h"
#include "arch/k3/include/asm/unistd.h"
#include "arch/k3/include/asm/processor.h"
#include "arch/k3/include/asm/uaccess.h"

extern int k3_get_current_pid(void);
extern uint64_t k3_get_jiffies(void);
extern void k3_schedule(k3_vm *vm);
extern void k3_exit_current_task(k3_vm *vm, int exit_code);
extern int k3_create_task(const char *comm, uint16_t entry_pc, uint16_t stack_ptr);

struct new_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct linux_sysinfo {
    long uptime;             /* Seconds since boot */
    unsigned long loads[3];  /* 1, 5, and 15 minute load averages */
    unsigned long totalram;  /* Total usable main memory size */
    unsigned long freeram;   /* Available memory size */
    unsigned long sharedram; /* Amount of shared memory */
    unsigned long bufferram; /* Memory used by buffers */
    unsigned long totalswap; /* Total swap space size */
    unsigned long freeswap;  /* Swap space still available */
    unsigned short procs;    /* Number of current processes */
    unsigned long totalhigh; /* Total high memory size */
    unsigned long freehigh;  /* Available high memory size */
    unsigned int mem_unit;   /* Memory unit size in bytes */
};

/*
 * sys_uname: returns system identification block.
 */
static int64_t k3_sys_uname(k3_vm *vm, uint16_t uaddr)
{
    struct new_utsname uts;
    memset(&uts, 0, sizeof(uts));
    strncpy(uts.sysname, "Linux", sizeof(uts.sysname) - 1);
    strncpy(uts.nodename, "k3-ternary", sizeof(uts.nodename) - 1);
    strncpy(uts.release, "0.1-k3", sizeof(uts.release) - 1);
    strncpy(uts.version, "#1 PREEMPT Sun Nov 1 2026", sizeof(uts.version) - 1);
    strncpy(uts.machine, "k3-9trit", sizeof(uts.machine) - 1);
    strncpy(uts.domainname, "(none)", sizeof(uts.domainname) - 1);

    /* Copy words to user address */
    const char *src = (const char *)&uts;
    size_t words = (sizeof(uts) + 1) / 2;
    for (size_t i = 0; i < words; ++i) {
        bt_word w;
        int64_t val = (unsigned char)src[i * 2] | (((int64_t)(unsigned char)src[i * 2 + 1]) << 8);
        bt_word_from_i64(val, &w);
        k3_vm_set_mem(vm, (uint16_t)(uaddr + i), &w);
    }
    return 0;
}

/*
 * sys_sysinfo: returns system statistics.
 */
static int64_t k3_sys_sysinfo(k3_vm *vm, uint16_t uaddr)
{
    bt_word w;
    /* word 0: uptime */
    bt_word_from_i64((int64_t)(k3_get_jiffies() / 100), &w);
    k3_vm_set_mem(vm, uaddr + 0, &w);
    /* word 1: total memory words */
    bt_word_from_i64(K3_VM_MEMORY_WORDS, &w);
    k3_vm_set_mem(vm, uaddr + 1, &w);
    /* word 2: free memory words */
    bt_word_from_i64(5104, &w);
    k3_vm_set_mem(vm, uaddr + 2, &w);
    /* word 3: process count */
    bt_word_from_i64(2, &w);
    k3_vm_set_mem(vm, uaddr + 3, &w);
    return 0;
}

/*
 * Linux System Call Dispatcher for K-Three Architecture.
 */
int64_t k3_dispatch_linux_syscall(k3_vm *vm, int64_t nr, int64_t a0, int64_t a1, int64_t a2)
{
    switch (nr) {
    case __NR_getpid:
        return k3_get_current_pid();

    case __NR_getppid:
        return 0;

    case __NR_getuid:
    case __NR_getgid:
        return 0; /* root (UID 0, GID 0) in single-user NOMMU */

    case __NR_sched_yield:
        k3_schedule(vm);
        return 0;

    case __NR_exit:
        k3_exit_current_task(vm, (int)a0);
        return 0;

    case __NR_uname:
        return k3_sys_uname(vm, (uint16_t)a0);

    case __NR_sysinfo:
        return k3_sys_sysinfo(vm, (uint16_t)a0);

    case __NR_write: {
        /* a0 = fd (1=stdout, 2=stderr), a1 = buf addr, a2 = len */
        if (a0 == 1 || a0 == 2) {
            for (int64_t i = 0; i < a2; ++i) {
                bt_word w;
                if (k3_vm_get_mem(vm, (uint16_t)(a1 + i), &w)) {
                    int64_t ch = 0;
                    bt_word_to_i64(&w, &ch);
                    putchar((int)(ch & 0xFF));
                }
            }
            fflush(stdout);
            return a2;
        }
        return -1;
    }

    case __NR_read: {
        /* a0 = fd (0=stdin), a1 = buf addr, a2 = max_len */
        if (a0 == 0) {
            int ch = getchar();
            if (ch != EOF) {
                bt_word w;
                bt_word_from_i64((int64_t)ch, &w);
                k3_vm_set_mem(vm, (uint16_t)a1, &w);
                return 1;
            }
            return 0;
        }
        return -1;
    }

    case __NR_clone:
    case __NR_fork: {
        /* Create a child task cloned from current task */
        int child_pid = k3_create_task("forked_child", (uint16_t)a0, (uint16_t)a1);
        return child_pid;
    }

    case __NR_nanosleep: {
        /* a0 = delay ms */
        if (a0 > 0) {
            /* Busy-wait / delay loop simulation for NOMMU */
            for (volatile int64_t i = 0; i < a0 * 1000; ++i) { }
        }
        return 0;
    }

    case __NR_reboot: {
        bt_word w;
        bt_word_from_i64(1, &w);
        k3_io_mmio_write(NULL, vm, K3_MMIO_SYS_RESET, &w);
        k3_exit_current_task(vm, 0);
        return 0;
    }

    /* K-Three Architecture Extensions */
    case __NR_k3_uptime: {
        bt_word w;
        if (k3_io_mmio_read(NULL, vm, K3_MMIO_TIMER_L, &w)) {
            int64_t t = 0;
            bt_word_to_i64(&w, &t);
            return t;
        }
        return 0;
    }

    case __NR_k3_entropy: {
        bt_word w;
        if (k3_io_mmio_read(NULL, vm, K3_MMIO_RNG_WORD, &w)) {
            int64_t r = 0;
            bt_word_to_i64(&w, &r);
            return r;
        }
        return 0;
    }

    case __NR_k3_leds: {
        if (a0 >= 0) {
            k3_io_set_leds(a0);
        }
        return k3_io_get_leds();
    }

    case __NR_k3_vram_flush: {
        bt_word w;
        bt_word_from_i64(1, &w);
        k3_io_mmio_write(NULL, vm, K3_MMIO_VRAM_CTRL, &w);
        return 0;
    }

    case __NR_k3_disk_dma: {
        /* a0 = sector, a1 = dma buffer addr, a2 = cmd (1=read, 2=write) */
        bt_word w;
        bt_word_from_i64(a0, &w);
        k3_io_mmio_write(NULL, vm, K3_MMIO_DISK_SECTOR, &w);
        bt_word_from_i64(a1, &w);
        k3_io_mmio_write(NULL, vm, K3_MMIO_DISK_DMA_ADDR, &w);
        bt_word_from_i64(16, &w);
        k3_io_mmio_write(NULL, vm, K3_MMIO_DISK_COUNT, &w);
        bt_word_from_i64(a2, &w);
        k3_io_mmio_write(NULL, vm, K3_MMIO_DISK_CMD, &w);
        if (k3_io_mmio_read(NULL, vm, K3_MMIO_DISK_STATUS, &w)) {
            int64_t stat = 0;
            bt_word_to_i64(&w, &stat);
            return stat;
        }
        return -1;
    }

    default:
        return -38; /* -ENOSYS: Function not implemented */
    }
}
