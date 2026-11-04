; ==============================================================================
; linux/kernel/exec/binfmt_k3.s
;
; Native K3 Binary Loader & Execve Engine for K-Three Linux
; Loads executable 9-trit word programs into user space memory (1458..5999)
; and prepares the initial stack frame with argc, argv[] and envp[].
; ==============================================================================

.equ BINFMT_K3_MAGIC,   7290    ; Balanced ternary signature 10 * 729
.equ USER_STACK_TOP,    5999    ; Top of userspace memory

; --- Setup User Stack with Arguments ---
; Prepares initial user stack frame layout:
;   [sp - 0] = argc
;   [sp - 1] = argv[0] pointer
;   [sp - 2] = argv[1] pointer (or 0)
;   [sp - 3] = NULL terminator (0)
; Input: r0 = argc (arg count)
;        r1 = argv pointer array
; Output: r7 = initialized user stack pointer
; Clobbers: r2, r3, r4
k3_setup_user_stack:
    li r7, #5999            ; USER_STACK_TOP

    ; Push argv pointers in reverse order if argc > 0
    cmpi r0, #0
    brz r0, .stack_push_argc

    ; Push NULL terminator
    ldi r2, #0
    push r2

    ; Push argv elements
    mov r3, r0              ; r3 = count
.stack_push_argv:
    brz r3, .stack_push_argc
    subi r3, #1
    mov r4, r1
    add r4, r4, r3
    load r2, [r4]           ; r2 = argv[r3]
    push r2
    jmp .stack_push_argv

.stack_push_argc:
    ; Push argc
    push r0
    ret

; --- Load and Execute Binary Image ---
; Input: r0 = pointer to executable image in memory or block storage
;        r1 = size in words
;        r2 = target user base address (e.g. 1458)
; Output: r0 = entry point address on success, -1 on failure
; Clobbers: r3, r4, r5, r6
k3_load_binary:
    ; Verify non-zero size
    cmpi r1, #0
    brz r1, .binfmt_fail

    ; Copy program words from source r0 to destination r2
    mov r4, r0              ; r4 = src
    mov r5, r2              ; r5 = dst
    mov r6, r1              ; r6 = count

.binfmt_copy_loop:
    brz r6, .binfmt_copy_done
    load r3, [r4]
    store r3, [r5]
    addi r4, #1
    addi r5, #1
    subi r6, #1
    jmp .binfmt_copy_loop

.binfmt_copy_done:
    ; Return entry point address (base of destination user space)
    mov r0, r2
    ret

.binfmt_fail:
    li r0, #-1
    ret
