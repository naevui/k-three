/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/fb.h
 *
 * Framebuffer memory and page mapping helpers for K-Three.
 */

#ifndef _ASM_K3_FB_H
#define _ASM_K3_FB_H

#include <linux/fb.h>
#include <linux/fs.h>

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
                               unsigned long off)
{
    (void)file;
    (void)vma;
    (void)off;
}

#define fb_is_primary_device(info) 1

#endif /* _ASM_K3_FB_H */
