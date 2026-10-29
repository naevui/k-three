; ==============================================================================
; arch/k3/kernel/entry.S
;
; Low-level Linux / Unix Exception and System Call Vector for K-Three.
;
; When the CPU hardware triggers a trap in user mode:
;   1. Hardware saves return PC into EPC (saved_pc)
;   2. Hardware sets mode to KERNEL_MODE
;   3. Hardware jumps to kernel_trap_entry
; ==============================================================================

    .equ SYS_EXIT, 0
    .equ SYS_PUTC, 1
    .equ SYS_GETC, 2
    .equ SYS_PUTS, 4

kernel_trap_entry:
    ; 1. Save user registers onto the kernel stack (r7)
    push r0
    push r1
    push r2
    push r3
    push r4
    push r5
    push r6

    ; 2. Determine trap source / dispatch syscall
    ; Retrieve saved r0 from stack
    mov r0, r7
    addi r0, #6
    load r0, [r0]

    ; Dispatch basic kernel syscalls:
    ; Syscall 0: Exit
    mov r1, r0
    brz r1, handle_sys_exit

    ; Syscall 1: PUTC (char in r1)
    subi r1, #1
    brz r1, handle_sys_putc

    ; Syscall 4: PUTS (str in r1)
    subi r1, #3
    brz r1, handle_sys_puts

    jmp trap_return

handle_sys_putc:
    mov r0, r7
    addi r0, #5
    load r0, [r0]           ; get user r1
    trap #1                 ; call host console putc
    jmp trap_return

handle_sys_puts:
    mov r0, r7
    addi r0, #5
    load r0, [r0]           ; get user r1 (address)
    trap #4                 ; call host console puts
    jmp trap_return

handle_sys_exit:
    ; Terminate process
    halt

trap_return:
    ; 3. Restore user registers from kernel stack
    pop r6
    pop r5
    pop r4
    pop r3
    pop r2
    pop r1
    pop r0

    ; 4. Atomic return to user mode and saved PC
    eret
