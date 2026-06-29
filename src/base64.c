/* base64.c - Internal base64 encoder for boba.
 *
 * Standard RFC 4648 base64 (alphabet A-Z a-z 0-9 + /), with '=' padding. */

#include "base64.h"

static const char b64_alphabet[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t boba_base64_encode(const unsigned char *in, size_t in_len,
                                char *out, size_t out_size)
{
    if (!out)
        return 0;
    if (!in && in_len > 0)
        return 0;

    size_t needed = BOBA_BASE64_ENCODED_LEN(in_len);
    if (out_size < needed)
        return 0;

    size_t i = 0;
    size_t o = 0;
    while (i + 3 <= in_len) {
        unsigned int v = ((unsigned int)in[i] << 16) |
                         ((unsigned int)in[i + 1] << 8) | (unsigned int)in[i + 2];
        out[o++] = b64_alphabet[(v >> 18) & 0x3F];
        out[o++] = b64_alphabet[(v >> 12) & 0x3F];
        out[o++] = b64_alphabet[(v >> 6) & 0x3F];
        out[o++] = b64_alphabet[v & 0x3F];
        i += 3;
    }

    size_t rem = in_len - i;
    if (rem == 1) {
        unsigned int v = (unsigned int)in[i] << 16;
        out[o++] = b64_alphabet[(v >> 18) & 0x3F];
        out[o++] = b64_alphabet[(v >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        unsigned int v = ((unsigned int)in[i] << 16) | ((unsigned int)in[i + 1] << 8);
        out[o++] = b64_alphabet[(v >> 18) & 0x3F];
        out[o++] = b64_alphabet[(v >> 12) & 0x3F];
        out[o++] = b64_alphabet[(v >> 6) & 0x3F];
        out[o++] = '=';
    }

    return o;
}
