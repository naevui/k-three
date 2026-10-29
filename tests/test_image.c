/*
 * Text and compact binary (.k3b) image round-trip tests for image.c.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "image.h"
#include "ternary.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(expr) do { \
    g_checks++; \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while (0)

static const char *TEXT_PATH = "/tmp/k3_test_image.k3";
static const char *BIN_PATH = "/tmp/k3_test_image.k3b";
static const char *BAD_PATH = "/tmp/k3_test_image_bad.k3";
static const char *EMPTY_PATH = "/tmp/k3_test_image_empty.k3";
static const char *TRUNC_PATH = "/tmp/k3_test_image_trunc.k3b";

static void words_to_i64(const bt_word *words, size_t count, int64_t *out)
{
    for (size_t i = 0; i < count; ++i) {
        out[i] = 0;
        (void)bt_word_to_i64(&words[i], &out[i]);
    }
}

static void test_text_roundtrip(void)
{
    const int64_t values[] = { 0, 1, -1, 9841, -9841, 1234, -5678 };
    const size_t count = sizeof(values) / sizeof(values[0]);
    bt_word words[16];
    bt_word loaded[16];
    size_t read = 0;
    int64_t got[16];

    for (size_t i = 0; i < count; ++i) {
        CHECK(bt_word_from_i64(values[i], &words[i]));
    }

    CHECK(k3_image_write(TEXT_PATH, words, count));
    CHECK(!k3_image_is_binary(TEXT_PATH));
    CHECK(k3_image_read(TEXT_PATH, loaded, 16, &read));
    CHECK(read == count);
    words_to_i64(loaded, read, got);
    for (size_t i = 0; i < count; ++i) {
        CHECK(got[i] == values[i]);
    }
}

static void test_binary_roundtrip(void)
{
    const int64_t values[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23 };
    const size_t count = sizeof(values) / sizeof(values[0]);
    bt_word words[16];
    bt_word loaded[16];
    size_t read = 0;
    int64_t got[16];

    for (size_t i = 0; i < count; ++i) {
        CHECK(bt_word_from_i64(values[i], &words[i]));
    }

    CHECK(k3_image_write_binary(BIN_PATH, words, count, 0));
    CHECK(k3_image_is_binary(BIN_PATH));
    CHECK(k3_image_read(BIN_PATH, loaded, 16, &read));
    CHECK(read == count);
    words_to_i64(loaded, read, got);
    for (size_t i = 0; i < count; ++i) {
        CHECK(got[i] == values[i]);
    }

    /* A capacity smaller than the stored word count is rejected. */
    read = 99;
    CHECK(!k3_image_read(BIN_PATH, loaded, 3, &read));
    CHECK(read == 0);
}

static void test_empty_and_errors(void)
{
    bt_word loaded[4];
    size_t read = 123;

    /* Empty text image: successful read with zero words. */
    CHECK(k3_image_write(EMPTY_PATH, NULL, 0));
    CHECK(k3_image_read(EMPTY_PATH, loaded, 4, &read));
    CHECK(read == 0);

    /* Missing file: failure, count cleared. */
    read = 123;
    CHECK(!k3_image_read("/tmp/k3_test_image_missing.k3", loaded, 4, &read));
    CHECK(read == 0);

    /* Invalid decimal word. */
    {
        FILE *f = fopen(BAD_PATH, "w");
        CHECK(f != NULL);
        if (f != NULL) {
            fputs("12 nope 34\n", f);
            fclose(f);
        }
    }
    read = 123;
    CHECK(!k3_image_read(BAD_PATH, loaded, 4, &read));
    CHECK(read == 0);

    /* Out-of-range word. */
    {
        FILE *f = fopen(BAD_PATH, "w");
        CHECK(f != NULL);
        if (f != NULL) {
            fputs("99999\n", f);
            fclose(f);
        }
    }
    CHECK(!k3_image_read(BAD_PATH, loaded, 4, &read));

    /* Truncated binary header. */
    {
        FILE *f = fopen(TRUNC_PATH, "wb");
        CHECK(f != NULL);
        if (f != NULL) {
            fwrite(K3B_MAGIC, 1, 4, f);
            fclose(f);
        }
    }
    CHECK(k3_image_is_binary(TRUNC_PATH));
    read = 123;
    CHECK(!k3_image_read(TRUNC_PATH, loaded, 4, &read));
    CHECK(read == 0);

    /* Binary image with no payload. */
    CHECK(k3_image_write_binary(BIN_PATH, NULL, 0, 0));
    CHECK(k3_image_is_binary(BIN_PATH));
    CHECK(k3_image_read(BIN_PATH, loaded, 4, &read));
    CHECK(read == 0);
}

static void test_comment_parsing(void)
{
    const char *path = "/tmp/k3_test_image_comments.k3";
    bt_word loaded[8];
    size_t read = 0;
    int64_t got[8];
    FILE *f = fopen(path, "w");

    CHECK(f != NULL);
    if (f != NULL) {
        fputs("; comment line\n", f);
        fputs("7, 8  9\n", f);
        fputs("10 // trailing comment\n", f);
        fclose(f);
    }
    CHECK(k3_image_read(path, loaded, 8, &read));
    CHECK(read == 4);
    words_to_i64(loaded, read, got);
    CHECK(got[0] == 7 && got[1] == 8 && got[2] == 9 && got[3] == 10);
    remove(path);
}

int main(void)
{
    test_text_roundtrip();
    test_binary_roundtrip();
    test_empty_and_errors();
    test_comment_parsing();

    remove(TEXT_PATH);
    remove(BIN_PATH);
    remove(BAD_PATH);
    remove(EMPTY_PATH);
    remove(TRUNC_PATH);

    printf("test_image: %d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
