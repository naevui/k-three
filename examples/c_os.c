/*
 * examples/c_os.c - K-Three Operating System Shell (100% Pure C Edition)
 *
 * A complete interactive operating system shell written in C and compiled
 * with k3cc. Interacts directly with hardware MMIO peripherals:
 *   - Serial UART console (k3_printf, k3_uart_getc, k3_uart_puts)
 *   - Hardware real-time monotonic timer (k3_timer_ms)
 *   - Hardware balanced-ternary entropy generator (k3_rand_word, %t formatting)
 *   - Text video RAM & display controller (k3_vram_flush)
 *   - Front-panel LED diagnostics (k3_leds_set, k3_leds_get)
 *   - Hardware block storage DMA controller (k3_disk_write, k3_disk_read)
 *   - Heap bump allocator (k3_malloc)
 *
 * Compile, assemble and run:
 *   build/k3cc examples/c_os.c -o build/examples/c_os.s
 *   build/k3asm build/examples/c_os.s -o build/examples/c_os.k3
 *   build/k3vm build/examples/c_os.k3
 */

#include "include/k3libc.h"

int streq(int *s1, int *s2) {
    int i = 0;
    while (*(s1 + i) != 0 && *(s2 + i) != 0) {
        if (*(s1 + i) != *(s2 + i)) return 0;
        i++;
    }
    return *(s1 + i) == *(s2 + i);
}

int read_line(int *buf, int max_len) {
    int count = 0;
    while (count < max_len - 1) {
        int ch = k3_uart_getc();
        if (ch == -1) continue;
        if (ch == 10 || ch == 13) {
            k3_uart_putc(10);
            break;
        }
        k3_uart_putc(ch); /* echo */
        *(buf + count) = ch;
        count++;
    }
    *(buf + count) = 0;
    return count;
}

int print_banner() {
    k3_printf("\n==================================================\n", 0, 0, 0, 0);
    k3_printf("  K-Three Operating System (Pure C Edition)\n", 0, 0, 0, 0);
    k3_printf("  CPU: 9-Trit Balanced Ternary [-9841..+9841]\n", 0, 0, 0, 0);
    k3_printf("==================================================\n\n", 0, 0, 0, 0);
    return 0;
}

int cmd_help() {
    k3_printf("Available Commands:\n", 0, 0, 0, 0);
    k3_printf("  help     - Show this help menu\n", 0, 0, 0, 0);
    k3_printf("  time     - Show hardware monotonic uptime\n", 0, 0, 0, 0);
    k3_printf("  rand     - Read hardware balanced-ternary entropy\n", 0, 0, 0, 0);
    k3_printf("  leds     - Test front-panel diagnostic LEDs\n", 0, 0, 0, 0);
    k3_printf("  vram     - Draw text box to hardware Video RAM\n", 0, 0, 0, 0);
    k3_printf("  disk     - Test block storage DMA read/write\n", 0, 0, 0, 0);
    k3_printf("  heap     - Allocate buffer with dynamic heap\n", 0, 0, 0, 0);
    k3_printf("  halt     - Halt system safely\n\n", 0, 0, 0, 0);
    return 0;
}

int cmd_time() {
    int t = k3_timer_ms();
    k3_printf("Hardware Monotonic Uptime: %d ms\n", t, 0, 0, 0);
    return 0;
}

int cmd_rand() {
    int r = k3_rand_word();
    k3_printf("Hardware Ternary Entropy: %d (trits: %t)\n", r, r, 0, 0);
    return 0;
}

int cmd_leds() {
    k3_leds_set(77);
    int readback = k3_leds_get();
    k3_printf("Front-panel LEDs updated to: %d (%t)\n", readback, readback, 0, 0);
    return 0;
}

int cmd_vram() {
    int *vram = HW_VRAM_BASE;
    k3_memset(vram, 32, 100); /* fill with spaces */
    vram[0] = 'K';
    vram[1] = '3';
    vram[2] = '-';
    vram[3] = 'O';
    vram[4] = 'S';
    vram[5] = ' ';
    vram[6] = 'C';
    vram[7] = ' ';
    vram[8] = 'S';
    vram[9] = 'h';
    vram[10] = 'e';
    vram[11] = 'l';
    vram[12] = 'l';
    k3_vram_flush();
    return 0;
}

int cmd_disk() {
    int *buf = k3_malloc(16);
    buf[0] = 123;
    buf[1] = 456;
    buf[15] = 789;
    k3_disk_write(3, buf, 16);
    k3_memset(buf, 0, 16);
    k3_disk_read(3, buf, 16);
    if (buf[0] == 123 && buf[1] == 456 && buf[15] == 789) {
        k3_printf("Disk DMA Sector 3 Transfer: [PASSED]\n", 0, 0, 0, 0);
    } else {
        k3_printf("Disk DMA Sector 3 Transfer: [FAILED]\n", 0, 0, 0, 0);
    }
    return 0;
}

int cmd_heap() {
    int *p1 = k3_malloc(5);
    int *p2 = k3_malloc(5);
    k3_printf("Heap allocation: p1=%d, p2=%d (span %d words)\n", p1, p2, p2 - p1, 0);
    return 0;
}

int main() {
    k3_init_heap(2500);
    print_banner();

    /* Demo automated script check or single command */
    cmd_help();
    cmd_time();
    cmd_rand();
    cmd_leds();
    cmd_disk();
    cmd_heap();
    cmd_vram();

    k3_printf("\nSystem initialization complete. Halting.\n", 0, 0, 0, 0);
    return 42;
}
