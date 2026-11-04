; ==============================================================================
; linux/kernel/exec/binfmt_k3.s
;
; Native K3 Binary Loader & Execve Engine for K-Three Linux
; Loads executable 9-trit word programs into user space memory (1458..5999)
; and prepares register/stack frame for execution.
; ==============================================================================

.equ BINFMT_K3_MAGIC,   19683   ; Balanced ternary signature 3^9

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
