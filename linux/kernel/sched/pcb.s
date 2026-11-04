; ==============================================================================
; linux/kernel/sched/pcb.s
;
; Process Control Block (PCB) Layout & Task Storage for K-Three Linux
; Supports up to 4 concurrent processes in cooperative/preemptive multitasking.
; ==============================================================================

; --- PCB Field Offsets (16 words per PCB) ---
.equ PCB_PID,           0       ; Process ID (0 = swapper/idle, 1 = kinit, ...)
.equ PCB_STATE,         1       ; 0 = dead, 1 = ready, 2 = running, 3 = blocked
.equ PCB_SAVED_SP,      2       ; Saved stack pointer r7
.equ PCB_SAVED_PC,      3       ; Exception / task return PC
.equ PCB_SAVED_R0,      4       ; General purpose registers r0..r6
.equ PCB_SAVED_R1,      5
.equ PCB_SAVED_R2,      6
.equ PCB_SAVED_R3,      7
.equ PCB_SAVED_R4,      8
.equ PCB_SAVED_R5,      9
.equ PCB_SAVED_R6,      10
.equ PCB_TICKS,         11      ; CPU ticks allocated
.equ PCB_BASE_ADDR,     12      ; Memory base address
.equ PCB_SIZE,          13      ; Memory size
.equ PCB_RESERVED_1,    14
.equ PCB_RESERVED_2,    15

.equ PCB_ENTRY_SIZE,    16

; Task states
.equ TASK_DEAD,         0
.equ TASK_READY,        1
.equ TASK_RUNNING,      2
.equ TASK_BLOCKED,      3

; Global Current Task Index (0..3)
k3_current_task:
    .word 0

; Total Registered Task Count
k3_task_count:
    .word 1

; Global PCB Table (4 tasks * 16 words = 64 words)
k3_pcb_table:
    .space 64
