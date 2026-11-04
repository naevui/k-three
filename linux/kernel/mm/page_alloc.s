; ==============================================================================
; linux/kernel/mm/page_alloc.s
;
; Physical Page Frame Allocator for K-Three Linux Kernel
;
; Memory is partitioned into 9 pages of 729 words each:
;   Page 0: 0..728      - Kernel text, trap vectors, interrupt vectors
;   Page 1: 729..1457   - Kernel data, PCB tables, BSS
;   Page 2: 1458..2186  - Userspace Task 0 (kinit) code & data
;   Page 3: 2187..2915  - Userspace Task 1 (sh) code & data
;   Page 4: 2916..3644  - Userspace Task 2 code & data
;   Page 5: 3645..4373  - Userspace Task 3 code & data
;   Page 6: 4374..5102  - Dynamic Page Cache / VFS buffer
;   Page 7: 5103..5831  - Userspace Heap Pool
;   Page 8: 5832..6560  - Stacks (5832..5999 user, 6130..6560 kernel) & MMIO (6000..6129)
; ==============================================================================

.equ PAGE_SIZE_WORDS,   729
.equ TOTAL_PAGES,       9

; Page Allocation Bitmap (1 word per page: 0 = free, 1 = reserved/allocated)
; Pages 0, 1 (Kernel) and 8 (Stack/MMIO) are permanently reserved.
k3_page_bitmap:
    .word 1             ; Page 0: Kernel text (reserved)
    .word 1             ; Page 1: Kernel data (reserved)
    .word 0             ; Page 2: Userspace Task 0 (free)
    .word 0             ; Page 3: Userspace Task 1 (free)
    .word 0             ; Page 4: Userspace Task 2 (free)
    .word 0             ; Page 5: Userspace Task 3 (free)
    .word 0             ; Page 6: Dynamic Page Cache (free)
    .word 0             ; Page 7: Heap Pool (free)
    .word 1             ; Page 8: Stacks & MMIO (reserved)

; --- Allocate One Free Page Frame ---
; Output: r0 = physical base address (e.g. 1458) or -1 if out of memory
; Clobbers: r1, r2, r4
k3_alloc_page:
    li r1, k3_page_bitmap
    ldi r2, #2              ; Start search from Page 2 (userspace pages)
.alloc_loop:
    cmpi r2, #8             ; Up to page 7
    brz r2, .alloc_fail

    ; Check bitmap[page]
    mov r4, r1
    add r4, r4, r2
    load r0, [r4]
    brz r0, .alloc_found

    addi r2, #1
    jmp .alloc_loop

.alloc_found:
    ; Mark allocated
    ldi r0, #1
    store r0, [r4]

    ; Calculate physical address = page_index * 729
    mov r0, r2
    li r1, #729
    mul r0, r0, r1
    ret

.alloc_fail:
    li r0, #-1
    ret

; --- Free Allocated Page Frame ---
; Input: r0 = physical base address
; Clobbers: r1, r2, r4
k3_free_page:
    ; page_index = address / 729
    li r1, #729
    ; Division loop: r2 = address / 729
    ldi r2, #0
.free_div_loop:
    sub r0, r0, r1
    brn r0, .free_div_done
    addi r2, #1
    jmp .free_div_loop
.free_div_done:
    ; Clear bitmap[page_index]
    li r1, k3_page_bitmap
    add r1, r1, r2
    ldi r0, #0
    store r0, [r1]
    ret
