/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/io.h
 *
 * Memory-Mapped I/O (MMIO) and Port I/O primitives for K-Three Linux.
 */

#ifndef _ASM_K3_IO_H
#define _ASM_K3_IO_H

#include <stdint.h>
#include <stdbool.h>

#include "io.h"

/*
 * In K-Three's flat-addressed NOMMU memory model, physical memory addresses
 * map 1:1 with virtual addresses.
 */
#define ioremap(cookie, size)       ((void *)(uintptr_t)(cookie))
#define iounmap(cookie)             do { } while (0)

#define __raw_readw(addr)           (*(volatile uint16_t *)(addr))
#define __raw_writew(val, addr)     (*(volatile uint16_t *)(addr) = (val))

static inline uint16_t readw(const volatile void *addr)
{
    return *(const volatile uint16_t *)addr;
}

static inline void writew(uint16_t val, volatile void *addr)
{
    *(volatile uint16_t *)addr = val;
}

/*
 * Port I/O emulation (mapped into MMIO space).
 */
static inline uint16_t inw(uint16_t port)
{
    return readw((void *)(uintptr_t)(K3_MMIO_BASE + port));
}

static inline void outw(uint16_t val, uint16_t port)
{
    writew(val, (void *)(uintptr_t)(K3_MMIO_BASE + port));
}

#endif /* _ASM_K3_IO_H */
