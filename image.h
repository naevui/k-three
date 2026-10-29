/*
 * Host-side word-image I/O for the K-Three tools.
 *
 * The image format is a debugging convenience, not part of the ISA: signed
 * decimal 9-trit words separated by whitespace or commas.  ';', '#' and '//'
 * start comments.  This module uses stdio and is therefore not part of the VM
 * core.
 */

#ifndef K3_IMAGE_H
#define K3_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ternary.h"

#define K3B_MAGIC "\x7FK3B"
#define K3B_VERSION 1

/* Binary executable file header for .k3b files */
typedef struct k3b_header {
    char magic[4];          /* "\x7FK3B" */
    uint16_t version;       /* 1 */
    uint16_t entry_point;   /* Initial PC (default 0) */
    uint32_t word_count;    /* Number of 9-trit words */
    uint32_t payload_bytes; /* Byte size of packed stream */
} k3b_header;

/*
 * Read up to `capacity` words from `path` into `words`.
 * Automatically detects whether the file is binary (.k3b) or text (.k3).
 */
bool k3_image_read(const char *path, bt_word *words, size_t capacity,
                   size_t *count);

/* Write `count` words, one per line, in decimal text format. */
bool k3_image_write(const char *path, const bt_word *words, size_t count);

/* Write `count` words in compact binary (.k3b) format using 2-bit/trit stream. */
bool k3_image_write_binary(const char *path, const bt_word *words, size_t count, uint16_t entry_point);

/* Check if a file starts with the K3B binary magic header. */
bool k3_image_is_binary(const char *path);

#endif /* K3_IMAGE_H */
