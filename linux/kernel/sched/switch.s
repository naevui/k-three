; ==============================================================================
; linux/kernel/sched/switch.s
;
; Round-Robin Preemptive Context Switcher for K-Three Linux
; Switches architectural registers r0..r7, PC and privilege between tasks.
; ==============================================================================

; --- Initialize Swapper Task (PID 0) ---
k3_sched_init:
    li r0, k3_pcb_table
    ; Task 0: PID = 0
    ldi r1, #0
    store r1, [r0]
    ; Task 0: State = TASK_RUNNING (2)
    addi r0, #1
    ldi r1, #2
    store r1, [r0]
    ret

; --- Select Next Ready Task (Round-Robin) ---
; Output: r0 = next task index (0..3)
; Clobbers: r1, r2, r4
k3_schedule_next:
    li r1, k3_current_task
    load r0, [r1]
    addi r0, #1
    cmpi r0, #4
    brn r0, .sched_keep
    ldi r0, #0
.sched_keep:
    ; Update current task
    store r0, [r1]
    ret
