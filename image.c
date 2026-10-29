#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "packed.h"

bool k3_image_is_binary(const char *path)
{
    if (path == NULL) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char magic[4];
    size_t read_bytes = fread(magic, 1, 4, f);
    fclose(f);
    return (read_bytes == 4 && memcmp(magic, K3B_MAGIC, 4) == 0);
}

bool k3_image_write_binary(const char *path, const bt_word *words, size_t count, uint16_t entry_point)
{
    if (path == NULL || (words == NULL && count != 0)) {
        return false;
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return false;
    }

    size_t buf_cap = (count / 4) * 9 + (count % 4) * 3 + 16;
    uint8_t *payload = NULL;
    size_t payload_bytes = 0;
    if (count > 0) {
        payload = (uint8_t *)calloc(buf_cap, 1);
        if (payload == NULL) {
            fclose(f);
            return false;
        }
        payload_bytes = bt_pack_stream(words, count, payload, buf_cap);
    }

    k3b_header header;
    memcpy(header.magic, K3B_MAGIC, 4);
    header.version = K3B_VERSION;
    header.entry_point = entry_point;
    header.word_count = (uint32_t)count;
    header.payload_bytes = (uint32_t)payload_bytes;

    if (fwrite(&header, 1, sizeof(header), f) != sizeof(header)) {
        perror(path);
        free(payload);
        fclose(f);
        return false;
    }

    if (payload_bytes > 0) {
        if (fwrite(payload, 1, payload_bytes, f) != payload_bytes) {
            perror(path);
            free(payload);
            fclose(f);
            return false;
        }
        free(payload);
    }

    fclose(f);
    return true;
}

bool k3_image_read(const char *path, bt_word *words, size_t capacity,
                   size_t *count)
{
    FILE *f;
    char line[512];
    size_t n = 0;

    if (count == NULL || (words == NULL && capacity != 0)) {
        return false;
    }
    *count = 0;

    f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        return false;
    }

    /* Check for .k3b binary format */
    char magic[4];
    if (fread(magic, 1, 4, f) == 4 && memcmp(magic, K3B_MAGIC, 4) == 0) {
        k3b_header header;
        memcpy(header.magic, magic, 4);
        if (fread(((char *)&header) + 4, 1, sizeof(header) - 4, f) != sizeof(header) - 4) {
            fprintf(stderr, "%s: truncated binary header\n", path);
            fclose(f);
            return false;
        }
        if (header.version != K3B_VERSION) {
            fprintf(stderr, "%s: unsupported k3b version %u\n", path, header.version);
            fclose(f);
            return false;
        }
        if (header.word_count > capacity) {
            fprintf(stderr, "%s: binary image (%u words) exceeds capacity (%zu words)\n",
                    path, header.word_count, capacity);
            fclose(f);
            return false;
        }

        if (header.payload_bytes > 0) {
            uint8_t *payload = (uint8_t *)malloc(header.payload_bytes);
            if (payload == NULL) {
                fclose(f);
                return false;
            }
            if (fread(payload, 1, header.payload_bytes, f) != header.payload_bytes) {
                fprintf(stderr, "%s: truncated binary payload\n", path);
                free(payload);
                fclose(f);
                return false;
            }
            size_t unpacked = bt_unpack_stream(payload, header.payload_bytes, words, capacity);
            free(payload);
            fclose(f);
            if (unpacked < header.word_count) {
                fprintf(stderr, "%s: failed to unpack all words (%zu of %u)\n", path, unpacked, header.word_count);
                return false;
            }
        } else {
            fclose(f);
        }
        *count = header.word_count;
        return true;
    }

    /* Not binary: rewind and read as text .k3 image */
    rewind(f);

    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        char *p = line;
        char *tok;

        if (len == sizeof(line) - 1 && line[len - 1] != '\n' && !feof(f)) {
            fprintf(stderr, "%s: line too long\n", path);
            fclose(f);
            return false;
        }

        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p == '\0' || *p == '#' || *p == ';') {
            continue;
        }
        for (size_t i = 0; p[i] != '\0'; ++i) {
            if (p[i] == ';' || p[i] == '#' ||
                (p[i] == '/' && p[i + 1] == '/')) {
                p[i] = '\0';
                break;
            }
        }

        tok = strtok(p, " \t\r\n,");
        while (tok != NULL) {
            char *end = NULL;
            long long value = strtoll(tok, &end, 10);
            bt_word word;

            if (end == tok || *end != '\0') {
                fprintf(stderr, "%s: invalid word '%s'\n", path, tok);
                fclose(f);
                return false;
            }
            if (!bt_word_from_i64((int64_t)value, &word)) {
                fprintf(stderr, "%s: %lld does not fit in 9 trits\n",
                        path, value);
                fclose(f);
                return false;
            }
            if (n >= capacity) {
                fprintf(stderr, "%s: more than %zu words\n", path, capacity);
                fclose(f);
                return false;
            }
            words[n++] = word;
            tok = strtok(NULL, " \t\r\n,");
        }
    }

    if (ferror(f)) {
        fprintf(stderr, "%s: read error\n", path);
        fclose(f);
        return false;
    }
    fclose(f);

    *count = n;
    return true;
}

bool k3_image_write(const char *path, const bt_word *words, size_t count)
{
    FILE *f = fopen(path, "w");

    if (f == NULL) {
        perror(path);
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        int64_t value = 0;

        (void)bt_word_to_i64(&words[i], &value);
        if (fprintf(f, "%lld\n", (long long)value) < 0) {
            fprintf(stderr, "%s: write error\n", path);
            fclose(f);
            return false;
        }
    }
    if (fclose(f) != 0) {
        perror(path);
        return false;
    }
    return true;
}
