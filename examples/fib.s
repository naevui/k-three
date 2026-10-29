; Compute Fibonacci numbers up to F(19) = 4181.
; Writes the first 20 Fibonacci numbers into fib_table[0..19].
; r1 = count (20), r2 = F(19) (4181), r3 = F(18) (2584).
; F(0)=0, F(1)=1, F(2)=1, F(3)=2, F(4)=3, F(5)=5, F(6)=8, F(7)=13,
; F(8)=21, F(9)=34, F(10)=55, F(11)=89, F(12)=144, F(13)=233, F(14)=377,
; F(15)=610, F(16)=987, F(17)=1597, F(18)=2584, F(19)=4181.
; Expected: r0 = 0, r1 = 20, r2 = 4181, r3 = 2584.

        ldi   r0, #0            ; a = 0
        ldi   r1, #1            ; b = 1
        ldi   r2, #0            ; count = 0
        ldi   r4, #20           ; limit = 20

        li    r5, fib_table
        store r0, [r5]          ; fib_table[0] = 0
        addi  r5, #1
        store r1, [r5]          ; fib_table[1] = 1

        ldi   r2, #2            ; count = 2

fib_loop:
        cmp   r5, r2, r4
        brz   r5, fib_done      ; if count == limit, done

        ; next = a + b
        add   r5, r0, r1        ; r5 = next
        mov   r0, r1            ; a = b
        mov   r1, r5            ; b = next

        ; store in table
        li    r6, fib_table
        add   r6, r6, r2
        store r1, [r6]          ; fib_table[count] = next

        addi  r2, #1            ; count++
        jmp   fib_loop

fib_done:
        mov   r3, r0            ; r3 = F(18) = 2584
        mov   r2, r1            ; r2 = F(19) = 4181
        mov   r1, r4            ; r1 = count = 20
        ldi   r0, #0
        halt

fib_table: .space 20
