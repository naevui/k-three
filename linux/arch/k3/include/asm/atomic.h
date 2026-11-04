/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/atomic.h
 *
 * Ternary atomic operations for K-Three Linux.
 */

#ifndef _ASM_K3_ATOMIC_H
#define _ASM_K3_ATOMIC_H

#include <stdint.h>

typedef struct {
    int64_t counter;
} atomic_t;

#define ATOMIC_INIT(i)          { (i) }

static inline int64_t atomic_read(const atomic_t *v)
{
    return v->counter;
}

static inline void atomic_set(atomic_t *v, int64_t i)
{
    v->counter = i;
}

static inline void atomic_add(int64_t i, atomic_t *v)
{
    v->counter += i;
}

static inline void atomic_sub(int64_t i, atomic_t *v)
{
    v->counter -= i;
}

static inline void atomic_inc(atomic_t *v)
{
    v->counter += 1;
}

static inline void atomic_dec(atomic_t *v)
{
    v->counter -= 1;
}

static inline int64_t atomic_cmpxchg(atomic_t *v, int64_t old, int64_t new_val)
{
    int64_t prev = v->counter;
    if (prev == old) {
        v->counter = new_val;
    }
    return prev;
}

static inline int64_t atomic_xchg(atomic_t *v, int64_t new_val)
{
    int64_t prev = v->counter;
    v->counter = new_val;
    return prev;
}

#endif /* _ASM_K3_ATOMIC_H */
