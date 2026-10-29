/* Decoder tests for isa.h / isa.c. */

#include "isa.h"
#include "ternary.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

static void check(int condition, const char *what)
{
    ++checks;
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

static bt_word word_from_trits(const int t[K3_TRITS])
{
    bt_word w;

    for (size_t i = 0; i < K3_TRITS; ++i) {
        w.t[i] = (trit_t)t[i];
    }
    return w;
}

static void check_decoded(const char *name, const int t[K3_TRITS],
                          unsigned opcode, unsigned a, unsigned b,
                          unsigned c, int32_t imm)
{
    bt_word w = word_from_trits(t);
    k3_isa_insn in;

    k3_isa_decode(&w, &in);
    check(in.opcode == opcode, name);
    check(in.a == a, name);
    check(in.b == b, name);
    check(in.c == c, name);
    check(in.imm == imm, name);
}

/* Encode a register field the way the assembler does: trit = digit - 1. */
static void put_digits(trit_t *t, unsigned value, size_t width)
{
    unsigned place = 1;

    for (size_t i = 0; i < width; ++i) {
        t[i] = (trit_t)((int)((value / place) % 3u) - 1);
        place *= 3;
    }
}

int main(void)
{
    /* The four encodings listed in ISA.md section 8. */
    static const int halt_t[K3_TRITS] =
        {0, -1, -1, 0, 0, 0, 0, 0, 0};
    static const int add_t[K3_TRITS] =
        {0, 0, -1, -1, 0, 0, -1, 1, -1};
    static const int ldi_t[K3_TRITS] =
        {-1, 0, -1, 0, -1, 0, -1, 1, 0};
    static const int brnz_t[K3_TRITS] =
        {0, -1, 1, 0, -1, 1, -1, 0, 0};

    check_decoded("HALT", halt_t, K3_OP_HALT, 4, 4, 4, 0);
    check_decoded("ADD r3, r1, r2", add_t, K3_OP_ADD, 3, 1, 2, -21);
    check_decoded("LDI r1, #6", ldi_t, K3_OP_LDI, 1, 1, 5, 6);
    check_decoded("BRNZ r1, #-2", brnz_t, K3_OP_BRNZ, 1, 2, 4, -2);

    /* Every register field value decodes in 0..8. */
    for (unsigned reg = 0; reg <= 8; ++reg) {
        bt_word w;
        k3_isa_insn in;

        bt_word_zero(&w);
        put_digits(&w.t[0], K3_OP_NOP, 3);
        put_digits(&w.t[3], reg, 2);
        put_digits(&w.t[5], reg, 2);
        put_digits(&w.t[7], reg, 2);
        k3_isa_decode(&w, &in);
        check(in.a == reg && in.b == reg && in.c == reg,
              "register field decodes 0..8");
    }

    /* Every opcode value decodes in 0..26. */
    for (unsigned opcode = 0; opcode < K3_OPCODE_COUNT; ++opcode) {
        bt_word w;
        k3_isa_insn in;

        bt_word_zero(&w);
        put_digits(&w.t[0], opcode, 3);
        k3_isa_decode(&w, &in);
        check(in.opcode == opcode, "opcode field decodes 0..26");
    }

    /* The immediate is a balanced 4-trit value over its whole range. */
    for (int value = -40; value <= 40; ++value) {
        bt_word w;
        k3_isa_insn in;

        bt_word_zero(&w);
        put_digits(&w.t[0], K3_OP_LDI, 3);
        check(bt_from_i64((int64_t)value, &w.t[5], 4),
              "4-trit immediate encodes -40..40");
        k3_isa_decode(&w, &in);
        check(in.imm == value, "4-trit immediate decodes -40..40");
    }
    {
        trit_t tmp[4];

        check(!bt_from_i64(41, tmp, 4), "41 does not fit in 4 trits");
        check(!bt_from_i64(-41, tmp, 4), "-41 does not fit in 4 trits");
    }

    /* Opcodes 0..26 are known and have mnemonics; 27 is unknown. */
    check(k3_isa_known(K3_OP_NOP), "0 is known");
    check(k3_isa_known(K3_OP_CMPI), "24 is known");
    check(k3_isa_known(K3_OP_ERET), "25 is known (ERET)");
    check(k3_isa_known(K3_OP_SETVEC), "26 is known (SETVEC)");
    check(!k3_isa_known(27), "27 is unknown");
    check(k3_isa_mnemonic(27) == NULL, "27 has no mnemonic");

    /* Spellings match ISA.md. */
    check(k3_isa_mnemonic(K3_OP_NOP) != NULL &&
          k3_isa_mnemonic(K3_OP_NOP)[0] == 'N', "NOP mnemonic");
    check(k3_isa_mnemonic(K3_OP_CMPI) != NULL &&
          k3_isa_mnemonic(K3_OP_CMPI)[0] == 'C', "CMPI mnemonic");
    check(k3_isa_mnemonic(K3_OP_ERET) != NULL &&
          strcmp(k3_isa_mnemonic(K3_OP_ERET), "ERET") == 0, "ERET mnemonic");
    check(k3_isa_mnemonic(K3_OP_SETVEC) != NULL &&
          strcmp(k3_isa_mnemonic(K3_OP_SETVEC), "SETVEC") == 0, "SETVEC mnemonic");
    for (unsigned op = 0; op <= K3_OP_SETVEC; ++op) {
        check(k3_isa_mnemonic(op) != NULL, "known opcode has a mnemonic");
    }

    printf("test_isa: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
