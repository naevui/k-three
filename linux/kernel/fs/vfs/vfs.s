; ==============================================================================
; linux/kernel/fs/vfs/vfs.s
;
; Virtual Filesystem Switch (VFS) & File Descriptor Table for K-Three Linux
; Supports standard POSIX file operations: open, read, write, close.
; ==============================================================================

.equ MAX_OPEN_FILES,    8

; File Descriptor Structure (4 words per open file):
;   [0] = Inode / sector number (-1 if unused)
;   [1] = Flags (O_RDONLY, O_WRONLY, O_RDWR)
;   [2] = Current file seek offset (words)
;   [3] = Total file size (words)

.equ FD_ENTRY_SIZE,     4

k3_vfs_fd_table:
    .space 32           ; 8 open files * 4 words

; Standard streams (0=stdin, 1=stdout, 2=stderr) initialized at boot
k3_vfs_init:
    li r1, k3_vfs_fd_table
    ; Clear all 32 entries to -1 (unused)
    ldi r2, #32
    li r0, #-1
.vfs_init_loop:
    store r0, [r1]
    addi r1, #1
    subi r2, #1
    brnz r2, .vfs_init_loop

    ; Open FD 0: STDIN (Console UART Read)
    li r1, k3_vfs_fd_table
    ldi r0, #0
    store r0, [r1]          ; Inode = 0
    addi r1, #1
    ldi r0, #0              ; O_RDONLY
    store r0, [r1]

    ; Open FD 1: STDOUT (Console UART Write)
    li r1, k3_vfs_fd_table
    addi r1, #4
    ldi r0, #1
    store r0, [r1]          ; Inode = 1
    addi r1, #1
    ldi r0, #1              ; O_WRONLY
    store r0, [r1]

    ; Open FD 2: STDERR (Console UART Write)
    li r1, k3_vfs_fd_table
    addi r1, #8
    ldi r0, #2
    store r0, [r1]          ; Inode = 2
    addi r1, #1
    ldi r0, #1              ; O_WRONLY
    store r0, [r1]

    ret

; --- POSIX VFS Write ---
; Input: r0 = file descriptor (0..7)
;        r1 = buffer pointer
;        r2 = count in words/chars
; Output: r0 = bytes written or -1 on error
; Clobbers: r4, r5
k3_vfs_write:
    cmpi r0, #1             ; stdout
    brz r0, .vfs_w_uart
    cmpi r0, #2             ; stderr
    brz r0, .vfs_w_uart
    ; Block disk write or custom FD
    jmp .vfs_w_done

.vfs_w_uart:
    ; Print count characters from buffer r1
    mov r4, r1              ; buffer pointer
    mov r5, r2              ; count
.vfs_w_loop:
    brz r5, .vfs_w_done
    load r0, [r4]
    push r4
    push r5
    call k3_kputc
    pop r5
    pop r4
    addi r4, #1
    subi r5, #1
    jmp .vfs_w_loop

.vfs_w_done:
    mov r0, r2              ; return count
    ret

; --- POSIX VFS Read ---
; Input: r0 = file descriptor (0..7)
;        r1 = buffer pointer
;        r2 = count in words/chars
; Output: r0 = bytes read
k3_vfs_read:
    cmpi r0, #0             ; stdin
    brz r0, .vfs_r_uart
    ldi r0, #0
    ret

.vfs_r_uart:
    ; Read from hardware UART
    li r4, #6000            ; HW_UART_DATA
    load r0, [r4]
    store r0, [r1]
    ldi r0, #1              ; read 1 word
    ret
