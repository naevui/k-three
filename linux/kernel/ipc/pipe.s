; ==============================================================================
; linux/kernel/ipc/pipe.s
;
; Unix Anonymous Pipe Inter-Process Communication (IPC) for K-Three Linux
; Implements a 32-word ring buffer with non-blocking read/write operations.
; ==============================================================================

.equ PIPE_BUF_SIZE,     32

k3_pipe_head:
    .word 0
k3_pipe_tail:
    .word 0
k3_pipe_count:
    .word 0

k3_pipe_buffer:
    .space 32

; --- Write to Pipe Ring Buffer ---
; Input: r0 = value/character to write
; Output: r0 = 1 on success, 0 if pipe full
; Clobbers: r1, r2, r4
k3_pipe_write:
    li r1, k3_pipe_count
    load r2, [r1]
    cmpi r2, #32            ; check if full
    brz r2, .pipe_w_full

    ; Store value at buffer[head]
    li r1, k3_pipe_head
    load r4, [r1]
    li r1, k3_pipe_buffer
    add r1, r1, r4
    store r0, [r1]

    ; head = (head + 1) % 32
    addi r4, #1
    cmpi r4, #32
    brn r4, .pipe_head_ok
    ldi r4, #0
.pipe_head_ok:
    li r1, k3_pipe_head
    store r4, [r1]

    ; count++
    li r1, k3_pipe_count
    load r2, [r1]
    addi r2, #1
    store r2, [r1]
    ldi r0, #1
    ret

.pipe_w_full:
    ldi r0, #0
    ret

; --- Read from Pipe Ring Buffer ---
; Output: r0 = value/character read, or -1 if pipe empty
; Clobbers: r1, r2, r4
k3_pipe_read:
    li r1, k3_pipe_count
    load r2, [r1]
    brz r2, .pipe_r_empty

    ; Load value from buffer[tail]
    li r1, k3_pipe_tail
    load r4, [r1]
    li r1, k3_pipe_buffer
    add r1, r1, r4
    load r0, [r1]
    push r0                 ; save read value

    ; tail = (tail + 1) % 32
    addi r4, #1
    cmpi r4, #32
    brn r4, .pipe_tail_ok
    ldi r4, #0
.pipe_tail_ok:
    li r1, k3_pipe_tail
    store r4, [r1]

    ; count--
    li r1, k3_pipe_count
    load r2, [r1]
    subi r2, #1
    store r2, [r1]

    pop r0                  ; restore read value
    ret

.pipe_r_empty:
    li r0, #-1
    ret
