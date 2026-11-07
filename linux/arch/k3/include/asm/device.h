/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/include/asm/device.h
 *
 * Architecture-specific device structure extensions for K-Three.
 */

#ifndef _ASM_K3_DEVICE_H
#define _ASM_K3_DEVICE_H

struct dev_archdata {
    void *dma_data;
};

struct pdev_archdata {
};

#endif /* _ASM_K3_DEVICE_H */
