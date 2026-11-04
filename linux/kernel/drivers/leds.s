; ==============================================================================
; linux/kernel/drivers/leds.s
;
; Front-Panel Status LED Controller Driver for K-Three Linux
; Interfaces with HW_SYS_LEDS (6012)
; ==============================================================================

; --- Set System LED Pattern ---
; Input: r0 = LED bitmask (0..15)
; Clobbers: r1
k3_set_leds:
    li r1, #6012            ; HW_SYS_LEDS
    store r0, [r1]
    ret

; --- Read Current System LED Pattern ---
; Output: r0 = current LED bitmask
; Clobbers: r1
k3_get_leds:
    li r1, #6012            ; HW_SYS_LEDS
    load r0, [r1]
    ret
