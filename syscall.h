#ifndef K3_SYSCALL_H
#define K3_SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#include "vm.h"

/*
 * Standard K-Three Syscall codes (0..37).
 * All codes fit in the 4-trit immediate [-40..+40] of TRAP #code.
 *
 * Calling convention:
 *   Arguments:   r0, r1, r2
 *   Return val:  r0 (and r1 for secondary/high-word returns)
 *   Scratch:     r0..r2 may be modified by return values
 *   Preserved:   r3..r7
 */

/* Process & Control */
#define K3_SYS_EXIT         0   /* Halt VM with exit code in r0 */

/* Console I/O */
#define K3_SYS_PUTC         1   /* Output char in r0 to stdout */
#define K3_SYS_GETC         2   /* Read char from stdin into r0 (-1 on EOF) */
#define K3_SYS_PUTNUM       3   /* Output integer in r0 as decimal */
#define K3_SYS_PUTS         4   /* Output null-terminated string at address in r0 to stdout */
#define K3_SYS_GETNUM       5   /* Read decimal integer from stdin into r0 */
#define K3_SYS_PUTTRITS     6   /* Output 9-trit string for r0 (e.g. +0-0+0--) */
#define K3_SYS_GETS         7   /* Read line from stdin into buf r0 (max len r1). Returns len in r0 (-1 on EOF) */
#define K3_SYS_WRITE        8   /* Write r1 characters from buffer r0 to stdout. Returns written count in r0 */

/* Randomness */
#define K3_SYS_RANDOM       9   /* Pseudo-random 9-trit word into r0 [-9841..9841] */
#define K3_SYS_RANDOM_TRIT  10  /* Random single trit into r0 (-1, 0, +1) */

/* File I/O (Virtual File Descriptors) */
#define K3_SYS_OPEN         11  /* Open path r0 with mode r1 (0:r, 1:w, 2:a, 3:r+). Returns fd in r0 (-1 err) */
#define K3_SYS_CLOSE        12  /* Close fd in r0. Returns 0 on success, -1 on error */
#define K3_SYS_READ         13  /* Read up to r2 words from fd r0 into buf r1. Returns words read in r0 (-1 err) */
#define K3_SYS_WRITE_FD     14  /* Write r2 words from buf r1 to fd r0. Returns words written in r0 (-1 err) */
#define K3_SYS_SEEK         15  /* Seek fd r0 to offset r1, whence r2 (0:set, 1:cur, 2:end). Returns offset in r0 */
#define K3_SYS_REMOVE       16  /* Remove file at path r0. Returns 0 on success, -1 on error */

/* Time & Timing */
#define K3_SYS_TIME         17  /* Millisecond timestamp: r0 = low 9 trits, r1 = high 9 trits */
#define K3_SYS_SLEEP        18  /* Sleep for r0 milliseconds */

/* Terminal & Screen Control (ANSI / TUI) */
#define K3_SYS_CLEAR        19  /* Clear terminal screen and reset cursor to home (1,1) */
#define K3_SYS_SETCURSOR    20  /* Set terminal cursor to row r0, col r1 (1-based) */
#define K3_SYS_KEYHIT       21  /* Non-blocking key check on stdin: 1 if key available, 0 if not into r0 */

/* System Info & Diagnostics */
#define K3_SYS_MEMSIZE      22  /* Total memory words (2187) into r0 */
#define K3_SYS_INSN_COUNT   23  /* Instruction count executed: r0 = low 9 trits, r1 = high 9 trits */
#define K3_SYS_PUTC_ERR     24  /* Output char in r0 to stderr */
#define K3_SYS_PUTS_ERR     25  /* Output null-terminated string at address in r0 to stderr */

/* Process Execution & Visuals */
#define K3_SYS_EXEC         26  /* Execute program file at path r0. Returns child exit status in r0 */
#define K3_SYS_PUTTRITS_COLOR 27/* Output 9-trit string for r0 with ANSI colors (+: green, -: red, 0: dim) */

/* Host Hardware & System Info */
#define K3_SYS_HOST_INFO    28  /* Write host string to buf r0 (max r1, sel r2: 0=OS, 1=Host, 2=Date, 3=All) */
#define K3_SYS_DATETIME     29  /* Real-time clock: r0=year, r1=month, r2=day, r3=hour, r4=min, r5=sec */
#define K3_SYS_HOST_METRICS 30  /* Host hardware: r0=cores, r1=ram_total_mb, r2=ram_free_mb */
#define K3_SYS_SYSINFO      31  /* Print full formatted system info block (arg r0 = command count) */
#define K3_SYS_HELP         32  /* Print full formatted OS command reference guide */

/* Graphical Interface & Framebuffer */
#define K3_SYS_DRAW_VRAM    33  /* Render VRAM buffer at r0 (w=r1, h=r2) to terminal */
#define K3_SYS_GUI_DESKTOP  34  /* Launch interactive K3-OS Graphical Desktop environment */
#define K3_SYS_RAW_MODE     35  /* Set terminal raw unbuffered mode: r0=1 (raw), r0=0 (cooked) */

/* Extended File I/O */
#define K3_SYS_RENAME       36  /* Rename file: r0 = old path, r1 = new path. Returns 0 or -1 */
#define K3_SYS_FILE_SIZE    37  /* File size: r0 = path. Returns size in bytes in r0 (-1 on error) */

#define K3_MAX_HOST_FDS     16

/*
 * Default host syscall handler.
 * Can be installed in k3_vm_callbacks.on_syscall.
 * Returns true if handled, false if unknown (falls back to TRAP fault).
 */
bool k3_host_syscall(void *user, k3_vm *vm, int32_t code);

/*
 * Reset syscall state (closes any open host file descriptors fd >= 3).
 */
void k3_syscall_reset(void);

#endif /* K3_SYSCALL_H */
