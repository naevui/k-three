/*
 * K-Three ISA v0 instruction decoding.
 *
 * This module is part of the core library and performs no I/O.  It is shared
 * by the VM and by the host tools so that there is exactly one definition of
 * the instruction layout.
 *
 * Field encoding (see ISA.md section 3):
 *   t[0..2]  opcode, base-3 digits
 *   t[3..4]  A register, base-3 digits
 *   t[5..6]  B register, base-3 digits (or the low half of a 4-trit immediate)
 *   t[7..8]  C register, base-3 digits (or the high half of that immediate)
 *
 * Opcode and register fields are digits with trit = digit - 1.  The I4
 * immediate t[5..8] is a balanced 4-trit value.  Decoding never validates
 * whether a field is actually used by the opcode; that is the caller's job.
 */

#ifndef K3_ISA_H
#define K3_ISA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ternary.h"

enum k3_opcode {
    K3_OP_NOP = 0,
    K3_OP_HALT = 1,
    K3_OP_MOV = 2,
    K3_OP_LDI = 3,
    K3_OP_ADD = 4,
    K3_OP_ADDI = 5,
    K3_OP_SUB = 6,
    K3_OP_SUBI = 7,
    K3_OP_NEG = 8,
    K3_OP_MUL = 9,
    K3_OP_CMP = 10,
    K3_OP_SHL = 11,
    K3_OP_SHLI = 12,
    K3_OP_SHR = 13,
    K3_OP_SHRI = 14,
    K3_OP_LOAD = 15,
    K3_OP_STORE = 16,
    K3_OP_JMP = 17,
    K3_OP_BRZ = 18,
    K3_OP_BRNZ = 19,
    K3_OP_BRN = 20,
    K3_OP_BRP = 21,
    K3_OP_TRAP = 22,
    K3_OP_LI = 23,
    K3_OP_CMPI = 24,
    K3_OP_ERET = 25,
    K3_OP_SETVEC = 26,
    K3_OPCODE_COUNT = 27
};

typedef struct k3_isa_insn {
    unsigned opcode; /* 0..26 */
    unsigned a;      /* decoded A field, 0..8 */
    unsigned b;      /* decoded B field, 0..8 */
    unsigned c;      /* decoded C field, 0..8 */
    int32_t imm;     /* sign-extended 4-trit immediate */
} k3_isa_insn;

/* Decode a word.  The input word must contain valid trits (-1, 0 or +1). */
void k3_isa_decode(const bt_word *w, k3_isa_insn *out);

/* True for opcodes 0..24; false for the reserved opcodes 25 and 26. */
bool k3_isa_known(unsigned opcode);

/* Canonical uppercase mnemonic, or NULL for a reserved opcode. */
const char *k3_isa_mnemonic(unsigned opcode);

#endif /* K3_ISA_H */
