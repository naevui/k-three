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

extern unsigned long k3_get_irq_count(unsigned int irq);
extern const char *k3_get_irq_name(unsigned int irq);

/*
 * /proc/interrupts: displays hardware IRQ counts.
 */
int k3_proc_interrupts(char *buf, size_t max_len)
{
    return snprintf(buf, max_len,
        "           CPU0\n"
        "  0:       %lu   K3-TIMER   %s\n"
        "  1:       %lu   K3-SYSCALL %s\n"
        "  2:       %lu   K3-FAULT   %s\n"
        "  3:       %lu   K3-ILLEGAL %s\n"
        "  4:       %lu   K3-OVERFLW %s\n",
        k3_get_irq_count(0), k3_get_irq_name(0),
        k3_get_irq_count(1), k3_get_irq_name(1),
        k3_get_irq_count(2), k3_get_irq_name(2),
        k3_get_irq_count(3), k3_get_irq_name(3),
        k3_get_irq_count(4), k3_get_irq_name(4)
    );
}
