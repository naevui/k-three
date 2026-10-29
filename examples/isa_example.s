; The example program from ISA.md section 8.
; r1 = 728, then count r2 down from 5.
; Expected: r1 = 728, r2 = 0, status halted.

start:  ldi  r1, #1
        shli r1, #6          ; 1 * 3^6 = 729
        addi r1, #-1         ; 728

        ldi  r2, #5
loop:   addi r2, #-1
        brnz r2, loop        ; offset -2 from this branch
        halt
