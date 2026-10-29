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

#include "ternary.h"

/*
 * Read up to `capacity` words from `path` into `words`.
 *
 * Returns true on success, with the number of words stored in *count.  On
 * failure, prints a message to stderr and returns false.  The destination is
 * unmodified when the file cannot be parsed; *count is set to 0.
 */
bool k3_image_read(const char *path, bt_word *words, size_t capacity,
                   size_t *count);

/* Write `count` words, one per line, in decimal. */
bool k3_image_write(const char *path, const bt_word *words, size_t count);

#endif /* K3_IMAGE_H */
