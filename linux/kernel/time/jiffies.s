; ==============================================================================
; linux/kernel/time/jiffies.s
;
; Kernel Jiffies Counter & Clocksource Abstraction for K-Three Linux
; Incremented on every timer interrupt tick (100 Hz / 10ms).
; ==============================================================================

; --- Global Jiffies Tick Counter (64-bit low/high) ---
k3_jiffies_l:
    .word 0
k3_jiffies_h:
    .word 0

; --- Timer Tick Handler (Invoked on K3_IRQ_TIMER) ---
; Clobbers: r0, r1, r2
k3_do_timer_tick:
    li r1, k3_jiffies_l
    load r0, [r1]
    addi r0, #1
    cmpi r0, #9841          ; Word maximum limit
    brp r0, .jiffies_overflow
    store r0, [r1]
    ret

.jiffies_overflow:
    ldi r0, #0
    store r0, [r1]
    li r1, k3_jiffies_h
    load r2, [r1]
    addi r2, #1
    store r2, [r1]
    ret

; --- Get Current Jiffies ---
; Output: r0 = low jiffies, r1 = high jiffies
; Clobbers: r2
k3_get_jiffies:
    li r2, k3_jiffies_l
    load r0, [r2]
    li r2, k3_jiffies_h
    load r1, [r2]
    ret
