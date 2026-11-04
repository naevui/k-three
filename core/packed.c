#include "packed.h"

uint8_t bt_pack_trit(trit_t t)
{
    switch (t) {
    case 0:
        return K3_PACKED_ZERO;
    case 1:
        return K3_PACKED_POS;
    case -1:
        return K3_PACKED_NEG;
    default:
        return K3_PACKED_INV;
    }
}

bool bt_unpack_trit(uint8_t bits, trit_t *out)
{
    switch (bits & K3_PACKED_TRIT_MASK) {
    case K3_PACKED_ZERO:
        if (out) *out = 0;
        return true;
    case K3_PACKED_POS:
        if (out) *out = 1;
        return true;
    case K3_PACKED_NEG:
        if (out) *out = -1;
        return true;
    default:
        return false;
    }
}

bool bt_packed_word_valid(uint32_t packed)
{
    if ((packed & ~K3_PACKED_WORD_MASK) != 0) {
        return false;
    }
    for (size_t i = 0; i < K3_TRITS; ++i) {
        uint8_t pair = (uint8_t)((packed >> (i * 2)) & K3_PACKED_TRIT_MASK);
        if (pair == K3_PACKED_INV) {
            return false;
        }
    }
    return true;
}

bool bt_word_pack(const bt_word *w, uint32_t *out)
{
    if (w == NULL || out == NULL || !bt_valid(w->t, K3_TRITS)) {
        return false;
    }
    uint32_t packed = 0;
    for (size_t i = 0; i < K3_TRITS; ++i) {
        uint8_t pair = bt_pack_trit(w->t[i]);
        packed |= ((uint32_t)pair) << (i * 2);
    }
    *out = packed;
    return true;
}

bool bt_word_unpack(uint32_t packed, bt_word *out)
{
    if (out == NULL || !bt_packed_word_valid(packed)) {
        return false;
    }
    for (size_t i = 0; i < K3_TRITS; ++i) {
        uint8_t pair = (uint8_t)((packed >> (i * 2)) & K3_PACKED_TRIT_MASK);
        (void)bt_unpack_trit(pair, &out->t[i]);
    }
    return true;
}

uint32_t bt_packed_neg(uint32_t packed)
{
    /* Bit 0 is POS flag, bit 1 is NEG flag (for each pair).
     * If pair is 01 (+1), swapping bits makes 10 (-1).
     * If pair is 10 (-1), swapping bits makes 01 (+1).
     * If pair is 00 (0), swapping bits makes 00 (0).
     * If pair is 11 (invalid), swapping bits makes 11.
     * So fast negation of all 9 pairs in parallel is:
     *   low bits moved to high: ((packed & 0x15555) << 1)
     *   high bits moved to low: ((packed & 0x2AAAA) >> 1)
     */
    uint32_t odd = (packed & 0x15555u) << 1;
    uint32_t even = (packed & 0x2AAAAu) >> 1;
    return (odd | even) & K3_PACKED_WORD_MASK;
}

size_t bt_pack_stream(const bt_word *words, size_t count, uint8_t *buf, size_t buf_cap)
{
    if (words == NULL || buf == NULL) {
        return 0;
    }

    size_t bytes_written = 0;
    size_t i = 0;

    /* Process groups of 4 words (72 bits = 9 bytes) */
    while (i + 4 <= count && bytes_written + 9 <= buf_cap) {
        uint32_t w0 = 0, w1 = 0, w2 = 0, w3 = 0;
        if (!bt_word_pack(&words[i], &w0) ||
            !bt_word_pack(&words[i + 1], &w1) ||
            !bt_word_pack(&words[i + 2], &w2) ||
            !bt_word_pack(&words[i + 3], &w3)) {
            return bytes_written;
        }

        uint64_t low64 = (uint64_t)w0 | ((uint64_t)w1 << 18) |
                         ((uint64_t)w2 << 36) | (((uint64_t)w3 & 0x3FF) << 54);
        uint8_t byte8 = (uint8_t)((w3 >> 10) & 0xFF);

        for (int b = 0; b < 8; ++b) {
            buf[bytes_written++] = (uint8_t)((low64 >> (b * 8)) & 0xFF);
        }
        buf[bytes_written++] = byte8;
        i += 4;
    }

    /* Trailing 1..3 words */
    while (i < count && bytes_written + 3 <= buf_cap) {
        uint32_t w = 0;
        if (!bt_word_pack(&words[i], &w)) {
            return bytes_written;
        }
        /* Write 18 bits as 3 bytes (leaving top 6 bits 0) */
        buf[bytes_written++] = (uint8_t)(w & 0xFF);
        buf[bytes_written++] = (uint8_t)((w >> 8) & 0xFF);
        buf[bytes_written++] = (uint8_t)((w >> 16) & 0x03);
        i++;
    }

    return bytes_written;
}

size_t bt_unpack_stream(const uint8_t *buf, size_t byte_count, bt_word *words, size_t words_cap)
{
    if (buf == NULL || words == NULL) {
        return 0;
    }

    size_t words_read = 0;
    size_t b = 0;

    while (b + 9 <= byte_count && words_read + 4 <= words_cap) {
        uint64_t low64 = 0;
        for (int j = 0; j < 8; ++j) {
            low64 |= ((uint64_t)buf[b + j]) << (j * 8);
        }
        uint32_t byte8 = buf[b + 8];

        uint32_t w0 = (uint32_t)(low64 & 0x3FFFF);
        uint32_t w1 = (uint32_t)((low64 >> 18) & 0x3FFFF);
        uint32_t w2 = (uint32_t)((low64 >> 36) & 0x3FFFF);
        uint32_t w3 = (uint32_t)(((low64 >> 54) & 0x3FF) | (byte8 << 10));

        if (!bt_word_unpack(w0, &words[words_read++]) ||
            !bt_word_unpack(w1, &words[words_read++]) ||
            !bt_word_unpack(w2, &words[words_read++]) ||
            !bt_word_unpack(w3, &words[words_read++])) {
            return words_read;
        }
        b += 9;
    }

    while (b + 3 <= byte_count && words_read < words_cap) {
        uint32_t w = (uint32_t)buf[b] |
                     ((uint32_t)buf[b + 1] << 8) |
                     ((uint32_t)(buf[b + 2] & 0x03) << 16);
        if (!bt_word_unpack(w, &words[words_read++])) {
            return words_read;
        }
        b += 3;
    }

    return words_read;
}
