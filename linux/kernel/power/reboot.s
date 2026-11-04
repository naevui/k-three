; ==============================================================================
; linux/kernel/power/reboot.s
;
; System Power Control, Reboot & Halt Architecture for K-Three Linux
; Interfaces with HW_SYS_RESET (6010) and HW_SYS_HALT (6011)
; ==============================================================================

; --- Hardware System Reset / Warm Reboot ---
; Clobbers: r0, r1
k3_sys_reset:
    li r1, #6010            ; HW_SYS_RESET
    ldi r0, #1
    store r0, [r1]
    halt

; --- Hardware System Power Off / Halt ---
; Clobbers: r0, r1
k3_sys_poweroff:
    li r1, #6011            ; HW_SYS_HALT
    ldi r0, #1
    store r0, [r1]
    halt
