#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "packed.h"
#include "ternary.h"

static long g_checks = 0;
static long g_failures = 0;

static void check(bool cond, const char *msg)
{
    ++g_checks;
    if (!cond) {
        ++g_failures;
        fprintf(stderr, "FAIL: %s\n", msg);
    }
}

static void test_trit_packing(void)
{
    check(bt_pack_trit(0) == K3_PACKED_ZERO, "pack 0");
    check(bt_pack_trit(1) == K3_PACKED_POS, "pack +1");
    check(bt_pack_trit(-1) == K3_PACKED_NEG, "pack -1");
    check(bt_pack_trit(2) == K3_PACKED_INV, "pack invalid");

    trit_t t = 99;
    check(bt_unpack_trit(K3_PACKED_ZERO, &t) && t == 0, "unpack 0");
    check(bt_unpack_trit(K3_PACKED_POS, &t) && t == 1, "unpack +1");
    check(bt_unpack_trit(K3_PACKED_NEG, &t) && t == -1, "unpack -1");
    check(!bt_unpack_trit(K3_PACKED_INV, &t), "unpack invalid 11");
}

static void test_exhaustive_words(void)
{
    /* Test all 3^9 = 19683 representable 9-trit words */
    for (int64_t val = -K3_WORD_MAX; val <= K3_WORD_MAX; ++val) {
        bt_word w;
        uint32_t packed = 0;
        bt_word w2;

        if (!bt_word_from_i64(val, &w)) {
            check(false, "bt_word_from_i64 failed");
            continue;
        }

        check(bt_word_pack(&w, &packed), "pack valid word");
        check(bt_packed_word_valid(packed), "packed word must be valid");

        check(bt_word_unpack(packed, &w2), "unpack valid word");
        int64_t val2 = 0;
        check(bt_word_to_i64(&w2, &val2) && val2 == val, "round-trip value match");

        /* Packed negation test */
        uint32_t neg_packed = bt_packed_neg(packed);
        bt_word neg_word;
        check(bt_word_unpack(neg_packed, &neg_word), "unpack negated word");
        int64_t neg_val = 0;
        check(bt_word_to_i64(&neg_word, &neg_val) && neg_val == -val, "negated value match");
    }
}

static void test_invalid_packed(void)
{
    /* Test that any packed word with '11' in any trit position is rejected */
    for (size_t pos = 0; pos < K3_TRITS; ++pos) {
        uint32_t bad_packed = ((uint32_t)K3_PACKED_INV) << (pos * 2);
        check(!bt_packed_word_valid(bad_packed), "word with 11 must be invalid");
        bt_word dummy;
        check(!bt_word_unpack(bad_packed, &dummy), "unpacking word with 11 must fail");
    }

    /* Bits outside 18-bit range */
    check(!bt_packed_word_valid(0x40000u), "bit 18 set must be invalid");
}

static void test_stream_packing(void)
{
    #define STREAM_WORDS 17
    bt_word original[STREAM_WORDS];
    for (size_t i = 0; i < STREAM_WORDS; ++i) {
        int64_t v = (int64_t)(i * 500) - 4000;
        (void)bt_word_from_i64(v, &original[i]);
    }

    uint8_t buf[256];
    size_t written = bt_pack_stream(original, STREAM_WORDS, buf, sizeof(buf));
    /* 17 words: 4 chunks of 4 words = 16 words (4 * 9 = 36 bytes) + 1 word (3 bytes) = 39 bytes */
    check(written == 39, "stream pack size match");

    bt_word recovered[STREAM_WORDS];
    size_t words_read = bt_unpack_stream(buf, written, recovered, STREAM_WORDS);
    check(words_read == STREAM_WORDS, "stream unpack count match");

    for (size_t i = 0; i < STREAM_WORDS; ++i) {
        int64_t v1 = 0, v2 = 0;
        (void)bt_word_to_i64(&original[i], &v1);
        (void)bt_word_to_i64(&recovered[i], &v2);
        check(v1 == v2, "stream round-trip value match");
    }
}

int main(void)
{
    test_trit_packing();
    test_invalid_packed();
    test_exhaustive_words();
    test_stream_packing();

    printf("test_packed: %ld checks, %ld failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
