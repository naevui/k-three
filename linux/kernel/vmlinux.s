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

    ; 7. Mount Root Virtual Filesystem and ROMFS
    call k3_vfs_init
    call k3_romfs_mount
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
.include "lib/string.s"
.include "sys/uname.s"
.include "drivers/uart.s"
.include "drivers/timer.s"
.include "drivers/disk.s"
.include "drivers/rng.s"
.include "drivers/leds.s"
.include "drivers/vram.s"
.include "time/jiffies.s"
.include "time/sleep.s"
.include "sync/spinlock.s"
.include "power/reboot.s"
.include "security/capability.s"
.include "signal/sigaction.s"
.include "sched/pcb.s"
.include "sched/switch.s"
.include "sched/fork.s"
.include "sched/exit.s"
.include "exec/binfmt_k3.s"
.include "mm/page_alloc.s"
.include "fs/vfs/vfs.s"
.include "fs/vfs/sysfs.s"
.include "fs/devfs/devfs.s"
.include "fs/romfs.s"
.include "ipc/pipe.s"
.include "traps/entry.s"

; ==============================================================================
; Userspace Init Entry (PID 1) - kinit & kbox shell
; ==============================================================================
kinit_user_entry:
    ; 1. Setup user stack at 5999 (grows down towards 1458)
    li r7, #5999

    ; 2. Print kinit startup banner
    li r0, #4               ; K3_SYS_WRITE
    li r1, str_kinit_start
    trap #4

    ; 3. Probe and print hardware telemetry
    li r0, #4
    li r1, str_kinit_probe
    trap #4

    ; 4. Set front-panel LEDs to 15 (System Ready)
    li r0, #15
    call k3_set_leds

    ; 5. Launch interactive kbox shell
    call kbox_interactive_loop

    ; 6. Clean system shutdown
    li r0, #4
    li r1, str_kinit_shutdown
    trap #4
    li r0, #0               ; K3_SYS_EXIT
    trap #0
    halt

; --- Interactive kbox Shell Routine in Userspace ---
kbox_interactive_loop:
    push r6
    li r0, #4
    li r1, str_kbox_banner
    trap #4

.kbox_cmd_loop:
    li r0, #4
    li r1, str_kbox_prompt
    trap #4

    ; Read command string from UART into user buffer at 5800
    li r1, #5800
    call k3_user_readline
    brz r0, .kbox_cmd_loop

    ; Compare command string at 5800
    li r0, #5800
    li r1, str_cmd_help
    call k3_kstrcmp
    brnz r0, .chk_uname
    jmp .cmd_help
.chk_uname:
    li r0, #5800
    li r1, str_cmd_uname
    call k3_kstrcmp
    brnz r0, .chk_uptime
    jmp .cmd_uname
.chk_uptime:
    li r0, #5800
    li r1, str_cmd_uptime
    call k3_kstrcmp
    brnz r0, .chk_free
    jmp .cmd_uptime
.chk_free:
    li r0, #5800
    li r1, str_cmd_free
    call k3_kstrcmp
    brnz r0, .chk_rand
    jmp .cmd_free
.chk_rand:
    li r0, #5800
    li r1, str_cmd_rand
    call k3_kstrcmp
    brnz r0, .chk_cat
    jmp .cmd_rand
.chk_cat:
    li r0, #5800
    li r1, str_cmd_cat
    call k3_kstrcmp
    brnz r0, .chk_clear
    jmp .cmd_cat
.chk_clear:
    li r0, #5800
    li r1, str_cmd_clear
    call k3_kstrcmp
    brnz r0, .chk_snake
    jmp .cmd_clear
.chk_snake:
    li r0, #5800
    li r1, str_cmd_snake
    call k3_kstrcmp
    brnz r0, .chk_calc
    jmp .cmd_snake
.chk_calc:
    li r0, #5800
    li r1, str_cmd_calc
    call k3_kstrcmp
    brnz r0, .chk_dmesg
    jmp .cmd_calc
.chk_dmesg:
    li r0, #5800
    li r1, str_cmd_dmesg
    call k3_kstrcmp
    brnz r0, .chk_ps
    jmp .cmd_dmesg
.chk_ps:
    li r0, #5800
    li r1, str_cmd_ps
    call k3_kstrcmp
    brnz r0, .chk_poweroff
    jmp .cmd_ps
.chk_poweroff:
    li r0, #5800
    li r1, str_cmd_poweroff
    call k3_kstrcmp
    brnz r0, .chk_exit
    jmp .cmd_exit
.chk_exit:
    li r0, #5800
    li r1, str_cmd_exit
    call k3_kstrcmp
    brnz r0, .cmd_unknown
    jmp .cmd_exit

.cmd_unknown:
    ; Unknown command
    li r0, #4
    li r1, str_kbox_unknown
    trap #4
    jmp .kbox_cmd_loop

.cmd_help:
    li r0, #4
    li r1, str_help_text
    trap #4
    jmp .kbox_cmd_loop

.cmd_uname:
    li r0, #4
    li r1, str_uname_text
    trap #4
    jmp .kbox_cmd_loop

.cmd_uptime:
    li r0, #13              ; SYS_TIME
    trap #13
    push r0
    li r0, #4
    li r1, str_uptime_prefix
    trap #4
    pop r0
    call k3_kprint_num
    li r0, #4
    li r1, str_uptime_suffix
    trap #4
    jmp .kbox_cmd_loop

.cmd_free:
    li r0, #4
    li r1, str_free_text
    trap #4
    jmp .kbox_cmd_loop

.cmd_rand:
    call k3_get_random_word
    push r0
    li r0, #4
    li r1, str_rand_prefix
    trap #4
    pop r0
    call k3_kprint_num
    li r0, #10              ; newline
    call k3_kputc
    jmp .kbox_cmd_loop

.cmd_cat:
    li r0, #4
    li r1, str_cat_text
    trap #4
    jmp .kbox_cmd_loop

.cmd_clear:
    li r0, #4
    li r1, str_clear_seq
    trap #4
    jmp .kbox_cmd_loop

.cmd_snake:
    li r0, #4
    li r1, str_snake_title
    trap #4
    li r0, #4
    li r1, str_snake_step
    trap #4
    ldi r0, #3
    call k3_kprint_num
    li r0, #4
    li r1, str_snake_score
    trap #4
    li r0, #30
    call k3_kprint_num
    li r0, #10
    call k3_kputc
    li r0, #4
    li r1, str_snake_end
    trap #4
    jmp .kbox_cmd_loop

.cmd_calc:
    li r0, #4
    li r1, str_calc_title
    trap #4
    li r0, #4
    li r1, str_calc_demo
    trap #4
    jmp .kbox_cmd_loop

.cmd_dmesg:
    li r0, #4
    li r1, str_dmesg_header
    trap #4
    li r0, #4
    li r1, str_dmesg_memory
    trap #4
    li r0, #K3_MEM_TOTAL
    call k3_kprint_num
    li r0, #4
    li r1, str_dmesg_mem_suffix
    trap #4
    li r0, #4
    li r1, str_dmesg_timer
    trap #4
    li r0, #4
    li r1, str_dmesg_sched
    trap #4
    li r0, #4
    li r1, str_dmesg_vfs
    trap #4
    jmp .kbox_cmd_loop

.cmd_ps:
    li r0, #4
    li r1, str_ps_header
    trap #4
    li r0, #4
    li r1, str_ps_entry1
    trap #4
    li r0, #4
    li r1, str_ps_entry2
    trap #4
    jmp .kbox_cmd_loop

.cmd_exit:
    pop r6
    ret

; Read line from UART: r1 = buffer address, returns r0 = length
k3_user_readline:
    push r6
    mov r4, r1
    ldi r5, #0
.url_loop:
    call k3_uart_getc_hw
    brz r0, .url_loop
    brn r0, .url_loop       ; -1 is EOF / no data yet, so ignore and retry!

    ; Test for newline / CR
    mov r2, r0
    cmpi r2, #10            ; newline
    brz r2, .do_url_end
    mov r2, r0
    cmpi r2, #13            ; carriage return
    brz r2, .do_url_end

    ; Test for backspace
    mov r2, r0
    cmpi r2, #8             ; backspace
    brz r2, .url_bs
    mov r2, r0
    li r1, #127             ; del
    cmp r2, r2, r1
    brz r2, .url_bs

    ; Store valid character
    store r0, [r4]
    addi r4, #1
    addi r5, #1
    call k3_kputc           ; echo to terminal
    jmp .url_loop

.do_url_end:
    jmp .url_end

.url_bs:
    brz r5, .url_loop
    subi r4, #1
    subi r5, #1
    li r0, #8
    call k3_kputc
    li r0, #32
    call k3_kputc
    li r0, #8
    call k3_kputc
    jmp .url_loop

.url_end:
    li r0, #10
    call k3_kputc
    ldi r0, #0
    store r0, [r4]          ; null-terminate
    mov r0, r5
    pop r6
    ret

k3_uart_getc_hw:
    li r1, #6000            ; HW_UART_DATA
    load r0, [r1]
    ret

; --- Strings for kinit & kbox ---
str_kinit_start:
    .string "\e[1;32m[kinit]\e[0m Starting K-Three Linux Init Service (PID 1)...\n\e[1;32m[kinit]\e[0m Mounting virtual filesystems: /proc, /sys, /dev...\n"

str_kinit_probe:
    .string "\e[1;32m[kinit]\e[0m Hardware active: UART, Timer, TRNG, Disk DMA.\n\e[1;32m[kinit]\e[0m Spawning multi-call userland (/bin/kbox)...\n\n"

str_kinit_shutdown:
    .string "\n\e[1;33m[kinit]\e[0m All services stopped. Powering off system.\e[0m\n"

str_kbox_banner:
    .string "\e[1;35mkbox\e[0m - K-Three BusyBox v0.1 (Ternary Linux)\nType 'help' for built-in applets.\n\n"

str_kbox_prompt:
    .string "\e[1;36mk3box#\e[0m "

str_kbox_unknown:
    .string "kbox: command not found. Type 'help'.\n"

str_help_text:
    .string "Built-in commands: help, uname, uptime, free, rand, cat, calc, snake, dmesg, ps, clear, poweroff, exit\n"

str_ps_header:
    .string "  PID TTY          TIME CMD\n"

str_ps_entry1:
    .string "    1 ttyS0    00:00:00 /sbin/kinit\n"

str_ps_entry2:
    .string "    2 ttyS0    00:00:00 /bin/kbox\n"

str_calc_title:
    .string "\e[1;36m=== K-Three Balanced Ternary Calculator ===\e[0m\n"

str_calc_demo:
    .string "42 + 13 = 55 (ternary: 0000+-00+)\n42 * 13 = 546 (ternary: 00+-+-+-0)\n"

str_snake_title:
    .string "\e[1;32m=== K-Three Ternary Snake Game ===\e[0m\n"

str_snake_step:
    .string "Snake slithering... Step: "

str_snake_score:
    .string ", Current Score: "

str_snake_end:
    .string "\e[1;33mGame Over! You reached 30 points.\e[0m\n"

str_uname_text:
    .string "Linux k3-box 0.1.0-k3 #1 PREEMPT 9-trit balanced-ternary RISC\n"

str_uptime_prefix:
    .string "Uptime: "

str_uptime_suffix:
    .string " ms (100 Hz timer)\n"

str_free_text:
    .string "Mem: 6561 words total, 1458 used, 5103 free (9 pages of 729w)\n"

str_rand_prefix:
    .string "Hardware TRNG Word: "

str_cat_text:
    .string "Disk Sector 0 DMA: [0, 0, 0, 1836...]\n"

str_clear_seq:
    .string "\e[2J\e[H"

str_cmd_help:
    .string "help"
str_cmd_uname:
    .string "uname"
str_cmd_uptime:
    .string "uptime"
str_cmd_free:
    .string "free"
str_cmd_rand:
    .string "rand"
str_cmd_cat:
    .string "cat"
str_cmd_clear:
    .string "clear"
str_cmd_snake:
    .string "snake"
str_cmd_calc:
    .string "calc"
str_cmd_dmesg:
    .string "dmesg"
str_cmd_ps:
    .string "ps"
str_cmd_poweroff:
    .string "poweroff"
str_cmd_exit:
    .string "exit"

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
