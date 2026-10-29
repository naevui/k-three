; ==============================================================================
; K3-OS (Kord-Three Operating System & Interactive Monitor)
;
; Features:
;  - Interactive CLI with ANSI colored prompt
;  - System telemetry (uptime, memory size, instruction count)
;  - Memory inspection (peek, mem dump, poke)
;  - Colored Balanced-Ternary Trits (+ in green, - in red, 0 in dim)
;  - Host VFS file viewing (cat)
;  - Subprocess execution (exec external .k3 programs!)
;  - Random balanced-ternary generator
; ==============================================================================

start:
    li r7, #2186            ; Initialize stack pointer at 2186

    ; Clear terminal and display boot banner
    trap #SYS_CLEAR
    li r0, str_banner
    trap #SYS_PUTS

shell_loop:
    ; Print ANSI colored prompt: "\033[1;32mk3>\033[0m "
    li r0, str_prompt
    trap #SYS_PUTS

    ; Read command line into buf_io (max 32 words)
    li r0, buf_io
    ldi r1, #32
    trap #SYS_GETS

    ; Check if stdin reached EOF (-1)
    brp r0, not_exit_eof
    brz r0, not_exit_eof
    jmp shell_exit
not_exit_eof:

    ; Trim trailing '\n' and '\r'
    li r0, buf_io
    call trim_newline

    ; Skip leading spaces
    li r0, buf_io
    call skip_spaces
    mov r2, r0              ; r2 = trimmed command string

    ; Ignore empty lines
    load r0, [r2]
    brz r0, shell_loop

    ; Increment command execution counter
    li r3, cmd_counter
    load r1, [r3]
    addi r1, #1
    store r1, [r3]

    ; Advance r2 past command char
    addi r2, #1

    ; --- Dispatch by first character (normalized: r0 - 100) ---
    subi r0, #40
    subi r0, #40
    subi r0, #20            ; r0 = char - 100

    ; 'h' (104 - 100 = 4)
    mov r1, r0
    subi r1, #4
    brnz r1, not_h
    call cmd_help
    jmp shell_loop
not_h:

    ; 'i' (105 - 100 = 5)
    mov r1, r0
    subi r1, #5
    brnz r1, not_i
    call cmd_info
    jmp shell_loop
not_i:

    ; 'q' (113 - 100 = 13)
    mov r1, r0
    subi r1, #13
    brnz r1, not_q
    jmp shell_exit
not_q:

    ; 'r' (114 - 100 = 14)
    mov r1, r0
    subi r1, #14
    brnz r1, not_r
    call cmd_rand
    jmp shell_loop
not_r:

    ; 'l' (108 - 100 = 8)
    mov r1, r0
    subi r1, #8
    brnz r1, not_l
    trap #SYS_CLEAR
    jmp shell_loop
not_l:

    ; 'e' (101 - 100 = 1)
    mov r1, r0
    subi r1, #1
    brnz r1, not_e
    mov r0, r2
    call cmd_echo
    jmp shell_loop
not_e:

    ; 'p' (112 - 100 = 12)
    mov r1, r0
    subi r1, #12
    brnz r1, not_p
    mov r0, r2
    call cmd_peek
    jmp shell_loop
not_p:

    ; 'm' (109 - 100 = 9)
    mov r1, r0
    subi r1, #9
    brnz r1, not_m
    mov r0, r2
    call cmd_mem
    jmp shell_loop
not_m:

    ; 'w' (119 - 100 = 19)
    mov r1, r0
    subi r1, #19
    brnz r1, not_w
    mov r0, r2
    call cmd_poke
    jmp shell_loop
not_w:

    ; 'c' (99 - 100 = -1)
    mov r1, r0
    subi r1, #-1
    brnz r1, not_c
    mov r0, r2
    call cmd_cat
    jmp shell_loop
not_c:

    ; 'x' (120 - 100 = 20)
    mov r1, r0
    subi r1, #20
    brnz r1, not_x
    mov r0, r2
    call cmd_exec
    jmp shell_loop
not_x:

    ; 'g' (103 - 100 = 3): GUI Desktop
    mov r1, r0
    subi r1, #3
    brnz r1, not_g
    call cmd_gui
    jmp shell_loop
not_g:

    ; Unknown command
    li r0, str_unknown
    trap #SYS_PUTS
    jmp shell_loop

shell_exit:
    li r0, str_bye
    trap #SYS_PUTS
    ldi r0, #0
    trap #SYS_EXIT
    halt

; ==============================================================================
; Command Handlers
; ==============================================================================

cmd_help:
    push r6
    trap #SYS_HELP
    pop r6
    ret

cmd_info:
    push r6
    li r3, cmd_counter
    load r0, [r3]
    trap #SYS_SYSINFO
    pop r6
    ret

cmd_rand:
    push r6
    li r0, str_rand
    trap #SYS_PUTS
    trap #SYS_RANDOM
    mov r2, r0
    trap #SYS_PUTNUM

    li r0, str_trits_prefix
    trap #SYS_PUTS
    mov r0, r2
    trap #SYS_PUTTRITS_COLOR
    li r0, str_trits_suffix
    trap #SYS_PUTS
    pop r6
    ret

cmd_echo:
    push r6
    call skip_cmd_word
    trap #SYS_PUTS
    li r0, str_nl
    trap #SYS_PUTS
    pop r6
    ret

cmd_mem:
    push r6
    call skip_cmd_word
    call parse_int
    mov r2, r0              ; r2 = start addr
    ldi r4, #4              ; r4 = count
mem_dump_loop:
    mov r0, r2
    call cmd_peek_addr
    addi r2, #1
    addi r4, #-1
    brp r4, mem_dump_loop
    pop r6
    ret

cmd_peek:
    push r6
    call skip_cmd_word
    call parse_int
    mov r2, r0
    call cmd_peek_addr
    pop r6
    ret

cmd_peek_addr:
    push r6
    brn r2, peek_err
    mov r1, r2
    li r3, #-2186
    add r1, r1, r3
    brp r1, peek_err

    li r0, str_mem_l
    trap #SYS_PUTS
    mov r0, r2
    trap #SYS_PUTNUM
    li r0, str_mem_r
    trap #SYS_PUTS

    load r3, [r2]
    mov r0, r3
    trap #SYS_PUTNUM
    li r0, str_trits_prefix
    trap #SYS_PUTS
    mov r0, r3
    trap #SYS_PUTTRITS_COLOR
    li r0, str_trits_suffix
    trap #SYS_PUTS
    pop r6
    ret
peek_err:
    li r0, str_err_addr
    trap #SYS_PUTS
    pop r6
    ret

cmd_poke:
    push r6
    call skip_cmd_word
    call parse_int
    mov r2, r0              ; r2 = addr
    mov r0, r1
    call parse_int
    mov r3, r0              ; r3 = val

    brn r2, poke_err
    mov r1, r2
    li r4, #-2186
    add r1, r1, r4
    brp r1, poke_err

    store r3, [r2]
    li r0, str_ok
    trap #SYS_PUTS
    pop r6
    ret
poke_err:
    li r0, str_err_addr
    trap #SYS_PUTS
    pop r6
    ret

cmd_cat:
    push r6
    call skip_cmd_word
    call skip_spaces
    mov r2, r0
    load r1, [r2]
    brz r1, cat_err

    mov r0, r2
    ldi r1, #0
    trap #SYS_OPEN
    brn r0, cat_err
    mov r3, r0

cat_loop:
    mov r0, r3
    li r1, buf_io
    ldi r2, #16
    trap #SYS_READ
    brz r0, cat_close
    brn r0, cat_close

    mov r4, r0
    li r2, buf_io
cat_out:
    load r0, [r2]
    trap #SYS_PUTC
    addi r2, #1
    addi r4, #-1
    brp r4, cat_out
    jmp cat_loop

cat_close:
    mov r0, r3
    trap #SYS_CLOSE
    pop r6
    ret
cat_err:
    li r0, str_err_file
    trap #SYS_PUTS
    pop r6
    ret

cmd_exec:
    push r6
    call skip_cmd_word
    call skip_spaces
    mov r2, r0
    load r1, [r2]
    brz r1, exec_err

    mov r0, r2
    trap #SYS_EXEC
    mov r2, r0
    brn r2, exec_err

    li r0, str_exit_code
    trap #SYS_PUTS
    mov r0, r2
    trap #SYS_PUTNUM
    li r0, str_nl
    trap #SYS_PUTS
    pop r6
    ret
exec_err:
    li r0, str_err_file
    trap #SYS_PUTS
    pop r6
    ret

cmd_gui:
    push r6
    trap #SYS_GUI_DESKTOP
    pop r6
    ret

; ==============================================================================
; Utility Routines
; ==============================================================================

skip_cmd_word:
    load r1, [r0]
    brz r1, skip_cmd_done
    mov r3, r1
    subi r3, #32           ; ' '
    brz r3, skip_cmd_done
    addi r0, #1
    jmp skip_cmd_word
skip_cmd_done:
    jmp skip_spaces

trim_newline:
    push r6
    push r1
    push r2
    mov r1, r0
trim_loop:
    load r2, [r1]
    brz r2, trim_done
    mov r3, r2
    subi r3, #10           ; '\n'
    brz r3, trim_zero
    mov r3, r2
    subi r3, #13           ; '\r'
    brz r3, trim_zero
    addi r1, #1
    ldi r3, #0
    brz r3, trim_loop
trim_zero:
    ldi r3, #0
    store r3, [r1]
trim_done:
    pop r2
    pop r1
    pop r6
    ret

skip_spaces:
    load r1, [r0]
    mov r2, r1
    subi r2, #32           ; ' '
    brz r2, skip_advance
    mov r2, r1
    subi r2, #9            ; '\t'
    brz r2, skip_advance
    ret
skip_advance:
    addi r0, #1
    jmp skip_spaces

parse_int:
    push r6
    push r2
    push r3
    push r4

    call skip_spaces
    mov r2, r0

    ldi r3, #0
    ldi r4, #1

    load r0, [r2]
    mov r1, r0
    subi r1, #40
    subi r1, #5             ; '-' = 45
    brnz r1, parse_digits
    ldi r4, #-1
    addi r2, #1

parse_digits:
    load r0, [r2]
    mov r1, r0
    subi r1, #40
    subi r1, #8             ; '0' = 48
    brn r1, parse_int_done
    mov r1, r0
    subi r1, #40
    subi r1, #18            ; '9' + 1 = 58
    brp r1, parse_int_done
    brz r1, parse_int_done

    subi r0, #40
    subi r0, #8             ; digit value
    mov r1, r3
    shli r3, #2             ; r3 = r3 * 9
    add r3, r3, r1          ; r3 = r3 * 10
    add r3, r3, r0

    addi r2, #1
    jmp parse_digits

parse_int_done:
    mul r0, r3, r4
    mov r1, r2
    pop r4
    pop r3
    pop r2
    pop r6
    ret

; ==============================================================================
; String Constants & Buffers
; ==============================================================================

str_banner:
    .string "K3-OS v0.2 (Ternary)\n"
str_prompt:
    .string "\e[1;32mk3>\e[0m "
str_bye:
    .string "Bye!\n"
str_unknown:
    .string "?\n"
str_nl:
    .string "\n"
str_ok:
    .string "ok\n"

str_rand:
    .string "R:"
str_trits_prefix:
    .string " ["
str_trits_suffix:
    .string "]\n"

str_mem_l:
    .string "m["
str_mem_r:
    .string "]="
str_exit_code:
    .string "ret:"
str_err_addr:
    .string "bad addr\n"
str_err_file:
    .string "bad file\n"

; Variables and working buffer placed after code & data
cmd_counter:
    .word 0
buf_io:
    .space 32
