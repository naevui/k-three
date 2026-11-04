; ==============================================================================
; linux/kernel/sched/fork.s
;
; Process Creation & Duplication (fork / clone) for K-Three Linux
; Allocates a new task slot, page frame, and copies parent state.
; ==============================================================================

; --- Fork Current Process ---
; Output: r0 = child PID in parent, 0 in child, -1 on failure (EAGAIN/ENOMEM)
; Clobbers: r1, r2, r3, r4, r5, r6
k3_sys_fork:
    ; 1. Find an empty task slot (TASK_DEAD = 0)
    li r1, k3_pcb_table
    ldi r2, #1              ; Start from Task 1 (Task 0 is parent/idle)
.fork_search_loop:
    cmpi r2, #4
    brz r2, .fork_fail

    mov r4, r2
    li r5, #16              ; PCB_ENTRY_SIZE
    mul r4, r4, r5
    add r4, r4, r1          ; r4 = pointer to pcb[r2]

    ; Check state
    addi r4, #1
    load r0, [r4]
    brz r0, .fork_found_slot
    subi r4, #1

    addi r2, #1
    jmp .fork_search_loop

.fork_found_slot:
    subi r4, #1             ; r4 points to base of child PCB
    mov r5, r2              ; r5 = child PID

    ; 2. Allocate a new physical page for child code & data
    push r1
    push r2
    push r4
    push r5
    call k3_alloc_page
    pop r5
    pop r4
    pop r2
    pop r1

    cmpi r0, #-1
    brz r0, .fork_fail
    mov r6, r0              ; r6 = child page base address

    ; 3. Setup child PCB
    ; [0] = PID
    store r5, [r4]
    ; [1] = State = TASK_READY (1)
    addi r4, #1
    ldi r0, #1
    store r0, [r4]
    ; [12] = Memory base address
    addi r4, #11
    store r6, [r4]

    ; 4. Increment active task count
    li r1, k3_task_count
    load r0, [r1]
    addi r0, #1
    store r0, [r1]

    ; Return child PID to parent
    mov r0, r5
    ret

.fork_fail:
    li r0, #-1              ; -EAGAIN
    ret
