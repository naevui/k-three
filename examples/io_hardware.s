; ==============================================================================
; examples/io_hardware.s - Pure Hardware MMIO Architecture Showcase
;
; Demonstrates direct Memory-Mapped I/O (MMIO) without software TRAP calls.
; Reads and writes hardware device registers mapped into physical memory:
;   - IO_UART_DATA (5000): direct serial console output
;   - IO_TIMER_L   (5004): real-time hardware monotonic timer
;   - IO_RNG_WORD  (5008): balanced-ternary entropy generator
;   - IO_SYS_LEDS  (5012): front-panel hardware LED indicator
;
; Assemble and run:
;   build/k3asm examples/io_hardware.s -o build/examples/io_hardware.k3
;   build/k3vm -d build/examples/io_hardware.k3
; ==============================================================================

start:
    ; 1. Output string via MMIO UART (no TRAP used!)
    li   r1, #IO_UART_DATA
    li   r2, msg_hello
print_loop:
    load r0, [r2]
    brz  r0, print_done
    store r0, [r1]          ; Direct write to hardware UART data register
    addi r2, #1
    jmp  print_loop

print_done:
    ; 2. Read hardware timer into r3
    li   r1, #IO_TIMER_L
    load r3, [r1]

    ; 3. Read true hardware ternary random word into r4
    li   r1, #IO_RNG_WORD
    load r4, [r1]

    ; 4. Update front-panel hardware LEDs with pattern #42
    li   r0, #42
    li   r1, #IO_SYS_LEDS
    store r0, [r1]

    ; 5. Halt normally
    halt

; ------------------------------------------------------------------------------
; Null-terminated String
; ------------------------------------------------------------------------------
msg_hello:
    .string "K-Three Hardware MMIO!\n"
