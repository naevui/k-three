/*
 * K-Three 2-bit-per-trit packed encoding.
 *
 * Each balanced trit (-1, 0, +1) is encoded in 2 bits:
 *   00 (0) ->  0
 *   01 (1) -> +1
 *   10 (2) -> -1
 *   11 (3) -> invalid / reserved
 *
 * A 9-trit word is packed into 18 bits of a uint32_t.
 * Trit 0 is in bits [1..0], trit 1 in bits [3..2], ..., trit 8 in bits [17..16].
 */

#ifndef K3_PACKED_H
#define K3_PACKED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ternary.h"

#define K3_PACKED_TRIT_MASK 0x3u
#define K3_PACKED_WORD_MASK 0x3FFFFu /* 18 bits */

#define K3_PACKED_ZERO 0x00u
#define K3_PACKED_POS  0x01u
#define K3_PACKED_NEG  0x02u
#define K3_PACKED_INV  0x03u

/* Encode a single trit (-1, 0, +1) into 2-bit representation (0..3). */
uint8_t bt_pack_trit(trit_t t);

/* Decode a 2-bit value into a trit. Returns false if bits == 11 (invalid). */
bool bt_unpack_trit(uint8_t bits, trit_t *out);

/* Check if an 18-bit packed word contains only valid trits (no 11 pairs). */
bool bt_packed_word_valid(uint32_t packed);

/* Pack a 9-trit bt_word into an 18-bit integer. Returns false on invalid input. */
bool bt_word_pack(const bt_word *w, uint32_t *out);

/* Unpack an 18-bit integer into a 9-trit bt_word. Returns false on invalid bits. */
bool bt_word_unpack(uint32_t packed, bt_word *out);

/* Fast packed negation: swaps 01 (+1) and 10 (-1). Preserves 00 and 11. */
uint32_t bt_packed_neg(uint32_t packed);

/* Pack an array of bt_words into a compact byte stream (9 trits = 18 bits = 2.25 bytes per word).
 * In 9-byte chunks, 4 words (72 bits) fit exactly into 9 bytes without waste.
 * Returns bytes written.
 */
size_t bt_pack_stream(const bt_word *words, size_t count, uint8_t *buf, size_t buf_cap);

/* Unpack a compact byte stream into an array of bt_words. Returns words read. */
size_t bt_unpack_stream(const uint8_t *buf, size_t byte_count, bt_word *words, size_t words_cap);

#endif /* K3_PACKED_H */
