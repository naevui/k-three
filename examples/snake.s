; ==============================================================================
; K3 TERNARY SNAKE - Pure Assembly Real-Time Game for K-Three
;
; Uses hardware non-blocking keyboard read (SYS_KEYHIT, SYS_GETC),
; ANSI cursor positioning (SYS_SETCURSOR), and timer sleep (SYS_SLEEP).
;
; Controls:
;   W / S / A / D : Move Up / Down / Left / Right
;   Q             : Quit Game
; ==============================================================================

start:
    li r7, #6560            ; Initialize stack pointer at top of memory (6561 words)

    ; 1. Enter raw terminal mode
    ldi r0, #1
    trap #SYS_RAW_MODE

    ; 2. Clear screen and hide cursor
    trap #SYS_CLEAR
    li r0, str_hide_cursor
    trap #SYS_PUTS

    ; 3. Draw arena header & walls
    call draw_arena

    ; 4. Initialize game state
    ldi r0, #3
    li r1, snake_len
    store r0, [r1]          ; length = 3

    ldi r0, #1              ; dir = 1 (RIGHT)
    li r1, snake_dir
    store r0, [r1]

    ldi r0, #0              ; score = 0
    li r1, score_val
    store r0, [r1]

    ; Initial head & body coordinates
    ; Head: (8, 6)
    ldi r0, #8
    li r1, snake_x
    store r0, [r1]
    ldi r0, #6
    li r1, snake_y
    store r0, [r1]

    ; Body 1: (7, 6)
    ldi r0, #7
    li r1, snake_x
    addi r1, #1
    store r0, [r1]
    ldi r0, #6
    li r1, snake_y
    addi r1, #1
    store r0, [r1]

    ; Body 2: (6, 6)
    ldi r0, #6
    li r1, snake_x
    addi r1, #2
    store r0, [r1]
    ldi r0, #6
    li r1, snake_y
    addi r1, #2
    store r0, [r1]

    ; Spawn initial food at (16, 6)
    ldi r0, #16
    li r1, food_x
    store r0, [r1]
    ldi r0, #6
    li r1, food_y
    store r0, [r1]
    call draw_food

game_loop:
    ; 1. Sleep 120 ms per frame
    li r0, #120
    trap #SYS_SLEEP

    ; 2. Check for non-blocking keypress
    trap #SYS_KEYHIT
    brnz r0, has_key
    jmp no_key

has_key:
    ; Key pressed, read character
    trap #SYS_GETC
    ; Check 'q' (113): exit
    mov r1, r0
    subi r1, #40
    subi r1, #40
    subi r1, #33            ; 'q' = 113
    brnz r1, not_q
    jmp game_over
not_q:

    ; 'w' (119): UP (dir 0)
    mov r1, r0
    subi r1, #40
    subi r1, #40
    subi r1, #39
    brz r1, is_w
    jmp check_d
is_w:
    li r2, snake_dir
    load r3, [r2]
    subi r3, #2             ; cannot reverse into DOWN
    brnz r3, set_w
    jmp no_key
set_w:
    ldi r3, #0
    store r3, [r2]
    jmp no_key

check_d:
    ; 'd' (100): RIGHT (dir 1)
    mov r1, r0
    subi r1, #40
    subi r1, #40
    subi r1, #20
    brnz r1, check_s
    li r2, snake_dir
    load r3, [r2]
    subi r3, #3             ; cannot reverse into LEFT
    brnz r3, set_d
    jmp no_key
set_d:
    ldi r3, #1
    store r3, [r2]
    jmp no_key

check_s:
    ; 's' (115): DOWN (dir 2)
    mov r1, r0
    subi r1, #40
    subi r1, #40
    subi r1, #35
    brnz r1, check_a
    li r2, snake_dir
    load r3, [r2]
    brnz r3, set_s          ; cannot reverse into UP
    jmp no_key
set_s:
    ldi r3, #2
    store r3, [r2]
    jmp no_key

check_a:
    ; 'a' (97): LEFT (dir 3)
    mov r1, r0
    subi r1, #40
    subi r1, #40
    subi r1, #17
    brnz r1, no_key
    li r2, snake_dir
    load r3, [r2]
    subi r3, #1             ; cannot reverse into RIGHT
    brnz r3, set_a
    jmp no_key
set_a:
    ldi r3, #3
    store r3, [r2]
    jmp no_key

no_key:
    ; 3. Erase current tail from screen
    li r0, snake_len
    load r0, [r0]
    addi r0, #-1            ; r0 = tail index

    li r1, snake_y
    add r1, r1, r0
    load r0, [r1]           ; r0 = tail_y

    li r2, snake_x
    add r2, r2, r0          ; wait: r0 is tail_y, let's fix
    ; tail erase helper
    call erase_tail

    ; 4. Shift body positions down: snake[i] = snake[i-1]
    li r0, snake_len
    load r0, [r0]
    addi r0, #-1            ; r0 = i (starts at len - 1)
shift_loop:
    brz r0, shift_done
    mov r1, r0
    addi r1, #-1            ; r1 = i - 1

    ; snake_x[i] = snake_x[i-1]
    li r2, snake_x
    add r3, r2, r1
    load r4, [r3]
    add r3, r2, r0
    store r4, [r3]

    ; snake_y[i] = snake_y[i-1]
    li r2, snake_y
    add r3, r2, r1
    load r4, [r3]
    add r3, r2, r0
    store r4, [r3]

    addi r0, #-1
    jmp shift_loop

shift_done:
    ; 5. Update head position based on snake_dir
    li r0, snake_x
    load r1, [r0]           ; r1 = head_x
    li r0, snake_y
    load r2, [r0]           ; r2 = head_y

    li r3, snake_dir
    load r3, [r3]

    ; dir 0 (UP)
    brnz r3, dir_not_0
    addi r2, #-1
    jmp dir_done
dir_not_0:
    subi r3, #1             ; dir 1 (RIGHT)
    brnz r3, dir_not_1
    addi r1, #1
    jmp dir_done
dir_not_1:
    subi r3, #1             ; dir 2 (DOWN)
    brnz r3, dir_not_2
    addi r2, #1
    jmp dir_done
dir_not_2:
    ; dir 3 (LEFT)
    addi r1, #-1

dir_done:
    ; Store new head coords
    li r0, snake_x
    store r1, [r0]
    li r0, snake_y
    store r2, [r0]

    ; 6. Check Wall Collisions (x: 2..23, y: 3..12)
    ; head_x (r1) <= 1 or >= 24
    mov r0, r1
    subi r0, #2
    brn r0, dead2
    mov r0, r1
    subi r0, #24
    brp r0, dead2
    brz r0, dead2

    ; head_y (r2) <= 2 or >= 13
    mov r0, r2
    subi r0, #3
    brn r0, dead2
    mov r0, r2
    subi r0, #13
    brp r0, dead2
    brz r0, dead2

    ; 7. Check Self Collision
    li r0, snake_len
    load r0, [r0]
    ldi r3, #1              ; r3 = check index
self_loop:
    mov r4, r0
    sub r4, r4, r3
    brz r4, self_ok

    li r4, snake_x
    add r4, r4, r3
    load r4, [r4]
    cmp r4, r4, r1          ; compare with head_x
    brnz r4, next_self

    li r4, snake_y
    add r4, r4, r3
    load r4, [r4]
    cmp r4, r4, r2          ; compare with head_y
    brz r4, dead2

next_self:
    addi r3, #1
    jmp self_loop

dead2:
    jmp game_over

self_ok:
    ; 8. Draw Snake Body at previous head (snake[1])
    li r0, snake_x
    addi r0, #1
    load r1, [r0]
    li r0, snake_y
    addi r0, #1
    load r0, [r0]           ; r0 = y, r1 = x
    trap #SYS_SETCURSOR
    li r0, str_body
    trap #SYS_PUTS

    ; 9. Draw Snake Head at (snake[0])
    li r0, snake_x
    load r1, [r0]
    li r0, snake_y
    load r0, [r0]           ; r0 = y, r1 = x
    trap #SYS_SETCURSOR
    li r0, str_head
    trap #SYS_PUTS

    ; 10. Check Food Collision
    li r0, food_x
    load r3, [r0]
    li r0, food_y
    load r4, [r0]

    cmp r0, r1, r3
    brnz r0, no_food_hit
    cmp r0, r2, r4
    brnz r0, no_food_hit

    ; Food Eaten!
    ; Increase score by 10
    li r0, score_val
    load r1, [r0]
    addi r1, #10
    store r1, [r0]

    ; Update Score display: cursor at row 1, col 30
    ldi r0, #1
    ldi r1, #30
    trap #SYS_SETCURSOR
    li r0, score_val
    load r0, [r0]
    trap #SYS_PUTNUM

    ; Increase snake length
    li r0, snake_len
    load r1, [r0]
    addi r1, #1
    store r1, [r0]

    ; Spawn new food
    call spawn_food

no_food_hit:
    jmp game_loop

game_over:
    ; Restore cursor, disable raw mode, print result
    ldi r0, #15
    ldi r1, #1
    trap #SYS_SETCURSOR

    li r0, str_show_cursor
    trap #SYS_PUTS

    ldi r0, #0
    trap #SYS_RAW_MODE

    li r0, str_game_over
    trap #SYS_PUTS

    li r0, score_val
    load r0, [r0]
    trap #SYS_PUTNUM

    li r0, str_nl
    trap #SYS_PUTS

    ldi r0, #0
    trap #SYS_EXIT
    halt

; ==============================================================================
; Subroutines
; ==============================================================================

erase_tail:
    push r6
    li r0, snake_len
    load r0, [r0]
    addi r0, #-1            ; tail index
    li r1, snake_x
    add r1, r1, r0
    load r1, [r1]           ; r1 = tail_x
    li r2, snake_y
    add r2, r2, r0
    load r0, [r2]           ; r0 = tail_y
    trap #SYS_SETCURSOR
    ldi r0, #' '
    trap #SYS_PUTC
    pop r6
    ret

spawn_food:
    push r6
    ; Pseudo-random X between 3 and 22:
    trap #SYS_RANDOM
    ; make positive
    brp r0, rx_pos
    neg r0, r0
rx_pos:
    mov r1, r0
    ; r0 % 20: approximate modulo
    li r2, #20
    call umod
    addi r0, #3             ; 3..22
    li r1, food_x
    store r0, [r1]

    ; Pseudo-random Y between 4 and 11:
    trap #SYS_RANDOM
    brp r0, ry_pos
    neg r0, r0
ry_pos:
    li r2, #8
    call umod
    addi r0, #4             ; 4..11
    li r1, food_y
    store r0, [r1]

    call draw_food
    pop r6
    ret

draw_food:
    push r6
    li r0, food_x
    load r1, [r0]           ; r1 = col (x)
    li r0, food_y
    load r0, [r0]           ; r0 = row (y)
    trap #SYS_SETCURSOR
    li r0, str_food
    trap #SYS_PUTS
    pop r6
    ret

draw_arena:
    push r6
    ; Top title
    ldi r0, #1
    ldi r1, #1
    trap #SYS_SETCURSOR
    li r0, str_title
    trap #SYS_PUTS

    ; Top wall at row 2
    ldi r0, #2
    ldi r1, #1
    trap #SYS_SETCURSOR
    li r0, str_wall_h
    trap #SYS_PUTS

    ; Side walls rows 3..12
    ldi r2, #3
draw_sides:
    mov r0, r2
    ldi r1, #1
    trap #SYS_SETCURSOR
    li r0, str_wall_v
    trap #SYS_PUTS

    addi r2, #1
    mov r3, r2
    subi r3, #13
    brn r3, draw_sides

    ; Bottom wall at row 13
    ldi r0, #13
    ldi r1, #1
    trap #SYS_SETCURSOR
    li r0, str_wall_h
    trap #SYS_PUTS
    pop r6
    ret

; umod: computes r0 = r1 % r2 (unsigned division)
umod:
    mov r0, r1
umod_loop:
    cmp r3, r0, r2
    brn r3, umod_done
    sub r0, r0, r2
    jmp umod_loop
umod_done:
    ret

; ==============================================================================
; String Constants & Variables
; ==============================================================================

str_title:
    .string "\e[1;36m=== K3 TERNARY SNAKE ===\e[0m  Score: 0"
str_wall_h:
    .string "+------------------------+"
str_wall_v:
    .string "|                        |"
str_head:
    .string "\e[1;32m@\e[0m"
str_body:
    .string "\e[1;32m#\e[0m"
str_food:
    .string "\e[1;33m*\e[0m"
str_hide_cursor:
    .string "\e[?25l"
str_show_cursor:
    .string "\e[?25h"
str_game_over:
    .string "\n\e[1;31m*** GAME OVER! ***\e[0m  Final Score: "
str_nl:
    .string "\n"

; Game variables allocated after code
snake_len:
    .word 3
snake_dir:
    .word 1
score_val:
    .word 0
food_x:
    .word 16
food_y:
    .word 6

snake_x:
    .space 64
snake_y:
    .space 64
