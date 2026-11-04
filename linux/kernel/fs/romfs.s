; ==============================================================================
; linux/kernel/fs/romfs.s
;
; Ternary ROMFS / MemFS In-Memory Block Storage Reader for K-Three Linux
; ==============================================================================

; --- Inode Entry Format (8 words) ---
; [0] = Inode ID
; [1] = File type (1 = regular, 2 = directory)
; [2] = Start sector on block storage
; [3] = File size in words
; [4..7] = 4-character short name

; Read File From Block Storage into User Memory
; Input: r0 = sector number
;        r1 = destination address
;        r2 = size in words
; Output: r0 = status (0 = success)
k3_romfs_read_file:
    ; Read in 16-word chunks via DMA
.romfs_loop:
    cmpi r2, #16
    brn r2, .romfs_small
    ; Read full 16 words
    push r0
    push r1
    push r2
    ldi r2, #16
    call k3_disk_read_sector
    pop r2
    pop r1
    pop r0
    addi r0, #1             ; next sector
    addi r1, #16            ; advance dest buffer
    subi r2, #16            ; decrease remaining words
    brz r2, .romfs_success
    jmp .romfs_loop

.romfs_small:
    brz r2, .romfs_success
    push r0
    push r1
    push r2
    call k3_disk_read_sector
    pop r2
    pop r1
    pop r0
.romfs_success:
    ldi r0, #0
    ret
