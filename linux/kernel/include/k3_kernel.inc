; ==============================================================================
; linux/kernel/include/k3_kernel.inc
;
; Core Architectural Definitions for Native K-Three Linux Kernel
; Balanced Ternary 9-Trit RISC Architecture [-9841 .. +9841]
; ==============================================================================

; --- CPU Privilege Modes ---
.equ K3_MODE_USER,      0
.equ K3_MODE_KERNEL,    1

; --- Memory Map Layout (6561 Words) ---
.equ K3_MEM_TOTAL,      6561
.equ K3_KERNEL_BASE,    0
.equ K3_KERNEL_LIMIT,   1457    ; First 2 pages (2 * 729 = 1458 words)
.equ K3_USER_BASE,      1458    ; Userspace code & data start
.equ K3_USER_LIMIT,     5999    ; Userspace memory end
.equ K3_MMIO_BASE,      6000    ; Hardware MMIO peripheral window
.equ K3_MMIO_LIMIT,     6129
.equ K3_KERNEL_SP,      6560    ; Top of physical memory (stack grows down)

; --- Hardware MMIO Registers ---
.equ K3_HW_UART_DATA,   6000
.equ K3_HW_UART_STAT,   6001
.equ K3_HW_UART_CTRL,   6002
.equ K3_HW_TIMER_L,     6004
.equ K3_HW_TIMER_H,     6005
.equ K3_HW_TIMER_INT,   6006
.equ K3_HW_TIMER_CTRL,  6007
.equ K3_HW_RNG_WORD,    6008
.equ K3_HW_RNG_TRIT,    6009
.equ K3_HW_SYS_RESET,   6010
.equ K3_HW_SYS_HALT,    6011
.equ K3_HW_SYS_LEDS,    6012
.equ K3_HW_SYS_TICKS_L, 6013
.equ K3_HW_SYS_TICKS_H, 6014
.equ K3_HW_SYS_MEMSIZE, 6015
.equ K3_HW_SYS_IRQ_CTRL,6016

.equ K3_HW_DISK_SECTOR, 6020
.equ K3_HW_DISK_DMA,    6021
.equ K3_HW_DISK_COUNT,  6022
.equ K3_HW_DISK_CMD,    6023
.equ K3_HW_DISK_STATUS, 6024
.equ K3_HW_DISK_CAPACITY, 6025

.equ K3_HW_VRAM_CTRL,   6026
.equ K3_HW_VRAM_COLS,   6027
.equ K3_HW_VRAM_ROWS,   6028
.equ K3_HW_VRAM_BASE,   6030

; --- Linux Syscall Numbers ---
.equ K3_SYS_EXIT,       0
.equ K3_SYS_FORK,       2
.equ K3_SYS_READ,       3
.equ K3_SYS_WRITE,      4
.equ K3_SYS_OPEN,       5
.equ K3_SYS_CLOSE,      6
.equ K3_SYS_WAITPID,    7
.equ K3_SYS_EXECVE,     11
.equ K3_SYS_TIME,       13
.equ K3_SYS_GETPID,     20
.equ K3_SYS_KILL,       37
.equ K3_SYS_NANOSLEEP,  162

; --- Task & Scheduler Limits ---
.equ K3_MAX_TASKS,      4
.equ K3_TASK_STACK_SIZE, 128
