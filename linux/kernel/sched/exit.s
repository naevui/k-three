; ==============================================================================
; linux/kernel/sched/exit.s
;
; Process Termination (exit / exit_group) & Zombie Cleanup for K-Three Linux
; Releases allocated physical page frames and updates process state to DEAD.
; ==============================================================================

; --- Terminate Process ---
; Input: r0 = process PID to terminate
; Output: r0 = 0 on success
; Clobbers: r1, r2, r4, r5
k3_sys_exit_task:
    ; Task 0 (init / idle) cannot be killed directly
    cmpi r0, #0
    brz r0, .exit_cannot_kill_init

    ; Calculate PCB pointer = k3_pcb_table + (pid * 16)
    li r1, k3_pcb_table
    mov r4, r0
    li r5, #16              ; PCB_ENTRY_SIZE
    mul r4, r4, r5
    add r4, r4, r1

    ; Set State = TASK_DEAD (0)
    addi r4, #1
    ldi r1, #0
    store r1, [r4]

    ; Free allocated physical page frame at [12]
    addi r4, #11
    load r0, [r4]
    brz r0, .exit_no_page
    call k3_free_page
.exit_no_page:

    ; Decrement active task count
    li r1, k3_task_count
    load r2, [r1]
    subi r2, #1
    store r2, [r1]

    ldi r0, #0
    ret

.exit_cannot_kill_init:
    li r0, #-1
    ret
