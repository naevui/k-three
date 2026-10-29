; ==============================================================================
; K-Three Syscall Showcase Demo
; Demonstrates console I/O, system info, timing, and ternary conversions.
; ==============================================================================

start:
    ; 1. Print welcome banner
    li r0, str_banner
    trap #SYS_PUTS

    ; 2. Query and print memory size
    li r0, str_memsize
    trap #SYS_PUTS
    trap #SYS_MEMSIZE
    trap #SYS_PUTNUM
    ldi r0, #'\n'
    trap #SYS_PUTC

    ; 3. Query instruction count
    li r0, str_insns
    trap #SYS_PUTS
    trap #SYS_INSN_COUNT
    trap #SYS_PUTNUM
    ldi r0, #'\n'
    trap #SYS_PUTC

    ; 4. Query current system millisecond clock
    li r0, str_time
    trap #SYS_PUTS
    trap #SYS_TIME
    trap #SYS_PUTNUM
    ldi r0, #'\n'
    trap #SYS_PUTC

    ; 5. Print random balanced ternary number
    li r0, str_rand
    trap #SYS_PUTS
    trap #SYS_RANDOM
    mov r1, r0          ; save random value in r1

    ; Print as signed decimal
    trap #SYS_PUTNUM

    ; Print separator
    li r0, str_trits
    trap #SYS_PUTS

    ; Print raw 9-trit representation
    mov r0, r1
    trap #SYS_PUTTRITS
    ldi r0, #'\n'
    trap #SYS_PUTC

    ; 6. Print goodbye and exit cleanly
    li r0, str_done
    trap #SYS_PUTS

    ldi r0, #0
    trap #SYS_EXIT
    halt

str_banner:
    .string "=== K-Three Syscall & OS Interface Demo ===\n"
str_memsize:
    .string "Total memory words: "
str_insns:
    .string "Instructions executed: "
str_time:
    .string "Clock tick (millis): "
str_rand:
    .string "Random word: "
str_trits:
    .string " (trits: "
str_done:
    .string ")\nDone!\n"
