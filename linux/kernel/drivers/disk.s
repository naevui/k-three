; ==============================================================================
; linux/kernel/drivers/disk.s
;
; Low-Level Hardware Block Storage DMA Driver for K-Three Linux
; Transfers 16-word sectors between Block Storage and CPU Memory
; ==============================================================================

.equ DISK_CMD_READ,     1
.equ DISK_CMD_WRITE,    2

; --- Read Sector via DMA ---
; Input: r0 = sector number
;        r1 = destination DMA buffer address
;        r2 = word count (1..16)
; Output: r0 = status (0 = success, -1 = error)
; Clobbers: r4, r5
k3_disk_read_sector:
    li r4, #6020            ; HW_DISK_SECTOR
    store r0, [r4]
    li r4, #6021            ; HW_DISK_DMA
    store r1, [r4]
    li r4, #6022            ; HW_DISK_COUNT
    store r2, [r4]
    li r4, #6023            ; HW_DISK_CMD
    ldi r5, #1              ; DISK_CMD_READ
    store r5, [r4]
    li r4, #6024            ; HW_DISK_STATUS
    load r0, [r4]
    ret

; --- Write Sector via DMA ---
; Input: r0 = sector number
;        r1 = source DMA buffer address
;        r2 = word count (1..16)
; Output: r0 = status (0 = success, -1 = error)
; Clobbers: r4, r5
k3_disk_write_sector:
    li r4, #6020            ; HW_DISK_SECTOR
    store r0, [r4]
    li r4, #6021            ; HW_DISK_DMA
    store r1, [r4]
    li r4, #6022            ; HW_DISK_COUNT
    store r2, [r4]
    li r4, #6023            ; HW_DISK_CMD
    ldi r5, #2              ; DISK_CMD_WRITE
    store r5, [r4]
    li r4, #6024            ; HW_DISK_STATUS
    load r0, [r4]
    ret
