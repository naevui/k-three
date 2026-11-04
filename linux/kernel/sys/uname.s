; ==============================================================================
; linux/kernel/sys/uname.s
;
; POSIX Uname System Architecture Telemetry Provider for K-Three Linux
; ==============================================================================

str_uname_sysname:
    .string "Linux"
str_uname_nodename:
    .string "k-three-box"
str_uname_release:
    .string "0.1.0-k3"
str_uname_version:
    .string "#1 PREEMPT Balanced Ternary 2026"
str_uname_machine:
    .string "k3-9trit"

; --- Provide System Uname Data ---
; Input: r0 = destination buffer pointer
; Clobbers: r1, r2, r3
k3_sys_uname:
    push r6
    push r0

    ; Copy sysname
    mov r1, r0
    li r0, str_uname_sysname
    call k3_kstrcpy
    pop r0
    pop r6
    ldi r0, #0
    ret

; Copy string helper: r0 = src, r1 = dst
k3_kstrcpy:
.strcpy_loop:
    load r2, [r0]
    store r2, [r1]
    brz r2, .strcpy_done
    addi r0, #1
    addi r1, #1
    jmp .strcpy_loop
.strcpy_done:
    ret
