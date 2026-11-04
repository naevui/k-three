; ==============================================================================
; linux/kernel/fs/devfs/devfs.s
;
; Virtual Device Filesystem (/dev) for K-Three Linux Kernel
; Supports standard Unix character & block devices:
;   /dev/null    (Major 1, Minor 3) - Discards writes, returns EOF on read
;   /dev/zero    (Major 1, Minor 5) - Returns infinite stream of zeroes
;   /dev/urandom (Major 1, Minor 9) - Streams hardware TRNG entropy
;   /dev/tty     (Major 5, Minor 0) - Terminal MMIO UART console
; ==============================================================================

.equ DEV_NULL_ID,       1
.equ DEV_ZERO_ID,       2
.equ DEV_URANDOM_ID,    3
.equ DEV_TTY_ID,        4

; --- Read From Device ---
; Input: r0 = device ID (DEV_*_ID)
;        r1 = buffer pointer
;        r2 = requested count
; Output: r0 = count read
; Clobbers: r3, r4, r5
k3_devfs_read:
    cmpi r0, #1             ; /dev/null
    brz r0, .dev_null_read
    cmpi r0, #2             ; /dev/zero
    brz r0, .dev_zero_read
    cmpi r0, #3             ; /dev/urandom
    brz r0, .dev_urandom_read
    cmpi r0, #4             ; /dev/tty
    brz r0, .dev_tty_read
    li r0, #-1
    ret

.dev_null_read:
    ldi r0, #0              ; EOF
    ret

.dev_zero_read:
    mov r4, r1
    mov r5, r2
    ldi r3, #0
.dev_zero_loop:
    brz r5, .dev_read_done
    store r3, [r4]
    addi r4, #1
    subi r5, #1
    jmp .dev_zero_loop

.dev_urandom_read:
    mov r4, r1
    mov r5, r2
.dev_urandom_loop:
    brz r5, .dev_read_done
    push r4
    push r5
    call k3_get_random_word
    pop r5
    pop r4
    store r0, [r4]
    addi r4, #1
    subi r5, #1
    jmp .dev_urandom_loop

.dev_tty_read:
    call k3_vfs_read
    ret

.dev_read_done:
    mov r0, r2
    ret

; --- Write To Device ---
; Input: r0 = device ID (DEV_*_ID)
;        r1 = buffer pointer
;        r2 = count
; Output: r0 = count written
k3_devfs_write:
    cmpi r0, #1             ; /dev/null -> discard
    brz r0, .dev_write_discard
    cmpi r0, #4             ; /dev/tty -> print to UART
    brz r0, .dev_tty_write
    mov r0, r2
    ret

.dev_write_discard:
    mov r0, r2
    ret

.dev_tty_write:
    call k3_vfs_write
    ret
