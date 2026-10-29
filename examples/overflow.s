; Arithmetic overflow is a trap, not a wrap.
; 9841 is the largest 9-trit value, so adding 1 must trap.
; Expected: r0 = 9841, status trapped, reason overflow.

        li   r0, #9841
        addi r0, #1
        halt
