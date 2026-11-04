/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/string.h
 *
 * Architecture-optimized string and memory primitives for K-Three Linux.
 */

#ifndef _ASM_K3_STRING_H
#define _ASM_K3_STRING_H

#include <stddef.h>
#include <stdint.h>

#define __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_STRLEN

static inline void *memset(void *s, int c, size_t count)
{
    char *xs = (char *)s;
    while (count--) {
        *xs++ = (char)c;
    }
    return s;
}

static inline void *memcpy(void *dest, const void *src, size_t count)
{
    char *tmp = (char *)dest;
    const char *s = (const char *)src;
    while (count--) {
        *tmp++ = *s++;
    }
    return dest;
}

static inline size_t strlen(const char *s)
{
    const char *sc = s;
    while (*sc != '\0') {
        sc++;
    }
    return (size_t)(sc - s);
}

#endif /* _ASM_K3_STRING_H */
