#include "disasm.h"

#include <stdio.h>

#include "isa.h"

size_t k3_disasm_format(const bt_word *words, size_t count, size_t index,
                        char *buf, size_t cap)
{
    k3_isa_insn in;

    if (words == NULL || buf == NULL || cap == 0 || index >= count) {
        return 0;
    }

    k3_isa_decode(&words[index], &in);

    switch (in.opcode) {
    case K3_OP_NOP:
        snprintf(buf, cap, "NOP");
        return 1;
    case K3_OP_HALT:
        snprintf(buf, cap, "HALT");
        return 1;
    case K3_OP_MOV:
        snprintf(buf, cap, "MOV r%u, r%u", in.a, in.b);
        return 1;
    case K3_OP_LDI:
        snprintf(buf, cap, "LDI r%u, #%d", in.a, (int)in.imm);
        return 1;
    case K3_OP_ADD:
        snprintf(buf, cap, "ADD r%u, r%u, r%u", in.a, in.b, in.c);
        return 1;
    case K3_OP_ADDI:
        snprintf(buf, cap, "ADDI r%u, #%d", in.a, (int)in.imm);
        return 1;
    case K3_OP_SUB:
        snprintf(buf, cap, "SUB r%u, r%u, r%u", in.a, in.b, in.c);
        return 1;
    case K3_OP_SUBI:
        snprintf(buf, cap, "SUBI r%u, #%d", in.a, (int)in.imm);
        return 1;
    case K3_OP_NEG:
        snprintf(buf, cap, "NEG r%u, r%u", in.a, in.b);
        return 1;
    case K3_OP_MUL:
        snprintf(buf, cap, "MUL r%u, r%u, r%u", in.a, in.b, in.c);
        return 1;
    case K3_OP_CMP:
        snprintf(buf, cap, "CMP r%u, r%u, r%u", in.a, in.b, in.c);
        return 1;
    case K3_OP_SHL:
        snprintf(buf, cap, "SHL r%u, r%u, r%u", in.a, in.b, in.c);
        return 1;
    case K3_OP_SHLI:
        snprintf(buf, cap, "SHLI r%u, #%d", in.a, (int)in.imm);
        return 1;
    case K3_OP_SHR:
        snprintf(buf, cap, "SHR r%u, r%u, r%u", in.a, in.b, in.c);
        return 1;
    case K3_OP_SHRI:
        snprintf(buf, cap, "SHRI r%u, #%d", in.a, (int)in.imm);
        return 1;
    case K3_OP_LOAD:
        snprintf(buf, cap, "LOAD r%u, [r%u]", in.a, in.b);
        return 1;
    case K3_OP_STORE:
        snprintf(buf, cap, "STORE r%u, [r%u]", in.a, in.b);
        return 1;
    case K3_OP_JMP:
        snprintf(buf, cap, "JMP r%u", in.a);
        return 1;
    case K3_OP_BRZ:
        snprintf(buf, cap, "BRZ r%u, %d", in.a, (int)in.imm);
        return 1;
    case K3_OP_BRNZ:
        snprintf(buf, cap, "BRNZ r%u, %d", in.a, (int)in.imm);
        return 1;
    case K3_OP_BRN:
        snprintf(buf, cap, "BRN r%u, %d", in.a, (int)in.imm);
        return 1;
    case K3_OP_BRP:
        snprintf(buf, cap, "BRP r%u, %d", in.a, (int)in.imm);
        return 1;
    case K3_OP_TRAP:
        snprintf(buf, cap, "TRAP #%d", (int)in.imm);
        return 1;
    case K3_OP_CMPI:
        snprintf(buf, cap, "CMPI r%u, #%d", in.a, (int)in.imm);
        return 1;
    case K3_OP_ERET:
        snprintf(buf, cap, "ERET");
        return 1;
    case K3_OP_SETVEC:
        snprintf(buf, cap, "SETVEC r%u", in.a);
        return 1;
    case K3_OP_LI: {
        int64_t value = 0;
        size_t used = 1;

        if (index + 1 < count) {
            (void)bt_word_to_i64(&words[index + 1], &value);
            used = 2;
            snprintf(buf, cap, "LI r%u, #%lld", in.a, (long long)value);
        } else {
            snprintf(buf, cap, "LI r%u, <missing literal>", in.a);
        }
        return used;
    }
    default:
        snprintf(buf, cap, "RESERVED op=%u", in.opcode);
        return 1;
    }
}
