/*
 * include/k3libc.h - K-Three Minimal C Standard Library & Hardware Drivers
 *
 * Provides hardware MMIO drivers, memory utilities, and bump allocator for
 * programs compiled with k3cc.
 *
 * Strict C11, balanced-ternary architecture.
 */

#ifndef K3_LIBC_H
#define K3_LIBC_H

/* Hardware MMIO Register Addresses (Page 8) */
#define HW_UART_DATA        6000
#define HW_UART_STAT        6001
#define HW_UART_CTRL        6002
#define HW_TIMER_L          6004
#define HW_TIMER_H          6005
#define HW_TIMER_INT        6006
#define HW_TIMER_CTRL       6007
#define HW_RNG_WORD         6008
#define HW_RNG_TRIT         6009
#define HW_SYS_RESET        6010
#define HW_SYS_HALT         6011
#define HW_SYS_LEDS         6012
#define HW_SYS_TICKS_L      6013
#define HW_SYS_TICKS_H      6014
#define HW_SYS_MEMSIZE      6015
#define HW_SYS_IRQ_CTRL     6016

#define HW_DISK_SECTOR      6020
#define HW_DISK_DMA         6021
#define HW_DISK_COUNT       6022
#define HW_DISK_CMD         6023
#define HW_DISK_STATUS      6024
#define HW_DISK_CAPACITY    6025

#define HW_VRAM_CTRL        6026
#define HW_VRAM_COLS        6027
#define HW_VRAM_ROWS        6028
#define HW_VRAM_BASE        6030
#define HW_VRAM_SIZE        100
#define HW_VRAM_END         6129

/* Heap Memory Allocator State */
int g_heap_ptr = 3000;

int k3_init_heap(int start_addr) {
    g_heap_ptr = start_addr;
    return g_heap_ptr;
}

int *k3_malloc(int words) {
    if (words <= 0) return 0;
    int *allocated = g_heap_ptr;
    g_heap_ptr += words;
    return allocated;
}

int k3_free(int *ptr) {
    ptr = 0;
    return 0;
}

/* Memory Utilities */
int k3_memset(int *dst, int val, int n) {
    int i;
    for (i = 0; i < n; i++) {
        *(dst + i) = val;
    }
    return 0;
}

int k3_memcpy(int *dst, int *src, int n) {
    int i;
    for (i = 0; i < n; i++) {
        *(dst + i) = *(src + i);
    }
    return 0;
}

int k3_memcmp(int *s1, int *s2, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (*(s1 + i) != *(s2 + i)) {
            return *(s1 + i) - *(s2 + i);
        }
    }
    return 0;
}

/* Hardware MMIO Driver Functions */
int k3_uart_putc(int ch) {
    int *reg = HW_UART_DATA;
    *reg = ch;
    return ch;
}

int k3_uart_puts(int *str) {
    int *reg = HW_UART_DATA;
    int i = 0;
    while (*(str + i) != 0) {
        *reg = *(str + i);
        i++;
    }
    return i;
}

int k3_uart_getc() {
    int *reg = HW_UART_DATA;
    return *reg;
}

int k3_timer_ms() {
    int *reg = HW_TIMER_L;
    return *reg;
}

int k3_rand_word() {
    int *reg = HW_RNG_WORD;
    return *reg;
}

int k3_rand_trit() {
    int *reg = HW_RNG_TRIT;
    return *reg;
}

int k3_leds_set(int val) {
    int *reg = HW_SYS_LEDS;
    *reg = val;
    return val;
}

int k3_leds_get() {
    int *reg = HW_SYS_LEDS;
    return *reg;
}

int k3_halt() {
    int *reg = HW_SYS_HALT;
    *reg = 1;
    return 0;
}

int k3_disk_read(int sector, int *dma_buf, int count) {
    int *s_reg = HW_DISK_SECTOR;
    int *d_reg = HW_DISK_DMA;
    int *c_reg = HW_DISK_COUNT;
    int *cmd   = HW_DISK_CMD;
    int *stat  = HW_DISK_STATUS;

    *s_reg = sector;
    *d_reg = dma_buf;
    *c_reg = count;
    *cmd   = 1; /* DISK_CMD_READ */
    return *stat;
}

int k3_disk_write(int sector, int *dma_buf, int count) {
    int *s_reg = HW_DISK_SECTOR;
    int *d_reg = HW_DISK_DMA;
    int *c_reg = HW_DISK_COUNT;
    int *cmd   = HW_DISK_CMD;
    int *stat  = HW_DISK_STATUS;

    *s_reg = sector;
    *d_reg = dma_buf;
    *c_reg = count;
    *cmd   = 2; /* DISK_CMD_WRITE */
    return *stat;
}

int k3_vram_flush() {
    int *reg = HW_VRAM_CTRL;
    *reg = 1;
    return 0;
}

/* Print decimal integer */
int k3_print_num(int n) {
    if (n < 0) {
        k3_uart_putc('-');
        n = -n;
    }
    if (n >= 10) {
        k3_print_num(n / 10);
    }
    k3_uart_putc('0' + (n % 10));
    return 0;
}

/* Print 9-trit balanced ternary representation (+0-0+0--) */
int k3_print_trits(int n) {
    int trits[9];
    int rem = n;
    int i;
    for (i = 0; i < 9; i++) {
        int m = rem % 3;
        if (m == 2 || m == -1) {
            trits[i] = -1;
            rem = (rem + 1) / 3;
        } else if (m == -2 || m == 1) {
            trits[i] = 1;
            rem = (rem - 1) / 3;
        } else {
            trits[i] = 0;
            rem = rem / 3;
        }
    }
    for (i = 8; i >= 0; i--) {
        if (trits[i] == 1) k3_uart_putc('+');
        else if (trits[i] == -1) k3_uart_putc('-');
        else k3_uart_putc('0');
    }
    return 0;
}

/* Formatted print engine (%d, %s, %c, %t, %%) */
int k3_printf(int *fmt, int a1, int a2, int a3, int a4) {
    int arg_idx = 0;
    int i = 0;
    while (*(fmt + i) != 0) {
        int ch = *(fmt + i);
        if (ch == '%' && *(fmt + i + 1) != 0) {
            i++;
            int spec = *(fmt + i);
            int val = 0;
            if (arg_idx == 0) val = a1;
            else if (arg_idx == 1) val = a2;
            else if (arg_idx == 2) val = a3;
            else if (arg_idx == 3) val = a4;
            arg_idx++;

            if (spec == 'd') {
                k3_print_num(val);
            } else if (spec == 's') {
                k3_uart_puts(val);
            } else if (spec == 'c') {
                k3_uart_putc(val);
            } else if (spec == 't') {
                k3_print_trits(val);
            } else if (spec == '%') {
                k3_uart_putc('%');
            }
        } else {
            k3_uart_putc(ch);
        }
        i++;
    }
    return i;
}

#endif /* K3_LIBC_H */
