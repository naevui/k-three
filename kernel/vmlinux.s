; ==============================================================================
; kernel/vmlinux.s - K-Three Balanced Ternary Linux Kernel Image
;
; Executable pure kernel image for the K-Three processor.
; Run with: ./build/k3vm build/vmlinux.k3
; ==============================================================================

start:
    ; 1. Set kernel stack pointer
    li r7, #6560

    ; 2. Register hardware trap vector
    li r0, kernel_trap_vector
    setvec r0

    ; 3. Clear screen and print Linux dmesg boot banner
    trap #SYS_CLEAR
    li r0, str_dmesg_boot
    trap #SYS_PUTS

    li r0, str_dmesg_mem
    trap #SYS_PUTS
    trap #SYS_MEMSIZE
    trap #SYS_PUTNUM
    li r0, str_dmesg_mem2
    trap #SYS_PUTS

    li r0, str_dmesg_ready
    trap #SYS_PUTS

    ; 4. Launch Userspace Shell via SYS_EXEC
    li r0, str_init_path
    trap #SYS_EXEC
    mov r2, r0

    ; 5. Shutdown and Power Off
    li r0, str_kernel_shutdown
    trap #SYS_PUTS
    ldi r0, #0
    trap #SYS_EXIT
    halt

; ------------------------------------------------------------------------------
; Kernel Hardware Trap / Exception Vector
; ------------------------------------------------------------------------------
kernel_trap_vector:
    ; Low-level trap handler reached on hardware exception
    eret

; ------------------------------------------------------------------------------
; Kernel String Constants
; ------------------------------------------------------------------------------
str_dmesg_boot:
    .string "\e[1;36m[    0.000000] Linux version 0.1-k3 (root@k3-build) (k3-elf-gcc) #1 PREEMPT\e[0m\n[    0.000000] Machine model: K-Three 9-Trit Balanced Ternary Processor\n[    0.000000] CPU features: 9-trit ALU, symmetric math, [-9841..+9841]\n"

str_dmesg_mem:
    .string "[    0.000000] Memory: "
str_dmesg_mem2:
    .string " words available (3 pages of 729 words, NOMMU)\n[    0.000000] Trap Vector registered at address 0x0000\n[    0.000000] Calibrating delay loop... 42.00 BogoMIPS\n[    0.000000] VFS: Mounted root (ternary-romfs) readonly.\n\e[1;32m[    0.000000] Freeing unused kernel memory... Done\e[0m\n\e[1;33m[    0.000000] Run /sbin/init as init process (pid 1)\e[0m\n\n"

str_dmesg_ready:
    .string "Welcome to K-Three Linux!\n"

str_init_path:
    .string "build/examples/k3os.k3"

str_kernel_shutdown:
    .string "\n\e[1;33m[kernel] System safely halted. Power off.\e[0m\n"
