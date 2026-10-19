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

/* Return byte offset of the Nth codepoint (0-indexed).
 * If cp_index >= total codepoints, returns text_len. */
size_t tui_utf8_byte_offset(const char *text, size_t text_len, int cp_index);

/* Return codepoint index for a given byte position. */
int tui_utf8_cp_index(const char *text, size_t byte_pos);

/* Return terminal display width of a Unicode codepoint.
 * Handles zero-width, wide (CJK), and emoji characters. */
int tui_codepoint_width(uint32_t cp);

/* How a terminal resolves the cell width of a grapheme cluster.
 *
 * Width is a property of the output terminal, not of Unicode alone: the
 * same string occupies different columns on different terminals. The
 * model names which rule is in effect, and is chosen once at startup
 * (runtime.h) and then held fixed for the life of the runtime.
 *
 * TUI_WIDTH_MODEL_GRID is the terminal-grid rule shared by xterm and
 * Windows Terminal: a variation selector chooses a glyph, not a cell
 * count, so VS15 never narrows a wide base. VS16 still widens a narrow
 * base, which is how emoji presentation is rendered in practice.
 *
 * TUI_WIDTH_MODEL_WCWIDTH is strict wcwidth: neither selector changes
 * the width of the base.
 *
 * TUI_WIDTH_MODEL_UTS51 is the text-presentation rule used by portty
 * (coffer's engine) and kitty: VS15 takes one cell where UTS #51 defines
 * a text glyph for the base, VS16 forces two cells.
 *
 * TUI_WIDTH_MODEL_AUTO is only a configuration value. Measurement
 * functions treat it as "use the ambient model". */
typedef enum TuiWidthModel
{
    TUI_WIDTH_MODEL_AUTO = 0,
    TUI_WIDTH_MODEL_GRID,
    TUI_WIDTH_MODEL_WCWIDTH,
    TUI_WIDTH_MODEL_UTS51,
} TuiWidthModel;

/* The process-wide model used by the zero-suffix entry points below.
 * It is installed by tui_runtime_create() before the component's init()
 * runs, and restored when the runtime is freed. Standalone callers get
 * TUI_WIDTH_MODEL_GRID until they set it. */
TuiWidthModel tui_width_model(void);

/* Set the ambient model. Returns 1 on success, 0 when refused: AUTO is
 * not a measurement model, and a live runtime pins the value (the model
 * decides committed scrollback geometry, so it must not change after
 * the first byte is emitted). */
int tui_width_model_set(TuiWidthModel model);

/* Display width of one grapheme cluster given as decoded codepoints:
 * VS16 forces emoji presentation (two cells), a regional indicator pair
 * is one flag, combining marks attach without widening. The zero-suffix
 * form measures with the ambient model; the _model form is explicit. */
int tui_cluster_width(const uint32_t *cps, uint32_t len);
int tui_cluster_width_model(const uint32_t *cps, uint32_t len,
                            TuiWidthModel model);

/* Display width of the next grapheme cluster in utf8[0..len). Advances
 * *bytes past the cluster; *bytes is 0 at end of input or when the next
 * character cannot be decoded. This is the column-safe unit: a caller
 * that emits or wraps by *bytes never splits a base from its modifiers. */
int tui_next_cluster(const char *utf8, size_t len, size_t *bytes);
int tui_next_cluster_model(const char *utf8, size_t len, size_t *bytes,
                           TuiWidthModel model);

/* Calculate display width (terminal columns) of a UTF-8 byte range.
 * Cluster-aware. Does not skip ANSI escape sequences. */
int tui_utf8_display_width_n(const char *text, size_t len);
int tui_utf8_display_width_n_model(const char *text, size_t len,
                                   TuiWidthModel model);

/* Calculate display width (terminal columns) of a NUL-terminated UTF-8 string.
 * Cluster-aware. Does not skip ANSI escape sequences. */
int tui_utf8_display_width(const char *str);
int tui_utf8_display_width_model(const char *str, TuiWidthModel model);

/* Calculate display width of a UTF-8 string, skipping ANSI escape sequences. */
size_t tui_utf8_display_width_ansi(const char *text, size_t len);
size_t tui_utf8_display_width_ansi_model(const char *text, size_t len,
                                         TuiWidthModel model);

#endif /* BOBA_UNICODE_H */
