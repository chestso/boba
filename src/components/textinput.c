/* textinput.c - Multi-line text input component implementation */

#include <boba/ansi_sequences.h>
#include <boba/cmd.h>
#include <boba/components/textinput.h>
#include <boba/text.h>
#include <boba/unicode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fit.h"

#define TEXTINPUT_INITIAL_CAP 256
#define TEXTINPUT_TYPE_ID     (TUI_COMPONENT_TYPE_BASE + 1)

/* --- One layout, one window -------------------------------------------
 *
 * Everything the component paints, reports or measures comes from ONE
 * table of visual rows built from the text, the content width and the
 * prompt widths. Before this, five render paths (absolute/relative x
 * single/multi x wrap/clip) each re-derived the row arithmetic, and
 * tui_textinput_cursor_pos() and tui_textinput_get_height() derived it a
 * third and fourth time; they could disagree, and in inline mode that
 * disagreement walks the prompt up the screen.
 *
 * A visual row is a byte range painted on one terminal row. With WRAP a
 * logical line becomes several rows; with CLIP it is one row, and the
 * horizontal window is applied at paint time.
 *
 * The table is a cache: rebuilt whenever the text, the width, the wrap
 * mode or the prompt widths change. A cursor move only re-locates the
 * cursor within it (O(1) instead of O(text)).
 */
typedef struct TuiVisualRow
{
    size_t byte_start, byte_end; /* text range painted on this row            */
    int logical_line;            /* index of the logical line it belongs to   */
    int first_of_line;           /* 1: the line's first row, 0: wrap remainder*/
    int prompt_w;                /* prefix display width on this row          */
} TuiVisualRow;

typedef struct TuiLayout
{
    /* Cache key: the geometry the table was built for. */
    uint64_t geometry_revision;
    uint64_t text_revision;
    int width; /* resolved content width, 0 = unconstrained */
    int wrap;  /* TuiTextInputWrap at build                */
    int show_prompt;
    int prompt_w, cont_w;

    TuiVisualRow *rows;
    size_t n_rows, cap;

    /* Where the cursor sits in the table, for the cursor it was located
     * for. cursor_byte is compared on its own so a pure move is cheap. */
    size_t cursor_byte;
    size_t cur_row; /* visual row index            */
    int cur_col;    /* display column inside it    */
} TuiLayout;

/* The model. Opaque to callers (see the header); every derived field here
 * is the component's own business, which is why the struct lives in this
 * file. */
struct TuiTextInput
{
    TuiModel base; /* Base model for component interface */

    char *text;      /* UTF-8 text content */
    size_t text_len; /* Current text length in bytes */
    size_t text_cap; /* Allocated capacity */

    size_t cursor_byte; /* Cursor position in bytes */
    size_t cursor_col;  /* Cursor's display column in its logical line */
    size_t cursor_row;  /* Logical line index (0-indexed) */

    /* Selection mark. When has_mark is set, the active region is
     * [min(mark_byte, cursor_byte), max(mark_byte, cursor_byte)). Motion
     * extends the region; edits clear it; Esc / C-g clear it; M-w reports
     * it to the clipboard hook; C-w kills it. */
    size_t mark_byte;
    int has_mark;
    int mark_from_shift; /* 1 when the mark was made by Shift-motion */

    /* Horizontal window for CLIP mode, in display columns. Shared by every
     * row: the text scrolls under one fixed column window, and the window
     * follows the cursor. Derived state, kept in step by
     * recalculate_cursor_position(). */
    int offset;
    int offset_right;

    const char *prompt;              /* Optional prompt string (not owned) */
    int prompt_len;                  /* Cached prompt display width */
    const char *continuation_prompt; /* Prompt for continuation lines */
    int continuation_prompt_len;     /* Cached continuation prompt width */

    int focused;     /* Whether component has focus */
    int multiline;   /* Allow multiple lines (Enter inserts newline) */
    int show_prompt; /* Whether to display the prompt (default: 1) */

    /* Lipgloss-shaped focused/blurred styles for the prompt. An unstyled
     * record means "paint it plain": there is no second, raw-ANSI colour
     * channel (the setter for one is gone). */
    TuiStyle focused_prompt_style;
    TuiStyle blurred_prompt_style;

    /* History management */
    char **history;    /* Array of past input lines */
    int history_size;  /* Max history entries */
    int history_count; /* Current number of history entries */
    int history_pos;   /* Navigation position (-1 = current input) */
    char *saved_input; /* Saved current input when navigating history */

    /* Tab completion */
    char *word_chars; /* Characters that form words for completion and word movement */

    /* Kill/yank buffer */
    char *kill_buf;      /* Killed text (malloc'd, NULL initially) */
    size_t kill_buf_len; /* Length of killed text in bytes */
    int last_was_kill;   /* Whether previous key was a kill command */

    /* Undo stack */
    struct
    {
        char *text;
        size_t text_len;
        size_t cursor_byte;
    } *undo_stack;
    int undo_count;
    int undo_cap;
    int ctrl_x_prefix; /* Waiting for second key after C-x */

    /* Pre-edit snapshot buffer (reused across keystrokes) */
    char *snap_buf;
    size_t snap_buf_cap;
    size_t snap_len;
    size_t snap_cursor;
    int snap_valid; /* Whether snapshot should be committed */

    int echo_mode;         /* 0 = normal, 1 = masked (one * per cluster) */
    TuiTextInputWrap wrap; /* CLIP (default) or WRAP                   */

    /* Rows drawn by the last view() pass. Used to erase rows the previous
     * frame painted and this one no longer does — the runtime does not
     * clear stale alt-screen rows, so the component owns cleanup of its
     * own area. It describes what is ON SCREEN, so it survives a geometry
     * change: the erase needs the old extent, not the new one. */
    int last_render_rows;

    /* Bumped by every change to the text's bytes (typing, deletes, kills,
     * undo, paste, set_text, clear, replace_range), never by movement.
     * Spans carry the revision they were computed for. */
    uint64_t revision;
    TuiTextSpan *spans; /* owned copy, sorted, disjoint */
    size_t n_spans;
    uint64_t spans_revision;

    /* Styled-text highlight hook. If set, render_text_range calls this
     * instead of emitting raw text bytes. */
    char *(*highlight_fn)(const char *text, size_t len, void *userdata);
    void *highlight_userdata;

    /* Clipboard reporting (see TuiTextInputClipboardFn). */
    TuiTextInputClipboardFn clipboard_fn; /* NULL = report nothing */
    void *clipboard_ud;
    int clipboard_cmd; /* 0 = emit no TuiCmd at all (default) */

    /* Ctrl+A: 0 = move to line start (readline, the default), 1 = select
     * all. Declared by the app; never derived from the platform. */
    int ctrl_a_select_all;

    /* --- geometry (see tui_textinput_set_geometry) -------------------- */
    TuiTextInputGeometry geom;  /* as the app declared it */
    int term_w, term_h;         /* last known terminal size, 0 = unknown */
    int content_width;          /* resolved from geom/term, 0 = free */
    int first_row;              /* resolved screen row, 0 = inline    */
    size_t top_byte;            /* scroll anchor: byte_start of the first
                                 * visible visual row. An offset, not a row
                                 * index, because row indices stop meaning
                                 * anything once a resize re-flows the text */
    uint64_t geometry_revision; /* bumped by every geometry change */

    TuiLayout layout; /* visual-row table (cache) */
};

/* Check if a character is a word character */
static int is_word_char(const TuiTextInput *input, char c)
{
    if (input->word_chars)
        return strchr(input->word_chars, c) != NULL;
    return c != ' ' && c != '\t'; /* fallback when word_chars not set */
}

/* --- Display columns --------------------------------------------------
 *
 * Every horizontal metric in this component — the prompt width, the
 * cursor's column, the wrap budget and the horizontal scroll window — is in
 * terminal DISPLAY COLUMNS (cells): a CJK or emoji cluster is one codepoint
 * but two cells, a combining mark none. Counting bytes or codepoints instead
 * (which this file used to do) shifts every derived column by the number of
 * wide clusters in the measured run. viewport.c measures the same way.
 *
 * Byte offsets stay the string-level unit (cursor_byte, render ranges);
 * tui_next_cluster / tui_prev_cluster move between the two, and every fit
 * (the horizontal scroll window, the wrap budget, where a vertical motion
 * lands) goes through text.h, so an edit never splits a grapheme cluster.
 *
 * The input buffer is measured with tui_utf8_display_width_n (its bytes are
 * literal glyphs); the prompt / continuation prompt are pre-rendered strings
 * that may carry their own SGR, so they go through
 * tui_utf8_display_width_ansi — the same call viewport.c and list_popup.c
 * make on rendered text. Chrome above the input (a status row) is not
 * this component's: it belongs to the boba statusline component, which
 * measures and lays out its own row. */

/* Byte offset of the end of the longest prefix of text[0..len) that fits in
 * `cols` display columns. Zero when `cols <= 0`; otherwise at least one
 * cluster is consumed, so a chunk walk always progresses, and a cluster wider
 * than `cols` is taken whole — a row never splits a grapheme cluster. */
static size_t fit_cols(const char *text, size_t len, int cols)
{
    if (cols <= 0)
        return 0;
    /* at_least_one: the first cluster is taken even when it alone exceeds
     * the budget, so a chunk walk always progresses and a row never
     * splits a grapheme cluster. */
    return tui_text_fit(text, len, cols, 1).bytes;
}

/* Display width of a rendered prompt string — NULL is zero, and any SGR the
 * string carries itself is not a column. */
static int prompt_cols(const char *prompt)
{
    if (!prompt)
        return 0;
    return (int)tui_utf8_display_width_ansi(prompt, strlen(prompt));
}

/* Display width of the prompt rendered on a given logical line:
 * line 0 uses the main prompt; later lines use the continuation prompt or
 * fall back to spaces of width prompt_len (matches render_continuation_prompt).
 * Chrome above the input is not part of this — the prompt column never
 * depends on anything outside the input. */
static int line_prompt_width(const TuiTextInput *input, int line_index)
{
    if (!input->show_prompt || !input->prompt || input->prompt_len <= 0)
        return 0;
    if (line_index == 0)
        return input->prompt_len;
    if (input->continuation_prompt && input->continuation_prompt_len > 0)
        return input->continuation_prompt_len;
    return input->prompt_len;
}

/* Byte range of the logical line containing the cursor */
static void cursor_line_bounds(const TuiTextInput *input, size_t *out_start,
                               size_t *out_end)
{
    size_t start = 0;
    for (size_t i = input->cursor_byte; i > 0; i--) {
        if (input->text[i - 1] == '\n') {
            start = i;
            break;
        }
    }
    size_t end = input->text_len;
    for (size_t i = input->cursor_byte; i < input->text_len; i++) {
        if (input->text[i] == '\n') {
            end = i;
            break;
        }
    }
    *out_start = start;
    *out_end = end;
}

/* --- geometry resolution ---------------------------------------------
 *
 * The app declares a TuiTextInputGeometry; the terminal reports a size;
 * these two produce the numbers the rest of the file uses:
 *
 *   content_width  columns of text per row, 0 = unconstrained
 *   first_row      absolute screen row of the first painted row, 0 = inline
 *
 * Nothing else in this file reads in->geom for sizing: if it is not one of
 * these two values, it is a bug waiting to disagree with the layout. */
static void resolve_geometry(TuiTextInput *input)
{
    if (input->geom.width > 0)
        input->content_width = input->geom.width;
    else if (input->geom.sizing == TUI_TEXTINPUT_FLOW) {
        TuiFitInsets ins = { 0, 0, input->geom.side_margin, 0 };
        input->content_width = tui_fit_cols(input->term_w, &ins, 0);
    } else
        input->content_width = 0;

    if (input->geom.place == TUI_TEXTINPUT_AT_ROW && input->geom.row > 0)
        input->first_row = input->geom.row;
    else
        input->first_row = 0;
}

/* Mark the layout stale. Every geometry change goes through here, so the
 * cache key can be a single counter instead of "did I forget a field?". */
static void invalidate_layout(TuiTextInput *input)
{
    input->geometry_revision++;
    resolve_geometry(input);
}

/* --- the visual-row layout (see the TuiLayout comment at the top) ----- */

static void layout_free(TuiLayout *L)
{
    free(L->rows);
    L->rows = NULL;
    L->n_rows = 0;
    L->cap = 0;
}

static int layout_push(TuiLayout *L, size_t start, size_t end, int line,
                       int first_of_line, int prompt_w)
{
    if (L->n_rows == L->cap) {
        size_t cap = L->cap ? L->cap * 2 : 32;
        TuiVisualRow *grown =
            (TuiVisualRow *)realloc(L->rows, cap * sizeof(*grown));
        if (!grown)
            return 0;
        L->rows = grown;
        L->cap = cap;
    }
    L->rows[L->n_rows].byte_start = start;
    L->rows[L->n_rows].byte_end = end;
    L->rows[L->n_rows].logical_line = line;
    L->rows[L->n_rows].first_of_line = first_of_line;
    L->rows[L->n_rows].prompt_w = prompt_w;
    L->n_rows++;
    return 1;
}

/* Where the cursor sits in the table: which visual row, and how many
 * display columns into it (prompt excluded). This is the arithmetic
 * tui_textinput_cursor_pos() used to redo, wrap walk included. */
static void layout_locate_cursor(const TuiTextInput *input, TuiLayout *L)
{
    L->cursor_byte = input->cursor_byte;
    L->cur_row = 0;
    L->cur_col = 0;
    if (L->n_rows == 0)
        return;

    /* The row the cursor's byte falls in: the last one that starts at or
     * before it. A cursor on a chunk boundary therefore belongs to the row
     * it opens, not to the one it closes. */
    size_t idx = 0;
    for (size_t k = 0; k < L->n_rows; k++) {
        if (L->rows[k].byte_start <= input->cursor_byte)
            idx = k;
        else
            break;
    }

    const TuiVisualRow *r = &L->rows[idx];
    size_t upto = input->cursor_byte > r->byte_start
                      ? input->cursor_byte - r->byte_start
                      : 0;
    if (upto > r->byte_end - r->byte_start)
        upto = r->byte_end - r->byte_start;
    int col = (int)tui_utf8_display_width_n(input->text + r->byte_start, upto);

    /* A cursor at the very end of a line whose last visual row is exactly
     * full sits at that row's end — not at the start of a fresh, empty row.
     * Folding by the raw width would report a phantom extra row, which in
     * inline mode walks the tracked cursor-up one row too far each frame. */
    if (input->wrap == TUI_TEXTINPUT_WRAP && input->content_width > 0 &&
        input->cursor_byte >= r->byte_end && col > 0) {
        int content_w = input->content_width - r->prompt_w;
        if (content_w < 1)
            content_w = 1;
        if (col == content_w)
            col -= 1;
    }

    L->cur_row = idx;
    L->cur_col = col;
}

/* Build the table for the current text/width/prompts. Returns 0 on
 * allocation failure, leaving no rows — the callers treat an empty layout
 * as "nothing to paint" rather than as a crash. */
static int layout_build(TuiTextInput *input)
{
    TuiLayout *L = &input->layout;

    L->n_rows = 0;
    int wrap = input->wrap == TUI_TEXTINPUT_WRAP && input->content_width > 0;

    size_t line_start = 0;
    int line_idx = 0;
    for (size_t i = 0; i <= input->text_len; i++) {
        if (i != input->text_len && input->text[i] != '\n')
            continue;

        int prompt_w = line_prompt_width(input, line_idx);
        if (wrap) {
            int content_w = input->content_width - prompt_w;
            if (content_w < 1) {
                /* Degenerate: the content width survives, the prompt does
                 * not. A row always holds at least one cluster, so the walk
                 * below always advances. */
                content_w = 1;
                prompt_w = 0;
            }
            size_t pos = line_start;
            for (;;) {
                size_t adv = fit_cols(input->text + pos, i - pos, content_w);
                if (adv == 0)
                    adv = i - pos; /* empty remainder: take it as one row */
                if (!layout_push(L, pos, pos + adv, line_idx, pos == line_start,
                                 prompt_w))
                    goto nomem;
                pos += adv;
                if (pos >= i)
                    break;
            }
        } else {
            /* CLIP: one row per logical line; the horizontal window is
             * applied when the row is painted. */
            if (!layout_push(L, line_start, i, line_idx, 1, prompt_w))
                goto nomem;
        }

        line_idx++;
        line_start = i + 1;
    }

    L->geometry_revision = input->geometry_revision;
    L->text_revision = input->revision;
    L->width = input->content_width;
    L->wrap = (int)input->wrap;
    L->show_prompt = input->show_prompt;
    L->prompt_w = input->prompt_len;
    L->cont_w = input->continuation_prompt_len;

    layout_locate_cursor(input, L);
    return 1;

nomem:
    layout_free(L);
    return 0;
}

/* The layout, built or reused. A cache hit with a moved cursor re-locates
 * the cursor only (O(1) instead of O(text)). */
static const TuiLayout *layout_get(TuiTextInput *input)
{
    TuiLayout *L = &input->layout;
    int wrap = input->wrap == TUI_TEXTINPUT_WRAP;

    if (L->rows && L->geometry_revision == input->geometry_revision &&
        L->text_revision == input->revision &&
        L->width == input->content_width && L->wrap == wrap &&
        L->show_prompt == input->show_prompt &&
        L->prompt_w == input->prompt_len &&
        L->cont_w == input->continuation_prompt_len) {
        if (L->cursor_byte != input->cursor_byte)
            layout_locate_cursor(input, L);
        return L;
    }
    (void)layout_build(input);
    return L;
}

/* The layout for a const model: the cache is the component's own
 * bookkeeping, not part of the observable model. */
static const TuiLayout *layout_get_c(const TuiTextInput *input)
{
    return layout_get((TuiTextInput *)input);
}

/* Rows the input may paint this frame (design: H). The natural height is
 * capped by the geometry, and by what the screen can give once the app's
 * reserves — or, for absolute placement, the rows above — are taken out. */
static int visible_rows(const TuiTextInput *input)
{
    const TuiLayout *L = layout_get_c(input);
    int total = L->n_rows > 0 ? (int)L->n_rows : 1;

    TuiFitInsets ins = { 0, 0, 0, 0 };
    int term_h = 0;

    /* The screen bounds the box when the input follows the window (FLOW,
     * which reserves rows for the app's chrome) or when it is placed at an
     * absolute row (the box is what is left below that row). Both can be
     * true, and the tighter of the two wins: an avail of term_h - above -
     * below is minimised by the LARGER `above`, so the two bounds combine
     * as a max instead of being alternatives. */
    if (input->term_h > 0 &&
        (input->geom.sizing == TUI_TEXTINPUT_FLOW || input->first_row > 0)) {
        term_h = input->term_h;
        if (input->geom.sizing == TUI_TEXTINPUT_FLOW) {
            ins.above = input->geom.reserve_above;
            ins.below = input->geom.reserve_below;
        }
        if (input->first_row > 0 && input->first_row - 1 > ins.above)
            ins.above = input->first_row - 1;
    }

    return tui_fit_rows(total, term_h, &ins, input->geom.min_rows,
                        input->geom.rows);
}

/* Index of the row the window currently starts at. The anchor is top_byte,
 * not a row index, because a resize re-flows the rows under it. */
static size_t top_row(const TuiLayout *L, const TuiTextInput *input)
{
    if (L->n_rows == 0)
        return 0;
    size_t lo = 0, hi = L->n_rows - 1, best = 0;
    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (L->rows[mid].byte_start <= input->top_byte) {
            best = mid;
            if (mid == hi)
                break;
            lo = mid + 1;
        } else {
            if (mid == 0)
                break;
            hi = mid - 1;
        }
    }
    return best;
}

/* Move the window so the cursor is inside it, with `scrolloff` rows of
 * context where the text allows, and never with blank rows at the bottom
 * (the clamp to n_rows - H is what pulls hidden rows back in when the box
 * grows). The only persisted scroll state is top_byte. */
static void ensure_cursor_visible(TuiTextInput *input)
{
    const TuiLayout *L = layout_get(input);
    size_t total = L->n_rows;
    int H = visible_rows(input);
    size_t top = top_row(L, input);

    int so = input->geom.scrolloff > 0 ? input->geom.scrolloff : 0;
    if (so > (H - 1) / 2)
        so = (H - 1) / 2;

    if (total <= (size_t)H) {
        top = 0;
    } else {
        size_t cur = L->cur_row;
        if (cur < top + (size_t)so)
            top = cur > (size_t)so ? cur - (size_t)so : 0;
        if (cur > top + (size_t)(H - 1 - so))
            top = cur - (size_t)(H - 1 - so);
        size_t max_top = total - (size_t)H;
        if (top > max_top)
            top = max_top;
    }

    input->top_byte = total > 0 ? L->rows[top].byte_start : 0;
}

/* Adjust horizontal scroll offset so cursor stays visible on its line.
 * offset/offset_right are display-column offsets within the logical line,
 * shared by every row in CLIP mode. */
static void handle_overflow(TuiTextInput *input)
{
    size_t line_start, line_end;
    cursor_line_bounds(input, &line_start, &line_end);
    size_t line_bytes = line_end - line_start;

    int prompt_width = line_prompt_width(input, (int)input->cursor_row);
    int content_width = input->content_width - prompt_width;

    int total = tui_utf8_display_width_n(input->text + line_start, line_bytes);

    if (content_width <= 0) {
        input->offset = 0;
        input->offset_right = total;
        return;
    }

    int cursor_col = (int)input->cursor_col;

    if (total <= content_width) {
        /* Everything fits */
        input->offset = 0;
        input->offset_right = total;
        return;
    }

    if (cursor_col < input->offset) {
        /* Cursor scrolled left of window */
        input->offset = cursor_col;
        input->offset_right = input->offset + content_width;
        if (input->offset_right > total)
            input->offset_right = total;
    } else if (cursor_col >= input->offset_right) {
        /* Cursor scrolled right of window */
        input->offset_right = cursor_col + 1;
        input->offset = input->offset_right - content_width;
        if (input->offset < 0)
            input->offset = 0;
    } else {
        /* Cursor in view — clamp offset_right */
        input->offset_right = input->offset + content_width;
        if (input->offset_right > total)
            input->offset_right = total;
    }
}

/* Count lines and find cursor position */
static void recalculate_cursor_position(TuiTextInput *input)
{
    input->cursor_row = 0;

    size_t line_start = 0;
    for (size_t i = 0; i < input->cursor_byte && i < input->text_len; i++) {
        if (input->text[i] == '\n') {
            input->cursor_row++;
            line_start = i + 1;
        }
    }
    input->cursor_col = (size_t)tui_utf8_display_width_n(
        input->text + line_start, input->cursor_byte - line_start);

    /* Adjust horizontal scroll so cursor stays visible. In WRAP mode there
     * is no horizontal window: the layout re-flows instead. */
    if (input->wrap != TUI_TEXTINPUT_WRAP)
        handle_overflow(input);

    /* And the vertical window, so the cursor is never scrolled out of the
     * box after an edit or a move. */
    ensure_cursor_visible(input);
}

/* Ensure text buffer has enough capacity */
static int ensure_capacity(TuiTextInput *input, size_t needed)
{
    if (input->text_cap >= needed)
        return 0;

    size_t new_cap = input->text_cap;
    if (new_cap == 0)
        new_cap = TEXTINPUT_INITIAL_CAP;
    while (new_cap < needed)
        new_cap *= 2;

    char *new_text = (char *)realloc(input->text, new_cap);
    if (!new_text)
        return -1;

    input->text = new_text;
    input->text_cap = new_cap;
    return 0;
}

/* Clear the selection mark (no-op if no mark is set). Called from edit
 * paths that invalidate any previously-set mark byte offset. */
static void clear_mark_if_set(TuiTextInput *input)
{
    if (input->has_mark) {
        input->has_mark = 0;
        input->mark_byte = 0;
        input->mark_from_shift = 0;
    }
}

/* Return the normalized selection range when has_mark is set:
 * *out_start = min(mark_byte, cursor_byte)
 * *out_end   = max(mark_byte, cursor_byte)
 * Returns 1 if a selection exists, 0 otherwise. */
static int selection_range(const TuiTextInput *input, size_t *out_start,
                           size_t *out_end)
{
    if (!input->has_mark)
        return 0;
    if (input->mark_byte <= input->cursor_byte) {
        *out_start = input->mark_byte;
        *out_end = input->cursor_byte;
    } else {
        *out_start = input->cursor_byte;
        *out_end = input->mark_byte;
    }
    return 1;
}

/* emacs' shift-select-mode, in two halves. This is the first: called
 * BEFORE a motion key moves the cursor. Shift starts a mark (marking it as
 * shift-made, so an unshifted motion may collapse it); an unshifted motion
 * collapses a shift-made one. A Ctrl+Space mark (mark_from_shift == 0) is
 * untouched — motion extends it, as it always did. */
static void begin_motion(TuiTextInput *input, int shift)
{
    if (shift && !input->has_mark) {
        input->mark_byte = input->cursor_byte;
        input->has_mark = 1;
        input->mark_from_shift = 1;
    } else if (!shift && input->has_mark && input->mark_from_shift) {
        clear_mark_if_set(input); /* an unshifted motion collapses it */
    }
}

/* The second half: typing / paste / backspace / delete over a Shift
 * selection replaces it. Emacs marks keep their old behaviour (an
 * insertion clears the mark and inserts). The deleted bytes do NOT enter
 * the kill ring — a replace is not a kill. Returns 1 if it deleted, so
 * Backspace / Delete can stop there. */
static int replace_shift_selection(TuiTextInput *input)
{
    size_t start = 0, end = 0;
    if (!input->mark_from_shift || !selection_range(input, &start, &end) ||
        end <= start)
        return 0;

    memmove(input->text + start, input->text + end, input->text_len - end);
    input->text_len -= (end - start);
    input->text[input->text_len] = '\0';
    input->revision++;
    input->cursor_byte = start;
    clear_mark_if_set(input);
    recalculate_cursor_position(input);
    return 1;
}

/* Insert text at cursor position */
static int insert_text(TuiTextInput *input, const char *text, size_t len)
{
    if (len == 0)
        return 0;

    /* Typing over a shift-made selection replaces it. Idempotent, so a
     * multi-run paste collapses the selection once — the later runs see
     * no mark — and the whole paste stays one undo entry. */
    (void)replace_shift_selection(input);

    if (ensure_capacity(input, input->text_len + len + 1) < 0)
        return -1;

    /* Move text after cursor */
    if (input->cursor_byte < input->text_len) {
        memmove(input->text + input->cursor_byte + len,
                input->text + input->cursor_byte,
                input->text_len - input->cursor_byte);
    }

    /* Insert new text */
    memcpy(input->text + input->cursor_byte, text, len);
    input->text_len += len;
    input->revision++;
    input->cursor_byte += len;
    input->text[input->text_len] = '\0';

    clear_mark_if_set(input);
    recalculate_cursor_position(input);
    return 0;
}

/* Insert a single UTF-8 codepoint */
static int insert_codepoint(TuiTextInput *input, uint32_t cp)
{
    char buf[5];
    int len = tui_utf8_encode(cp, buf);
    return insert_text(input, buf, len);
}

/* Insert a bracketed-paste payload as text.
 *
 * The payload is the terminal's bytes for what the user copied, so it
 * arrives as LINES, not as keystrokes: a newline in it is a newline,
 * never an Enter key press (which is what a terminal without bracketed
 * paste delivers — and what would submit the line mid-paste).
 *
 * Line endings are normalized on the way in: CRLF, a lone CR and a lone
 * LF all mean one line break, because that is what the terminals send
 * for the same copied text. What a line break becomes is the input's
 * own shape: '\n' in a multiline input, and a single SPACE in a
 * single-line one (the bubbles textinput's rule — the one-row buffer
 * has no lines, so the break joins words rather than gluing them).
 *
 * Other C0 control bytes are dropped: they are not text, and the buffer
 * is what the view paints and the app reads. A truncated UTF-8 tail is
 * dropped whole, never inserted half-decoded.
 *
 * The payload lands as ONE edit — insert_text per run — so the caller
 * wraps it in a single undo snapshot: one undo restores the buffer the
 * paste landed on, not one per character pasted. */
static void insert_paste(TuiTextInput *input, const TuiPasteMsg *paste)
{
    if (!paste || !paste->text)
        return;

    const char *p = paste->text;
    const char *end = p + paste->len;

    while (p < end) {
        unsigned char c = (unsigned char)*p;

        if (c == '\r' || c == '\n') {
            if (c == '\r' && p + 1 < end && p[1] == '\n')
                p++; /* CRLF is one break, not two */
            p++;
            insert_codepoint(input, input->multiline ? '\n' : ' ');
            continue;
        }
        if (c < 0x20) { /* C0 control bytes are not text */
            p++;
            continue;
        }

        size_t bytes = 0;
        (void)tui_next_cluster(p, (size_t)(end - p), &bytes);
        if (bytes == 0)
            break; /* a truncated tail is dropped whole */
        insert_text(input, p, bytes);
        p += bytes;
    }
}

/* Delete character before cursor (backspace) */
static void delete_before(TuiTextInput *input)
{
    /* A shift-made selection is what Backspace deletes. */
    if (replace_shift_selection(input))
        return;

    if (input->cursor_byte == 0)
        return;

    size_t prev = tui_prev_cluster(input->text, input->text_len,
                                   input->cursor_byte);
    size_t del_len = input->cursor_byte - prev;

    memmove(input->text + prev, input->text + input->cursor_byte,
            input->text_len - input->cursor_byte);
    input->text_len -= del_len;
    input->revision++;
    input->cursor_byte = prev;
    input->text[input->text_len] = '\0';

    clear_mark_if_set(input);
    recalculate_cursor_position(input);
}

/* Delete character at cursor (delete key) */
static void delete_at(TuiTextInput *input)
{
    /* A shift-made selection is what Delete deletes. */
    if (replace_shift_selection(input))
        return;

    if (input->cursor_byte >= input->text_len)
        return;

    size_t clen = 0;
    (void)tui_next_cluster(input->text + input->cursor_byte,
                           input->text_len - input->cursor_byte, &clen);
    if (clen == 0)
        clen = 1; /* truncated tail: remove the byte */
    size_t del_end = input->cursor_byte + clen;
    if (del_end > input->text_len)
        del_end = input->text_len;

    memmove(input->text + input->cursor_byte, input->text + del_end,
            input->text_len - del_end);
    input->text_len -= (del_end - input->cursor_byte);
    input->revision++;
    input->text[input->text_len] = '\0';

    clear_mark_if_set(input);
}

/* Move cursor left by one grapheme cluster */
static void cursor_left(TuiTextInput *input)
{
    if (input->cursor_byte > 0) {
        input->cursor_byte =
            tui_prev_cluster(input->text, input->text_len, input->cursor_byte);
        recalculate_cursor_position(input);
    }
}

/* Move cursor right by one grapheme cluster */
static void cursor_right(TuiTextInput *input)
{
    if (input->cursor_byte < input->text_len) {
        size_t clen = 0;
        (void)tui_next_cluster(input->text + input->cursor_byte,
                               input->text_len - input->cursor_byte, &clen);
        if (clen == 0)
            clen = 1;
        input->cursor_byte += clen;
        if (input->cursor_byte > input->text_len)
            input->cursor_byte = input->text_len;
        recalculate_cursor_position(input);
    }
}

/* Move cursor left by one word (Ctrl+Left) */
static void cursor_word_left(TuiTextInput *input)
{
    if (input->cursor_byte == 0)
        return;

    size_t pos = input->cursor_byte;

    /* Skip non-word characters before the word */
    while (pos > 0 && !is_word_char(input, input->text[pos - 1]))
        pos--;

    /* Skip the word itself */
    while (pos > 0 && is_word_char(input, input->text[pos - 1]))
        pos--;

    input->cursor_byte = pos;
    recalculate_cursor_position(input);
}

/* Move cursor right by one word (Ctrl+Right) */
static void cursor_word_right(TuiTextInput *input)
{
    if (input->cursor_byte >= input->text_len)
        return;

    size_t pos = input->cursor_byte;

    /* Skip the current word */
    while (pos < input->text_len && is_word_char(input, input->text[pos]))
        pos++;

    /* Skip non-word characters after the word */
    while (pos < input->text_len && !is_word_char(input, input->text[pos]))
        pos++;

    input->cursor_byte = pos;
    recalculate_cursor_position(input);
}

/* Move cursor to start of current line */
static void cursor_home(TuiTextInput *input)
{
    while (input->cursor_byte > 0 &&
           input->text[input->cursor_byte - 1] != '\n') {
        input->cursor_byte--;
    }
    recalculate_cursor_position(input);
}

/* Move cursor to end of current line */
static void cursor_end(TuiTextInput *input)
{
    while (input->cursor_byte < input->text_len &&
           input->text[input->cursor_byte] != '\n') {
        input->cursor_byte++;
    }
    recalculate_cursor_position(input);
}

/* Find line start position for given line number */
static size_t find_line_start(const char *text, size_t len, int line)
{
    if (line <= 0)
        return 0;

    int current_line = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            current_line++;
            if (current_line == line)
                return i + 1;
        }
    }
    return len;
}

/* Find line length (without newline) */
static size_t line_length(const char *text, size_t len, size_t start)
{
    size_t end = start;
    while (end < len && text[end] != '\n') {
        end++;
    }
    return end - start;
}

/* Display column of the cursor within its logical line (the unit the
 * horizontal scroll window and cursor_pos use). */
static int cursor_display_col(const TuiTextInput *input, size_t line_start)
{
    return tui_utf8_display_width_n(input->text + line_start,
                                    input->cursor_byte - line_start);
}

/* Byte offset of the first cluster at or after display column `col` in a
 * line; a strict fit, so the landing is always on a cluster boundary and
 * never past a cluster that alone exceeds the column. */
static size_t line_byte_for_col(const char *line, size_t len, int col)
{
    if (col <= 0)
        return 0;
    return tui_text_fit(line, len, col, 0).bytes;
}

/* Move cursor up one line, keeping the cursor's DISPLAY COLUMN (clamped
 * to a shorter line's width) and landing on a cluster boundary. */
static void cursor_up(TuiTextInput *input)
{
    if (input->cursor_row == 0)
        return;

    /* Find previous line start */
    size_t prev_line_start =
        find_line_start(input->text, input->text_len, (int)input->cursor_row - 1);
    size_t prev_line_len =
        line_length(input->text, input->text_len, prev_line_start);

    size_t cur_line_start =
        find_line_start(input->text, input->text_len, (int)input->cursor_row);
    int col = cursor_display_col(input, cur_line_start);

    input->cursor_byte = prev_line_start +
                         line_byte_for_col(input->text + prev_line_start,
                                           prev_line_len, col);
    recalculate_cursor_position(input);
}

/* Move cursor down one line, keeping the cursor's display column. */
static void cursor_down(TuiTextInput *input)
{
    /* Find next line */
    size_t next_line_start = input->cursor_byte;
    while (next_line_start < input->text_len &&
           input->text[next_line_start] != '\n') {
        next_line_start++;
    }
    if (next_line_start >= input->text_len)
        return;        /* No next line */
    next_line_start++; /* Skip newline */

    size_t next_line_len =
        line_length(input->text, input->text_len, next_line_start);

    size_t cur_line_start =
        find_line_start(input->text, input->text_len, (int)input->cursor_row);
    int col = cursor_display_col(input, cur_line_start);

    input->cursor_byte = next_line_start +
                         line_byte_for_col(input->text + next_line_start,
                                           next_line_len, col);
    recalculate_cursor_position(input);
}

/* Move the cursor by `delta` VISUAL rows, keeping its display column where
 * the target row is long enough. This is what PgUp/PgDn want: cursor
 * motion, not window motion, so ensure_cursor_visible() (and with it
 * scrolloff and the "never leave blank rows at the bottom" clamp) stays
 * the only thing that scrolls. */
static void cursor_rows(TuiTextInput *input, int delta)
{
    if (delta == 0)
        return;

    const TuiLayout *L = layout_get(input);
    if (L->n_rows == 0)
        return;

    long target = (long)L->cur_row + (long)delta;
    if (target < 0)
        target = 0;
    if (target > (long)L->n_rows - 1)
        target = (long)L->n_rows - 1;
    if ((size_t)target == L->cur_row) {
        /* Already at the edge the motion wants to leave from. Not a no-op
         * though: the cursor may have to move within its own row. */
        return;
    }

    const TuiVisualRow *row = &L->rows[(size_t)target];
    size_t off = line_byte_for_col(input->text + row->byte_start,
                                   row->byte_end - row->byte_start, L->cur_col);
    input->cursor_byte = row->byte_start + off;
    recalculate_cursor_position(input);
}

/* Which way a kill runs, i.e. how a repeat joins the ring. */
typedef enum
{
    KILL_FORWARD,
    KILL_BACKWARD,
} KillDir;

/* How a kill joins the ring. REPLACE starts a new chain; APPEND and
 * PREPEND extend an existing one, so `C-w C-w` reads in document order. */
typedef enum
{
    KILL_SAVE_REPLACE,
    KILL_SAVE_APPEND,
    KILL_SAVE_PREPEND,
} KillSaveMode;

/* What one key press wants the caller to do with the clipboard. Filled in
 * by the edit paths, consumed once at the tail of update(). */
typedef enum
{
    CLIP_NONE,
    CLIP_KILL,
    CLIP_COPY,
    CLIP_CUT,
    CLIP_PASTE_REQUEST,
} ClipKind;

/* One key press's clipboard outcome. CLIP_COPY names the byte range (it
 * does not enter the ring); every other kind delivers the ring. */
typedef struct
{
    ClipKind kind;
    size_t start, end;
} ClipEvent;

/* CLIP_NONE never reaches delivery; every other kind has a public mirror.
 * Mapped explicitly rather than cast, so growing either enum is a
 * compiler warning here instead of a silent shift. */
static TuiTextInputClipKind public_clip_kind(ClipKind kind)
{
    switch (kind) {
    case CLIP_KILL:
        return TUI_TEXTINPUT_CLIP_KILL;
    case CLIP_COPY:
        return TUI_TEXTINPUT_CLIP_COPY;
    case CLIP_CUT:
        return TUI_TEXTINPUT_CLIP_CUT;
    case CLIP_PASTE_REQUEST:
        return TUI_TEXTINPUT_CLIP_PASTE_REQUEST;
    case CLIP_NONE:
        break;
    }
    return TUI_TEXTINPUT_CLIP_KILL; /* unreachable: CLIP_NONE is not delivered */
}

/* KILL and CUT are the only kinds that may become a TuiCmd (the opt-in
 * path); COPY and PASTE_REQUEST are hook-only, whatever the app asked
 * for. */
static int clip_emits_cmd(ClipKind kind)
{
    return kind == CLIP_KILL || kind == CLIP_CUT;
}

/* Ring bookkeeping: join `text` to the kill ring under `mode`. The new
 * contents are built in a fresh buffer first, so on OOM the ring stays
 * exactly as it was — the delete still happens (undo holds the text, and
 * a half-rewritten ring is worse than a stale one). Returns 0 / -1; the
 * caller kills regardless. */
static int kill_save(TuiTextInput *input, const char *text, size_t len,
                     KillSaveMode mode)
{
    if (len == 0)
        return -1;

    int has_old = mode != KILL_SAVE_REPLACE && input->kill_buf &&
                  input->kill_buf_len > 0;
    size_t old_len = has_old ? input->kill_buf_len : 0;
    size_t new_len = old_len + len;

    char *buf = (char *)malloc(new_len);
    if (!buf)
        return -1;

    if (has_old && mode == KILL_SAVE_PREPEND) {
        memcpy(buf, text, len);
        memcpy(buf + len, input->kill_buf, old_len);
    } else {
        if (old_len > 0)
            memcpy(buf, input->kill_buf, old_len);
        memcpy(buf + old_len, text, len);
    }

    free(input->kill_buf);
    input->kill_buf = buf;
    input->kill_buf_len = new_len;
    return 0;
}

/* The one kill primitive: remove [start, end), join it to the ring (as a
 * REPLACE, or as an APPEND/PREPEND when it continues a kill chain), fix
 * the cursor, and record the clipboard outcome in `ev`. Every deletion
 * that fills the ring goes through here — a second memmove-and-kill_save
 * pair is the bug this exists to prevent.
 *
 * `last_was_kill` is set HERE, so a no-op (an empty range) never extends
 * the chain. */
static void kill_range(TuiTextInput *input, size_t start, size_t end,
                       KillDir dir, int was_kill, ClipKind kind,
                       ClipEvent *ev)
{
    if (end > input->text_len)
        end = input->text_len;
    if (start >= end)
        return; /* nothing to kill, no chain */

    size_t n = end - start;
    KillSaveMode mode = !was_kill ? KILL_SAVE_REPLACE
                                  : (dir == KILL_FORWARD ? KILL_SAVE_APPEND
                                                         : KILL_SAVE_PREPEND);
    (void)kill_save(input, input->text + start, n, mode);

    memmove(input->text + start, input->text + end, input->text_len - end);
    input->text_len -= n;
    input->text[input->text_len] = '\0';
    input->revision++;

    if (input->cursor_byte >= end)
        input->cursor_byte -= n;
    else if (input->cursor_byte > start)
        input->cursor_byte = start;

    clear_mark_if_set(input);
    recalculate_cursor_position(input);
    input->last_was_kill = 1;
    ev->kind = kind;
}

/* C-k: kill to the end of the line; at EOL take the newline too (joining
 * the next line in); at the end of the buffer do nothing at all. "Two
 * presses remove the line" falls out of this — it does not depend on
 * was_kill. */
static void kill_line(TuiTextInput *input, int was_kill, ClipEvent *ev)
{
    size_t start = input->cursor_byte, end = start;
    while (end < input->text_len && input->text[end] != '\n')
        end++;
    if (end == start) {
        if (end < input->text_len)
            end++; /* the '\n' */
        else
            return; /* end of buffer: no-op, no chain */
    }
    kill_range(input, start, end, KILL_FORWARD, was_kill, CLIP_KILL, ev);
}

/* C-w (no mark) / Alt+Backspace / Ctrl+Backspace: kill the previous word. */
static void kill_word_backward(TuiTextInput *input, int was_kill, ClipEvent *ev)
{
    if (input->cursor_byte == 0)
        return;

    size_t pos = input->cursor_byte;

    /* Skip non-word characters before the word */
    while (pos > 0 && !is_word_char(input, input->text[pos - 1]))
        pos--;

    /* Skip the word itself */
    while (pos > 0 && is_word_char(input, input->text[pos - 1]))
        pos--;

    kill_range(input, pos, input->cursor_byte, KILL_BACKWARD, was_kill,
               CLIP_KILL, ev);
}

/* C-u / Cmd+Backspace: kill back to the start of the line. */
static void kill_to_line_start(TuiTextInput *input, int was_kill,
                               ClipEvent *ev)
{
    size_t start = input->cursor_byte;
    while (start > 0 && input->text[start - 1] != '\n')
        start--;
    kill_range(input, start, input->cursor_byte, KILL_BACKWARD, was_kill,
               CLIP_KILL, ev);
}

/* Transpose the two clusters before the cursor (Ctrl+T) */
static void transpose_chars(TuiTextInput *input)
{
    if (input->cursor_byte == 0 || input->text_len < 2)
        return;

    /* If at end of text, transpose the two clusters before cursor.
     * Otherwise, transpose the cluster before and at cursor, then
     * advance. Clusters, not codepoints: swapping a base with its
     * combining mark (or half a ZWJ sequence) is not an edit a user can
     * intend. */
    size_t pos = input->cursor_byte;
    if (pos >= input->text_len)
        pos = input->text_len; /* at end */

    /* Find the two clusters to swap */
    size_t c2_start, c2_end, c1_start;

    if (pos >= input->text_len) {
        /* At end: swap the last two clusters */
        c2_end = input->text_len;
        c2_start = tui_prev_cluster(input->text, input->text_len, c2_end);
        c1_start = tui_prev_cluster(input->text, input->text_len, c2_start);
    } else {
        /* In middle: swap the cluster before the cursor with the one at
         * the cursor */
        c2_start = pos;
        size_t clen = 0;
        (void)tui_next_cluster(input->text + pos, input->text_len - pos,
                               &clen);
        if (clen == 0)
            clen = 1;
        c2_end = pos + clen;
        if (c2_end > input->text_len)
            c2_end = input->text_len;
        c1_start = tui_prev_cluster(input->text, input->text_len, c2_start);
    }

    size_t c1_len = c2_start - c1_start;
    size_t c2_len = c2_end - c2_start;

    if (c1_len == 0 || c2_len == 0)
        return;

    /* Swap using a temp buffer; a ZWJ cluster can exceed the tiny stack
     * buffer two ASCII characters need. */
    char stack_tmp[64];
    char *tmp = stack_tmp;
    if (c1_len + c2_len > sizeof(stack_tmp)) {
        tmp = (char *)malloc(c1_len + c2_len);
        if (!tmp)
            return;
    }

    memcpy(tmp, input->text + c2_start, c2_len);
    memcpy(tmp + c2_len, input->text + c1_start, c1_len);
    memcpy(input->text + c1_start, tmp, c1_len + c2_len);
    input->revision++;
    if (tmp != stack_tmp)
        free(tmp);

    input->cursor_byte = c2_end;
    clear_mark_if_set(input);
    recalculate_cursor_position(input);
}

/* Capture pre-edit state into the reusable snapshot buffer */
static inline void undo_snapshot(TuiTextInput *input)
{
    size_t needed = input->text_len + 1;
    if (input->snap_buf_cap < needed) {
        size_t new_cap = input->snap_buf_cap == 0 ? 256 : input->snap_buf_cap;
        while (new_cap < needed)
            new_cap *= 2;
        char *new_buf = (char *)realloc(input->snap_buf, new_cap);
        if (!new_buf)
            return;
        input->snap_buf = new_buf;
        input->snap_buf_cap = new_cap;
    }
    memcpy(input->snap_buf, input->text, input->text_len + 1);
    input->snap_len = input->text_len;
    input->snap_cursor = input->cursor_byte;
    input->snap_valid = 1;
}

/* Push snapshot onto undo stack only if text actually changed */
static void undo_commit(TuiTextInput *input)
{
    if (!input->snap_valid)
        return;
    input->snap_valid = 0;

    /* Text unchanged — nothing to do */
    if (input->snap_len == input->text_len &&
        memcmp(input->snap_buf, input->text, input->snap_len) == 0) {
        return;
    }

    /* Text was modified — reset history navigation so next arrow-up
     * treats current text as a new prefix search. */
    if (input->history_pos != -1) {
        input->history_pos = -1;
        free(input->saved_input);
        input->saved_input = NULL;
    }

    /* Grow stack if needed */
    if (input->undo_count >= input->undo_cap) {
        int new_cap = input->undo_cap == 0 ? 32 : input->undo_cap * 2;
        void *new_stack =
            realloc(input->undo_stack, new_cap * sizeof(*input->undo_stack));
        if (!new_stack)
            return;
        input->undo_stack = new_stack;
        input->undo_cap = new_cap;
    }

    /* Copy snapshot into the undo stack */
    char *copy = (char *)malloc(input->snap_len + 1);
    if (!copy)
        return;
    memcpy(copy, input->snap_buf, input->snap_len + 1);

    int idx = input->undo_count;
    input->undo_stack[idx].text = copy;
    input->undo_stack[idx].text_len = input->snap_len;
    input->undo_stack[idx].cursor_byte = input->snap_cursor;
    input->undo_count++;
}

/* Restore most recent undo snapshot */
static void undo_pop(TuiTextInput *input)
{
    if (input->undo_count == 0)
        return;

    int idx = input->undo_count - 1;

    /* Restore state */
    if (ensure_capacity(input, input->undo_stack[idx].text_len + 1) < 0)
        return;
    memcpy(input->text, input->undo_stack[idx].text,
           input->undo_stack[idx].text_len + 1);
    input->text_len = input->undo_stack[idx].text_len;
    input->revision++;
    input->cursor_byte = input->undo_stack[idx].cursor_byte;
    if (input->cursor_byte > input->text_len)
        input->cursor_byte = input->text_len;
    recalculate_cursor_position(input);

    free(input->undo_stack[idx].text);
    input->undo_count--;
}

/* Free the undo stack */
static void undo_free(TuiTextInput *input)
{
    for (int i = 0; i < input->undo_count; i++) {
        free(input->undo_stack[i].text);
    }
    free(input->undo_stack);
    input->undo_stack = NULL;
    input->undo_count = 0;
    input->undo_cap = 0;
}

/* Check if a history entry matches the current prefix filter */
static int history_matches_prefix(const char *entry, const char *prefix,
                                  size_t prefix_len)
{
    if (prefix_len == 0)
        return 1;
    return strncmp(entry, prefix, prefix_len) == 0;
}

/* Navigate to previous history entry (Up arrow / Ctrl+P)
 * When the user has typed a prefix before navigating, only visit
 * history entries that start with that prefix. */
static void history_prev(TuiTextInput *input)
{
    if (!input->history || input->history_count == 0)
        return;

    undo_free(input);
    input->snap_valid = 0;

    /* Save current input if we're at position -1 */
    if (input->history_pos == -1) {
        free(input->saved_input);
        input->saved_input = strdup(input->text);
    }

    /* Search for next matching entry */
    const char *prefix = input->saved_input ? input->saved_input : "";
    size_t prefix_len = strlen(prefix);

    for (int i = input->history_pos + 1; i < input->history_count; i++) {
        if (history_matches_prefix(input->history[i], prefix, prefix_len)) {
            input->history_pos = i;
            tui_textinput_set_text(input, input->history[i]);
            return;
        }
    }
}

/* Navigate to next history entry (Down arrow / Ctrl+N)
 * Respects the same prefix filter as history_prev. */
static void history_next(TuiTextInput *input)
{
    if (input->history_pos < 0)
        return;

    undo_free(input);
    input->snap_valid = 0;

    const char *prefix = input->saved_input ? input->saved_input : "";
    size_t prefix_len = strlen(prefix);

    /* Search for previous matching entry (toward more recent) */
    for (int i = input->history_pos - 1; i >= 0; i--) {
        if (history_matches_prefix(input->history[i], prefix, prefix_len)) {
            input->history_pos = i;
            tui_textinput_set_text(input, input->history[i]);
            return;
        }
    }

    /* No more matches — restore saved input */
    input->history_pos = -1;
    if (input->saved_input) {
        tui_textinput_set_text(input, input->saved_input);
        free(input->saved_input);
        input->saved_input = NULL;
    } else {
        tui_textinput_clear(input);
    }
}

/* Submit current text: clear, reset history, return command */
static TuiUpdateResult line_submit(TuiTextInput *input)
{
    char *line = strdup(input->text);
    tui_textinput_clear(input);
    input->history_pos = -1;
    free(input->saved_input);
    input->saved_input = NULL;
    return tui_update_result(tui_cmd_line_submit(line));
}

/* Replace bytes in text buffer from [start, start+old_len) with new_word */
static void replace_word(TuiTextInput *input, int start, int old_len,
                         const char *new_word)
{
    int new_len = (int)strlen(new_word);
    int tail_start = start + old_len;
    int tail_len = (int)input->text_len - tail_start;
    size_t new_text_len = (size_t)(start + new_len + tail_len);

    if (ensure_capacity(input, new_text_len + 1) < 0)
        return;

    /* Shift tail to make room (or shrink) */
    memmove(input->text + start + new_len, input->text + tail_start,
            (size_t)tail_len);
    /* Copy new word in */
    memcpy(input->text + start, new_word, (size_t)new_len);

    input->text_len = new_text_len;
    input->revision++;
    input->text[input->text_len] = '\0';
    input->cursor_byte = (size_t)(start + new_len);
    clear_mark_if_set(input);
    recalculate_cursor_position(input);
}

/* Create a new text input component */
TuiTextInput *tui_textinput_create(const TuiTextInputConfig *config)
{
    TuiTextInput *input = (TuiTextInput *)malloc(sizeof(TuiTextInput));
    if (!input)
        return NULL;

    memset(input, 0, sizeof(TuiTextInput));
    input->base.type = TEXTINPUT_TYPE_ID;

    /* Allocate initial text buffer */
    input->text_cap = TEXTINPUT_INITIAL_CAP;
    input->text = (char *)malloc(input->text_cap);
    if (!input->text) {
        free(input);
        return NULL;
    }
    input->text[0] = '\0';
    input->text_len = 0;
    input->focused = 1; /* Focused by default */
    input->multiline = 0;
    input->show_prompt = 1;  /* Show prompt by default */
    input->history_pos = -1; /* -1 means we're at current input */
    input->wrap = TUI_TEXTINPUT_CLIP;
    input->geom.sizing = TUI_TEXTINPUT_NATURAL;
    input->geom.place = TUI_TEXTINPUT_INLINE;
    input->geom.scrolloff = 1; /* one row of context by default */

    /* Lipgloss-shaped focused/blurred prompt styles default to plain (no styling). */
    input->focused_prompt_style = tui_style_new();
    input->blurred_prompt_style = tui_style_new();

    /* No word_chars set by default — is_word_char falls back to non-whitespace */
    input->word_chars = NULL;

    /* Apply config if provided */
    if (config) {
        input->prompt = config->prompt;
        if (input->prompt) {
            input->prompt_len = prompt_cols(input->prompt);
        }
        input->multiline = config->multiline;
        input->wrap = config->wrap;
        input->geom = config->geometry;
        if (input->geom.rows == 0)
            input->geom.rows = config->rows;
        /* A zeroed geometry means "the defaults", and the default scrolloff
         * is one row of context. set_geometry() copies literally, so an app
         * that really wants none asks for it there. */
        if (input->geom.scrolloff == 0)
            input->geom.scrolloff = 1;
    }

    invalidate_layout(input);
    return input;
}

/* Free text input component */
void tui_textinput_free(TuiTextInput *input)
{
    if (!input)
        return;
    if (input->text)
        free(input->text);
    if (input->saved_input)
        free(input->saved_input);
    if (input->history) {
        for (int i = 0; i < input->history_count; i++) {
            free(input->history[i]);
        }
        free(input->history);
    }
    free(input->kill_buf);
    free(input->word_chars);
    free(input->snap_buf);
    free(input->spans);
    layout_free(&input->layout);
    undo_free(input);
    free(input);
}

/* --- Windows / macOS editing keys ------------------------------------
 *
 * The platform never decides a binding: a Mac user over ssh to a Linux
 * box must still get Mac keys, so an app asks for what it wants and boba
 * accepts both encodings at once. Modifiers decide; where they collide
 * with a readline/emacs binding, the readline binding wins (it is
 * handled by the switch below, which the resolver leaves alone).
 *
 * META is Cmd/Super. The switch below has no entry for most of these
 * chords, so the resolver claims them BEFORE it; every existing emacs
 * binding is untouched, and Ctrl+C / Ctrl+D keep their parser-level
 * meanings (interrupt / EOF). */

/* Move to the very start / end of the buffer. */
static void buffer_home(TuiTextInput *input)
{
    input->cursor_byte = 0;
    recalculate_cursor_position(input);
}

static void buffer_end(TuiTextInput *input)
{
    input->cursor_byte = input->text_len;
    recalculate_cursor_position(input);
}

/* Is there a line below the cursor's? */
static int has_next_line(const TuiTextInput *input)
{
    for (size_t i = input->cursor_byte; i < input->text_len; i++) {
        if (input->text[i] == '\n')
            return 1;
    }
    return 0;
}

/* Ctrl+Delete / Alt+d: kill from the cursor to the end of the next word. */
static void kill_word_forward(TuiTextInput *input, int was_kill, ClipEvent *ev)
{
    if (input->cursor_byte >= input->text_len)
        return;

    size_t pos = input->cursor_byte;

    /* Skip the current word */
    while (pos < input->text_len && is_word_char(input, input->text[pos]))
        pos++;

    /* Skip the punctuation/spacing after it, as the backward scan does */
    while (pos < input->text_len && !is_word_char(input, input->text[pos]))
        pos++;

    kill_range(input, input->cursor_byte, pos, KILL_FORWARD, was_kill,
               CLIP_KILL, ev);
}

/* Cmd+A / the Ctrl+A knob: select everything. Shift-made, so the next
 * insertion replaces it and the next plain motion collapses it. */
static void select_all(TuiTextInput *input)
{
    input->mark_byte = 0;
    input->cursor_byte = input->text_len;
    input->has_mark = 1;
    input->mark_from_shift = 1;
    recalculate_cursor_position(input);
}

/* Cmd+C / Ctrl+Insert / Ctrl+Shift+C: fill `ev` with the selection, and
 * ONLY the selection. No selection copies nothing — whatever happens on a
 * terminal, an explicit copy never falls back to the whole input (M-w
 * does, and keeps doing so). The selection stays: a copy is not an edit. */
static void copy_selection(TuiTextInput *input, ClipEvent *ev)
{
    size_t start = 0, end = 0;
    if (!selection_range(input, &start, &end) || end <= start)
        return;
    ev->start = start;
    ev->end = end;
    ev->kind = CLIP_COPY;
}

/* Cmd+X / Shift+Delete: cut the selection. Nothing without one. */
static void cut_selection(TuiTextInput *input, ClipEvent *ev)
{
    size_t start = 0, end = 0;
    if (!selection_range(input, &start, &end) || end <= start)
        return;
    /* was_kill = 0: a cut always starts a fresh ring, so the delivered
     * text is exactly the cut bytes. */
    kill_range(input, start, end, KILL_BACKWARD, 0, CLIP_CUT, ev);
}

/* M-w: copy the selection, or (no mark) the whole input — the
 * viewport's "no-mark = current line" rule. This fallback lives on M-w
 * only; see copy_selection(). */
static void copy_region_or_all(TuiTextInput *input, ClipEvent *ev)
{
    size_t start = 0, end = 0;
    if (selection_range(input, &start, &end) && end > start) {
        ev->start = start;
        ev->end = end;
        ev->kind = CLIP_COPY;
    } else if (input->text_len > 0) {
        ev->start = 0;
        ev->end = input->text_len;
        ev->kind = CLIP_COPY;
    }
    clear_mark_if_set(input);
}

/* The Windows/macOS editing chords, resolved in one place BEFORE the
 * readline switch. Returns 1 when the key was fully handled; 0 lets the
 * switch (emacs bindings, plain motion, plain editing) have it.
 *
 * One case per key, and the conditions are modifier tests rather than a
 * key table: the same physical chord arrives as a bare rune from a
 * legacy terminal and as a key event with real modifiers under the kitty
 * protocol, and both land here. */
static int edit_shortcut(TuiTextInput *input, const TuiKeyMsg *key,
                         int was_kill, ClipEvent *ev)
{
    int shift = (key->mods & TUI_MOD_SHIFT) != 0;
    int word = (key->mods & (TUI_MOD_CTRL | TUI_MOD_ALT)) != 0;
    int cmd = (key->mods & TUI_MOD_META) != 0;

    switch (key->key) {
    case TUI_KEY_LEFT:
        if (!word && !cmd)
            return 0; /* plain / shift only: the generic motion path */
        begin_motion(input, shift);
        if (cmd)
            cursor_home(input);
        else
            cursor_word_left(input);
        return 1;

    case TUI_KEY_RIGHT:
        if (!word && !cmd)
            return 0;
        begin_motion(input, shift);
        if (cmd)
            cursor_end(input);
        else
            cursor_word_right(input);
        return 1;

    case TUI_KEY_UP:
        if (cmd) {
            begin_motion(input, shift);
            buffer_home(input);
            return 1;
        }
        if (shift && !word) {
            /* Shift+Up selects by line, and CLAMPS at the first one
             * instead of walking into the history. */
            begin_motion(input, shift);
            if (input->cursor_row > 0)
                cursor_up(input);
            else
                buffer_home(input);
            return 1;
        }
        return 0;

    case TUI_KEY_DOWN:
        if (cmd) {
            begin_motion(input, shift);
            buffer_end(input);
            return 1;
        }
        if (shift && !word) {
            begin_motion(input, shift);
            if (has_next_line(input))
                cursor_down(input);
            else
                buffer_end(input);
            return 1;
        }
        return 0;

    case TUI_KEY_HOME:
        begin_motion(input, shift);
        if (word)
            buffer_home(input);
        else
            cursor_home(input);
        return 1;

    case TUI_KEY_END:
        begin_motion(input, shift);
        if (word)
            buffer_end(input);
        else
            cursor_end(input);
        return 1;

    case TUI_KEY_BACKSPACE:
        if (cmd) {
            kill_to_line_start(input, was_kill, ev); /* Cmd+Backspace */
            return 1;
        }
        if (word) {
            kill_word_backward(input, was_kill, ev); /* Ctrl/Alt/ESC-DEL */
            return 1;
        }
        return 0;

    case TUI_KEY_DELETE:
        if (shift && !word) {
            cut_selection(input, ev); /* Shift+Delete cuts, if there is a selection */
            return 1;
        }
        if (word) {
            kill_word_forward(input, was_kill, ev); /* Ctrl+Delete */
            return 1;
        }
        return 0;

    case TUI_KEY_INSERT:
        if (key->mods & TUI_MOD_CTRL) {
            copy_selection(input, ev); /* Ctrl+Insert copies, if selected */
            return 1;
        }
        if (shift) {
            ev->kind = CLIP_PASTE_REQUEST; /* Shift+Insert asks for a paste */
            return 1;
        }
        return 0;

    default:
        break;
    }

    if (key->key != TUI_KEY_NONE)
        return 0;

    /* Rune chords: the modifiers are all that tell them apart. */
    uint32_t r = key->rune;
    char lower = (r >= 'A' && r <= 'Z') ? (char)(r - 'A' + 'a') : (char)r;

    if (cmd) {
        switch (lower) {
        case 'a':
            select_all(input); /* Cmd+A */
            return 1;
        case 'c':
            copy_selection(input, ev); /* Cmd+C */
            return 1;
        case 'x':
            cut_selection(input, ev); /* Cmd+X */
            return 1;
        case 'v':
            ev->kind = CLIP_PASTE_REQUEST; /* Cmd+V */
            return 1;
        case 'z':
            if (!shift) { /* Cmd+Z; Cmd+Shift+Z (redo) is unbound */
                undo_pop(input);
                input->snap_valid = 0;
            }
            return 1;
        default:
            /* SWALLOW every other Cmd+rune. Without this arm the rune
             * falls through to insert_codepoint() and Cmd+Q types "q". */
            return 1;
        }
    }

    if ((key->mods & TUI_MOD_CTRL) && shift) {
        if (lower == 'c') {
            copy_selection(input, ev); /* Ctrl+Shift+C */
            return 1;
        }
        if (lower == 'v') {
            ev->kind = CLIP_PASTE_REQUEST; /* Ctrl+Shift+V */
            return 1;
        }
        return 0; /* other Ctrl+Shift chords keep their current reading */
    }

    if ((key->mods & TUI_MOD_CTRL) && lower == 'z' &&
        !(key->mods & TUI_MOD_ALT)) {
        undo_pop(input);
        /* undo_pop() edited the text: drop the snapshot this key took, or
         * undo_commit() would push the pre-undo state as a NEW undo entry
         * and the stack would never shrink. */
        input->snap_valid = 0;
        return 1;
    }

    if ((key->mods & TUI_MOD_ALT) && !(key->mods & TUI_MOD_CTRL)) {
        switch (lower) {
        case 'b':
            begin_motion(input, shift);
            cursor_word_left(input);
            return 1;
        case 'f':
            begin_motion(input, shift);
            cursor_word_right(input);
            return 1;
        case 'd':
            kill_word_forward(input, was_kill, ev);
            return 1;
        case 'w':
            copy_region_or_all(input, ev);
            return 1;
        default:
            return 0; /* Alt+other keeps today's swallow in the switch */
        }
    }

    return 0;
}

/* Update text input with message */
TuiUpdateResult tui_textinput_update(TuiTextInput *input, TuiMsg msg)
{
    if (!input)
        return tui_update_result_none();

    /* The terminal size is environment, not focus: a resize while the input
     * is blurred still moves its window, so the model is correct when focus
     * comes back. It is handled before the focus gate for that reason. */
    if (msg.type == TUI_MSG_WINDOW_SIZE) {
        input->term_w = msg.data.size.width > 0 ? msg.data.size.width : 0;
        input->term_h = msg.data.size.height > 0 ? msg.data.size.height : 0;
        invalidate_layout(input);
        if (input->wrap != TUI_TEXTINPUT_WRAP)
            handle_overflow(input);
        ensure_cursor_visible(input);
        return tui_update_result_none();
    }

    if (msg.type == TUI_MSG_FOCUS) {
        input->focused = 1;
        return tui_update_result_none();
    }

    if (msg.type == TUI_MSG_BLUR) {
        input->focused = 0;
        return tui_update_result_none();
    }

    if (!input->focused)
        return tui_update_result_none();

    /* Bracketed paste is text, not keystrokes: it is inserted as one
     * edit (one undo entry), and a newline in it stays a newline
     * instead of submitting the line. */
    if (msg.type == TUI_MSG_PASTE) {
        undo_snapshot(input);
        insert_paste(input, &msg.data.paste);
        undo_commit(input);
        return tui_update_result_none();
    }

    /* Ctrl+D arrives as TUI_MSG_EOF, a semantic verdict from the input
     * parser — the control byte and the kitty protocol's `CSI 100;5u`
     * alike — never as a ctrl+d key press. readline's meaning for it is
     * delete-char, so that is what the component does; it never decides to
     * quit, because "end of input" is the app's call to make (a shell
     * exits, an editor waits, a MUD client never leaves a session). */
    if (msg.type == TUI_MSG_EOF) {
        undo_snapshot(input);
        delete_at(input);
        undo_commit(input);
        return tui_update_result_none();
    }

    if (msg.type != TUI_MSG_KEY_PRESS)
        return tui_update_result_none();

    /* Ignore key release events (only emitted under Kitty kbd protocol). */
    if (msg.data.key.action == TUI_KEY_ACTION_RELEASE)
        return tui_update_result_none();

    TuiKeyMsg key = msg.data.key;

    /* Handle C-x prefix: if set, check for C-x C-u (undo) */
    if (input->ctrl_x_prefix) {
        input->ctrl_x_prefix = 0;
        if ((key.mods & TUI_MOD_CTRL) && (key.rune == 'u' || key.rune == 'U') &&
            key.key == TUI_KEY_NONE) {
            undo_pop(input);
            return tui_update_result_none();
        }
        /* Not C-u after C-x: fall through to normal handling */
    }

    /* Snapshot pre-edit state; will be pushed to undo stack only if text changes
     */
    undo_snapshot(input);

    /* Track consecutive kill commands for append behavior */
    int was_kill = input->last_was_kill;
    input->last_was_kill = 0;

    /* What this key press wants done with the clipboard. The edit paths
     * fill it in; it is delivered once, at the tail. */
    ClipEvent ev = { CLIP_NONE, 0, 0 };

    /* Windows/macOS editing chords first: the switch below has no entry
     * for most of them, and every entry it DOES have keeps precedence
     * (readline wins, and Ctrl+C / Ctrl+D never reach here as keys). */
    if (edit_shortcut(input, &key, was_kill, &ev))
        goto done;

    /* Handle special keys */
    switch (key.key) {
    case TUI_KEY_LEFT:
        begin_motion(input, key.mods & TUI_MOD_SHIFT);
        if (key.mods & TUI_MOD_CTRL)
            cursor_word_left(input);
        else
            cursor_left(input);
        break;

    case TUI_KEY_RIGHT:
        begin_motion(input, key.mods & TUI_MOD_SHIFT);
        if (key.mods & TUI_MOD_CTRL)
            cursor_word_right(input);
        else
            cursor_right(input);
        break;

    case TUI_KEY_UP:
        begin_motion(input, key.mods & TUI_MOD_SHIFT);
        if (input->multiline && input->cursor_row > 0) {
            cursor_up(input);
        } else {
            history_prev(input);
        }
        break;

    case TUI_KEY_DOWN:
        begin_motion(input, key.mods & TUI_MOD_SHIFT);
        if (input->multiline) {
            /* Check if there's a next line */
            size_t pos = input->cursor_byte;
            while (pos < input->text_len && input->text[pos] != '\n')
                pos++;
            if (pos < input->text_len) {
                cursor_down(input);
                break;
            }
        }
        history_next(input);
        break;

    case TUI_KEY_TAB:
    {
        /* Shift+Tab is not bound by textinput; let the parent route it. */
        if (key.mods & TUI_MOD_SHIFT)
            return tui_update_result_none();
        int word_start = 0;
        char *prefix = tui_textinput_word_at_cursor(input, &word_start);
        if (prefix)
            return tui_update_result(tui_cmd_tab_complete(prefix, word_start));
        break;
    }

    case TUI_KEY_HOME:
        begin_motion(input, key.mods & TUI_MOD_SHIFT);
        cursor_home(input);
        break;

    case TUI_KEY_PAGE_UP:
    {
        /* A page is a screenful of motion, not a scroll: the cursor moves
         * by H - 1 visual rows and the window follows it. */
        int page = visible_rows(input) - 1;
        if (page < 1)
            page = 1;
        begin_motion(input, key.mods & TUI_MOD_SHIFT);
        cursor_rows(input, -page);
        break;
    }

    case TUI_KEY_PAGE_DOWN:
    {
        int page = visible_rows(input) - 1;
        if (page < 1)
            page = 1;
        begin_motion(input, key.mods & TUI_MOD_SHIFT);
        cursor_rows(input, page);
        break;
    }

    case TUI_KEY_END:
        begin_motion(input, key.mods & TUI_MOD_SHIFT);
        cursor_end(input);
        break;

    case TUI_KEY_BACKSPACE:
        delete_before(input);
        break;

    case TUI_KEY_ESCAPE:
        /* Esc clears an active selection; otherwise no-op. */
        clear_mark_if_set(input);
        break;

    case TUI_KEY_DELETE:
        delete_at(input);
        break;

    case TUI_KEY_ENTER:
        if (input->multiline && (key.mods & TUI_MOD_SHIFT)) {
            /* Shift+Enter in multiline: insert newline */
            insert_codepoint(input, '\n');
        } else {
            return line_submit(input);
        }
        break;

    case TUI_KEY_NONE:
        /* Regular character */
        if (key.rune >= 0x20 || key.rune == '\t') {
            /* Skip control characters with Ctrl modifier (except Tab) */
            if ((key.mods & TUI_MOD_CTRL) && key.rune != '\t') {
                /* Handle Ctrl+key shortcuts */
                if (key.rune == ' ') {
                    /* Ctrl+Space: toggle selection mark at cursor. */
                    if (input->has_mark) {
                        input->has_mark = 0;
                    } else {
                        input->mark_byte = input->cursor_byte;
                        input->has_mark = 1;
                    }
                } else if (key.rune == 'a' || key.rune == 'A') {
                    /* Ctrl+A: select-all when the app asked for Windows
                     * semantics, otherwise move to the start of the line
                     * (readline). Never platform-derived — see
                     * tui_textinput_set_ctrl_a_select_all(). */
                    if (input->ctrl_a_select_all)
                        select_all(input);
                    else
                        cursor_home(input);
                } else if (key.rune == 'b' || key.rune == 'B') {
                    /* Ctrl+B: Move back one character */
                    cursor_left(input);
                } else if (key.rune == 'd' || key.rune == 'D') {
                    /* Ctrl+D: delete character under cursor. The parser
                     * never emits this as a key press — it sends
                     * TUI_MSG_EOF, handled above — but a host that
                     * re-issues the verdict as a key (or a test that
                     * injects one) gets the same edit. The textinput never
                     * decides to quit. */
                    delete_at(input);
                } else if (key.rune == 'e' || key.rune == 'E') {
                    /* Ctrl+E: Move to end of line */
                    cursor_end(input);
                } else if (key.rune == 'f' || key.rune == 'F') {
                    /* Ctrl+F: Move forward one character */
                    cursor_right(input);
                } else if (key.rune == 'g' || key.rune == 'G') {
                    /* Ctrl+G: clear selection mark if set */
                    clear_mark_if_set(input);
                } else if (key.rune == 'h' || key.rune == 'H') {
                    /* Ctrl+H: Delete previous character (backspace) */
                    delete_before(input);
                } else if (key.rune == 'j' || key.rune == 'J') {
                    /* Ctrl+J: Insert newline in multiline, submit in single-line */
                    if (input->multiline) {
                        insert_codepoint(input, '\n');
                    } else {
                        return line_submit(input);
                    }
                } else if (key.rune == 'k' || key.rune == 'K') {
                    /* Ctrl+K: kill to end of line; at EOL take the newline
                     * (joining the next line); at end of buffer, nothing. */
                    kill_line(input, was_kill, &ev);
                } else if (key.rune == 'n' || key.rune == 'N') {
                    /* Ctrl+N: Next history entry (or cursor down if mid-text) */
                    if (input->multiline) {
                        size_t pos = input->cursor_byte;
                        while (pos < input->text_len && input->text[pos] != '\n')
                            pos++;
                        if (pos < input->text_len) {
                            cursor_down(input);
                            break;
                        }
                    }
                    history_next(input);
                } else if (key.rune == 'p' || key.rune == 'P') {
                    /* Ctrl+P: Previous history entry (or cursor up if mid-text) */
                    if (input->multiline && input->cursor_row > 0) {
                        cursor_up(input);
                        break;
                    }
                    history_prev(input);
                } else if (key.rune == 't' || key.rune == 'T') {
                    /* Ctrl+T: Transpose characters before cursor */
                    transpose_chars(input);
                } else if (key.rune == 'u' || key.rune == 'U') {
                    /* Ctrl+U: Kill to start of line */
                    kill_to_line_start(input, was_kill, &ev);
                } else if (key.rune == 'w' || key.rune == 'W') {
                    /* Ctrl+W: kill region (when mark is set) or kill word
                     * backward (default). Matches emacs's overload of
                     * kill-region / backward-kill-word. */
                    size_t sel_start, sel_end;
                    if (selection_range(input, &sel_start, &sel_end) &&
                        sel_end > sel_start) {
                        kill_range(input, sel_start, sel_end, KILL_BACKWARD,
                                   was_kill, CLIP_KILL, &ev);
                    } else {
                        kill_word_backward(input, was_kill, &ev);
                    }
                } else if (key.rune == 'x' || key.rune == 'X') {
                    /* Ctrl+X: prefix for C-x C-u (undo) */
                    input->ctrl_x_prefix = 1;
                } else if (key.rune == 'y' || key.rune == 'Y') {
                    /* Ctrl+Y: Yank (paste from kill buffer) */
                    if (input->kill_buf && input->kill_buf_len > 0) {
                        insert_text(input, input->kill_buf, input->kill_buf_len);
                    }
                } else if (key.rune == '_') {
                    /* Ctrl+_: Undo */
                    undo_pop(input);
                    return tui_update_result_none();
                }
            } else if ((key.mods & TUI_MOD_ALT) && !(key.mods & TUI_MOD_CTRL)) {
                /* An Alt chord the resolver did not claim. Swallowed
                 * rather than inserted as text: M-b/M-f/M-d/M-w and the
                 * Alt+motion keys are handled by edit_shortcut(). */
            } else if (key.text_len > 0) {
                /* The terminal reported what the key produced (the kitty
                 * protocol's associated text): insert THAT, not the key
                 * code — `CSI 97;2;65u` is shift+a, whose key code is
                 * the unshifted 97 while its text is "A". A grapheme (a
                 * dead key plus its base, an IME result) arrives whole. */
                insert_text(input, key.text, (size_t)key.text_len);
            } else if ((key.mods & TUI_MOD_SHIFT) && key.rune >= 'a' &&
                       key.rune <= 'z' && !(key.mods & (TUI_MOD_CTRL | TUI_MOD_ALT))) {
                /* A terminal that reports keys as escape codes without
                 * their text (the "report all keys" flag without the
                 * associated-text one) sends shift+a as the key code 97
                 * plus SHIFT: recover the capital rather than inserting
                 * the unshifted letter. ASCII letters are the case that
                 * is layout-independent — a shifted digit or punctuation
                 * key is left to a terminal that reports its text. */
                insert_codepoint(input, key.rune - 0x20);
            } else {
                insert_codepoint(input, key.rune);
            }
        }
        break;

    default:
        break;
    }

/* Everything above compiles a ClipEvent; delivery happens here, once,
 * after undo_commit(), so the hook sees the final text. */
done:
    undo_commit(input);

    TuiCmd *cmd = NULL;
    if (ev.kind != CLIP_NONE) {
        const char *ptr = NULL;
        size_t len = 0;
        switch (ev.kind) {
        case CLIP_KILL:
        case CLIP_CUT:
            ptr = input->kill_buf;
            len = input->kill_buf_len;
            break;
        case CLIP_COPY:
            ptr = input->text + ev.start;
            len = ev.end - ev.start;
            break;
        case CLIP_PASTE_REQUEST:
        case CLIP_NONE:
            break; /* ptr stays NULL: there is nothing to hand over */
        }

        if (input->clipboard_fn)
            input->clipboard_fn(input, public_clip_kind(ev.kind), ptr, len,
                                input->clipboard_ud);

        /* Off by default: an app opts back in to the old behaviour, and
         * even then only a KILL or a CUT can become a command. */
        if (input->clipboard_cmd && clip_emits_cmd(ev.kind) && ptr && len > 0)
            cmd = tui_cmd_clipboard_copy(ptr, len);
    }
    return tui_update_result(cmd);
}

/* True if a TuiStyle carries any user-visible styling (color or attribute). */
static int style_has_styling(const TuiStyle *s)
{
    if (!s)
        return 0;
    return s->fg.type != TUI_COLOR_NONE || s->bg.type != TUI_COLOR_NONE || s->bold || s->italic || s->underline || s->strikethrough || s->reverse || s->blink || s->faint;
}

/* Pick the right TuiStyle for the input's current focus state. */
static const TuiStyle *prompt_style_for(const TuiTextInput *input)
{
    return input->focused ? &input->focused_prompt_style
                          : &input->blurred_prompt_style;
}

/* Append `content` wrapped in a TuiStyle, or plain when the style carries
 * no styling. A styled render that fails to allocate falls back to the raw
 * text: an unstyled prompt beats a missing one. */
static void emit_styled(DynamicBuffer *out, const TuiStyle *style,
                        const char *content)
{
    if (style_has_styling(style)) {
        TuiStyle inline_style = *style;
        inline_style.inline_ = 1;
        char *rendered = tui_style_render(&inline_style, content);
        if (rendered) {
            dynamic_buffer_append_str(out, rendered);
            free(rendered);
            return;
        }
    }
    dynamic_buffer_append_str(out, content);
}

/* Render continuation prompt or space-padding for lines after the first:
 * the continuation prompt when one is set, otherwise spaces of the main
 * prompt's width, so continuation rows align under the text column. */
static void render_continuation_prompt(const TuiTextInput *input,
                                       DynamicBuffer *out)
{
    if (input->continuation_prompt && input->continuation_prompt_len > 0) {
        emit_styled(out, prompt_style_for(input), input->continuation_prompt);
    } else {
        for (int j = 0; j < input->prompt_len; j++)
            dynamic_buffer_append(out, " ", 1);
    }
}

/* The span covering byte `pos`, or NULL. Spans are sorted and disjoint
 * (set_spans enforces it), so the scan stops at the first span past pos;
 * an input carries a handful, so a linear walk beats anything cleverer. */
static const TuiTextSpan *span_covering(const TuiTextInput *input, size_t pos)
{
    for (size_t k = 0; k < input->n_spans; k++) {
        if (pos < input->spans[k].start)
            return NULL;
        if (pos < input->spans[k].end)
            return &input->spans[k];
    }
    return NULL;
}

/* Are the app's spans describing the text as it is NOW? A span set is
 * stamped with the revision it was computed for; an edit moves the
 * revision, and spans for text that no longer exists are never painted
 * (a stale underline on the wrong bytes is worse than none for the one
 * frame before the app re-declares). */
static int spans_current(const TuiTextInput *input)
{
    return input->n_spans > 0 && input->spans_revision == input->revision;
}

/* Paint one run: the bytes [run, run+len) share one span and one
 * selection state. A run in a span goes through the span's style (with
 * reverse added inside a selection, so the span's own reset cannot end
 * the highlight early); a plain run keeps the selection's SGR toggles. */
static void emit_span_run(DynamicBuffer *out, const char *run, size_t len,
                          const TuiTextSpan *span, int in_sel)
{
    if (len == 0)
        return;
    if (!span) {
        if (in_sel)
            dynamic_buffer_append_str(out, SGR_REVERSE);
        dynamic_buffer_append(out, run, len);
        if (in_sel)
            dynamic_buffer_append_str(out, SGR_REVERSE_OFF);
        return;
    }
    TuiStyle st = span->style;
    st.inline_ = 1;
    if (in_sel)
        st.reverse = 1;
    char *z = (char *)malloc(len + 1);
    char *styled = NULL;
    if (z) {
        memcpy(z, run, len);
        z[len] = '\0';
        styled = tui_style_render(&st, z);
        free(z);
    }
    if (styled) {
        dynamic_buffer_append_str(out, styled);
        free(styled);
    } else {
        dynamic_buffer_append(out, run, len); /* OOM: unstyled beats absent */
    }
}

/* render_text_range when spans are live: walk cluster by cluster
 * (offsets are absolute, so a token wrapped across rows or scrolled
 * half out of the window is styled exactly where its bytes are), cut a
 * run whenever the span or the selection state changes, and compose the
 * two. Echo mode still masks, one '*' per cluster. */
static void render_text_range_spans(const TuiTextInput *input,
                                    DynamicBuffer *out, size_t byte_start,
                                    size_t byte_end)
{
    size_t sel_start = 0, sel_end = 0;
    int has_sel = selection_range(input, &sel_start, &sel_end);

    char *run = (char *)malloc(byte_end - byte_start + 1);
    if (!run)
        return;
    size_t run_len = 0;
    const TuiTextSpan *run_span = NULL;
    int run_sel = 0;

    size_t i = byte_start;
    while (i < byte_end) {
        size_t clen = 0;
        (void)tui_next_cluster(input->text + i, input->text_len - i, &clen);
        if (clen == 0)
            clen = 1;
        if (i + clen > byte_end)
            clen = byte_end - i;

        const TuiTextSpan *sp = span_covering(input, i);
        int in_sel = has_sel && i >= sel_start && i < sel_end;
        if (run_len > 0 && (sp != run_span || in_sel != run_sel)) {
            emit_span_run(out, run, run_len, run_span, run_sel);
            run_len = 0;
        }
        run_span = sp;
        run_sel = in_sel;

        if (input->echo_mode == 1)
            run[run_len++] = '*';
        else {
            memcpy(run + run_len, input->text + i, clen);
            run_len += clen;
        }
        i += clen;
    }
    emit_span_run(out, run, run_len, run_span, run_sel);
    free(run);
}

/* Render input->text[byte_start..byte_end) to `out`, applying SGR_REVERSE
 * over any portion that falls inside the active selection (when has_mark
 * is set). Honors echo_mode by emitting one '*' per grapheme cluster —
 * the unit the cursor and the column math move by — while tracking
 * selection toggles at cluster boundaries, so a mask never splits a
 * cluster or disagrees with the rendered width. */
static void render_text_range(const TuiTextInput *input, DynamicBuffer *out,
                              size_t byte_start, size_t byte_end)
{
    /* When a highlight callback is set, delegate the entire text range
     * to the callback. It returns a malloc'd styled string that we
     * append and free. Selection/echo handling is skipped — the callback
     * owns all styling. */
    if (input->highlight_fn && byte_end > byte_start) {
        char *styled = input->highlight_fn(input->text + byte_start,
                                           byte_end - byte_start,
                                           input->highlight_userdata);
        if (styled) {
            dynamic_buffer_append_str(out, styled);
            free(styled);
        }
        return;
    }

    if (spans_current(input) && byte_end > byte_start) {
        render_text_range_spans(input, out, byte_start, byte_end);
        return;
    }

    size_t sel_start = 0, sel_end = 0;
    int has_sel = selection_range(input, &sel_start, &sel_end);

    int reversed = 0;
    size_t i = byte_start;
    while (i < byte_end) {
        size_t clen = 0;
        (void)tui_next_cluster(input->text + i, input->text_len - i, &clen);
        if (clen == 0)
            clen = 1;
        if (i + clen > byte_end)
            clen = byte_end - i;

        if (has_sel) {
            int in_sel = (i >= sel_start && i < sel_end);
            if (in_sel && !reversed) {
                dynamic_buffer_append_str(out, SGR_REVERSE);
                reversed = 1;
            } else if (!in_sel && reversed) {
                dynamic_buffer_append_str(out, SGR_REVERSE_OFF);
                reversed = 0;
            }
        }

        if (input->echo_mode == 1)
            dynamic_buffer_append(out, "*", 1);
        else
            dynamic_buffer_append(out, input->text + i, clen);

        i += clen;
    }

    if (reversed)
        dynamic_buffer_append_str(out, SGR_REVERSE_OFF);
}

/* Byte range of `row` to paint in CLIP mode: the shared horizontal window
 * [offset, offset_right), in display columns, intersected with the row.
 * Every row scrolls under the same window, so a line never jumps sideways
 * relative to its neighbours just because the cursor is on another line. */
static void clip_range(const TuiTextInput *input, const TuiVisualRow *row,
                       size_t *out_start, size_t *out_end)
{
    *out_start = row->byte_start;
    *out_end = row->byte_end;

    if (input->content_width <= 0 || input->offset_right <= input->offset)
        return; /* unconstrained: the whole line */

    size_t len = row->byte_end - row->byte_start;
    size_t s = fit_cols(input->text + row->byte_start, len, input->offset);
    size_t e = fit_cols(input->text + row->byte_start, len, input->offset_right);

    *out_start = row->byte_start + s;
    *out_end = row->byte_start + e;
}

/* The prefix painted on a row: the main prompt on the first row of logical
 * line 0, the continuation prompt (or blank indent) everywhere else. A row
 * whose prompt does not fit at all carries prompt_w == 0 and gets none. */
static void emit_row_prefix(const TuiTextInput *input, DynamicBuffer *out,
                            const TuiVisualRow *row)
{
    if (row->prompt_w <= 0 || !input->show_prompt || !input->prompt ||
        input->prompt_len <= 0)
        return;

    if (row->logical_line == 0 && row->first_of_line) {
        emit_styled(out, prompt_style_for(input), input->prompt);
    } else {
        render_continuation_prompt(input, out);
    }
}

/* Erase rows the previous frame painted and this one does not.
 *
 * After an absolute-mode render has drawn rows [first_row, first_row + H),
 * any rows within the previous frame's extent but beyond this one are
 * blanked: the runtime does not clear stale alt-screen rows, so the
 * component owns cleanup of its own area. The loop is clamped to the
 * screen, so a frame that shrank because the terminal shrank cannot target
 * rows that no longer exist. */
static void erase_surplus(TuiTextInput *input, DynamicBuffer *out, int H,
                          int prev_rows)
{
    if (prev_rows <= H)
        return;

    int from = input->first_row + H;
    int to = input->first_row + prev_rows;
    if (input->term_h > 0 && to > input->term_h + 1)
        to = input->term_h + 1;

    char pos_buf[32];
    for (int r = from; r < to; r++) {
        snprintf(pos_buf, sizeof(pos_buf), CSI "%d;1H", r);
        dynamic_buffer_append_str(out, pos_buf);
        dynamic_buffer_append_str(out, EL_TO_END);
    }
}

/* Render text input to output buffer.
 *
 * ONE path for every mode. The layout decides what the rows are, the window
 * decides which of them are visible, and the only thing the placement
 * changes is how each row starts:
 *
 *   AT_ROW  an absolute CSI <row>;1H, so the rows always land inside the
 *           screen no matter what the cursor did last frame
 *   INLINE  "\r" for the first row and "\r\n" for the rest, which is what
 *           the runtime counts to place the cursor; H rows must therefore
 *           always be <= the screen
 *
 * A focused input's cursor is kept inside [top, top + H) by
 * ensure_cursor_visible(), so the rows painted and the cursor reported by
 * tui_textinput_cursor_pos() describe the same window.
 *
 * Surrounding decoration (border lines, status bars) is the parent's
 * responsibility — see tui_border_render_horizontal() for the lipgloss
 * idiom.
 */
void tui_textinput_view(const TuiTextInput *input, DynamicBuffer *out)
{
    if (!input || !out)
        return;

    /* view() updates the window bookkeeping (last_render_rows) and may
     * build the layout cache. The pointer is const-qualified for callers,
     * but this state is the component's own. */
    TuiTextInput *mutable_input = (TuiTextInput *)input;

    const TuiLayout *L = layout_get(mutable_input);
    int H = visible_rows(mutable_input);
    size_t top = top_row(L, mutable_input);
    int absolute = mutable_input->first_row > 0;
    int wrap = input->wrap == TUI_TEXTINPUT_WRAP && input->content_width > 0;
    int prev_rows = mutable_input->last_render_rows;

    char pos_buf[32];

    for (int i = 0; i < H; i++) {
        if (absolute) {
            snprintf(pos_buf, sizeof(pos_buf), CSI "%d;1H",
                     mutable_input->first_row + i);
            dynamic_buffer_append_str(out, pos_buf);
        } else {
            dynamic_buffer_append_str(out, i == 0 ? "\r" : "\r\n");
        }
        dynamic_buffer_append_str(out, EL_TO_END);

        size_t idx = top + (size_t)i;
        if (idx >= L->n_rows)
            continue; /* allocation failure: paint blank rows, not garbage */

        const TuiVisualRow *row = &L->rows[idx];
        emit_row_prefix(input, out, row);

        size_t range_start, range_end;
        if (wrap) {
            range_start = row->byte_start;
            range_end = row->byte_end;
        } else {
            clip_range(input, row, &range_start, &range_end);
        }
        if (range_end > range_start)
            render_text_range(input, out, range_start, range_end);
    }

    if (absolute)
        erase_surplus(mutable_input, out, H, prev_rows);

    /* Only an absolute frame is erased later: an inline frame's rows are
     * tracked by the runtime, and a leftover count here would let a later
     * AT_ROW placement erase rows that are not ours. */
    mutable_input->last_render_rows = absolute ? H : 0;
}

/* Get current text content */
const char *tui_textinput_text(const TuiTextInput *input)
{
    return input ? input->text : NULL;
}

/* Get text length */
size_t tui_textinput_len(const TuiTextInput *input)
{
    return input ? input->text_len : 0;
}

/* Set text content */
void tui_textinput_set_text(TuiTextInput *input, const char *text)
{
    if (!input)
        return;

    if (!text || *text == '\0') {
        tui_textinput_clear(input);
        return;
    }

    size_t len = strlen(text);
    if (ensure_capacity(input, len + 1) < 0)
        return;

    memcpy(input->text, text, len);
    input->text[len] = '\0';
    input->text_len = len;
    input->revision++;
    input->cursor_byte = len;
    clear_mark_if_set(input);
    recalculate_cursor_position(input);
}

/* Clear text content */
void tui_textinput_clear(TuiTextInput *input)
{
    if (!input)
        return;
    input->text_len = 0;
    input->revision++;
    input->cursor_byte = 0;
    input->cursor_row = 0;
    input->cursor_col = 0;
    input->offset = 0;
    input->offset_right = 0;
    input->top_byte = 0;
    if (input->text)
        input->text[0] = '\0';
    clear_mark_if_set(input);
    undo_free(input);
    ensure_cursor_visible(input);
}

/* Check if focused */
int tui_textinput_is_focused(const TuiTextInput *input)
{
    return input ? input->focused : 0;
}

/* Get cursor position */
size_t tui_textinput_cursor(const TuiTextInput *input)
{
    return input ? input->cursor_byte : 0;
}

/* Set cursor position */
void tui_textinput_set_cursor(TuiTextInput *input, size_t pos)
{
    if (!input)
        return;
    if (pos > input->text_len)
        pos = input->text_len;
    input->cursor_byte = pos;
    recalculate_cursor_position(input);
}

/* Get number of lines */
int tui_textinput_line_count(const TuiTextInput *input)
{
    if (!input || input->text_len == 0)
        return 1;

    int count = 1;
    for (size_t i = 0; i < input->text_len; i++) {
        if (input->text[i] == '\n')
            count++;
    }
    return count;
}

/* Count visual rows for a single logical line (soft-wrap aware).
 * When soft_wrap is off, always 1. When on, the line is walked in
 * content_width display-column chunks, exactly as the renderer paints it. */
/* --- window accessors -------------------------------------------------
 *
 * What an app can ask about the window, answered from the same layout and
 * the same top_row() the renderer uses, so the numbers it draws chrome
 * from can never describe a different frame than the one on screen. */

int tui_textinput_total_rows(const TuiTextInput *input)
{
    if (!input)
        return 1;
    const TuiLayout *L = layout_get_c(input);
    return L->n_rows > 0 ? (int)L->n_rows : 1;
}

int tui_textinput_visible_rows(const TuiTextInput *input)
{
    if (!input)
        return 1;
    return visible_rows(input);
}

TuiTextInputScroll tui_textinput_scroll_state(const TuiTextInput *input)
{
    TuiTextInputScroll s = { 1, 1, 0, 0, 0, 0, 0 };
    if (!input)
        return s;

    const TuiLayout *L = layout_get_c(input);
    int total = L->n_rows > 0 ? (int)L->n_rows : 1;
    int H = visible_rows(input);
    size_t top = top_row(L, input);

    s.total_rows = total;
    s.visible_rows = H;
    s.first_row = (int)top;
    s.hidden_above = (int)top;
    s.hidden_below = total - H - (int)top;
    if (s.hidden_below < 0)
        s.hidden_below = 0;
    s.offset_left = input->offset;
    s.offset_right = input->offset_right;
    return s;
}

void tui_textinput_cursor_visual(const TuiTextInput *input, int *row, int *col)
{
    if (row)
        *row = 0;
    if (col)
        *col = 0;
    if (!input || !input->focused)
        return;

    const TuiLayout *L = layout_get_c(input);
    int H = visible_rows(input);
    size_t top = top_row(L, input);
    if (L->n_rows == 0 || L->cur_row < top ||
        L->cur_row >= top + (size_t)H)
        return; /* outside the window: nothing sane to report */

    int wrap = input->wrap == TUI_TEXTINPUT_WRAP && input->content_width > 0;
    if (row)
        *row = (int)(L->cur_row - top);
    if (col) {
        int c = L->rows[L->cur_row].prompt_w + L->cur_col -
                (wrap ? 0 : input->offset);
        *col = c > 0 ? c : 0;
    }
}

int tui_textinput_has_selection(const TuiTextInput *input)
{
    return input && input->has_mark && input->mark_byte != input->cursor_byte;
}

int tui_textinput_has_mark(const TuiTextInput *input)
{
    return input && input->has_mark;
}

size_t tui_textinput_mark(const TuiTextInput *input)
{
    if (!input)
        return 0;
    return input->has_mark ? input->mark_byte : input->cursor_byte;
}

int tui_textinput_selection_range(const TuiTextInput *input, size_t *start,
                                  size_t *end)
{
    if (!tui_textinput_has_selection(input))
        return 0;
    size_t a = input->mark_byte, b = input->cursor_byte;
    if (a > b) {
        size_t t = a;
        a = b;
        b = t;
    }
    if (start)
        *start = a;
    if (end)
        *end = b;
    return 1;
}

int tui_textinput_undo_depth(const TuiTextInput *input)
{
    return input ? input->undo_count : 0;
}

/* Set maximum history size */
void tui_textinput_set_history_size(TuiTextInput *input, int size)
{
    if (!input || size < 0)
        return;

    /* Free existing history if shrinking */
    if (input->history && size < input->history_count) {
        for (int i = size; i < input->history_count; i++) {
            free(input->history[i]);
        }
        input->history_count = size;
    }

    /* Reallocate or allocate history array */
    if (size == 0) {
        free(input->history);
        input->history = NULL;
    } else {
        char **new_history =
            (char **)realloc(input->history, size * sizeof(char *));
        if (new_history) {
            input->history = new_history;
        }
    }
    input->history_size = size;
    input->history_pos = -1;
}

/* Add a line to history */
void tui_textinput_history_add(TuiTextInput *input, const char *line)
{
    if (!input || !line || !line[0] || input->history_size == 0)
        return;

    /* Allocate history array if needed */
    if (!input->history) {
        input->history = (char **)calloc(input->history_size, sizeof(char *));
        if (!input->history)
            return;
    }

    /* Remove any existing duplicate entry (squash) */
    for (int i = 0; i < input->history_count; i++) {
        if (input->history[i] && strcmp(input->history[i], line) == 0) {
            free(input->history[i]);
            /* Shift entries up to fill the gap */
            for (int j = i; j < input->history_count - 1; j++) {
                input->history[j] = input->history[j + 1];
            }
            input->history[input->history_count - 1] = NULL;
            input->history_count--;
            break;
        }
    }

    /* Make room for new entry at position 0 */
    if (input->history_count >= input->history_size) {
        /* Free oldest entry */
        free(input->history[input->history_size - 1]);
        input->history_count = input->history_size - 1;
    }

    /* Shift existing entries down */
    for (int i = input->history_count; i > 0; i--) {
        input->history[i] = input->history[i - 1];
    }

    /* Add new entry at position 0 */
    input->history[0] = strdup(line);
    if (input->history[0]) {
        input->history_count++;
    }

    /* Reset history position */
    input->history_pos = -1;
}

/* Insert a completion word, replacing the current word from word_start to cursor */
void tui_textinput_insert_completion(TuiTextInput *input, int word_start,
                                     const char *word)
{
    if (!input || !word)
        return;
    if (word_start < 0)
        word_start = 0;
    tui_textinput_replace_range(input, (size_t)word_start, input->cursor_byte,
                                word);
}

char *tui_textinput_word_at_cursor(const TuiTextInput *input, int *word_start)
{
    if (word_start)
        *word_start = -1;
    if (!input || !word_start)
        return NULL;

    /* Cursor must be within or at end of a word */
    if (input->cursor_byte == 0)
        return NULL;

    /* The character just before cursor must be a word char */
    if (!is_word_char(input, input->text[input->cursor_byte - 1]))
        return NULL;

    /* Scan backward to find word start */
    int start = (int)input->cursor_byte;
    while (start > 0 && is_word_char(input, input->text[start - 1]))
        start--;

    int len = (int)input->cursor_byte - start;
    char *word = (char *)malloc(len + 1);
    if (!word)
        return NULL;
    memcpy(word, input->text + start, len);
    word[len] = '\0';
    *word_start = start;
    return word;
}

/* Set characters that form words for tab completion and word movement */
void tui_textinput_set_word_chars(TuiTextInput *input, const char *chars)
{
    if (!input)
        return;
    free(input->word_chars);
    input->word_chars = chars ? strdup(chars) : NULL;
}

/* Set whether to show the prompt */
void tui_textinput_set_show_prompt(TuiTextInput *input, int show)
{
    if (!input)
        return;
    input->show_prompt = show;
    invalidate_layout(input);
    ensure_cursor_visible(input);
}

/* Set the prompt string */
void tui_textinput_set_prompt(TuiTextInput *input, const char *prompt)
{
    if (!input)
        return;
    input->prompt = prompt;
    input->prompt_len = prompt_cols(prompt);
    /* Prompt widths are part of the layout key. */
    invalidate_layout(input);
}

/* Set the continuation prompt string */
void tui_textinput_set_continuation_prompt(TuiTextInput *input,
                                           const char *prompt)
{
    if (!input)
        return;
    input->continuation_prompt = prompt;
    input->continuation_prompt_len = prompt_cols(prompt);
    invalidate_layout(input);
}

/* Set echo mode */
void tui_textinput_set_echo_mode(TuiTextInput *input, int mode)
{
    if (input)
        input->echo_mode = mode;
}

/* Set wrap mode */
void tui_textinput_set_wrap(TuiTextInput *input, TuiTextInputWrap mode)
{
    if (!input || input->wrap == mode)
        return;
    input->wrap = mode;
    invalidate_layout(input);
    if (mode != TUI_TEXTINPUT_WRAP)
        handle_overflow(input);
    ensure_cursor_visible(input);
}

/* Get echo mode */
int tui_textinput_get_echo_mode(const TuiTextInput *input)
{
    return input ? input->echo_mode : 0;
}

/* Set styled-text highlight callback */
uint64_t tui_textinput_revision(const TuiTextInput *input)
{
    return input ? input->revision : 0;
}

void tui_textinput_replace_range(TuiTextInput *input, size_t start, size_t end,
                                 const char *text)
{
    if (!input || !text)
        return;
    if (end > input->text_len)
        end = input->text_len;
    if (start > end)
        start = end;
    undo_snapshot(input);
    replace_word(input, (int)start, (int)(end - start), text);
    undo_commit(input);
}

int tui_textinput_set_spans(TuiTextInput *input, const TuiTextSpan *spans,
                            size_t n)
{
    if (!input)
        return -1;
    /* Validate before touching anything: sorted, disjoint, non-empty,
     * inside the text. A bad set is refused whole and the old one stays. */
    size_t prev_end = 0;
    for (size_t k = 0; k < n; k++) {
        if (!spans || spans[k].start >= spans[k].end ||
            spans[k].end > input->text_len || spans[k].start < prev_end)
            return -1;
        prev_end = spans[k].end;
    }
    TuiTextSpan *copy = NULL;
    if (n > 0) {
        copy = (TuiTextSpan *)malloc(n * sizeof(*copy));
        if (!copy)
            return -1;
        memcpy(copy, spans, n * sizeof(*copy));
    }
    free(input->spans);
    input->spans = copy;
    input->n_spans = n;
    input->spans_revision = input->revision;
    return 0;
}

void tui_textinput_set_text_renderer(TuiTextInput *input,
                                     char *(*fn)(const char *text,
                                                 size_t len,
                                                 void *userdata),
                                     void *userdata)
{
    if (input) {
        input->highlight_fn = fn;
        input->highlight_userdata = userdata;
    }
}

void tui_textinput_set_clipboard_hook(TuiTextInput *input,
                                      TuiTextInputClipboardFn fn,
                                      void *userdata)
{
    if (input) {
        input->clipboard_fn = fn;
        input->clipboard_ud = userdata;
    }
}

void tui_textinput_set_clipboard_cmd(TuiTextInput *input, int enable)
{
    if (input)
        input->clipboard_cmd = enable ? 1 : 0;
}

const char *tui_textinput_kill_buffer(const TuiTextInput *input, size_t *len)
{
    if (len)
        *len = input ? input->kill_buf_len : 0;
    if (!input)
        return NULL;
    return input->kill_buf;
}

void tui_textinput_set_ctrl_a_select_all(TuiTextInput *input, int enable)
{
    if (input)
        input->ctrl_a_select_all = enable ? 1 : 0;
}

void tui_textinput_set_focused_prompt_style(TuiTextInput *input, TuiStyle s)
{
    if (input)
        input->focused_prompt_style = s;
}

void tui_textinput_set_blurred_prompt_style(TuiTextInput *input, TuiStyle s)
{
    if (input)
        input->blurred_prompt_style = s;
}

/* --- geometry --------------------------------------------------------- */

void tui_textinput_set_geometry(TuiTextInput *input,
                                const TuiTextInputGeometry *g)
{
    if (!input)
        return;
    if (g)
        input->geom = *g;
    else
        memset(&input->geom, 0, sizeof(input->geom));

    invalidate_layout(input);

    /* A new width re-flows the rows under the anchor, and a new box height
     * changes how many of them are visible; both are answered by the same
     * pass, in this order (handle_overflow reads cursor_col). */
    if (input->wrap != TUI_TEXTINPUT_WRAP)
        handle_overflow(input);
    ensure_cursor_visible(input);
}

TuiTextInputGeometry tui_textinput_geometry(const TuiTextInput *input)
{
    TuiTextInputGeometry g;
    memset(&g, 0, sizeof(g));
    if (input)
        g = input->geom;
    return g;
}

void tui_textinput_set_placement(TuiTextInput *input, int row)
{
    if (!input)
        return;
    input->geom.place = row > 0 ? TUI_TEXTINPUT_AT_ROW : TUI_TEXTINPUT_INLINE;
    input->geom.row = row > 0 ? row : 0;
    invalidate_layout(input);
    ensure_cursor_visible(input);
    /* The old screen frame no longer applies, but last_render_rows is
     * deliberately kept: erase_surplus() needs the OLD extent to clean up
     * what the previous placement painted. */
}

void tui_textinput_set_rows(TuiTextInput *input, int rows)
{
    if (!input)
        return;
    input->geom.rows = rows > 0 ? rows : 0;
    invalidate_layout(input);
    ensure_cursor_visible(input);
}

/* Report cursor position for the runtime. The window decides: the cursor's
 * visual row relative to `top`, and its column inside that row, minus the
 * horizontal window in CLIP mode. */
TuiCursor tui_textinput_cursor_pos(const TuiTextInput *input)
{
    if (!input || !input->focused)
        return tui_cursor_hidden();

    const TuiLayout *L = layout_get_c(input);
    int H = visible_rows(input);
    size_t top = top_row(L, input);

    /* Cannot happen after ensure_cursor_visible(), but a hidden cursor is
     * the right answer if it ever does: a cursor placed outside the window
     * would paint a row this frame never wrote. */
    if (L->n_rows == 0 || L->cur_row < top ||
        L->cur_row >= top + (size_t)H)
        return tui_cursor_hidden();

    /* base_row is the input's FIRST row — the row the prompt is painted on,
     * in this component's own coordinates. A composing app that paints
     * chrome above the input adds that row to what it declares. */
    int base_row = input->first_row > 0 ? input->first_row : 1;
    int wrap = input->wrap == TUI_TEXTINPUT_WRAP && input->content_width > 0;

    int row = base_row + (int)(L->cur_row - top);
    int col = L->rows[L->cur_row].prompt_w + L->cur_col -
              (wrap ? 0 : input->offset) + 1;
    if (col < 1)
        col = 1;
    return tui_cursor_at(row, col);
}

static TuiInitResult textinput_init(void *config)
{
    TuiModel *model =
        (TuiModel *)tui_textinput_create((const TuiTextInputConfig *)config);
    return tui_init_result_none(model);
}

static TuiUpdateResult textinput_update(TuiModel *model, TuiMsg msg)
{
    return tui_textinput_update((TuiTextInput *)model, msg);
}

static TuiView textinput_view_slot(const TuiModel *model, DynamicBuffer *out)
{
    const TuiTextInput *input = (const TuiTextInput *)model;
    tui_textinput_view(input, out);
    TuiView v = tui_view_default(out);
    v.cursor = tui_textinput_cursor_pos(input);
    return v;
}

static void textinput_free(TuiModel *model)
{
    tui_textinput_free((TuiTextInput *)model);
}

/* Static component interface instance */
static const TuiComponent textinput_component = {
    .init = textinput_init,
    .update = textinput_update,
    .view = textinput_view_slot,
    .free = textinput_free,
};

/* Get component interface for text input */
const TuiComponent *tui_textinput_component(void)
{
    return &textinput_component;
}
