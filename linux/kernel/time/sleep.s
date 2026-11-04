; ==============================================================================
; linux/kernel/time/sleep.s
;
; High-Resolution Sleep & Busy-Wait Loop for K-Three Linux
; Implements k3_msleep and SYS_NANOSLEEP (162).
; ==============================================================================

; --- Sleep for N Milliseconds ---
; Input: r0 = duration in milliseconds
; Clobbers: r0, r1, r2, r4
k3_msleep:
    push r6
    push r4
    mov r4, r0              ; r4 = requested sleep ms

    ; Get start time in ms
    call k3_get_time_ms
    mov r2, r0              ; r2 = start time

.msleep_loop:
    call k3_get_time_ms     ; r0 = current time
    sub r0, r0, r2          ; elapsed = current - start
    brn r0, .msleep_time_wrapped
    jmp .msleep_check

.msleep_time_wrapped:
    ; Handle counter wrap-around
    neg r0, r0

.msleep_check:
    cmp r1, r0, r4          ; compare elapsed vs requested
    brn r1, .msleep_loop

    pop r4
    pop r6
    ret
