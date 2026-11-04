/*
 * K-Three balanced-ternary reference arithmetic.
 *
 * A "word" is K3_TRITS (= 9) balanced trits.  A trit is stored as one int8_t
 * with the value -1, 0 or +1.  No packing is performed.
 *
 * Trit order: t[0] is the least significant trit (weight 3^0), t[n-1] is the
 * most significant trit (weight 3^(n-1)).  The represented value is
 *
 *     sum(t[i] * 3^i)
 *
 * so an n-trit word represents exactly the integers in
 * [-(3^n - 1) / 2, +(3^n - 1) / 2].
 *
 * All functions except bt_valid/bt_to_i64 assume every input trit is -1, 0 or
 * +1.  Passing any other value is outside the contract.  Output buffers must
 * not alias input buffers unless the individual function says otherwise.
 */

#ifndef K3_TERNARY_H
#define K3_TERNARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define K3_TRITS 9
#define K3_WORD_MIN (-9841)
#define K3_WORD_MAX (9841)

typedef int8_t trit_t;

typedef struct bt_word {
    trit_t t[K3_TRITS];
} bt_word;

typedef enum bt_status {
    BT_OK = 0,
    BT_OVERFLOW = 1
} bt_status;

/* Return true iff every trit in t[0..n-1] is -1, 0 or +1. */
bool bt_valid(const trit_t *t, size_t n);

/* Set every trit in t[0..n-1] to 0.  n may be zero (t is not dereferenced). */
void bt_zero(trit_t *t, size_t n);

/*
 * Convert an n-trit value to int64_t.
 *
 * Returns false and stores 0 in *out if a trit is invalid or if the value does
 * not fit in int64_t.  Otherwise returns true.
 */
bool bt_to_i64(const trit_t *t, size_t n, int64_t *out);

/*
 * Convert an int64_t to exactly n balanced trits.
 *
 * Returns false if value is outside the representable range, in which case all
 * n output trits are set to 0.  On success writes t[0..n-1].
 * n == 0 is legal: it succeeds only for value == 0.
 */
bool bt_from_i64(int64_t value, trit_t *out, size_t n);

/*
 * Compare two n-trit values numerically.  Returns -1, 0 or +1.
 * Inputs may alias each other; no output is written.
 */
int bt_cmp(const trit_t *a, const trit_t *b, size_t n);

/*
 * Addition and subtraction.
 *
 * The low n trits of the exact result are written to out.  If carry is not
 * NULL it receives the balanced carry trit c in {-1, 0, +1} such that
 *
 *     value(a) + value(b) == value(out) + c * 3^n   (add)
 *     value(a) - value(b) == value(out) + c * 3^n   (sub)
 *
 * BT_OVERFLOW is returned iff c != 0.  The result is the wrapped low-n-trit
 * value, not a saturated value; the caller must inspect the status/carry.
 *
 * out may alias a and/or b.
 */
bt_status bt_add(const trit_t *a, const trit_t *b, size_t n,
                 trit_t *out, trit_t *carry);
bt_status bt_sub(const trit_t *a, const trit_t *b, size_t n,
                 trit_t *out, trit_t *carry);

/*
 * Two's-complement-style negation in balanced ternary.  The n-trit range is
 * symmetric, so negation never overflows.  out may alias a.
 */
void bt_neg(const trit_t *a, size_t n, trit_t *out);

/*
 * Multiplication.
 *
 * Writes the exact 2n-trit product split into two n-trit halves:
 *
 *     value(a) * value(b) == value(out) + value(high) * 3^n
 *
 * Both out and high must point to n writable trits and must not overlap each
 * other or either input.  BT_OVERFLOW is returned iff high is not zero (that
 * is, iff the product does not fit in n trits).  The product always fits in
 * 2n trits.
 */
bt_status bt_mul(const trit_t *a, const trit_t *b, size_t n,
                 trit_t *out, trit_t *high);

/*
 * Shift left by amount trits: out is the low n trits of value(a) * 3^amount.
 * BT_OVERFLOW is returned iff any nonzero trit was shifted past position n-1.
 * If amount >= n the result is zero and the status is BT_OVERFLOW iff a is
 * nonzero.  out must not alias a.
 */
bt_status bt_shl(const trit_t *a, size_t n, size_t amount, trit_t *out);

/*
 * Shift right by amount trits, discarding the amount least significant trits.
 *
 * This is balanced division by 3^amount with the balanced remainder discarded,
 * i.e. round-to-nearest with ties impossible because 3^amount is odd.  It is
 * not truncation toward zero.  Examples:
 *
 *      5 >> 1 ==  2      (5 = 3*2 - 1)
 *     -5 >> 1 == -2      (-5 = 3*(-2) + 1)
 *      4 >> 1 ==  1
 *
 * Right shift never overflows.  If amount >= n the result is zero.  out must
 * not alias a.
 */
void bt_shr(const trit_t *a, size_t n, size_t amount, trit_t *out);

/* Convenience helpers for the fixed 9-trit word. */
void bt_word_zero(bt_word *w);
bool bt_word_from_i64(int64_t value, bt_word *w);
bool bt_word_to_i64(const bt_word *w, int64_t *out);

#endif /* K3_TERNARY_H */
