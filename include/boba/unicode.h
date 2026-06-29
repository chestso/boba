/* unicode.h - UTF-8 and Unicode utility functions
 *
 * Pure utility functions for UTF-8 encoding/decoding and Unicode
 * character width calculation. No boba type dependencies.
 */

#ifndef BOBA_UNICODE_H
#define BOBA_UNICODE_H

#include <stddef.h>
#include <stdint.h>

/* Get byte length of UTF-8 character from lead byte.
 * Returns 1-4 for valid lead bytes, 1 for invalid. */
int tui_utf8_char_len(const char *ptr);

/* Decode a UTF-8 sequence of known length to a Unicode codepoint. */
uint32_t tui_utf8_decode(const char *ptr, int len);

/* Encode a Unicode codepoint to UTF-8 bytes.
 * Writes up to 4 bytes into buf (plus NUL terminator).
 * Returns number of bytes written. */
int tui_utf8_encode(uint32_t cp, char buf[5]);

/* Find previous UTF-8 character boundary before pos.
 * Returns byte offset of the previous character start. */
size_t tui_utf8_prev_char(const char *text, size_t pos);

/* Count total codepoints in a UTF-8 string of len bytes. */
int tui_utf8_codepoint_count(const char *text, size_t len);

/* Return byte offset of the Nth codepoint (0-indexed).
 * If cp_index >= total codepoints, returns text_len. */
size_t tui_utf8_byte_offset(const char *text, size_t text_len, int cp_index);

/* Return codepoint index for a given byte position. */
int tui_utf8_cp_index(const char *text, size_t byte_pos);

/* Return terminal display width of a Unicode codepoint.
 * Handles zero-width, wide (CJK), and emoji characters. */
int tui_codepoint_width(uint32_t cp);

/* Calculate display width (terminal columns) of a NUL-terminated UTF-8 string.
 * Does not skip ANSI escape sequences. */
int tui_utf8_display_width(const char *str);

/* Calculate display width of a UTF-8 string, skipping ANSI escape sequences. */
size_t tui_utf8_display_width_ansi(const char *text, size_t len);

#endif /* BOBA_UNICODE_H */
