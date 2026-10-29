; Compute 7! = 5040 in r1.
; 7! fits in a 9-trit word (maximum 9841); 8! does not.
; Expected: r0 = 0, r1 = 5040, status halted.

        ldi  r0, #7
        ldi  r1, #1
loop:   mul  r1, r1, r0
        addi r0, #-1
        brnz r0, loop
        halt
