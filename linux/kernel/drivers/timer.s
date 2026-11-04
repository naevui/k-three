; ==============================================================================
; linux/kernel/drivers/timer.s
;
; Low-Level Hardware MMIO Timer & Clockevent Driver
; Uses MMIO registers 6004..6007 and K3_IRQ_TIMER (IRQ 1)
; ==============================================================================

; --- Initialize Hardware Timer ---
; Input: r0 = timer interval in milliseconds (e.g. 10 ms per tick)
; Clobbers: r1
k3_timer_init:
    li r1, #6006            ; HW_TIMER_INT
    store r0, [r1]
    li r1, #6007            ; HW_TIMER_CTRL
    ldi r0, #1              ; Enable timer interrupts
    store r0, [r1]
    ret

; --- Read Current Hardware Milliseconds ---
; Output: r0 = low 9-trit timer value
; Clobbers: r1
k3_get_time_ms:
    li r1, #6004            ; HW_TIMER_L
    load r0, [r1]
    ret

; --- Acknowledge and Clear Timer Interrupt ---
; Clobbers: r0, r1
k3_timer_ack:
    li r1, #6007            ; HW_TIMER_CTRL
    ldi r0, #1              ; Acknowledge & clear IRQ
    store r0, [r1]
    ret
