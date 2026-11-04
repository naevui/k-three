; ==============================================================================
; linux/kernel/fs/romfs.s
;
; Ternary ROMFS Block Storage File System for K-Three Linux
; Mounts disk.img via MMIO Block DMA driver and provides:
;   - k3_romfs_mount: Reads superblock (sector 0) and verifies 19683 magic
;   - k3_romfs_lookup: Searches inode table for matching 8-char filename
;   - k3_romfs_read_file: Streams sectors into memory via DMA
; ==============================================================================

.equ ROMFS_MAGIC,       19683
.equ ROMFS_SECTOR_SIZE, 16

k3_romfs_mounted:
    .word 0
k3_romfs_total_sectors:
    .word 0
k3_romfs_inode_count:
    .word 0

; Scratch DMA sector buffer (16 words)
k3_romfs_sector_buf:
    .space 16

; --- Mount ROMFS Filesystem ---
; Output: r0 = 0 on success, -1 on invalid magic
; Clobbers: r1, r2, r4
k3_romfs_mount:
    push r6
    ; Read sector 0 (Superblock) into k3_romfs_sector_buf
    ldi r0, #0              ; sector 0
    li r1, k3_romfs_sector_buf
    ldi r2, #16             ; 16 words
    call k3_disk_read_sector

    ; Verify Superblock Magic [0] == 19683
    li r1, k3_romfs_sector_buf
    load r0, [r1]
    li r2, #19683
    cmp r2, r0, r2
    brnz r2, .romfs_mount_fail

    ; Save total sectors [1]
    addi r1, #1
    load r0, [r1]
    li r2, k3_romfs_total_sectors
    store r0, [r2]

    ; Save inode count [2]
    addi r1, #1
    load r0, [r1]
    li r2, k3_romfs_inode_count
    store r0, [r2]

    ; Mark mounted = 1
    li r2, k3_romfs_mounted
    ldi r0, #1
    store r0, [r2]

    ldi r0, #0
    pop r6
    ret

.romfs_mount_fail:
    li r0, #-1
    pop r6
    ret

; --- Find Inode by Name ---
; Input: r0 = pointer to filename string
; Output: r0 = start sector, r1 = file size in words (-1 if not found)
; Clobbers: r2, r3, r4, r5
k3_romfs_lookup:
    push r6
    push r0                 ; save search name

    li r1, k3_romfs_inode_count
    load r5, [r1]           ; r5 = remaining inodes to scan
    ldi r4, #1              ; r4 = current inode sector (sectors 1..N)

.lookup_loop:
    brz r5, .lookup_not_found

    ; Read inode sector r4 into buffer
    mov r0, r4
    li r1, k3_romfs_sector_buf
    ldi r2, #16
    push r4
    push r5
    call k3_disk_read_sector
    pop r5
    pop r4

    ; Compare filename at buffer[3..10]
    mov r0, r7
    load r0, [r0]           ; r0 = search name
    li r1, k3_romfs_sector_buf
    addi r1, #3             ; buffer[3] is filename start
    call k3_kstrcmp
    brz r0, .lookup_match

    addi r4, #1             ; next inode sector
    subi r5, #1
    jmp .lookup_loop

.lookup_match:
    pop r0                  ; discard search name
    ; Inode buffer[1] = start sector, buffer[2] = size
    li r2, k3_romfs_sector_buf
    addi r2, #1
    load r0, [r2]           ; start sector
    addi r2, #1
    load r1, [r2]           ; size in words
    pop r6
    ret

.lookup_not_found:
    pop r0                  ; discard search name
    li r0, #-1
    li r1, #-1
    pop r6
    ret

; --- Read File From Block Storage into User Memory ---
; Input: r0 = sector number
;        r1 = destination address
;        r2 = size in words
; Output: r0 = status (0 = success)
k3_romfs_read_file:
    push r6
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
    pop r6
    ret
