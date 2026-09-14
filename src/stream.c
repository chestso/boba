/* stream.c - streaming transcript component (see include/boba/stream.h)
 *
 * Internal model, per stream:
 *
 *   raw buffer  - all unconsumed bytes (trimmed to the watermark after
 *                 each commit batch; offsets below refer into it)
 *   prev        - the previous completed line (for classify's `prev`)
 *   pend        - the last completed content line, awaiting the NEXT
 *                 line's verdict (the one-line lookahead window)
 *   tail        - the unterminated line being assembled
 *   container   - fence state: line-emitted (labeled) or byte-emitted
 *   block       - a live block-mode kind (table), finalized on
 *                 blank / block start / stream end
 *
 * Byte-emitted content (the system stream, unlabeled fences) stages
 * straight into the transcript's staging buffer, wrapped explicitly at
 * the terminal width. Everything else freezes into emission units and
 * renders through the row sink into staging. tui_runtime_flush() runs
 * the commit pass, so all units staged since the last flush reach the
 * scrollback through exactly one transcript_write.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boba/ansi_sequences.h>
#include <boba/stream.h>
#include <boba/unicode.h>

#include "stream_internal.h"

#define STREAM_RAW_INIT   256
#define STREAM_STAGE_INIT 4096

/* ------------------------------------------------------------------ */
/* Structures                                                          */
/* ------------------------------------------------------------------ */

typedef struct TuiStream
{
    const char *name;         /* borrowed from config */
    const TuiClassifier *cls; /* NULL = system stream */

    DynamicBuffer *raw;

    /* previous completed line (classify's prev) */
    int has_prev;
    size_t prev_off, prev_len;

    /* lookahead subject */
    int has_pend;
    size_t pend_off, pend_len;
    TuiBlockKind pend_kind;

    /* current line under construction: [tail_off, raw->len) */
    size_t tail_off;

    /* container (fence) state */
    int in_container;
    int container_byte; /* 1 = unlabeled (byte-emitted) */
    TuiBlockKind container_kind;
    size_t stage_pos; /* next raw offset to stage (byte containers) */

    /* block-mode live block (table) */
    int has_block;
    size_t block_off, block_len;
    TuiBlockKind block_kind;

    /* kind of the current line-mode block */
    TuiBlockKind cur_kind;
} TuiStream;

/* Row sink destinations. */
enum
{
    SINK_COMMIT = 0, /* staging buffer, commit framing (\r\n rows) */
    SINK_FRAME,      /* live scratch, EL-prefixed row recording       */
    SINK_COUNT,      /* dry run: count rows only                      */
};

struct TuiRowSink
{
    TuiTranscript *t;
    DynamicBuffer *buf; /* staging / scratch; NULL for SINK_COUNT */
    int dest;
    int width;
    int col;
    int open;             /* a row is open (content written since last row end) */
    int esc;              /* split escape state for zero-width accounting */
    size_t row_start_off; /* content start of the open row */

    /* frame row recording (ring of the last N rows) */
    size_t *rstart;
    size_t *rend;
    int rcap;
    int nrows;
};

struct TuiTranscript
{
    TuiTranscriptConfig cfg;
    int width, height;

    TuiStream *streams; /* n_user + 1 entries; last = system */
    size_t n_user;

    /* commit staging (shared across streams; order = freeze order) */
    DynamicBuffer *staging;
    int row_open; /* the output row (staged or committed) is open */
    int row_col;  /* display cols written on that row */
    int row_esc;  /* split ESC state on that row */

    int orphan_partial; /* clear() while a row was open: forget, emit nothing */
    unsigned long commit_count;

    /* live planner scratch (reused; see memory-reuse principle) */
    DynamicBuffer *live_scratch;
    size_t *row_starts;
    size_t *row_ends;
    int row_ring_cap;
};

/* Forward declarations for the block-mode helpers (used by
 * process_line, defined below it). */
static void stream_open_block(TuiTranscript *t, TuiStream *s, size_t off,
                              size_t end);
static void stream_extend_block(TuiTranscript *t, TuiStream *s, size_t end);
static void stream_finalize_block(TuiTranscript *t, TuiStream *s);

/* ------------------------------------------------------------------ */
/* Display-column accounting                                           */
/* ------------------------------------------------------------------ */

/* Advance a display column over a byte run (see stream_internal.h). */
int tui_rowcols_advance(int col, const char *bytes, size_t len, int *esc_state)
{
    size_t i = 0;
    int esc = esc_state ? *esc_state : 0;
    while (i < len) {
        unsigned char c = (unsigned char)bytes[i];
        if (esc == 1) {
            if (c == '[') {
                esc = 2;
                i++;
                continue;
            }
            if (c == ']') {
                esc = 3;
                i++;
                continue;
            }
            esc = 0;
            i++;
            continue;
        }
        if (esc == 2) {
            if (c >= 0x40 && c <= 0x7E)
                esc = 0;
            i++;
            continue;
        }
        if (esc == 3) {
            if (c == 0x07) {
                esc = 0;
                i++;
                continue;
            }
            if (c == 0x1b) {
                esc = 4;
                i++;
                continue;
            }
            i++;
            continue;
        }
        if (esc == 4) {
            esc = 0;
            i++;
            continue;
        }
        if (c == 0x1b) {
            esc = 1;
            i++;
            continue;
        }
        if (c == '\n') {
            col = 0;
            i++;
            continue;
        }
        if (c == '\r') {
            i++;
            continue;
        }
        if (c == '\t') {
            col += 8 - (col % 8);
            i++;
            continue;
        }
        if (c < 0x20) {
            i++;
            continue;
        }
        int cl = tui_utf8_char_len(bytes + i);
        if (cl <= 0 || i + (size_t)cl > len)
            break;
        uint32_t cp = tui_utf8_decode(bytes + i, cl);
        col += tui_codepoint_width(cp);
        i += (size_t)cl;
    }
    if (esc_state)
        *esc_state = esc;
    return col;
}

/* ------------------------------------------------------------------ */
/* Byte-mode staging (system stream + unlabeled fences)                */
/* ------------------------------------------------------------------ */

/* Emit the explicit row break; never soft-wrap. */
static void stage_row_break(TuiTranscript *t)
{
    dynamic_buffer_append(t->staging, "\r\n", 2);
    t->row_open = 0;
    t->row_col = 0;
    t->row_esc = 0;
}

/* Append glyph bytes with explicit wrapping at t->width. */
static void stage_glyph(TuiTranscript *t, const char *bytes, size_t len,
                        int gw)
{
    int w = t->width > 1 ? t->width : 80;
    if (t->row_col + gw > w)
        stage_row_break(t);
    dynamic_buffer_append(t->staging, bytes, len);
    t->row_open = 1;
    t->row_col += gw;
    if (t->row_col >= w)
        stage_row_break(t);
}

/* Copy one ESC-initiated sequence verbatim (zero display width). The
 * carry state resumes a sequence split across chunks; consumes only
 * the sequence bytes. */
static void stage_escape(TuiTranscript *t, const char *bytes, size_t len,
                         size_t *i)
{
    while (*i < len) {
        unsigned char c = (unsigned char)bytes[*i];
        int esc = t->row_esc;
        if (esc == 0 && c != 0x1b)
            return;
        dynamic_buffer_append(t->staging, bytes + *i, 1);
        t->row_open = 1; /* bytes (even zero-width) are on the open row */
        (*i)++;
        if (esc == 0) {
            t->row_esc = 1;
            continue;
        }
        if (esc == 1) {
            if (c == '[')
                t->row_esc = 2;
            else if (c == ']')
                t->row_esc = 3;
            else
                t->row_esc = 0;
            continue;
        }
        if (esc == 2) {
            if (c >= 0x40 && c <= 0x7E)
                t->row_esc = 0;
            continue;
        }
        if (esc == 3) {
            if (c == 0x07)
                t->row_esc = 0;
            else if (c == 0x1b)
                t->row_esc = 4;
            continue;
        }
        /* esc == 4 */
        t->row_esc = 0;
    }
}

/* Normalize + wrap raw byte-run text into the staging buffer. Handles
 * LF/CRLF/CR -> \r\n, hard tabs (8-col stops), ANSI passthrough with
 * explicit wrapping, and no raw \r on output. */
static void stage_bytes(TuiTranscript *t, const char *bytes, size_t len)
{
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)bytes[i];
        if (t->row_esc) {
            /* resume an escape sequence split across chunks */
            stage_escape(t, bytes, len, &i);
            continue;
        }
        if (c == '\r') {
            if (i + 1 < len && bytes[i + 1] == '\n')
                i++;
            stage_row_break(t);
            i++;
            continue;
        }
        if (c == '\n') {
            stage_row_break(t);
            i++;
            continue;
        }
        if (c == '\t') {
            int w = t->width > 1 ? t->width : 80;
            int stop = 8 - (t->row_col % 8);
            if (t->row_col + stop > w)
                stop = w - t->row_col;
            for (int k = 0; k < stop; k++)
                dynamic_buffer_append(t->staging, " ", 1);
            t->row_open = 1;
            t->row_col += stop;
            if (t->row_col >= w)
                stage_row_break(t);
            i++;
            continue;
        }
        if (c == 0x1b) {
            stage_escape(t, bytes, len, &i);
            continue;
        }
        if (c < 0x20) {
            i++; /* drop stray controls */
            continue;
        }
        int cl = tui_utf8_char_len(bytes + i);
        if (cl <= 0 || i + (size_t)cl > len)
            break;
        uint32_t cp = tui_utf8_decode(bytes + i, cl);
        int gw = tui_codepoint_width(cp);
        stage_glyph(t, bytes + i, (size_t)cl, gw);
        i += (size_t)cl;
    }
}

/* Stage [s->stage_pos, upto) of a byte container's raw bytes. */
static void stream_stage_range(TuiTranscript *t, TuiStream *s, size_t upto)
{
    if (!s->in_container || !s->container_byte)
        return;
    if (upto > s->raw->len)
        upto = s->raw->len;
    if (s->stage_pos < upto) {
        stage_bytes(t, s->raw->data + s->stage_pos, upto - s->stage_pos);
        s->stage_pos = upto;
    }
}

/* ------------------------------------------------------------------ */
/* Row sink                                                            */
/* ------------------------------------------------------------------ */

static void sink_note_row_start(TuiRowSink *s)
{
    if (s->open)
        return;
    s->open = 1;
    s->row_start_off = s->buf ? s->buf->len : 0;
}

static void sink_record_row(TuiRowSink *s)
{
    if (s->dest == SINK_COMMIT)
        return;
    if (s->dest == SINK_FRAME && s->rstart && s->rcap > 0) {
        size_t start = s->open ? s->row_start_off
                               : (s->buf ? s->buf->len : 0);
        size_t end = s->buf ? s->buf->len : 0;
        int idx = s->nrows % s->rcap;
        s->rstart[idx] = start;
        s->rend[idx] = end;
    }
    s->nrows++;
}

/* Internal row break: end the row if open and start fresh. */
static void sink_row_break(TuiRowSink *s)
{
    if (s->dest == SINK_COMMIT) {
        dynamic_buffer_append(s->buf, "\r\n", 2);
        if (s->t) {
            s->t->row_open = 0;
            s->t->row_col = 0;
            s->t->row_esc = 0;
        }
    }
    sink_record_row(s);
    s->open = 0;
    s->col = 0;
}

/* Copy an ESC-rooted byte sequence for zero-width accounting; the
 * carry state (s->esc) resumes across calls. Consumes only the
 * sequence bytes. */
static void sink_escape(TuiRowSink *s, const char *utf8, size_t len,
                        size_t *i)
{
    while (*i < len) {
        unsigned char c = (unsigned char)utf8[*i];
        int esc = s->esc;
        if (esc == 0 && c != 0x1b)
            return;
        if (s->buf)
            dynamic_buffer_append(s->buf, utf8 + *i, 1);
        sink_note_row_start(s);
        (*i)++;
        if (esc == 0) {
            s->esc = 1;
            continue;
        }
        if (esc == 1) {
            if (c == '[')
                s->esc = 2;
            else if (c == ']')
                s->esc = 3;
            else
                s->esc = 0;
            continue;
        }
        if (esc == 2) {
            if (c >= 0x40 && c <= 0x7E)
                s->esc = 0;
            continue;
        }
        if (esc == 3) {
            if (c == 0x07)
                s->esc = 0;
            else if (c == 0x1b)
                s->esc = 4;
            continue;
        }
        /* esc == 4 */
        s->esc = 0;
    }
}

/* Append UTF-8 text to the current row (public: tui_row_text). */
void tui_row_text(TuiRowSink *s, const char *utf8, size_t len)
{
    if (!s || !utf8)
        return;
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)utf8[i];
        if (s->esc) {
            /* resume an escape sequence split across calls */
            sink_escape(s, utf8, len, &i);
            continue;
        }
        if (c == '\r') {
            i++;
            continue;
        }
        if (c == '\n') {
            sink_row_break(s);
            i++;
            continue;
        }
        if (c == '\t') {
            int stop = 8 - (s->col % 8);
            if (s->col + stop > s->width)
                stop = s->width - s->col;
            sink_note_row_start(s);
            for (int k = 0; k < stop; k++) {
                if (s->buf)
                    dynamic_buffer_append(s->buf, " ", 1);
            }
            s->col += stop;
            if (s->col >= s->width)
                sink_row_break(s);
            i++;
            continue;
        }
        if (c == 0x1b) {
            sink_escape(s, utf8, len, &i);
            continue;
        }
        if (c < 0x20) {
            i++; /* drop stray controls */
            continue;
        }
        int cl = tui_utf8_char_len(utf8 + i);
        if (cl <= 0 || i + (size_t)cl > len)
            break;
        uint32_t cp = tui_utf8_decode(utf8 + i, cl);
        int gw = tui_codepoint_width(cp);
        if (s->col + gw > s->width)
            sink_row_break(s); /* explicit wrap, never soft-wrap */
        sink_note_row_start(s);
        if (s->buf)
            dynamic_buffer_append(s->buf, utf8 + i, (size_t)cl);
        s->col += gw;
        if (s->col >= s->width)
            sink_row_break(s);
        i += (size_t)cl;
    }
}

void tui_row_attr(TuiRowSink *s, TuiAttr a)
{
    if (!s || !s->buf || s->dest == SINK_COUNT)
        return;
    char buf[80];
    size_t n = 0;
    buf[n++] = '\x1b';
    buf[n++] = '[';
    buf[n++] = '0';
    if (a.bold)
        buf[n++] = ';', buf[n++] = '1';
    if (a.dim)
        buf[n++] = ';', buf[n++] = '2';
    if (a.italic)
        buf[n++] = ';', buf[n++] = '3';
    if (a.underline)
        buf[n++] = ';', buf[n++] = '4';
    if (a.has_fg)
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, ";38;2;%d;%d;%d",
                              a.fg_r, a.fg_g, a.fg_b);
    if (a.has_bg)
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, ";48;2;%d;%d;%d",
                              a.bg_r, a.bg_g, a.bg_b);
    buf[n++] = 'm';
    dynamic_buffer_append(s->buf, buf, n);
}

void tui_row_attr_reset(TuiRowSink *s)
{
    if (!s || !s->buf || s->dest == SINK_COUNT)
        return;
    dynamic_buffer_append(s->buf, "\x1b[0m", 4);
}

void tui_row_pad_to(TuiRowSink *s, int display_col)
{
    if (!s)
        return;
    if (display_col > s->width)
        display_col = s->width;
    if (display_col <= s->col)
        return;
    int pad = display_col - s->col;
    sink_note_row_start(s);
    if (s->buf) {
        for (int k = 0; k < pad; k++)
            dynamic_buffer_append(s->buf, " ", 1);
    }
    s->col += pad;
    if (s->col >= s->width)
        sink_row_break(s);
}

void tui_row_end(TuiRowSink *s)
{
    if (!s)
        return;
    if (s->dest == SINK_COMMIT) {
        sink_row_break(s); /* appends \r\n, closes the row */
        return;
    }
    sink_record_row(s);
    s->open = 0;
    s->col = 0;
}

void tui_row_image(TuiRowSink *s, const TuiImageSpec *spec)
{
    /* Image transport lands with the image tier (see stream.h). Until
     * then IMAGE blocks degrade to their text via render_block. */
    (void)s;
    (void)spec;
}

/* ------------------------------------------------------------------ */
/* Emission units                                                      */
/* ------------------------------------------------------------------ */

/* Prepare a commit sink: a rendered unit always starts on a fresh row. */
static void sink_commit_begin(TuiTranscript *t, TuiRowSink *s)
{
    memset(s, 0, sizeof(*s));
    s->t = t;
    s->dest = SINK_COMMIT;
    s->buf = t->staging;
    s->width = t->width > 1 ? t->width : 80;
    if (t->row_open) {
        /* a byte-mode partial row is open; a full rendered unit starts
         * on its own row */
        dynamic_buffer_append(t->staging, "\r\n", 2);
        t->row_open = 0;
        t->row_col = 0;
        t->row_esc = 0;
    }
}

static void sink_commit_end(TuiRowSink *s)
{
    if (s->open)
        tui_row_end(s);
}

/* Render one emission unit (a frozen line or a finalized block). */
static void emit_unit(TuiTranscript *t, TuiStream *s, TuiBlockKind kind,
                      size_t off, size_t len)
{
    if (!t->cfg.render_block)
        return;
    TuiBlock blk;
    memset(&blk, 0, sizeof(blk));
    blk.kind = kind;
    blk.state = TUI_BLOCK_FINAL;
    blk.off = off;
    blk.len = len;

    TuiRowSink sink;
    sink_commit_begin(t, &sink);
    t->cfg.render_block(&blk, s->raw->data + off, len, sink.width, &sink,
                        t->cfg.user_data);
    sink_commit_end(&sink);
    /* unit rows always end terminated */
    t->row_open = 0;
    t->row_col = 0;
    t->row_esc = 0;
}

/* Freeze the pending line, if any. */
static void stream_freeze_pend(TuiTranscript *t, TuiStream *s)
{
    if (!s->has_pend)
        return;
    emit_unit(t, s, s->pend_kind, s->pend_off, s->pend_len);
    s->has_pend = 0;
}

/* Block-mode (table) helpers. */
static void stream_open_block(TuiTranscript *t, TuiStream *s, size_t off,
                              size_t end)
{
    (void)t;
    if (end <= off) {
        s->has_block = 0;
        return;
    }
    s->has_block = 1;
    s->block_off = off;
    s->block_len = end - off;
    s->block_kind = TUI_BLOCK_TABLE;
    s->has_pend = 0;
}

static void stream_extend_block(TuiTranscript *t, TuiStream *s, size_t end)
{
    (void)t;
    if (s->has_block && end > s->block_off)
        s->block_len = end - s->block_off;
}

static void stream_finalize_block(TuiTranscript *t, TuiStream *s)
{
    if (!s->has_block)
        return;
    emit_unit(t, s, s->block_kind, s->block_off, s->block_len);
    s->has_block = 0;
}

/* ------------------------------------------------------------------ */
/* Line processing                                                     */
/* ------------------------------------------------------------------ */

/* A completed line [ls, le); `has_nl` = the newline is present. */
static void process_line(TuiTranscript *t, TuiStream *s, size_t ls, size_t le,
                         int has_nl)
{
    const char *raw = s->raw->data;
    size_t llen = le - ls;
    size_t end = has_nl ? le + 1 : le;
    const char *prev = s->has_prev ? raw + s->prev_off : NULL;
    size_t prev_len = s->has_prev ? s->prev_len : 0;

    TuiBlockKind okind = TUI_BLOCK_PARAGRAPH;
    TuiLineClass v = s->cls->classify(s->cls->state, raw + ls, llen, prev,
                                      prev_len, &okind);

    if (s->in_container) {
        if (!s->container_byte) {
            /* line-emitted container (labeled fence): auto-finalize
             * each completed line; verdicts matter only for CLOSE */
            if (v == TUI_LINE_CONTAINER_CLOSE) {
                stream_freeze_pend(t, s);
                emit_unit(t, s, s->container_kind, ls, llen);
                s->in_container = 0;
                s->cur_kind = TUI_BLOCK_PARAGRAPH;
            } else {
                stream_freeze_pend(t, s);
                s->has_pend = 1;
                s->pend_off = ls;
                s->pend_len = llen;
                s->pend_kind = s->container_kind;
            }
        } else {
            /* byte container: bytes staged as they arrive; CLOSE ends it */
            if (has_nl)
                stream_stage_range(t, s, end);
            if (v == TUI_LINE_CONTAINER_CLOSE)
                s->in_container = 0;
        }
        return;
    }

    switch (v) {
    case TUI_LINE_BLANK:
        if (s->has_block)
            stream_finalize_block(t, s);
        stream_freeze_pend(t, s);
        s->cur_kind = TUI_BLOCK_PARAGRAPH;
        break;

    case TUI_LINE_BLOCK_START:
        if (s->has_block)
            stream_finalize_block(t, s);
        stream_freeze_pend(t, s);
        if (okind == TUI_BLOCK_TABLE) {
            stream_open_block(t, s, ls, end);
        } else if (okind == TUI_BLOCK_FENCE_PLAIN || okind == TUI_BLOCK_RAW) {
            s->in_container = 1;
            s->container_byte = 1;
            s->container_kind = okind;
            s->stage_pos = ls;
            stream_stage_range(t, s, end);
        } else if (okind == TUI_BLOCK_FENCE) {
            s->in_container = 1;
            s->container_byte = 0;
            s->container_kind = okind;
            s->has_pend = 1;
            s->pend_off = ls;
            s->pend_len = llen;
            s->pend_kind = okind;
        } else {
            s->cur_kind = okind;
            s->has_pend = 1;
            s->pend_off = ls;
            s->pend_len = llen;
            s->pend_kind = okind;
        }
        break;

    case TUI_LINE_RECLASSIFY_PREV:
        if (s->has_block) {
            /* prev inside a block-mode live block: splitting is not
             * exercised by any current grammar; keeping the block
             * intact is the conservative read (debug: contract
             * violation). */
            assert(!"RECLASSIFY_PREV against a live block");
            stream_extend_block(t, s, end);
            break;
        }
        if (!s->has_pend) {
            /* no previous line to reclassify: treat as a continue */
            assert(!"RECLASSIFY_PREV without a pending line");
            s->has_pend = 1;
            s->pend_off = ls;
            s->pend_len = llen;
            s->pend_kind = s->cur_kind;
            break;
        }
        if (okind == TUI_BLOCK_TABLE) {
            /* header (prev) + delimiter (line) open the table */
            stream_open_block(t, s, s->pend_off, end);
        } else {
            /* line-mode reclass (e.g. setext): the pair is one unit */
            size_t off = s->pend_off;
            emit_unit(t, s, okind, off, end - off);
            s->has_pend = 0;
        }
        s->cur_kind = TUI_BLOCK_PARAGRAPH;
        break;

    case TUI_LINE_CONTAINER_OPEN:
        stream_freeze_pend(t, s);
        s->in_container = 1;
        s->container_byte =
            (okind == TUI_BLOCK_FENCE_PLAIN || okind == TUI_BLOCK_RAW);
        s->container_kind = okind;
        if (s->container_byte) {
            s->stage_pos = ls;
            stream_stage_range(t, s, end);
        } else {
            s->has_pend = 1;
            s->pend_off = ls;
            s->pend_len = llen;
            s->pend_kind = okind;
        }
        s->cur_kind = TUI_BLOCK_PARAGRAPH;
        break;

    case TUI_LINE_CONTAINER_CLOSE:
        /* defensive: close without open — treat as a content line */
        stream_freeze_pend(t, s);
        s->has_pend = 1;
        s->pend_off = ls;
        s->pend_len = llen;
        s->pend_kind = s->cur_kind;
        break;

    case TUI_LINE_CONTINUES:
    default:
        if (s->has_block) {
            stream_extend_block(t, s, end);
            break;
        }
        stream_freeze_pend(t, s);
        s->has_pend = 1;
        s->pend_off = ls;
        s->pend_len = llen;
        s->pend_kind = s->cur_kind;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Stream ingest                                                       */
/* ------------------------------------------------------------------ */

/* Scan completed lines in the raw buffer; stage byte-container bytes
 * for whatever remains unterminated. */
static void stream_scan(TuiTranscript *t, TuiStream *s)
{
    for (;;) {
        size_t remaining = s->raw->len - s->tail_off;
        const char *nl = remaining
                             ? memchr(s->raw->data + s->tail_off, '\n',
                                      remaining)
                             : NULL;
        if (!nl)
            break;
        size_t le = (size_t)(nl - s->raw->data);
        size_t ls = s->tail_off;
        process_line(t, s, ls, le, 1);
        /* classify context: the completed line becomes `prev` */
        s->has_prev = 1;
        s->prev_off = ls;
        s->prev_len = le - ls;
        s->tail_off = le + 1;
    }
    if (s->in_container && s->container_byte)
        stream_stage_range(t, s, s->raw->len);
}

static void stream_append(TuiTranscript *t, TuiStream *s, const char *text,
                          size_t len)
{
    dynamic_buffer_append(s->raw, text, len);
    stream_scan(t, s);
}

/* Finalize: emit the trailing unterminated line, freeze everything,
 * close containers. Does NOT reset the classifier (caller decides).
 * The system stream has no classifier and stages bytes on receipt —
 * there is nothing to finalize. */
static void stream_finalize(TuiTranscript *t, TuiStream *s)
{
    if (!s->cls) {
        /* system stream: stateless pass-through, nothing to finalize */
        s->raw->len = 0;
        s->tail_off = 0;
        return;
    }
    if (s->raw->len > s->tail_off) {
        size_t ls = s->tail_off;
        process_line(t, s, ls, s->raw->len, 0);
        s->has_prev = 1;
        s->prev_off = ls;
        s->prev_len = s->raw->len - ls;
        s->tail_off = s->raw->len;
    }
    if (s->in_container) {
        if (s->container_byte)
            stream_stage_range(t, s, s->raw->len);
        s->in_container = 0;
    }
    stream_freeze_pend(t, s);
    if (s->has_block)
        stream_finalize_block(t, s);
}

/* ------------------------------------------------------------------ */
/* Default classifier                                                  */
/* ------------------------------------------------------------------ */

static TuiLineClass default_classify(void *state, const char *line, size_t len,
                                     const char *prev, size_t prev_len,
                                     TuiBlockKind *out_kind)
{
    (void)state;
    (void)line;
    (void)len;
    *out_kind = TUI_BLOCK_PARAGRAPH;
    if (!prev || prev_len == 0)
        return TUI_LINE_BLOCK_START;
    return TUI_LINE_CONTINUES;
}

static void default_reset(void *state)
{
    (void)state;
}

const TuiClassifier *tui_classifier_default(void)
{
    static const TuiClassifier def = {
        .state = NULL,
        .classify = default_classify,
        .reset = default_reset,
    };
    return &def;
}

/* ------------------------------------------------------------------ */
/* Trim                                                                */
/* ------------------------------------------------------------------ */

/* Drop the committed prefix below the watermark (the lowest offset any
 * retained structure references). Runs after each commit batch. */
static void transcript_trim(TuiTranscript *t)
{
    for (size_t i = 0; i <= t->n_user; i++) {
        TuiStream *s = &t->streams[i];
        if (!s->raw || s->raw->len == 0)
            continue;
        if (!s->cls) {
            /* system stream: nothing references the buffer */
            s->raw->len = 0;
            s->tail_off = 0;
            continue;
        }
        size_t wm = s->raw->len;
        if (s->tail_off < wm)
            wm = s->tail_off;
        if (s->has_prev && s->prev_off < wm)
            wm = s->prev_off;
        if (s->has_pend && s->pend_off < wm)
            wm = s->pend_off;
        if (s->has_block && s->block_off < wm)
            wm = s->block_off;
        if (s->in_container && s->container_byte && s->stage_pos < wm)
            wm = s->stage_pos;
        if (wm == 0 || wm > s->raw->len)
            continue;
        memmove(s->raw->data, s->raw->data + wm, s->raw->len - wm);
        s->raw->len -= wm;
        if (s->has_prev)
            s->prev_off -= wm;
        if (s->has_pend)
            s->pend_off -= wm;
        if (s->has_block)
            s->block_off -= wm;
        s->tail_off -= wm;
        if (s->in_container && s->container_byte)
            s->stage_pos -= wm;
    }
}

/* ------------------------------------------------------------------ */
/* Commit pass                                                         */
/* ------------------------------------------------------------------ */

/* Close an open output row: append the break so the next unit starts
 * below it (called at stream end / submit). */
static void transcript_close_row(TuiTranscript *t)
{
    if (!t->row_open)
        return;
    dynamic_buffer_append(t->staging, "\r\n", 2);
    t->row_open = 0;
    t->row_col = 0;
    t->row_esc = 0;
}

int tui_transcript_commit_pending(TuiTranscript *t, TuiRuntime *rt)
{
    if (!t || !rt)
        return 0;

    if (t->orphan_partial) {
        tui_runtime_transcript_orphan(rt);
        t->orphan_partial = 0;
        t->row_open = 0;
        t->row_col = 0;
        t->row_esc = 0;
    }
    if (t->staging->len == 0)
        return 0;

    tui_runtime_transcript_write(rt, t->staging->data, t->staging->len);
    t->commit_count++;
    dynamic_buffer_clear(t->staging);
    transcript_trim(t);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Messages                                                            */
/* ------------------------------------------------------------------ */

static TuiMsg stream_text_msg(int type, int stream_id, const char *text,
                              size_t len)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = (TuiMsgType)type;
    msg.data.stream.stream_id = stream_id;
    if (text && len > 0) {
        char *copy = malloc(len + 1);
        if (copy) {
            memcpy(copy, text, len);
            copy[len] = '\0';
            msg.data.stream.text = copy;
            msg.data.stream.len = len;
        }
    }
    return msg;
}

TuiMsg tui_msg_stream_delta(int stream_id, const char *text, size_t len)
{
    return stream_text_msg(TUI_MSG_STREAM_DELTA, stream_id, text, len);
}

TuiMsg tui_msg_stream_text(int stream_id, const char *text, size_t len)
{
    return stream_text_msg(TUI_MSG_STREAM_TEXT, stream_id, text, len);
}

TuiMsg tui_msg_stream_end(int stream_id)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_STREAM_END;
    msg.data.stream.stream_id = stream_id;
    return msg;
}

TuiMsg tui_msg_transcript_submit(const char *text, size_t len)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_TRANSCRIPT_SUBMIT;
    if (text && len > 0) {
        char *copy = malloc(len + 1);
        if (copy) {
            memcpy(copy, text, len);
            copy[len] = '\0';
            msg.data.submit.text = copy;
            msg.data.submit.len = len;
        }
    }
    return msg;
}

TuiMsg tui_msg_transcript_clear(void)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_TRANSCRIPT_CLEAR;
    return msg;
}

/* ------------------------------------------------------------------ */
/* Stream / transcript lifecycle                                       */
/* ------------------------------------------------------------------ */

static TuiStream *transcript_stream_for(TuiTranscript *t, int stream_id)
{
    if (stream_id < 0)
        return &t->streams[t->n_user]; /* system */
    if ((size_t)stream_id >= t->n_user)
        return NULL;
    return &t->streams[stream_id];
}

static void stream_reset(TuiTranscript *t, TuiStream *s)
{
    (void)t;
    if (s->raw)
        s->raw->len = 0;
    s->has_prev = 0;
    s->prev_off = s->prev_len = 0;
    s->has_pend = 0;
    s->pend_off = s->pend_len = 0;
    s->pend_kind = TUI_BLOCK_PARAGRAPH;
    s->tail_off = 0;
    s->in_container = 0;
    s->container_byte = 0;
    s->container_kind = TUI_BLOCK_PARAGRAPH;
    s->stage_pos = 0;
    s->has_block = 0;
    s->block_off = s->block_len = 0;
    s->block_kind = TUI_BLOCK_PARAGRAPH;
    s->cur_kind = TUI_BLOCK_PARAGRAPH;
    if (s->cls && s->cls->reset)
        s->cls->reset(s->cls->state);
}

TuiTranscript *tui_transcript_create(const TuiTranscriptConfig *cfg)
{
    TuiTranscript *t = calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    if (cfg)
        t->cfg = *cfg;

    t->width = 80;
    t->height = 24;
    t->staging = dynamic_buffer_create(STREAM_STAGE_INIT);
    t->live_scratch = dynamic_buffer_create(1024);
    if (!t->staging || !t->live_scratch)
        goto fail;

    t->n_user = t->cfg.n_streams;
    t->streams = calloc(t->n_user + 1, sizeof(TuiStream));
    if (!t->streams)
        goto fail;

    for (size_t i = 0; i < t->n_user; i++) {
        TuiStream *s = &t->streams[i];
        s->name = t->cfg.streams ? t->cfg.streams[i].name : NULL;
        s->cls = (t->cfg.classifiers && t->cfg.classifiers[i])
                     ? t->cfg.classifiers[i]
                     : tui_classifier_default();
        s->raw = dynamic_buffer_create(STREAM_RAW_INIT);
        if (!s->raw)
            goto fail;
        s->pend_kind = TUI_BLOCK_PARAGRAPH;
        s->cur_kind = TUI_BLOCK_PARAGRAPH;
    }
    /* system stream: verbatim pass-through, no classifier */
    {
        TuiStream *s = &t->streams[t->n_user];
        s->name = "system";
        s->cls = NULL;
        s->raw = dynamic_buffer_create(STREAM_RAW_INIT);
        if (!s->raw)
            goto fail;
    }
    return t;

fail:
    tui_transcript_free(t);
    return NULL;
}

void tui_transcript_free(TuiTranscript *t)
{
    if (!t)
        return;
    if (t->streams) {
        for (size_t i = 0; i <= t->n_user; i++) {
            if (t->streams[i].raw)
                dynamic_buffer_destroy(t->streams[i].raw);
        }
        free(t->streams);
    }
    dynamic_buffer_destroy(t->staging);
    dynamic_buffer_destroy(t->live_scratch);
    free(t->row_starts);
    free(t->row_ends);
    free(t);
}

/* ------------------------------------------------------------------ */
/* Update                                                              */
/* ------------------------------------------------------------------ */

TuiUpdateResult tui_transcript_update(TuiTranscript *t, TuiMsg msg)
{
    if (!t)
        return tui_update_result_none();

    switch (msg.type) {
    case TUI_MSG_WINDOW_SIZE:
        if (msg.data.size.width > 0)
            t->width = msg.data.size.width;
        if (msg.data.size.height > 0)
            t->height = msg.data.size.height;
        return tui_update_result_none();

    case TUI_MSG_STREAM_DELTA:
    {
        TuiStream *s = transcript_stream_for(t, msg.data.stream.stream_id);
        if (!s || !s->cls || !msg.data.stream.text || msg.data.stream.len == 0)
            return tui_update_result_none();
        stream_append(t, s, msg.data.stream.text, msg.data.stream.len);
        return tui_update_result_none();
    }

    case TUI_MSG_STREAM_TEXT:
    {
        /* raw entry, system stream only; one byte unit per call */
        if (msg.data.stream.stream_id >= 0 || !msg.data.stream.text ||
            msg.data.stream.len == 0)
            return tui_update_result_none();
        TuiStream *s = &t->streams[t->n_user];
        dynamic_buffer_append(s->raw, msg.data.stream.text,
                              msg.data.stream.len);
        stage_bytes(t, msg.data.stream.text, msg.data.stream.len);
        return tui_update_result_none();
    }

    case TUI_MSG_STREAM_END:
    {
        TuiStream *s = transcript_stream_for(t, msg.data.stream.stream_id);
        if (!s)
            return tui_update_result_none();
        stream_finalize(t, s);
        if (s->cls && s->cls->reset)
            s->cls->reset(s->cls->state);
        transcript_close_row(t);
        return tui_update_result_none();
    }

    case TUI_MSG_TRANSCRIPT_SUBMIT:
    {
        /* finalize every LIVE block across all streams; no echo */
        for (size_t i = 0; i <= t->n_user; i++)
            stream_finalize(t, &t->streams[i]);
        transcript_close_row(t);
        return tui_update_result_none();
    }

    case TUI_MSG_TRANSCRIPT_CLEAR:
    {
        /* new chat: reset everything; emit nothing */
        if (t->row_open)
            t->orphan_partial = 1;
        t->row_open = 0;
        t->row_col = 0;
        t->row_esc = 0;
        dynamic_buffer_clear(t->staging);
        for (size_t i = 0; i <= t->n_user; i++)
            stream_reset(t, &t->streams[i]);
        return tui_update_result_none();
    }

    default:
        return tui_update_result_none();
    }
}

/* ------------------------------------------------------------------ */
/* Live region planner                                                 */
/* ------------------------------------------------------------------ */

static int ensure_row_ring(TuiTranscript *t, int cap)
{
    if (t->row_ring_cap >= cap)
        return 0;
    int ncap = cap > 0 ? cap : 1;
    size_t *ns = realloc(t->row_starts, (size_t)ncap * sizeof(size_t));
    if (!ns)
        return -1;
    t->row_starts = ns;
    size_t *ne = realloc(t->row_ends, (size_t)ncap * sizeof(size_t));
    if (!ne)
        return -1;
    t->row_ends = ne;
    t->row_ring_cap = ncap;
    return 0;
}

/* One planning function for both view() and live_rows() (contract: no
 * second implementation to drift). Returns the number of rows the live
 * region will emit. `tc` is const to callers; the scratch buffers it
 * reuses are cache, not state. */
static int live_plan(const TuiTranscript *tc, DynamicBuffer *out, int width,
                     int rows_cap, int count_only)
{
    TuiTranscript *t = (TuiTranscript *)tc;
    int w = width > 0 ? width : 80;
    int cap = rows_cap > 0 ? rows_cap : 1;
    if (ensure_row_ring(t, cap + 2) != 0)
        return 0;

    TuiRowSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.t = t;
    sink.dest = count_only ? SINK_COUNT : SINK_FRAME;
    sink.buf = count_only ? NULL : t->live_scratch;
    sink.width = w;
    sink.rstart = t->row_starts;
    sink.rend = t->row_ends;
    sink.rcap = t->row_ring_cap;
    if (!count_only)
        dynamic_buffer_clear(t->live_scratch);

    for (size_t i = 0; i < t->n_user; i++) {
        TuiStream *s = &t->streams[i];
        int has_live = 0;
        if (s->has_block)
            has_live = 1;
        if (s->has_pend || s->tail_off < s->raw->len)
            has_live = 1;
        if (!has_live)
            continue;
        if (sink.open)
            sink_row_break(&sink);

        if (s->has_block) {
            if (t->cfg.render_live) {
                TuiBlock blk;
                memset(&blk, 0, sizeof(blk));
                blk.kind = s->block_kind;
                blk.state = TUI_BLOCK_LIVE;
                blk.off = s->block_off;
                blk.len = s->block_len;
                t->cfg.render_live(&blk, s->raw->data + s->block_off,
                                   s->block_len, w, cap, &sink,
                                   t->cfg.user_data);
                if (sink.open)
                    sink_row_break(&sink);
            }
        } else {
            /* boba-owned plain rows: lookahead line + partial tail */
            size_t start = s->has_pend ? s->pend_off : s->tail_off;
            if (start < s->raw->len)
                tui_row_text(&sink, s->raw->data + start,
                             s->raw->len - start);
        }
    }
    if (sink.open)
        sink_row_break(&sink);

    int kept = sink.nrows < cap ? sink.nrows : cap;
    if (!count_only && out) {
        for (int k = 0; k < kept; k++) {
            int idx = (sink.nrows - kept + k) % sink.rcap;
            size_t start = t->row_starts[idx];
            size_t end = t->row_ends[idx];
            dynamic_buffer_append_str(out, k == 0 ? "\r" : "\r\n");
            dynamic_buffer_append_str(out, EL_TO_END);
            if (end > start)
                dynamic_buffer_append(out, t->live_scratch->data + start,
                                      end - start);
        }
    }
    return kept;
}

void tui_transcript_view(const TuiTranscript *t, DynamicBuffer *out, int width,
                         int rows_cap)
{
    if (!t || !out)
        return;
    (void)live_plan(t, out, width, rows_cap, 0);
}

int tui_transcript_live_rows(const TuiTranscript *t, int width, int rows_cap)
{
    if (!t)
        return 0;
    return live_plan(t, NULL, width, rows_cap, 1);
}

/* ------------------------------------------------------------------ */
/* Introspection                                                       */
/* ------------------------------------------------------------------ */

unsigned long tui_transcript_commit_count(const TuiTranscript *t)
{
    return t ? t->commit_count : 0;
}

size_t tui_transcript_stream_raw_len(const TuiTranscript *t, int stream_id)
{
    if (!t)
        return 0;
    TuiStream *s = transcript_stream_for((TuiTranscript *)t, stream_id);
    return s ? s->raw->len : 0;
}

/* ------------------------------------------------------------------ */
/* Component wrapper (standalone use)                                  */
/* ------------------------------------------------------------------ */

static TuiInitResult transcript_init(void *config)
{
    return tui_init_result_none((TuiModel *)config);
}

static TuiUpdateResult transcript_component_update(TuiModel *model, TuiMsg msg)
{
    return tui_transcript_update((TuiTranscript *)model, msg);
}

static TuiView transcript_component_view(const TuiModel *model,
                                         DynamicBuffer *out)
{
    const TuiTranscript *t = (const TuiTranscript *)model;
    if (t && out)
        tui_transcript_view(t, out, t->width, t->height);
    TuiView v = tui_view_default(out);
    v.render_mode = TUI_RENDER_INLINE;
    return v;
}

static void transcript_component_free(TuiModel *model)
{
    tui_transcript_free((TuiTranscript *)model);
}

const TuiComponent *tui_transcript_component(TuiTranscript *t)
{
    (void)t;
    static const TuiComponent comp = {
        .init = transcript_init,
        .update = transcript_component_update,
        .view = transcript_component_view,
        .free = transcript_component_free,
    };
    return &comp;
}
