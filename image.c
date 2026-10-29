#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    f = fopen(path, "r");
    if (f == NULL) {
        perror(path);
        return false;
    }

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
