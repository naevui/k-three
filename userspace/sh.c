/* SPDX-License-Identifier: GPL-2.0 */
/*
 * userspace/sh.c - K-Three Minimal Userspace Shell (/bin/sh)
 *
 * Standalone shell utility for K-Three Linux running on balanced ternary VM.
 */

#include "include/k3libc.h"

int *sh_prompt  = "\033[1;36msh-k3$\033[0m ";
int *sh_help    = "Commands: help, info, mem, rand, time, leds <val>, clear, exit\n";
int *sh_bye     = "Shell exited.\n";
int *sh_unknown = "Unknown command. Type 'help' for command list.\n";
int *sh_cls     = "\033[2J\033[H";

int str_eq(int *s1, int *s2) {
    int i = 0;
    while (*(s1 + i) != 0 && *(s2 + i) != 0) {
        if (*(s1 + i) != *(s2 + i)) return 0;
        i++;
    }
    return (*(s1 + i) == *(s2 + i));
}

int sh_read_line(int *buf, int max_len) {
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

int main() {
    int line[64];
    k3_printf(sh_cls, 0, 0, 0, 0);
    k3_printf("K-Three Userspace Shell (/bin/sh)\nType 'help' for available commands.\n\n", 0, 0, 0, 0);

    int running = 1;
    while (running == 1) {
        k3_printf(sh_prompt, 0, 0, 0, 0);
        int len = sh_read_line(line, 64);
        if (len == 0) continue;

        if (str_eq(line, "help") == 1) {
            k3_printf(sh_help, 0, 0, 0, 0);
        } else if (str_eq(line, "info") == 1) {
            k3_printf("Architecture: 9-trit balanced ternary RISC\nMemory: 6561 words\n", 0, 0, 0, 0);
        } else if (str_eq(line, "time") == 1) {
            int t = k3_timer_ms();
            k3_printf("Uptime: %d ms\n", t, 0, 0, 0);
        } else if (str_eq(line, "rand") == 1) {
            int r = k3_rand_word();
            k3_printf("Hardware RNG Word: %d (%t)\n", r, r, 0, 0);
        } else if (str_eq(line, "clear") == 1) {
            k3_printf(sh_cls, 0, 0, 0, 0);
        } else if (str_eq(line, "exit") == 1) {
            running = 0;
        } else {
            k3_printf(sh_unknown, 0, 0, 0, 0);
        }
    }

    k3_printf(sh_bye, 0, 0, 0, 0);
    return 0;
}
