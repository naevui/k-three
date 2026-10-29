; Trial-division prime finder for every prime up to 41.
; Primes are stored at the `primes` array; r1 = count, r2 = largest prime,
; r3 = sum of all primes.
; Expected: r0 = 0, r1 = 13, r2 = 41, r3 = 238.

        li    r1, var_count
        ldi   r0, #0
        store r0, [r1]          ; count = 0
        li    r1, var_last
        store r0, [r1]          ; last = 0
        li    r1, var_sum
        store r0, [r1]          ; sum = 0
        ldi   r0, #40
        addi  r0, #1
        li    r1, var_limit
        store r0, [r1]          ; limit = 41
        ldi   r0, #2
        li    r1, var_n
        store r0, [r1]          ; n = 2

outer:
        li    r1, var_n
        load  r0, [r1]          ; r0 = n
        li    r1, var_limit
        load  r1, [r1]          ; r1 = limit
        cmp   r2, r0, r1
        brp   r2, maybe_done    ; n > limit
        jmp   inner_start

maybe_done:
        jmp   done

inner_start:
        ldi   r0, #2
        li    r1, var_d
        store r0, [r1]          ; d = 2

inner:
        li    r1, var_d
        load  r1, [r1]          ; r1 = d
        mul   r2, r1, r1        ; r2 = d * d
        li    r3, var_n
        load  r0, [r3]          ; r0 = n
        cmp   r3, r2, r0
        brp   r3, prime         ; d * d > n  => n is prime

        ; divisibility test: subtract d from n until the remainder is <= 0
        mov   r2, r0
div_loop:
        brz   r2, composite     ; remainder 0 => n is composite
        brn   r2, next_d        ; remainder < 0 => not divisible by d
        sub   r2, r2, r1
        jmp   div_loop

next_d:
        li    r3, var_d
        load  r1, [r3]
        addi  r1, #1
        store r1, [r3]          ; d = d + 1
        jmp   inner

composite:
        li    r3, var_n
        load  r1, [r3]
        addi  r1, #1
        store r1, [r3]          ; n = n + 1
        jmp   outer

prime:
        li    r3, var_count
        load  r2, [r3]          ; r2 = count
        li    r4, primes
        add   r4, r4, r2        ; r4 = &primes[count]
        li    r1, var_n
        load  r1, [r1]          ; r1 = n
        store r1, [r4]          ; primes[count] = n
        addi  r2, #1
        store r2, [r3]          ; count = count + 1
        li    r3, var_last
        store r1, [r3]          ; last = n
        li    r3, var_sum
        load  r2, [r3]
        add   r2, r2, r1
        store r2, [r3]          ; sum = sum + n
        jmp   composite

done:
        li    r1, var_count
        load  r1, [r1]          ; r1 = count
        li    r2, var_last
        load  r2, [r2]          ; r2 = largest prime
        li    r3, var_sum
        load  r3, [r3]          ; r3 = sum of primes
        ldi   r0, #0
        halt

var_count: .word 0
var_last:  .word 0
var_sum:   .word 0
var_limit: .word 0
var_n:     .word 0
var_d:     .word 0
primes:    .space 16
