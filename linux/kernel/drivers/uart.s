; ==============================================================================
; linux/kernel/drivers/uart.s
;
; Low-Level Hardware MMIO UART Driver for K-Three Linux Kernel
; Communicates via MMIO register 6000 (Page 8)
; ==============================================================================

; --- Print Single Character to UART ---
; Input: r0 = ASCII character code
; Clobbers: r1, r2
k3_kputc:
    li r1, #6000            ; K3_HW_UART_DATA
    store r0, [r1]
    ret

; --- Print Null-Terminated String to UART ---
; Input: r0 = pointer to null-terminated string
; Clobbers: r0, r1, r2
k3_kputs:
    mov r2, r0              ; r2 = string pointer
    li r1, #6000            ; r1 = UART DATA register
.kputs_loop:
    load r0, [r2]
    brz r0, .kputs_done
    store r0, [r1]
    addi r2, #1
    jmp .kputs_loop
.kputs_done:
    ret

; --- Print Decimal Integer to UART ---
; Input: r0 = signed 9-trit integer
; Clobbers: r0, r1, r2, r4
k3_kprint_num:
    push r6
    push r4
    brn r0, .knum_neg
    jmp .knum_pos
.knum_neg:
    neg r0, r0
    push r0
    li r0, #45              ; '-'
    call k3_kputc
    pop r0
.knum_pos:
    mov r4, r0              ; r4 = original number
    cmpi r0, #10
    brn r0, .knum_single
    ; Recursive division of r4 by 10
    ldi r0, #0
.knum_div_loop:
    subi r4, #10
    brn r4, .knum_div_done
    addi r0, #1
    jmp .knum_div_loop
.knum_div_done:
    addi r4, #10            ; remainder in r4
    push r4                 ; save remainder
    call k3_kprint_num
    pop r0                  ; restore remainder
    addi r0, #40
    addi r0, #8             ; '0' + digit
    call k3_kputc
    pop r4
    pop r6
    ret
.knum_single:
    mov r0, r4
    addi r0, #40
    addi r0, #8             ; +48 ('0' + digit)
    call k3_kputc
    pop r4
    pop r6
    ret
