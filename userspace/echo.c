/* SPDX-License-Identifier: GPL-2.0 */
/*
 * userspace/echo.c - K-Three Minimal Userspace Echo (/bin/echo)
 *
 * Standalone utility to print strings/telemetry to UART console.
 */

#include "include/k3libc.h"

int *msg = "Hello from K-Three Linux Userspace (/bin/echo)!\n";

int main() {
    k3_printf(msg, 0, 0, 0, 0);
    return 0;
}
