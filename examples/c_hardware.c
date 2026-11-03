/*
 * examples/c_hardware.c - K-Three Bare-Metal C Hardware Showcase
 *
 * Demonstrates compiling pure C code with k3cc, including:
 *   - #include "include/k3libc.h" standard library and hardware drivers
 *   - Direct Memory-Mapped I/O (MMIO) to UART, timer, RNG, LEDs
 *   - Hardware Block Storage (Disk) DMA read/write
 *   - Dynamic memory heap allocation (k3_malloc)
 *   - First-class string pointers and character indexing
 *
 * Compile, assemble and run:
 *   build/k3cc examples/c_hardware.c -o build/examples/c_hardware.s
 *   build/k3asm build/examples/c_hardware.s -o build/examples/c_hardware.k3
 *   build/k3vm -d build/examples/c_hardware.k3
 */

#include "include/k3libc.h"

int *g_banner = "=== K-Three Bare-Metal C Hardware Showcase ===\n";

int main() {
    /* 1. Direct hardware serial UART output */
    k3_uart_puts(g_banner);

    /* 2. Read hardware timer and entropy generator */
    int t0 = k3_timer_ms();
    int r0 = k3_rand_word();

    /* 3. Front-panel diagnostic LEDs */
    k3_leds_set(42);

    /* 4. Dynamic heap allocation and memory utilities */
    k3_init_heap(2500);
    int *disk_buf = k3_malloc(16);
    k3_memset(disk_buf, 0, 16);

    disk_buf[0] = 111;
    disk_buf[1] = 222;
    disk_buf[2] = 333;
    disk_buf[15] = 999;

    /* 5. Hardware Block Device (Disk) DMA Write to sector 7 */
    k3_disk_write(7, disk_buf, 16);

    /* Clear memory buffer */
    k3_memset(disk_buf, 0, 16);

    /* Hardware Block Device (Disk) DMA Read from sector 7 */
    k3_disk_read(7, disk_buf, 16);

    /* 6. Verify DMA transfer */
    int ok = 0;
    if (disk_buf[0] == 111 && disk_buf[1] == 222 &&
        disk_buf[2] == 333 && disk_buf[15] == 999) {
        ok = 1;
        k3_uart_puts("Disk DMA transfer: OK\n");
    }

    k3_uart_puts("Hardware test completed successfully!\n");
    return k3_leds_get() + ok; // 42 + 1 = 43
}
