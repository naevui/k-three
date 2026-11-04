; ==============================================================================
; linux/kernel/sync/spinlock.s
;
; Native Kernel Spinlock & Atomic Synchronization Primitives for K-Three
; Provides mutual exclusion for task scheduling and I/O buffer management.
; ==============================================================================

; --- Acquire Spinlock ---
; Input: r0 = pointer to spinlock word (0 = unlocked, 1 = locked)
; Clobbers: r1, r2
k3_spin_lock:
.spin_try:
    load r1, [r0]
    brnz r1, .spin_retry
    ; Lock available, acquire it
    ldi r2, #1
    store r2, [r0]
    ret

.spin_retry:
    ; Busy-wait spin loop
    jmp .spin_try

; --- Release Spinlock ---
; Input: r0 = pointer to spinlock word
; Clobbers: r1
k3_spin_unlock:
    ldi r1, #0
    store r1, [r0]
    ret
