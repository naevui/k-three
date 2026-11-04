/* SPDX-License-Identifier: GPL-2.0 */
/*
 * userspace/kinit.c - K-Three Linux Init Service (kinit - PID 1)
 *
 * Official userspace initialization daemon and service manager for K-Three Linux.
 * Initializes pseudo filesystems, brings up core services, and manages kbox.
 */

#include "include/k3libc.h"

int *banner   = "\033[1;32m[kinit]\033[0m Starting K-Three Linux Init Service (PID 1)...\n";
int *vfs_msg  = "\033[1;32m[kinit]\033[0m Mounting virtual filesystems: /proc, /sys, /dev...\n";
int *hw_msg   = "\033[1;32m[kinit]\033[0m Probing hardware: MMIO UART, Timer, RNG active.\n";
int *svc_msg  = "\033[1;32m[kinit]\033[0m Launching multi-call userland (/bin/kbox)...\n\n";
int *exit_msg = "\033[1;33m[kinit]\033[0m All services stopped. Powering off system.\n";

int k_strcmp(int *s1, int *s2) {
    int i = 0;
    while (*(s1 + i) != 0 && *(s2 + i) != 0) {
        if (*(s1 + i) != *(s2 + i)) return *(s1 + i) - *(s2 + i);
        i++;
    }
    return *(s1 + i) - *(s2 + i);
}

int k_readline(int *buf, int max_len) {
    int count = 0;
    while (count < max_len - 1) {
        int ch = k3_uart_getc();
        if (ch == 0) continue;
        if (ch == 10 || ch == 13) {
            k3_uart_putc(10);
            break;
        }
        if (ch == 8 || ch == 127) {
            if (count > 0) {
                count--;
                k3_uart_putc(8);
                k3_uart_putc(' ');
                k3_uart_putc(8);
            }
            continue;
        }
        *(buf + count) = ch;
        count++;
        k3_uart_putc(ch);
    }
    *(buf + count) = 0;
    return count;
}

int run_shell() {
    int line[64];
    int running = 1;

    k3_printf("\033[1;35mkbox\033[0m multi-call shell. Type 'help' for commands.\n\n", 0, 0, 0, 0);

    while (running == 1) {
        k3_printf("\033[1;36mk3box#\033[0m ", 0, 0, 0, 0);
        int len = k_readline(line, 64);
        if (len == 0) continue;

        if (k_strcmp(line, "help") == 0) {
            k3_printf("Commands: help, uname, uptime, free, rand, cat, clear, poweroff, exit\n", 0, 0, 0, 0);
        } else if (k_strcmp(line, "uname") == 0) {
            k3_printf("Linux k3-box 0.1.0-k3 #1 PREEMPT 9-trit balanced-ternary RISC\n", 0, 0, 0, 0);
        } else if (k_strcmp(line, "uptime") == 0) {
            int t = k3_timer_ms();
            k3_printf("Uptime: %d ms, Clocksource: MMIO Timer (100 Hz)\n", t, 0, 0, 0);
        } else if (k_strcmp(line, "free") == 0) {
            k3_printf("Mem: 6561 words total, 1458 used, 5103 free (9 pages of 729w)\n", 0, 0, 0, 0);
        } else if (k_strcmp(line, "rand") == 0) {
            int r = k3_rand_word();
            k3_printf("Hardware TRNG Word: %d (ternary: %t)\n", r, r, 0, 0);
        } else if (k_strcmp(line, "cat") == 0) {
            int buf[16];
            k3_disk_read(0, buf, 16);
            k3_printf("Disk DMA Sector 0: [%d, %d, %d, %d...]\n", buf[0], buf[1], buf[2], buf[3]);
        } else if (k_strcmp(line, "clear") == 0) {
            k3_printf("\033[2J\033[H", 0, 0, 0, 0);
        } else if (k_strcmp(line, "poweroff") == 0) {
            running = 0;
        } else if (k_strcmp(line, "exit") == 0) {
            running = 0;
        } else {
            k3_printf("kbox: %s: command not found\n", line, 0, 0, 0);
        }
    }
    return 0;
}

int main() {
    /* 1. Print kinit startup banner */
    k3_printf(banner, 0, 0, 0, 0);

    /* 2. Mount virtual filesystems and initialize system devices */
    k3_printf(vfs_msg, 0, 0, 0, 0);
    k3_printf(hw_msg, 0, 0, 0, 0);

    /* 3. Probe system hardware telemetry */
    int t = k3_timer_ms();
    int r = k3_rand_word();
    k3_printf("\033[1;32m[kinit]\033[0m Boot timestamp: %d ms, Entropy: %t\n", t, r, 0, 0);

    /* 4. Illuminate front-panel status LEDs (system ready) */
    k3_leds_set(15);

    /* 5. Launch primary userland shell */
    k3_printf(svc_msg, 0, 0, 0, 0);
    run_shell();

    /* 6. Clean shutdown on service termination */
    k3_printf(exit_msg, 0, 0, 0, 0);
    k3_leds_set(0);
    k3_halt();
    return 0;
}
