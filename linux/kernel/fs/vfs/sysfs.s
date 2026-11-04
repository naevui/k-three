; ==============================================================================
; linux/kernel/fs/vfs/sysfs.s
;
; Virtual System Information Filesystem (/sys) for K-Three Linux
; Exposes hardware status, clock ticks, and CPU architecture parameters.
; ==============================================================================

; --- Read System Ticks (Uptime counter) ---
; Output: r0 = low 9-trit ticks, r1 = high 9-trit ticks
; Clobbers: r2
k3_sysfs_read_ticks:
    li r2, #6013            ; HW_SYS_TICKS_L
    load r0, [r2]
    li r2, #6014            ; HW_SYS_TICKS_H
    load r1, [r2]
    ret

; --- Read Physical Memory Size ---
; Output: r0 = memory words (6561)
; Clobbers: r1
k3_sysfs_read_memsize:
    li r1, #6015            ; HW_SYS_MEMSIZE
    load r0, [r1]
    ret
