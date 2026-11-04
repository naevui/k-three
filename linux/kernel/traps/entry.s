; ==============================================================================
; linux/kernel/traps/entry.s
;
; Native Hardware Trap & Syscall Dispatch Engine for K-Three Linux
; Invoked directly by K-Three hardware on exception or SYS_TRAP instruction.
; ==============================================================================

; --- Hardware Trap Entry Point ---
; Vector address registered via SETVEC instruction.
; Hardware automatically:
;   - Sets mode to KERNEL_MODE
;   - Saves return PC into EPC
;   - Jumps here
k3_trap_entry:
    ; 1. Preserve interrupted context onto Kernel Stack
    push r0
    push r1
    push r2
    push r3
    push r4
    push r5
    push r6

    ; 2. Fetch system call code / trap reason (passed in user r0)
    ; User's original r0 is at [r7 + 6]
    mov r4, r7
    addi r4, #6
    load r2, [r4]

    ; 3. Dispatch system calls
    mov r0, r2
    cmpi r0, #0             ; SYS_EXIT
    brnz r0, .try_putc
    jmp .trap_sys_exit
.try_putc:
    mov r0, r2
    cmpi r0, #1             ; SYS_PUTC
    brnz r0, .try_puts
    jmp .trap_sys_putc
.try_puts:
    mov r0, r2
    cmpi r0, #4             ; SYS_PUTS
    brnz r0, .try_time
    jmp .trap_sys_puts
.try_time:
    mov r0, r2
    cmpi r0, #13            ; SYS_TIME
    brnz r0, .try_getpid
    jmp .trap_sys_time
.try_getpid:
    mov r0, r2
    cmpi r0, #20            ; SYS_GETPID
    brz r0, .trap_sys_getpid
    jmp .trap_done

.trap_sys_putc:
    ; Fetch user r1 from [r7 + 5]
    mov r4, r7
    addi r4, #5
    load r0, [r4]
    call k3_kputc
    jmp .trap_done

.trap_sys_puts:
    mov r4, r7
    addi r4, #5
    load r0, [r4]
    call k3_kputs
    jmp .trap_done

.trap_sys_time:
    call k3_get_time_ms
    ; Place return value into saved r0 on stack [r7 + 6]
    mov r4, r7
    addi r4, #6
    store r0, [r4]
    jmp .trap_done

.trap_sys_getpid:
    li r4, k3_current_task
    load r0, [r4]
    mov r4, r7
    addi r4, #6
    store r0, [r4]
    jmp .trap_done

.trap_sys_exit:
    ; System poweroff on primary init exit
    li r0, str_sys_halted
    call k3_kputs
    li r1, #6011            ; HW_SYS_HALT
    ldi r0, #1
    store r0, [r1]
    halt

.trap_done:
    ; 4. Restore preserved registers from stack
    pop r6
    pop r5
    pop r4
    pop r3
    pop r2
    pop r1
    pop r0

    ; 5. Return to user mode via hardware ERET instruction
    eret

str_sys_halted:
    .string "\n\e[1;36m[kernel] Process 1 exited. System halted safely.\e[0m\n"
