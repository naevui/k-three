/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/cacheflush.h
 *
 * Cache flushing and coherent DMA operations for K-Three.
 *
 * K-Three hardware has a unified flat memory space with coherent caches (no-op flush).
 */

#ifndef _ASM_K3_CACHEFLUSH_H
#define _ASM_K3_CACHEFLUSH_H

#define flush_cache_all()                       do { } while (0)
#define flush_cache_mm(mm)                      do { } while (0)
#define flush_cache_dup_mm(mm)                  do { } while (0)
#define flush_cache_range(vma, start, end)      do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)      do { } while (0)
#define flush_dcache_page(page)                 do { } while (0)
#define flush_dcache_mmap_lock(mapping)         do { } while (0)
#define flush_dcache_mmap_unlock(mapping)       do { } while (0)
#define flush_icache_range(start, end)          do { } while (0)
#define flush_icache_page(vma, pg)              do { } while (0)
#define flush_icache_user_range(vma, pg, adr, len) do { } while (0)
#define flush_cache_vmap(start, end)            do { } while (0)
#define flush_cache_vunmap(start, end)          do { } while (0)

#endif /* _ASM_K3_CACHEFLUSH_H */
