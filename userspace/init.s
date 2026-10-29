; ==============================================================================
; userspace/init.s
;
; PID 1 - The first userspace init process for K-Three Ternary Linux.
; Runs in USER_MODE (unprivileged).
; ==============================================================================

start:
    ; 1. Print init banner
    li r0, str_init_banner
    trap #SYS_PUTS

    ; 2. Launch userspace shell
    li r0, str_shell_banner
    trap #SYS_PUTS

    ; Spawn the interactive shell
    li r0, str_sh_path
    trap #SYS_EXEC

    ; 3. If shell exits, print shutdown message and halt
    li r0, str_shutdown
    trap #SYS_PUTS

    ldi r0, #0
    trap #SYS_EXIT
    halt

str_init_banner:
    .string "\033[1;32m[init]\033[0m Starting K-Three Linux Userspace (PID 1)...\n\033[1;32m[init]\033[0m Mounting procfs and sysfs...\n\033[1;32m[init]\033[0m Spawning /bin/sh...\n"

str_shell_banner:
    .string "Welcome to K-Three Linux!\n"

str_sh_path:
    .string "build/examples/k3os.k3"

str_shutdown:
    .string "\033[1;33m[init]\033[0m System halted. Power off.\n"
