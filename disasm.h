/*
 * Host-side instruction formatting for the K-Three tools.
 *
 * This module uses snprintf and is therefore not part of the VM core; the
 * core decoder lives in isa.h/isa.c and performs no I/O.
 */

#ifndef K3_DISASM_H
#define K3_DISASM_H

#include <stddef.h>

#include "ternary.h"

/*
 * Format the instruction at words[index] into `buf`.
 *
 * Returns the number of words consumed: 1 normally, 2 for LI followed by its
 * literal, or 0 when index is out of range.  A reserved opcode is formatted
 * as "RESERVED op=N".  An LI with no following word is formatted as
 * "LI rN, <missing literal>" and consumes one word.
 */
size_t k3_disasm_format(const bt_word *words, size_t count, size_t index,
                        char *buf, size_t cap);

#endif /* K3_DISASM_H */
