/* base64.h - Internal base64 encoder for boba.
 *
 * Used by ansi_format_osc52() to wrap clipboard text. Not installed; not part
 * of the public API. */

#ifndef BOBA_INTERNAL_BASE64_H
#define BOBA_INTERNAL_BASE64_H

#include <stddef.h>

/* Required output buffer size to encode `in_len` bytes (no terminating null). */
#define BOBA_BASE64_ENCODED_LEN(in_len) (4 * (((in_len) + 2) / 3))

/* Encode `in_len` bytes from `in` into `out`. Writes
 * BOBA_BASE64_ENCODED_LEN(in_len) bytes; does not null-terminate.
 * `out_size` must be at least that many bytes.
 * Returns the number of bytes written, or 0 on failure (NULL args, buffer
 * too small). */
size_t boba_base64_encode(const unsigned char *in, size_t in_len,
                          char *out, size_t out_size);

#endif /* BOBA_INTERNAL_BASE64_H */
