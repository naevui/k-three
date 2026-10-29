; Store 123 at address 10, load it back and compare.
; Expected: r1 = r2 = 123, r3 = 0 (equal), mem[10] = 123, status halted.

        ldi   r0, #10
        li    r1, #123
        store r1, [r0]
        load  r2, [r0]
        cmp   r3, r2, r1
        brnz  r3, bad
        halt
bad:    trap  #1
