; Sum 1 + 2 + ... + 6 = 21 using a label as the branch target.
; Expected: r0 = 0, r1 = 21, status halted.

        ldi  r0, #6
        ldi  r1, #0
loop:   add  r1, r1, r0
        addi r0, #-1
        brnz r0, loop
        halt
