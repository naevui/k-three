/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/fs/procfs.c
 *
 * Virtual /proc Filesystem Driver for K-Three Balanced Ternary Linux.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vm.h"

extern uint64_t k3_get_jiffies(void);

/*
 * /proc/cpuinfo: displays processor model, radix, and balanced-ternary features.
 */
int k3_proc_cpuinfo(char *buf, size_t max_len)
{
    return snprintf(buf, max_len,
        "processor\t: 0\n"
        "model name\t: K-Three 9-Trit Balanced Ternary Processor\n"
        "radix\t\t: 3 (balanced ternary: -1, 0, +1)\n"
        "word size\t: 9 trits (symmetric range [-9841..+9841])\n"
        "addressing\t: word-addressed (3^8 = 6561 words, 9 pages of 729)\n"
        "features\t: alu9 mul9 shl9 shr9 setvec eret mmio timer_irq dma_disk vram\n"
        "bogomips\t: 42.00\n"
        "flags\t\t: ternary nommu preempt fpu_none\n"
    );
}

/*
 * /proc/meminfo: displays memory consumption in 9-trit words.
 */
int k3_proc_meminfo(char *buf, size_t max_len)
{
    return snprintf(buf, max_len,
        "MemTotal:\t    6561 words\n"
        "MemFree:\t    5103 words\n"
        "MemAvailable:\t    5103 words\n"
        "Buffers:\t      64 words\n"
        "Cached:\t     128 words\n"
        "SwapTotal:\t       0 words\n"
        "SwapFree:\t       0 words\n"
    );
}

/*
 * /proc/version: displays Linux kernel version and build identity.
 */
int k3_proc_version(char *buf, size_t max_len)
{
    return snprintf(buf, max_len,
        "Linux version 0.1-k3 (root@k3-build) (k3cc) #1 PREEMPT Sun Nov 1 2026\n"
    );
}

/*
 * /proc/uptime: displays uptime in seconds and idle time.
 */
int k3_proc_uptime(char *buf, size_t max_len)
{
    uint64_t jiffies = k3_get_jiffies();
    uint64_t sec = jiffies / 100;
    uint64_t frac = jiffies % 100;
    return snprintf(buf, max_len, "%llu.%02llu %llu.00\n",
                    (unsigned long long)sec, (unsigned long long)frac, (unsigned long long)sec);
}

/*
 * /proc/interrupts: displays hardware IRQ counts.
 */
int k3_proc_interrupts(char *buf, size_t max_len)
{
    return snprintf(buf, max_len,
        "           CPU0\n"
        "  0:       %llu   K3-TIMER   timer tick\n"
        "  1:          0   K3-SYSCALL trap vector\n"
        "  2:          0   K3-MMIO    uart rx/tx\n"
        "  3:          0   K3-DMA     block storage\n",
        (unsigned long long)k3_get_jiffies()
    );
}
