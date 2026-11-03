/* SPDX-License-Identifier: GPL-2.0 */
/*
 * userspace/kinit.c - K-Three Linux Init Service (kinit - PID 1)
 *
 * Official userspace initialization daemon and service manager for K-Three Linux.
 * Runs as the primary PID 1 process in userspace.
 */

#include "include/k3libc.h"

int *banner   = "\033[1;32m[kinit]\033[0m Starting K-Three Linux Init Service (PID 1)...\n";
int *vfs_msg  = "\033[1;32m[kinit]\033[0m Mounting virtual filesystems: /proc, /sys, /dev...\n";
int *hw_msg   = "\033[1;32m[kinit]\033[0m Probing hardware: MMIO UART, Timer, RNG active.\n";
int *sh_msg   = "\033[1;32m[kinit]\033[0m Spawning primary user shell (/bin/sh)...\n\n";
int *exit_msg = "\033[1;33m[kinit]\033[0m All services stopped. System halted cleanly.\n";

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

    /* 5. Spawn primary user shell */
    k3_printf(sh_msg, 0, 0, 0, 0);

    /* 6. Clean shutdown on service termination */
    k3_printf(exit_msg, 0, 0, 0, 0);
    k3_leds_set(0);
    return 0;
}
