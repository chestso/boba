/* unicode.c - UTF-8 and Unicode utility functions */

#include <boba/unicode.h>

#include <stdbool.h>
#include <string.h>

#include "ansi_scan.h"
#include "width_internal.h"

int tui_utf8_char_len(const char *ptr)
{
    unsigned char c = (unsigned char)*ptr;
    if ((c & 0x80) == 0)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1; /* Invalid, treat as single byte */
}

uint32_t tui_utf8_decode(const char *ptr, int len)
{
    unsigned char c = (unsigned char)*ptr;
    uint32_t cp;
    switch (len) {
    case 1:
        cp = c;
        break;
    case 2:
        cp = c & 0x1F;
        break;
    case 3:
        cp = c & 0x0F;
        break;
    case 4:
        cp = c & 0x07;
        break;
    default:
        return c;
    }
    for (int i = 1; i < len; i++)
        cp = (cp << 6) | ((unsigned char)ptr[i] & 0x3F);
    return cp;
}

int tui_utf8_encode(uint32_t cp, char buf[5])
{
    int len;
    if (cp < 0x80) {
        buf[0] = (char)cp;
        len = 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        len = 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        len = 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        len = 4;
    }
    buf[len] = '\0';
    return len;
}

size_t tui_utf8_prev_char(const char *text, size_t pos)
{
    if (pos == 0)
        return 0;
    pos--;
    while (pos > 0 && ((unsigned char)text[pos] & 0xC0) == 0x80) {
        pos--;
    }
    return pos;
}

size_t tui_utf8_byte_offset(const char *text, size_t text_len, int cp_index)
{
    size_t offset = 0;
    int cp = 0;
    while (offset < text_len && cp < cp_index) {
        offset += tui_utf8_char_len(text + offset);
        cp++;
    }
    return offset;
}

int tui_utf8_cp_index(const char *text, size_t byte_pos)
{
    int cp = 0;
    size_t i = 0;
    while (i < byte_pos) {
        i += tui_utf8_char_len(text + i);
        cp++;
    }
    return cp;
}

/* --- Interval tables (UCD-derived) ------------------------------------ */

#include "unicode_tables.h"

#define TU_CLUSTER_MAX 16 /* max codepoints in one grapheme cluster */

static int range_lookup(const TuiRange *table, size_t n, uint32_t cp)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < table[mid].lo)
            hi = mid;
        else if (cp > table[mid].hi)
            lo = mid + 1;
        else
            return 1;
    }
    return 0;
}

int tui_codepoint_width(uint32_t cp)
{
    /* ASCII fast path */
    if (cp < 0x7Fu)
        return cp < 0x20u ? 0 : 1;
    if (cp < 0xA0u)
        return 0; /* DEL + C1 controls */
    if (cp == 0x00ADu)
        return 0; /* soft hyphen */

    if (range_lookup(TU_ZERO, TU_ZERO_LEN, cp))
        return 0;
    if (range_lookup(TU_WIDE, TU_WIDE_LEN, cp))
        return 2;
    return 1;
}

/* --- Grapheme clusters ------------------------------------------------ */

static bool tu_is_extend(uint32_t cp)
{
    /* GB9: Extend | ZWJ — no break before these. */
    return range_lookup(TU_EXTEND, TU_EXTEND_LEN, cp) != 0;
}

static bool tu_is_spacing_mark(uint32_t cp)
{
    /* GB9a: SpacingMark — no break before these. */
    return range_lookup(TU_SPACING_MARK, TU_SPACING_MARK_LEN, cp) != 0;
}

static bool tu_is_prepend(uint32_t cp)
{
    /* GB9b: Prepend — forces the next character into this cluster. */
    return range_lookup(TU_PREPEND, TU_PREPEND_LEN, cp) != 0;
}

static bool tu_is_extended_pictographic(uint32_t cp)
{
    return range_lookup(TU_EXT_PICT, TU_EXT_PICT_LEN, cp) != 0;
}

static bool tu_is_regional_indicator(uint32_t cp)
{
    return cp >= 0x1F1E6u && cp <= 0x1F1FFu;
}

/* Does a grapheme break sit between `prev` and `cur`? A stateless
 * approximation of UAX #29 GB3-GB13 (same shape coffer uses): enough
 * for the dominant cases — combining marks attach, a ZWJ emoji sequence
 * holds together, regional indicators pair into a flag. */
static bool tu_grapheme_break_before(uint32_t prev, uint32_t cur)
{
    if (prev == 0x0Du && cur == 0x0Au) /* GB3: CR x LF */
        return false;
    if (cur == 0x0Au || cur == 0x0Du || cur == 0x00) /* GB4-5: controls */
        return true;
    if (tu_is_extend(cur)) /* GB9: x Extend, x ZWJ */
        return false;
    if (tu_is_spacing_mark(cur)) /* GB9a */
        return false;
    if (tu_is_prepend(prev)) /* GB9b */
        return false;
    if (prev == 0x200Du && tu_is_extended_pictographic(cur)) /* GB11 */
        return false;
    if (tu_is_regional_indicator(prev) && tu_is_regional_indicator(cur)) /* GB12-13 */
        return false;
    return true; /* GB999 */
}

/* The ambient model. GRID is deterministic for standalone callers and
 * for tests; a runtime installs the terminal's model before init(). */
static TuiWidthModel s_width_model = TUI_WIDTH_MODEL_GRID;
static int s_width_model_pinned = 0;

TuiWidthModel tui_width_model(void)
{
    return s_width_model;
}

const char *tui_width_model_name(TuiWidthModel model)
{
    switch (model) {
    case TUI_WIDTH_MODEL_AUTO:
        return "auto";
    case TUI_WIDTH_MODEL_GRID:
        return "grid";
    case TUI_WIDTH_MODEL_WCWIDTH:
        return "wcwidth";
    case TUI_WIDTH_MODEL_UTS51:
        return "uts51";
    }
    return "unknown";
}

static int width_model_is_measurement(TuiWidthModel model)
{
    return model == TUI_WIDTH_MODEL_GRID || model == TUI_WIDTH_MODEL_WCWIDTH ||
           model == TUI_WIDTH_MODEL_UTS51;
}

int tui_width_model_set(TuiWidthModel model)
{
    if (s_width_model_pinned || !width_model_is_measurement(model))
        return 0;
    s_width_model = model;
    return 1;
}

int tui_width_model_pin(TuiWidthModel model)
{
    if (s_width_model_pinned || !width_model_is_measurement(model))
        return 0;
    s_width_model = model;
    s_width_model_pinned = 1;
    return 1;
}

void tui_width_model_unpin(void)
{
    s_width_model_pinned = 0;
}

/* The width rule, parameterized by the terminal model. AUTO means "the
 * ambient model" (it is a configuration value, not a measurement one). */
static int cluster_width(const uint32_t *cps, uint32_t len, TuiWidthModel model)
{
    if (len == 0)
        return 0;
    if (model == TUI_WIDTH_MODEL_AUTO)
        model = s_width_model;
    /* A regional indicator pair is one flag, two cells. */
    if (len >= 2 && tu_is_regional_indicator(cps[0]) && tu_is_regional_indicator(cps[1]))
        return 2;

    int base = tui_codepoint_width(cps[0]);
    /* Strict wcwidth: a variation selector selects a glyph, not a cell
     * count, so neither selector changes the base's width. */
    if (model == TUI_WIDTH_MODEL_WCWIDTH)
        return base;

    /* Presentation selectors, scanned from the END: the last one in the
     * cluster wins.
     *
     * VS16 forces emoji presentation (two cells) even on a Narrow base.
     *
     * VS15 requests the TEXT presentation. Under UTS51 (portty, kitty)
     * it takes one cell whenever UTS #51 defines a text glyph for the
     * base (TU_TEXT_PRES); a base with no text sequence (a CJK
     * ideograph, an SMP emoji like U+1F600) has no one-cell glyph to
     * fall back to, so it keeps its East Asian width. Under GRID
     * (xterm, Windows Terminal) the selector is presentation only: the
     * base's East Asian width is the cell count either way. */
    for (uint32_t i = len; i-- > 0;) {
        if (cps[i] == 0xFE0Fu)
            return 2;
        if (cps[i] == 0xFE0Eu) {
            if (model == TUI_WIDTH_MODEL_UTS51 &&
                range_lookup(TU_TEXT_PRES, TU_TEXT_PRES_LEN, cps[0]))
                return 1;
            return base;
        }
    }
    return base;
}

int tui_cluster_width(const uint32_t *cps, uint32_t len)
{
    return cluster_width(cps, len, s_width_model);
}

int tui_cluster_width_model(const uint32_t *cps, uint32_t len,
                            TuiWidthModel model)
{
    return cluster_width(cps, len, model);
}

int tui_next_cluster_model(const char *utf8, size_t len, size_t *bytes,
                           TuiWidthModel model)
{
    uint32_t cluster[TU_CLUSTER_MAX];
    uint32_t clen = 0;
    size_t i = 0;

    while (i < len) {
        int char_len = tui_utf8_char_len(utf8 + i);
        if (char_len <= 0 || i + (size_t)char_len > len)
            break; /* truncated tail */
        uint32_t cp = tui_utf8_decode(utf8 + i, char_len);
        if (clen > 0 && tu_grapheme_break_before(cluster[clen - 1], cp))
            break;
        /* Past the cap further joiners are folded in rather than
         * overflowing the stack buffer (matches the grid rule). */
        if (clen < TU_CLUSTER_MAX)
            cluster[clen++] = cp;
        i += (size_t)char_len;
    }

    if (bytes)
        *bytes = i;
    return cluster_width(cluster, clen, model);
}

int tui_next_cluster(const char *utf8, size_t len, size_t *bytes)
{
    return tui_next_cluster_model(utf8, len, bytes, s_width_model);
}

size_t tui_prev_cluster(const char *text, size_t len, size_t pos)
{
    if (!text || pos == 0)
        return 0;
    if (pos > len)
        pos = len;

    /* A cluster boundary is only knowable from the cluster's start (a
     * leading combining mark forms a cluster of its own, so stepping back
     * from `pos` by break rules alone can land mid-cluster). Walk forward
     * from 0 and keep the last boundary before pos; the walker is the
     * same one the renderer uses, so the two cannot disagree. Editing
     * buffers are prompt-sized, so the linear scan is the simple, exact
     * answer. */
    size_t prev = 0;
    size_t i = 0;
    while (i < pos) {
        size_t bytes = 0;
        (void)tui_next_cluster(text + i, len - i, &bytes);
        if (bytes == 0)
            break; /* truncated tail: no further boundary */
        if (i + bytes > pos)
            return i; /* pos sits inside this cluster */
        prev = i;
        i += bytes;
    }
    return prev;
}

/* --- Display width ---------------------------------------------------- */

int tui_utf8_display_width_n_model(const char *text, size_t len,
                                   TuiWidthModel model)
{
    if (!text)
        return 0;
    size_t i = 0;
    int width = 0;
    while (i < len) {
        size_t bytes = 0;
        int w = tui_next_cluster_model(text + i, len - i, &bytes, model);
        if (bytes == 0)
            break;
        width += w;
        i += bytes;
    }
    return width;
}

int tui_utf8_display_width_n(const char *text, size_t len)
{
    return tui_utf8_display_width_n_model(text, len, s_width_model);
}

int tui_utf8_display_width_model(const char *str, TuiWidthModel model)
{
    if (!str)
        return 0;
    return tui_utf8_display_width_n_model(str, strlen(str), model);
}

int tui_utf8_display_width(const char *str)
{
    return tui_utf8_display_width_model(str, s_width_model);
}

size_t tui_utf8_display_width_ansi_model(const char *text, size_t len,
                                         TuiWidthModel model)
{
    size_t width = 0;
    size_t i = 0;
    /* The one escape grammar (ansi_scan.h): CSI, OSC, DCS/APC/PM/SOS and
     * two-byte escapes are all sequences, and a sequence occupies no
     * columns. Carrying state here is irrelevant for a whole-buffer
     * call, but it keeps this walker identical to the stream and row
     * arithmetic ones. */
    TuiAnsiScan scan = { 0 };

    while (i < len) {
        unsigned char c = (unsigned char)text[i];
        if (scan.state != 0 || c == 0x1b) {
            TuiAnsiKind kind = TUI_ANSI_KIND_NONE;
            i++;
            if (tui_ansi_scan_feed(&scan, c, &kind)) {
                scan.len = 0;
                scan.nostore = 0;
            }
            continue;
        }
        if (c < 0x20) { /* control bytes occupy no columns */
            i++;
            continue;
        }
        size_t bytes = 0;
        int w = tui_next_cluster_model(text + i, len - i, &bytes, model);
        if (bytes == 0)
            break;
        width += (size_t)w;
        i += bytes;
    }

    return width;
}

size_t tui_utf8_display_width_ansi(const char *text, size_t len)
{
    return tui_utf8_display_width_ansi_model(text, len, s_width_model);
}
