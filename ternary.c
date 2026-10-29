#include "ternary.h"

#include <assert.h>
#include <limits.h>

bool bt_valid(const trit_t *t, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (t[i] < -1 || t[i] > 1) {
            return false;
        }
    }
    return true;
}

void bt_zero(trit_t *t, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        t[i] = 0;
    }
}

bool bt_to_i64(const trit_t *t, size_t n, int64_t *out)
{
    const int64_t max3 = INT64_MAX / 3;
    const int64_t min3 = INT64_MIN / 3;
    int64_t acc = 0;

    for (size_t i = n; i > 0; --i) {
        int d = t[i - 1];

        if (d < -1 || d > 1) {
            *out = 0;
            return false;
        }

        if (acc > max3 || acc < min3 - 1) {
            *out = 0;
            return false;
        }

        if (acc == min3 - 1) {
            /*
             * 3*acc would be INT64_MIN - 1.  Only d == +1 brings the
             * result back to INT64_MIN; any smaller d remains out of range.
             */
            if (d != 1) {
                *out = 0;
                return false;
            }
            acc = INT64_MIN;
        } else {
            acc *= 3;
            if ((d > 0 && acc > INT64_MAX - d) ||
                (d < 0 && acc < INT64_MIN - d)) {
                *out = 0;
                return false;
            }
            acc += d;
        }
    }

    *out = acc;
    return true;
}

bool bt_from_i64(int64_t value, trit_t *out, size_t n)
{
    int64_t v = value;

    for (size_t i = 0; i < n; ++i) {
        int64_t q = v / 3;
        int r = (int)(v - q * 3); /* -2 .. +2, C truncating division */

        if (r == 2) {
            r = -1;
            q += 1;
        } else if (r == -2) {
            r = 1;
            q -= 1;
        }

        out[i] = (trit_t)r;
        v = q;
    }

    if (v != 0) {
        bt_zero(out, n);
        return false;
    }
    return true;
}

int bt_cmp(const trit_t *a, const trit_t *b, size_t n)
{
    for (size_t i = n; i > 0; --i) {
        if (a[i - 1] < b[i - 1]) {
            return -1;
        }
        if (a[i - 1] > b[i - 1]) {
            return 1;
        }
    }
    return 0;
}

/*
 * Add a signed digit to a 2n-trit balanced-ternary accumulator split into two
 * n-trit halves, propagating balanced carries.  The exact product of two
 * n-trit values fits in 2n trits, so the carry chain cannot leave the buffer
 * when this is used for multiplication.
 */
static trit_t mul_get(const trit_t *low, const trit_t *high, size_t n, size_t pos)
{
    return (pos < n) ? low[pos] : high[pos - n];
}

static void mul_set(trit_t *low, trit_t *high, size_t n, size_t pos,
                    trit_t value)
{
    if (pos < n) {
        low[pos] = value;
    } else {
        high[pos - n] = value;
    }
}

static void mul_add_at(trit_t *low, trit_t *high, size_t n,
                       size_t pos, int value)
{
    while (value != 0) {
        assert(pos < 2 * n);

        int sum = (int)mul_get(low, high, n, pos) + value;
        value = 0;

        if (sum > 1) {
            sum -= 3;
            value = 1;
        } else if (sum < -1) {
            sum += 3;
            value = -1;
        }

        mul_set(low, high, n, pos, (trit_t)sum);
        ++pos;
    }
}

bt_status bt_add(const trit_t *a, const trit_t *b, size_t n,
                 trit_t *out, trit_t *carry)
{
    int c = 0;

    for (size_t i = 0; i < n; ++i) {
        int sum = (int)a[i] + (int)b[i] + c;

        if (sum > 1) {
            sum -= 3;
            c = 1;
        } else if (sum < -1) {
            sum += 3;
            c = -1;
        } else {
            c = 0;
        }

        out[i] = (trit_t)sum;
    }

    if (carry != NULL) {
        *carry = (trit_t)c;
    }
    return (c == 0) ? BT_OK : BT_OVERFLOW;
}

bt_status bt_sub(const trit_t *a, const trit_t *b, size_t n,
                 trit_t *out, trit_t *carry)
{
    int c = 0;

    for (size_t i = 0; i < n; ++i) {
        int diff = (int)a[i] - (int)b[i] + c;

        if (diff > 1) {
            diff -= 3;
            c = 1;
        } else if (diff < -1) {
            diff += 3;
            c = -1;
        } else {
            c = 0;
        }

        out[i] = (trit_t)diff;
    }

    if (carry != NULL) {
        *carry = (trit_t)c;
    }
    return (c == 0) ? BT_OK : BT_OVERFLOW;
}

void bt_neg(const trit_t *a, size_t n, trit_t *out)
{
    for (size_t i = 0; i < n; ++i) {
        out[i] = (trit_t)(-a[i]);
    }
}

bt_status bt_mul(const trit_t *a, const trit_t *b, size_t n,
                 trit_t *out, trit_t *high)
{
    bt_zero(out, n);
    bt_zero(high, n);

    for (size_t i = 0; i < n; ++i) {
        if (a[i] == 0) {
            continue;
        }
        for (size_t j = 0; j < n; ++j) {
            int product = (int)a[i] * (int)b[j];
            if (product != 0) {
                mul_add_at(out, high, n, i + j, product);
            }
        }
    }

    for (size_t i = 0; i < n; ++i) {
        if (high[i] != 0) {
            return BT_OVERFLOW;
        }
    }
    return BT_OK;
}

bt_status bt_shl(const trit_t *a, size_t n, size_t amount, trit_t *out)
{
    bool overflow = false;

    if (amount >= n) {
        bt_zero(out, n);
        for (size_t i = 0; i < n; ++i) {
            if (a[i] != 0) {
                overflow = true;
                break;
            }
        }
        return overflow ? BT_OVERFLOW : BT_OK;
    }

    for (size_t i = 0; i < n; ++i) {
        out[i] = (i < amount) ? 0 : a[i - amount];
    }

    for (size_t i = n - amount; i < n; ++i) {
        if (a[i] != 0) {
            overflow = true;
            break;
        }
    }

    return overflow ? BT_OVERFLOW : BT_OK;
}

void bt_shr(const trit_t *a, size_t n, size_t amount, trit_t *out)
{
    if (amount >= n) {
        bt_zero(out, n);
        return;
    }

    for (size_t i = 0; i < n - amount; ++i) {
        out[i] = a[i + amount];
    }
    for (size_t i = n - amount; i < n; ++i) {
        out[i] = 0;
    }
}

void bt_word_zero(bt_word *w)
{
    bt_zero(w->t, K3_TRITS);
}

bool bt_word_from_i64(int64_t value, bt_word *w)
{
    return bt_from_i64(value, w->t, K3_TRITS);
}

bool bt_word_to_i64(const bt_word *w, int64_t *out)
{
    return bt_to_i64(w->t, K3_TRITS, out);
}
