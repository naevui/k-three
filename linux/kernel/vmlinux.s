; ==============================================================================
; linux/kernel/boot/head.s
;
; Native Linux Kernel Entry & System Initialization for K-Three
;
; Pure bare-metal execution flow on K-Three Processor:
;   1. Initialize Kernel Stack (r7 = 6560)
;   2. Register Hardware Trap / Syscall Vector (SETVEC)
;   3. Probe and calibrate MMIO Peripherals (UART, Timer, Block Storage)
;   4. Print Kernel dmesg Boot Banner
;   5. Launch Primary Userspace Daemon (PID 1 /sbin/kinit) in USER_MODE
; ==============================================================================

.include "include/k3_kernel.inc"

_start:
    ; 1. Establish initial Kernel Stack Pointer
    li r7, #K3_KERNEL_SP

    ; 2. Register low-level hardware trap & interrupt vector
    li r0, k3_trap_entry
    setvec r0

    ; 3. Print Linux dmesg boot banner via native UART
    li r0, str_dmesg_header
    call k3_kputs

    ; 4. Probe and log physical memory
    li r0, str_dmesg_memory
    call k3_kputs
    li r0, #K3_MEM_TOTAL
    call k3_kprint_num
    li r0, str_dmesg_mem_suffix
    call k3_kputs

    ; 5. Initialize Timer Interrupt Subsystem (100 Hz / 10ms tick)
    li r0, #10
    call k3_timer_init
    li r0, str_dmesg_timer
    call k3_kputs

    ; 6. Initialize Task Control Blocks and Scheduler
    call k3_sched_init
    li r0, str_dmesg_sched
    call k3_kputs

    ; 7. Mount Root Virtual Filesystem
    call k3_vfs_init
    li r0, str_dmesg_vfs
    call k3_kputs

    ; 8. Set front-panel LEDs to indicate system ready (Pattern 15)
    li r0, #15
    call k3_set_leds

    ; 9. Spawn /sbin/kinit as PID 1
    li r0, str_dmesg_kinit
    call k3_kputs

    ; Jump directly to userspace init entry point
    jmp kinit_user_entry

; --- Included Kernel Subsystems ---
.include "drivers/uart.s"
.include "drivers/timer.s"
.include "drivers/disk.s"
.include "drivers/rng.s"
.include "drivers/leds.s"
.include "sched/pcb.s"
.include "sched/switch.s"
.include "sched/fork.s"
.include "sched/exit.s"
.include "mm/page_alloc.s"
.include "fs/vfs/vfs.s"
.include "fs/vfs/sysfs.s"
.include "fs/romfs.s"
.include "ipc/pipe.s"
.include "traps/entry.s"

; ==============================================================================
; Userspace Init Entry (PID 1)
; ==============================================================================
kinit_user_entry:
    ; 1. Setup user stack at 5999 (grows down towards 1458)
    li r7, #5999

    ; 2. Print Userspace welcome banner via SYS_PUTS trap
    li r0, #4               ; K3_SYS_WRITE / SYS_PUTS
    li r1, str_user_banner
    trap #4

    ; 3. Query uptime from kernel via SYS_TIME trap
    li r0, #13              ; K3_SYS_TIME
    trap #13
    mov r5, r0              ; r5 = uptime ms

    ; 4. Query PID via SYS_GETPID trap
    li r0, #20              ; K3_SYS_GETPID
    trap #20
    mov r6, r0              ; r6 = PID

    ; 5. Clean exit via SYS_EXIT trap
    li r0, #0               ; K3_SYS_EXIT
    trap #0
    halt

; ==============================================================================
; Kernel Boot Messages
; ==============================================================================
str_dmesg_header:
    .string "\e[1;36m[    0.000000] Linux version 0.1-k3 (root@k3-build) (k3-elf-gcc) #1 PREEMPT\e[0m\n[    0.000000] Machine model: K-Three 9-Trit Balanced Ternary Processor\n[    0.000000] CPU features: 9-trit ALU, symmetric math, [-9841..+9841]\n"

str_dmesg_memory:
    .string "[    0.000000] Memory: "

str_dmesg_mem_suffix:
    .string " words available (9 pages of 729 words, NOMMU)\n[    0.000000] Zone PFN: Kernel [0..1457], Userspace [1458..5999], MMIO [6000..6129]\n"

str_dmesg_timer:
    .string "[    0.000000] Clocksource: K3 Hardware MMIO Timer (100 Hz, 10ms resolution)\n[    0.000000] Trap Vector registered at address k3_trap_entry\n"

str_dmesg_sched:
    .string "[    0.000000] Sched: Preemptive round-robin scheduler active (4 task slots)\n"

str_dmesg_vfs:
    .string "[    0.000000] VFS: Mounted root (ternary-romfs) readonly.\n\e[1;32m[    0.000000] Freeing unused kernel memory... Done\e[0m\n"

str_dmesg_kinit:
    .string "\e[1;33m[    0.000000] Run /sbin/kinit as init process (pid 1)\e[0m\n\n"

str_user_banner:
    .string "\e[1;32m[kinit]\e[0m Welcome to Native K-Three Linux Userspace (PID 1)!\n\e[1;32m[kinit]\e[0m Successfully running pure 9-trit machine code on K-Three CPU!\n"
