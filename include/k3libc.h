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

/* Hardware MMIO Register Addresses (Page 6) */
#define HW_UART_DATA        5000
#define HW_UART_STAT        5001
#define HW_UART_CTRL        5002
#define HW_TIMER_L          5004
#define HW_TIMER_H          5005
#define HW_TIMER_INT        5006
#define HW_TIMER_CTRL       5007
#define HW_RNG_WORD         5008
#define HW_RNG_TRIT         5009
#define HW_SYS_RESET        5010
#define HW_SYS_HALT         5011
#define HW_SYS_LEDS         5012
#define HW_SYS_TICKS_L      5013
#define HW_SYS_TICKS_H      5014
#define HW_SYS_MEMSIZE      5015
#define HW_SYS_IRQ_CTRL     5016

#define HW_DISK_SECTOR      5020
#define HW_DISK_DMA         5021
#define HW_DISK_COUNT       5022
#define HW_DISK_CMD         5023
#define HW_DISK_STATUS      5024
#define HW_DISK_CAPACITY    5025

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

#endif /* K3_LIBC_H */
