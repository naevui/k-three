; ==============================================================================
; linux/kernel/drivers/vram.s
;
; Hardware Text Framebuffer / Video RAM Driver for K-Three Linux
; Manages 100-word text buffer at MMIO 6030..6129 with screen flush.
; ==============================================================================

.equ VRAM_BASE_ADDR,    6030
.equ VRAM_CTRL_REG,     6026
.equ VRAM_BUFFER_SIZE,  100

; --- Clear Video RAM Framebuffer ---
; Clobbers: r0, r1, r2
k3_vram_clear:
    li r1, #6030            ; VRAM_BASE_ADDR
    li r2, #100             ; VRAM_BUFFER_SIZE
    ldi r0, #32             ; ASCII Space ' '
.vram_clear_loop:
    store r0, [r1]
    addi r1, #1
    subi r2, #1
    brnz r2, .vram_clear_loop
    ret

; --- Write Character to VRAM Offset ---
; Input: r0 = character ASCII code
;        r1 = offset (0..99)
; Clobbers: r2
k3_vram_write_char:
    li r2, #6030            ; VRAM_BASE_ADDR
    add r2, r2, r1
    store r0, [r2]
    ret

; --- Flush VRAM to Physical Display / Host Console ---
; Clobbers: r0, r1
k3_vram_flush:
    li r1, #6026            ; VRAM_CTRL_REG
    ldi r0, #1              ; Command 1 = FLUSH
    store r0, [r1]
    ret
