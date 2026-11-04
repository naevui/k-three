/* SPDX-License-Identifier: GPL-2.0 */
/*
 * userspace/cat.c - K-Three Minimal Block/VRAM Viewer (/bin/cat)
 *
 * Demonstrates reading disk sector 0 DMA buffer and printing contents.
 */

#include "include/k3libc.h"

int dma_buffer[16];

int main() {
    k3_printf("--- /bin/cat (Reading Disk Sector 0 DMA) ---\n", 0, 0, 0, 0);

    /* Read sector 0 into dma_buffer (16 words) */
    int stat = k3_disk_read(0, dma_buffer, 16);
    k3_printf("Disk DMA read status: %d\n", stat, 0, 0, 0);

    k3_printf("Sector 0 words: [%d, %d, %d, %d...]\n",
              dma_buffer[0], dma_buffer[1], dma_buffer[2], dma_buffer[3]);

    k3_printf("--- End of /bin/cat ---\n", 0, 0, 0, 0);
    k3_halt();
    return 0;
}
