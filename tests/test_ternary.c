#include "ternary.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned long checks;
static unsigned long failures;

static void report_failure(const char *file, int line, const char *message)
{
    ++failures;
    if (failures <= 25) {
        fprintf(stderr, "FAIL %s:%d: %s\n", file, line, message);
    }
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++checks;                                                            \
        if (!(cond)) {                                                       \
            report_failure(__FILE__, __LINE__, #cond);                       \
        }                                                                    \
    } while (0)

#define CHECK_EQ_I64(got, want)                                              \
    do {                                                                     \
        int64_t got_ = (int64_t)(got);                                        \
        int64_t want_ = (int64_t)(want);                                      \
        ++checks;                                                            \
        if (got_ != want_) {                                                  \
            ++failures;                                                      \
            if (failures <= 25) {                                            \
                fprintf(stderr,                                              \
                        "FAIL %s:%d: %s == %" PRId64 ", want %" PRId64 "\n", \
                        __FILE__, __LINE__, #got, got_, want_);              \
            }                                                                \
        }                                                                    \
    } while (0)

static int64_t pow3i(size_t n)
{
    int64_t p = 1;
    for (size_t i = 0; i < n; ++i) {
        p *= 3;
    }
    return p;
}

/*
 * Independent reference: extract n balanced trits and return the low-n value.
 * Also used to derive the expected carry/high half.
 */
static int64_t reference_wrap(int64_t value, size_t n)
{
    int64_t v = value;
    int64_t low = 0;
    int64_t p = 1;

    for (size_t i = 0; i < n; ++i) {
        int64_t q = v / 3;
        int r = (int)(v - q * 3);

        if (r == 2) {
            r = -1;
            q += 1;
        } else if (r == -2) {
            r = 1;
            q -= 1;
        }

        low += (int64_t)r * p;
        v = q;
        p *= 3;
    }

    return low;
}

/* Independent reference for dropping the low `amount` balanced trits. */
static int64_t reference_shr(int64_t value, size_t amount)
{
    int64_t v = value;

    for (size_t i = 0; i < amount; ++i) {
        int64_t q = v / 3;
        int r = (int)(v - q * 3);

        if (r == 2) {
            q += 1;
        } else if (r == -2) {
            q -= 1;
        }
        v = q;
    }

    return v;
}

static void check_value(const trit_t *t, size_t n, int64_t want,
                        const char *label)
{
    int64_t got = 0;
    bool ok = bt_to_i64(t, n, &got);

    ++checks;
    if (!ok || got != want) {
        ++failures;
        if (failures <= 25) {
            fprintf(stderr,
                    "FAIL %s: value %" PRId64 " (conversion ok=%d), "
                    "want %" PRId64 "\n",
                    label, got, (int)ok, want);
        }
    }

    ++checks;
    if (!bt_valid(t, n)) {
        ++failures;
        if (failures <= 25) {
            fprintf(stderr, "FAIL %s: invalid trit in result\n", label);
        }
    }
}

static void verify_basic_ops(size_t n, int64_t radix,
                             const trit_t *a, int64_t av,
                             const trit_t *b, int64_t bv)
{
    trit_t out[K3_TRITS];
    trit_t high[K3_TRITS];
    trit_t carry = 42;

    int64_t exact = av + bv;
    int64_t wrapped = reference_wrap(exact, n);
    int64_t carry_want = (exact - wrapped) / radix;
    bt_status status = bt_add(a, b, n, out, &carry);

    CHECK((status == BT_OVERFLOW) == (carry_want != 0));
    CHECK_EQ_I64(carry, carry_want);
    check_value(out, n, wrapped, "add");

    CHECK_EQ_I64(bt_add(a, b, n, out, NULL), status);

    exact = av - bv;
    wrapped = reference_wrap(exact, n);
    carry_want = (exact - wrapped) / radix;
    carry = 42;
    status = bt_sub(a, b, n, out, &carry);

    CHECK((status == BT_OVERFLOW) == (carry_want != 0));
    CHECK_EQ_I64(carry, carry_want);
    check_value(out, n, wrapped, "sub");

    CHECK_EQ_I64(bt_sub(a, b, n, out, NULL), status);

    exact = av * bv;
    wrapped = reference_wrap(exact, n);
    int64_t high_want = (exact - wrapped) / radix;
    status = bt_mul(a, b, n, out, high);

    CHECK((status == BT_OVERFLOW) == (high_want != 0));
    check_value(out, n, wrapped, "mul low");
    check_value(high, n, high_want, "mul high");

    int cmp_want = (av < bv) ? -1 : (av > bv) ? 1 : 0;
    CHECK_EQ_I64(bt_cmp(a, b, n), cmp_want);

    bt_neg(a, n, out);
    check_value(out, n, -av, "neg");
}

static void verify_shift(size_t n, const trit_t *a, int64_t av, size_t amount)
{
    trit_t out[K3_TRITS];

    int64_t exact = av * pow3i(amount);
    int64_t wrapped = reference_wrap(exact, n);
    bt_status status = bt_shl(a, n, amount, out);

    CHECK((status == BT_OVERFLOW) == (exact != wrapped));
    check_value(out, n, wrapped, "shl");

    bt_shr(a, n, amount, out);
    check_value(out, n, reference_shr(av, amount), "shr");
}

static void test_conversion_4(void)
{
    trit_t t[4];

    for (int v = -40; v <= 40; ++v) {
        CHECK(bt_from_i64(v, t, 4));
        check_value(t, 4, v, "from_i64/4");

        int64_t back = 123;
        CHECK(bt_to_i64(t, 4, &back));
        CHECK_EQ_I64(back, v);
        CHECK_EQ_I64(bt_cmp(t, t, 4), 0);
    }

    CHECK(!bt_from_i64(41, t, 4));
    for (size_t i = 0; i < 4; ++i) {
        CHECK_EQ_I64(t[i], 0);
    }

    CHECK(!bt_from_i64(-41, t, 4));
    for (size_t i = 0; i < 4; ++i) {
        CHECK_EQ_I64(t[i], 0);
    }

    for (size_t i = 0; i < 4; ++i) {
        t[i] = 1;
    }
    check_value(t, 4, 40, "all +1/4");

    for (size_t i = 0; i < 4; ++i) {
        t[i] = -1;
    }
    check_value(t, 4, -40, "all -1/4");
}

static void test_conversion_9(void)
{
    trit_t t[K3_TRITS];
    trit_t all_plus[K3_TRITS];
    trit_t all_minus[K3_TRITS];

    /* Exhaustive round-trip over every representable 9-trit value. */
    for (int64_t v = K3_WORD_MIN; v <= K3_WORD_MAX; ++v) {
        CHECK(bt_from_i64(v, t, K3_TRITS));
        check_value(t, K3_TRITS, v, "from_i64/9");

        int64_t back = 0;
        CHECK(bt_to_i64(t, K3_TRITS, &back));
        CHECK_EQ_I64(back, v);
    }

    CHECK(!bt_from_i64(K3_WORD_MAX + 1, t, K3_TRITS));
    for (size_t i = 0; i < K3_TRITS; ++i) {
        CHECK_EQ_I64(t[i], 0);
    }

    CHECK(!bt_from_i64(K3_WORD_MIN - 1, t, K3_TRITS));
    for (size_t i = 0; i < K3_TRITS; ++i) {
        CHECK_EQ_I64(t[i], 0);
    }

    for (size_t i = 0; i < K3_TRITS; ++i) {
        all_plus[i] = 1;
        all_minus[i] = -1;
    }
    check_value(all_plus, K3_TRITS, K3_WORD_MAX, "all +1/9");
    check_value(all_minus, K3_TRITS, K3_WORD_MIN, "all -1/9");

    bt_word w;
    bt_word_zero(&w);
    int64_t word_value = 7;
    CHECK(bt_word_to_i64(&w, &word_value));
    CHECK_EQ_I64(word_value, 0);

    CHECK(bt_word_from_i64(K3_WORD_MAX, &w));
    CHECK(bt_word_to_i64(&w, &word_value));
    CHECK_EQ_I64(word_value, K3_WORD_MAX);

    CHECK(!bt_word_from_i64(K3_WORD_MAX + 1, &w));
    CHECK(bt_word_to_i64(&w, &word_value));
    CHECK_EQ_I64(word_value, 0);
}

static void test_ranges(void)
{
    trit_t buf[64];
    int64_t back = 123;

    CHECK(bt_from_i64(0, NULL, 0));
    CHECK(!bt_from_i64(1, NULL, 0));
    CHECK(!bt_from_i64(-1, NULL, 0));

    CHECK(bt_to_i64(NULL, 0, &back));
    CHECK_EQ_I64(back, 0);

    CHECK(bt_from_i64(INT64_MAX, buf, 64));
    CHECK(bt_to_i64(buf, 64, &back));
    CHECK_EQ_I64(back, INT64_MAX);

    CHECK(bt_from_i64(INT64_MIN, buf, 64));
    CHECK(bt_to_i64(buf, 64, &back));
    CHECK_EQ_I64(back, INT64_MIN);

    for (size_t i = 0; i < 40; ++i) {
        buf[i] = 1;
    }
    CHECK(bt_to_i64(buf, 40, &back));
    CHECK_EQ_I64(back, INT64_C(6078832729528464400));

    buf[40] = 1;
    back = 123;
    CHECK(!bt_to_i64(buf, 41, &back));
    CHECK_EQ_I64(back, 0);

    for (size_t i = 0; i < 40; ++i) {
        buf[i] = -1;
    }
    CHECK(bt_to_i64(buf, 40, &back));
    CHECK_EQ_I64(back, INT64_C(-6078832729528464400));

    buf[40] = -1;
    back = 123;
    CHECK(!bt_to_i64(buf, 41, &back));
    CHECK_EQ_I64(back, 0);

    {
        static const int64_t wide_values[] = {
            INT64_MIN, INT64_MIN + 1, INT64_MIN + 2, INT64_MIN + 3,
            INT64_C(-9223372036854775800), INT64_C(-1234567890123456789),
            INT64_C(1234567890123456789), INT64_MAX - 3, INT64_MAX - 2,
            INT64_MAX - 1, INT64_MAX
        };

        for (size_t i = 0;
             i < sizeof wide_values / sizeof wide_values[0]; ++i) {
            CHECK(bt_from_i64(wide_values[i], buf, 64));
            CHECK(bt_to_i64(buf, 64, &back));
            CHECK_EQ_I64(back, wide_values[i]);
        }
    }

    for (size_t i = 0; i < 64; ++i) {
        buf[i] = 1;
    }
    back = 123;
    CHECK(!bt_to_i64(buf, 64, &back));
    CHECK_EQ_I64(back, 0);

    for (size_t i = 0; i < 64; ++i) {
        buf[i] = -1;
    }
    back = 123;
    CHECK(!bt_to_i64(buf, 64, &back));
    CHECK_EQ_I64(back, 0);

    buf[0] = 2;
    back = 123;
    CHECK(!bt_to_i64(buf, 1, &back));
    CHECK_EQ_I64(back, 0);
    CHECK(!bt_valid(buf, 1));

    buf[0] = 1;
    CHECK(bt_valid(buf, 1));

    trit_t z = 0;
    trit_t carry = 7;
    CHECK_EQ_I64(bt_add(&z, &z, 0, &z, &carry), BT_OK);
    CHECK_EQ_I64(carry, 0);
    CHECK_EQ_I64(bt_sub(&z, &z, 0, &z, &carry), BT_OK);
    CHECK_EQ_I64(carry, 0);
    CHECK_EQ_I64(bt_mul(&z, &z, 0, &z, &z), BT_OK);

    trit_t one[4] = {1, 0, 0, 0};
    trit_t out[4];
    CHECK_EQ_I64(bt_shl(one, 4, SIZE_MAX, out), BT_OVERFLOW);
    for (size_t i = 0; i < 4; ++i) {
        CHECK_EQ_I64(out[i], 0);
    }

    bt_shr(one, 4, SIZE_MAX, out);
    for (size_t i = 0; i < 4; ++i) {
        CHECK_EQ_I64(out[i], 0);
    }

    trit_t zero4[4] = {0, 0, 0, 0};
    CHECK_EQ_I64(bt_shl(zero4, 4, 100, out), BT_OK);
    for (size_t i = 0; i < 4; ++i) {
        CHECK_EQ_I64(out[i], 0);
    }
}

static void test_exhaustive_4(void)
{
    enum { VALUE_COUNT = 81, N = 4 };
    trit_t words[VALUE_COUNT][N];

    for (int i = 0; i < VALUE_COUNT; ++i) {
        CHECK(bt_from_i64(i - 40, words[i], N));
    }

    for (int ai = 0; ai < VALUE_COUNT; ++ai) {
        for (int bi = 0; bi < VALUE_COUNT; ++bi) {
            verify_basic_ops(N, 81, words[ai], ai - 40, words[bi], bi - 40);
        }
    }

    for (int ai = 0; ai < VALUE_COUNT; ++ai) {
        for (size_t amount = 0; amount <= 5; ++amount) {
            verify_shift(N, words[ai], ai - 40, amount);
        }
    }

    /* Documented aliasing support. */
    trit_t a[N];
    trit_t b[N];
    trit_t carry = 0;
    CHECK(bt_from_i64(17, a, N));
    CHECK(bt_from_i64(-5, b, N));

    CHECK_EQ_I64(bt_add(a, b, N, a, &carry), BT_OK);
    CHECK_EQ_I64(carry, 0);
    check_value(a, N, 12, "aliased add");

    CHECK_EQ_I64(bt_sub(a, b, N, a, &carry), BT_OK);
    CHECK_EQ_I64(carry, 0);
    check_value(a, N, 17, "aliased sub");

    bt_neg(a, N, a);
    check_value(a, N, -17, "aliased neg");
}

static const int64_t edge_values[] = {
    0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5,
    13, -13, 14, -14, 40, -40, 41, -41,
    80, -80, 81, -81, 121, -121, 122, -122,
    242, -242, 243, -243,
    1093, -1093, 1094, -1094,
    3280, -3280, 3281, -3281,
    6560, -6560, 6561, -6561,
    9840, -9840, 9841, -9841
};

static void test_edges_9(void)
{
    enum { N = K3_TRITS };
    const size_t count = sizeof edge_values / sizeof edge_values[0];
    trit_t words[sizeof edge_values / sizeof edge_values[0]][N];

    for (size_t i = 0; i < count; ++i) {
        CHECK(bt_from_i64(edge_values[i], words[i], N));
        check_value(words[i], N, edge_values[i], "edge conversion");
    }

    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < count; ++j) {
            verify_basic_ops(N, 19683, words[i], edge_values[i],
                             words[j], edge_values[j]);
        }
    }

    for (size_t i = 0; i < count; ++i) {
        for (size_t amount = 0; amount <= 10; ++amount) {
            verify_shift(N, words[i], edge_values[i], amount);
        }
    }
}

static uint64_t rng_state = UINT64_C(0x9e3779b97f4a7c15);

static uint64_t rng_next(void)
{
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static int64_t random_word_value(void)
{
    return (int64_t)(rng_next() % UINT64_C(19683)) - 9841;
}

static void test_random_9(void)
{
    enum { N = K3_TRITS, ITERATIONS = 50000 };
    trit_t a[N];
    trit_t b[N];

    for (int iteration = 0; iteration < ITERATIONS; ++iteration) {
        int64_t av = random_word_value();
        int64_t bv = random_word_value();

        CHECK(bt_from_i64(av, a, N));
        CHECK(bt_from_i64(bv, b, N));

        verify_basic_ops(N, 19683, a, av, b, bv);

        size_t amount = (size_t)(rng_next() % 14);
        verify_shift(N, a, av, amount);
        verify_shift(N, b, bv, amount);
    }
}

int main(void)
{
    test_conversion_4();
    test_conversion_9();
    test_ranges();
    test_exhaustive_4();
    test_edges_9();
    test_random_9();

    printf("test_ternary: %lu checks, %lu failures\n", checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
