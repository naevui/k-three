/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/bitops.h
 *
 * Bitwise operations and binary flag manipulation for K-Three Linux.
 */

#ifndef _ASM_K3_BITOPS_H
#define _ASM_K3_BITOPS_H

#include <stdbool.h>
#include <stdint.h>

#define BIT(nr)                 (1UL << (nr))
#define BITS_PER_LONG           64

static inline void set_bit(int nr, volatile unsigned long *addr)
{
    addr[nr / BITS_PER_LONG] |= (1UL << (nr % BITS_PER_LONG));
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
    addr[nr / BITS_PER_LONG] &= ~(1UL << (nr % BITS_PER_LONG));
}

static inline void change_bit(int nr, volatile unsigned long *addr)
{
    addr[nr / BITS_PER_LONG] ^= (1UL << (nr % BITS_PER_LONG));
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
    return (addr[nr / BITS_PER_LONG] & (1UL << (nr % BITS_PER_LONG))) != 0;
}

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
    unsigned long mask = (1UL << (nr % BITS_PER_LONG));
    volatile unsigned long *p = &addr[nr / BITS_PER_LONG];
    unsigned long old = *p;
    *p = old | mask;
    return (old & mask) != 0;
}

static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
    unsigned long mask = (1UL << (nr % BITS_PER_LONG));
    volatile unsigned long *p = &addr[nr / BITS_PER_LONG];
    unsigned long old = *p;
    *p = old & ~mask;
    return (old & mask) != 0;
}

static inline int ffs(int x)
{
    return __builtin_ffs(x);
}

static inline int fls(int x)
{
    return x ? sizeof(x) * 8 - __builtin_clz(x) : 0;
}

#endif /* _ASM_K3_BITOPS_H */
