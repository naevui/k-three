; ==============================================================================
; linux/kernel/lib/string.s
;
; Native Kernel String & Memory Manipulation Library for K-Three Linux
; Implements k3_strlen, k3_strcmp, k3_strcpy, k3_memset, k3_memcpy.
; ==============================================================================

; --- Calculate String Length ---
; Input: r0 = string pointer
; Output: r0 = length in words
; Clobbers: r1, r2
k3_kstrlen:
    mov r1, r0
    ldi r0, #0
.strlen_loop:
    load r2, [r1]
    brz r2, .strlen_done
    addi r1, #1
    addi r0, #1
    jmp .strlen_loop
.strlen_done:
    ret

; --- Compare Two Strings ---
; Input: r0 = s1 pointer, r1 = s2 pointer
; Output: r0 = 0 if equal, non-zero if different
; Clobbers: r2, r3
k3_kstrcmp:
    mov r2, r0
    mov r3, r1
.strcmp_loop:
    load r0, [r2]
    load r1, [r3]
    sub r0, r0, r1
    brnz r0, .strcmp_diff
    load r0, [r2]
    brz r0, .strcmp_equal
    addi r2, #1
    addi r3, #1
    jmp .strcmp_loop
.strcmp_diff:
    ret
.strcmp_equal:
    ldi r0, #0
    ret

; --- Set Memory Buffer to Value ---
; Input: r0 = dest pointer, r1 = value, r2 = count
; Output: r0 = dest pointer
; Clobbers: r3, r4
k3_kmemset:
    mov r3, r0
    mov r4, r2
.memset_loop:
    brz r4, .memset_done
    store r1, [r3]
    addi r3, #1
    subi r4, #1
    jmp .memset_loop
.memset_done:
    ret

; --- Copy Memory Buffer ---
; Input: r0 = dest pointer, r1 = src pointer, r2 = count
; Output: r0 = dest pointer
; Clobbers: r3, r4, r5
k3_kmemcpy:
    mov r3, r0
    mov r4, r1
    mov r5, r2
.memcpy_loop:
    brz r5, .memcpy_done
    load r2, [r4]
    store r2, [r3]
    addi r3, #1
    addi r4, #1
    subi r5, #1
    jmp .memcpy_loop
.memcpy_done:
    ret
