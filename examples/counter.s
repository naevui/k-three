; Count down from 3 to 0.
; Expected: r0 = 0, status halted.

        ldi  r0, #3
loop:   addi r0, #-1
        brnz r0, loop
        halt
