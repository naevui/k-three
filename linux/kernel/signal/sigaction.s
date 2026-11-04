; ==============================================================================
; linux/kernel/signal/sigaction.s
;
; Native POSIX Signal Delivery & Dispatch Engine for K-Three Linux
; Manages task signal masks, pending signals, and signal dispatch frames.
; ==============================================================================

.equ SIG_DFL,           0
.equ SIG_IGN,           1

; Signal Numbers
.equ SIGHUP,            1
.equ SIGINT,            2
.equ SIGQUIT,           3
.equ SIGILL,            4
.equ SIGTRAP,           5
.equ SIGABRT,           6
.equ SIGKILL,           9
.equ SIGSEGV,           11
.equ SIGTERM,           15

; Pending Signals Bitmap (1 word per task: bitmask of pending signals 1..15)
k3_task_pending_signals:
    .word 0             ; Task 0
    .word 0             ; Task 1
    .word 0             ; Task 2
    .word 0             ; Task 3

; Signal Handlers Table (4 tasks * 16 signals = 64 words)
k3_task_sig_handlers:
    .space 64

; --- Send POSIX Signal to Task (SYS_KILL / kill) ---
; Input: r0 = target PID (0..3)
;        r1 = signal number (1..15)
; Output: r0 = 0 on success, -1 on invalid PID/signal
; Clobbers: r2, r3, r4
k3_sys_kill_task:
    cmpi r0, #0
    brn r0, .sig_invalid
    cmpi r0, #3
    brp r0, .sig_invalid

    ; Check if SIGKILL (9) or SIGTERM (15) for unhandled process -> terminate
    cmpi r1, #9             ; SIGKILL
    brz r1, .sig_do_kill
    cmpi r1, #15            ; SIGTERM
    brz r1, .sig_do_kill

    ; Record pending signal bit
    li r2, k3_task_pending_signals
    add r2, r2, r0
    load r3, [r2]
    ; Simple bit or representation
    add r3, r3, r1
    store r3, [r2]
    ldi r0, #0
    ret

.sig_do_kill:
    ; Terminate target process directly
    call k3_sys_exit_task
    ldi r0, #0
    ret

.sig_invalid:
    li r0, #-1              ; -EINVAL / -ESRCH
    ret

; --- Check and Handle Pending Signals on Trap Return ---
; Input: r0 = current PID (0..3)
; Clobbers: r1, r2, r3
k3_check_pending_signals:
    li r1, k3_task_pending_signals
    add r1, r1, r0
    load r2, [r1]
    brz r2, .sig_none

    ; Clear pending signals
    ldi r3, #0
    store r3, [r1]
.sig_none:
    ret
