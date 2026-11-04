/* SPDX-License-Identifier: GPL-2.0 */
/*
 * userspace/kbox.c - K-Three BusyBox (kbox)
 *
 * Swiss Army Knife for K-Three Linux. Combines core Unix utilities
 * into a single multi-call binary compiled by k3cc.
 *
 * Supported commands:
 *   help, sh, echo, cat, uptime, uname, free, rand, clear, poweroff
 */

#include "include/k3libc.h"

int *kbox_banner = "\033[1;35mkbox\033[0m - K-Three Multi-Call Binary v0.1\nType 'help' for built-in applets.\n\n";
int *kbox_prompt = "\033[1;36mk3box#\033[0m ";
int *kbox_help_msg = "Applets: help, uname, uptime, free, rand, cat, clear, poweroff, exit\n";

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

/* Applet: uname */
int applet_uname() {
    k3_printf("Linux k3-box 0.1.0-k3 #1 PREEMPT 9-trit balanced-ternary RISC\n", 0, 0, 0, 0);
    return 0;
}

/* Applet: uptime */
int applet_uptime() {
    int t = k3_timer_ms();
    k3_printf("Uptime: %d ms, Clocksource: MMIO Timer (100 Hz)\n", t, 0, 0, 0);
    return 0;
}

/* Applet: free */
int applet_free() {
    k3_printf("              total        used        free\n", 0, 0, 0, 0);
    k3_printf("Mem:          6561        1458        5103 words\n", 0, 0, 0, 0);
    k3_printf("Pages:           9           2           7 frames (729w each)\n", 0, 0, 0, 0);
    return 0;
}

/* Applet: rand */
int applet_rand() {
    int r = k3_rand_word();
    k3_printf("Hardware TRNG: %d (balanced ternary: %t)\n", r, r, 0, 0);
    return 0;
}

/* Applet: cat (sector 0) */
int applet_cat() {
    int buf[16];
    int stat = k3_disk_read(0, buf, 16);
    k3_printf("Disk Sector 0 DMA: [%d, %d, %d, %d...]\n", buf[0], buf[1], buf[2], buf[3]);
    return 0;
}

/* Primary kbox interactive shell */
int run_kbox_shell() {
    int line[64];
    k3_printf(kbox_banner, 0, 0, 0, 0);

    int running = 1;
    while (running == 1) {
        k3_printf(kbox_prompt, 0, 0, 0, 0);
        int len = k_readline(line, 64);
        if (len == 0) continue;

        if (k_strcmp(line, "help") == 0) {
            k3_printf(kbox_help_msg, 0, 0, 0, 0);
        } else if (k_strcmp(line, "uname") == 0) {
            applet_uname();
        } else if (k_strcmp(line, "uptime") == 0) {
            applet_uptime();
        } else if (k_strcmp(line, "free") == 0) {
            applet_free();
        } else if (k_strcmp(line, "rand") == 0) {
            applet_rand();
        } else if (k_strcmp(line, "cat") == 0) {
            applet_cat();
        } else if (k_strcmp(line, "clear") == 0) {
            k3_printf("\033[2J\033[H", 0, 0, 0, 0);
        } else if (k_strcmp(line, "poweroff") == 0) {
            k3_printf("\033[1;31mSystem powering off...\033[0m\n", 0, 0, 0, 0);
            k3_halt();
        } else if (k_strcmp(line, "exit") == 0) {
            running = 0;
        } else {
            k3_printf("kbox: command not found: %s. Type 'help'.\n", line, 0, 0, 0);
        }
    }
    return 0;
}

int main() {
    run_kbox_shell();
    k3_printf("kbox shell terminated.\n", 0, 0, 0, 0);
    k3_halt();
    return 0;
}
