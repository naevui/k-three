; ==============================================================================
; linux/kernel/drivers/rng.s
;
; Hardware True Random Number Generator (TRNG) Driver for K-Three Linux
; Interfaces with HW_RNG_WORD (6008) and HW_RNG_TRIT (6009)
; ==============================================================================

; --- Get Hardware Random Word [-9841 .. +9841] ---
; Output: r0 = random 9-trit balanced-ternary word
; Clobbers: r1
k3_get_random_word:
    li r1, #6008            ; HW_RNG_WORD
    load r0, [r1]
    ret

; --- Get Hardware Random Trit [-1, 0, +1] ---
; Output: r0 = random trit (-1, 0, or 1)
; Clobbers: r1
k3_get_random_trit:
    li r1, #6009            ; HW_RNG_TRIT
    load r0, [r1]
    ret
