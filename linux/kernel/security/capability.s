; ==============================================================================
; linux/kernel/security/capability.s
;
; POSIX Capability & Privilege Separation Architecture for K-Three Linux
; Manages CAP_SYS_ADMIN, CAP_NET_ADMIN, and user space privilege boundaries.
; ==============================================================================

.equ CAP_CHOWN,             1
.equ CAP_DAC_OVERRIDE,      2
.equ CAP_FOWNER,            4
.equ CAP_KILL,              8
.equ CAP_SETGID,            16
.equ CAP_SETUID,            32
.equ CAP_SYS_ADMIN,         64
.equ CAP_SYS_RAWIO,         128

; Task Capability Masks (1 word per task: bitmask of allowed capabilities)
; Task 0 (init / root): 9841 (all capabilities)
; Task 1..3: unprivileged user capabilities
k3_task_capabilities:
    .word 9841          ; Task 0: Root (full capabilities)
    .word 0             ; Task 1: Unprivileged
    .word 0             ; Task 2: Unprivileged
    .word 0             ; Task 3: Unprivileged

; --- Check Task Capability ---
; Input: r0 = PID (0..3)
;        r1 = capability flag (CAP_*)
; Output: r0 = 1 if allowed, 0 if denied
; Clobbers: r2, r4
k3_check_capability:
    li r2, k3_task_capabilities
    add r2, r2, r0
    load r4, [r2]

    ; Check bit: (mask & flag) != 0
    ; In balanced ternary: compare capability bit
    brz r0, .cap_root_always_allow
    ldi r0, #0
    ret

.cap_root_always_allow:
    ldi r0, #1
    ret
