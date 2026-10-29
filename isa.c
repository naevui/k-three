#include "isa.h"

static unsigned decode_field(const trit_t *t, size_t pos, size_t width)
{
    unsigned value = 0;
    unsigned place = 1;

    for (size_t i = 0; i < width; ++i) {
        value += (unsigned)(t[pos + i] + 1) * place;
        place *= 3;
    }
    return value;
}

void k3_isa_decode(const bt_word *w, k3_isa_insn *out)
{
    out->opcode = decode_field(w->t, 0, 3);
    out->a = decode_field(w->t, 3, 2);
    out->b = decode_field(w->t, 5, 2);
    out->c = decode_field(w->t, 7, 2);
    out->imm = (int32_t)w->t[5] + 3 * (int32_t)w->t[6] +
               9 * (int32_t)w->t[7] + 27 * (int32_t)w->t[8];
}

bool k3_isa_known(unsigned opcode)
{
    return opcode <= (unsigned)K3_OP_SETVEC;
}

const char *k3_isa_mnemonic(unsigned opcode)
{
    static const char *const names[K3_OPCODE_COUNT] = {
        "NOP", "HALT", "MOV", "LDI", "ADD", "ADDI", "SUB", "SUBI",
        "NEG", "MUL", "CMP", "SHL", "SHLI", "SHR", "SHRI", "LOAD",
        "STORE", "JMP", "BRZ", "BRNZ", "BRN", "BRP", "TRAP", "LI",
        "CMPI", "ERET", "SETVEC"
    };

    if (opcode >= (unsigned)K3_OPCODE_COUNT) {
        return NULL;
    }
    return names[opcode];
}
