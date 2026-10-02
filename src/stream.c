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
 * straight into the transcript's staging buffer. Everything else
 * freezes into emission units and renders through the row sink into
 * staging. Committed bytes carry no width breaks — the terminal owns
 * soft-wrapping (and reflow); only logical line breaks are emitted.
 * tui_runtime_flush() runs the commit pass, so all units staged since
 * the last flush reach the scrollback through exactly one
 * transcript_write.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boba/ansi_sequences.h>
#include <boba/stream.h>
#include <boba/unicode.h>

#include "base64.h"
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
    const TuiAttr *live_attr; /* borrowed from config (NULL = plain) */

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

    /* block-mode live block (table, image) */
    int has_block;
    size_t block_off, block_len;
    TuiBlockKind block_kind;
    int block_image_id; /* IMAGE: the unit's id (boba counter) */

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

/* ------------------------------------------------------------------ */
/* Escape-sequence policy                                              */
/* ------------------------------------------------------------------ */

/* The transcript seam's promise: framing bytes (cursor movement, EL,
 * OSC, APC/DCS, anything out-of-band) are UNREPRESENTABLE in app text.
 * SGR attribute sequences are the one exception — styling, not
 * framing; everything else is scanned, swallowed and dropped. A
 * sequence split across chunks carries its scan state (and its bytes
 * are buffered) so the decision is always made on the whole sequence.
 */
#define ESC_SEQ_MAX 64

typedef struct
{
    int state;  /* 0 ground, 1 after ESC, 2 CSI, 3 OSC, 4 string, 5 ESC-in-string */
    int resume; /* state to return to from 5 (OSC vs DCS/APC/PM/SOS) */
    int nostore;
    size_t len;
    char buf[ESC_SEQ_MAX];
} EscScan;

/* Feed one byte. Returns 1 when a complete sequence decision is ready:
 * *emit set, sc->buf/sc->len hold the sequence bytes (may be zero when
 * dropped). Returns 0 while the sequence is still open. The caller
 * only calls this while a sequence is open (sc->state != 0) or the
 * byte is ESC. */
static int esc_scan_feed(EscScan *sc, unsigned char c, int *emit)
{
    if (!sc->nostore && sc->len < ESC_SEQ_MAX)
        sc->buf[sc->len++] = (char)c;
    else
        sc->nostore = 1;

    switch (sc->state) {
    case 0: /* the ESC that starts the sequence */
        sc->state = 1;
        return 0;
    case 1: /* after ESC: kind selector */
        if (c == '[')
            sc->state = 2;
        else if (c == ']')
            sc->state = 3;
        else if (c == 'P' || c == '_' || c == '^' || c == 'X')
            sc->state = 4;
        else {
            sc->state = 0;
            *emit = 0; /* two-byte escape: never emitted */
            return 1;
        }
        return 0;
    case 2: /* CSI: final byte in 0x40..0x7E; only SGR (m) is kept */
        if (c >= 0x40 && c <= 0x7E) {
            sc->state = 0;
            *emit = (c == 'm' && !sc->nostore);
            return 1;
        }
        return 0;
    case 3: /* OSC: BEL or ESC \ terminates; never emitted */
        if (c == 0x07) {
            sc->state = 0;
            *emit = 0;
            return 1;
        }
        if (c == 0x1b) {
            sc->resume = 3;
            sc->state = 5;
        }
        return 0;
    case 4: /* DCS / APC / PM / SOS: ESC \ terminates; never emitted */
        if (c == 0x1b) {
            sc->resume = 4;
            sc->state = 5;
        }
        return 0;
    default: /* 5: ESC seen inside a string */
        if (c == '\\') {
            sc->state = 0;
            *emit = 0;
            return 1;
        }
        sc->state = sc->resume;
        return 0;
    }
}

struct TuiRowSink
{
    TuiTranscript *t;
    DynamicBuffer *buf; /* staging / scratch; NULL for SINK_COUNT */
    int dest;
    int width;
    int col;
    int open;             /* a row is open (content written since last row end) */
    int image_rows;       /* pending reservation: an image occupies this
                           * many screen rows on the open row (0 = none) */
    EscScan esc;          /* split escape scan state (shared policy) */
    size_t row_start_off; /* content start of the open row */

    /* frame row recording (ring of the last N rows) */
    size_t *rstart;
    size_t *rend;
    int rcap;
    int nrows;
};

/* Deferred emission units (the IMAGE gate's hold). An IMAGE unit whose
 * rendering depends on the terminal profile cannot render at freeze
 * time while the profile is unresolved, so it — and everything that
 * freezes behind it, including byte staging and row closes — is held
 * in freeze order and replayed once the profile resolves (see
 * tui_transcript_commit_pending). Byte-granular ranges reference the
 * owning stream's raw buffer: the trim never runs while the queue is
 * non-empty (it runs only after a write, and a write only happens
 * after a drain), so the offsets stay valid for the hold's duration. */
enum
{
    TDEFER_UNIT,  /* render one emission unit (kind, off, len)     */
    TDEFER_BYTES, /* stage a raw byte range (off..len) verbatim    */
    TDEFER_CLOSE, /* close the open output row (stream end/submit) */
};

typedef struct TuiDefer
{
    unsigned char type;
    int stream_idx; /* 0..n_user; TDEFER_* use it to find the stream */
    TuiBlockKind kind;
    size_t off, len;
    int image_id;
} TuiDefer;

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
    EscScan esc;  /* split escape scan state (see above) */

    int orphan_partial; /* clear() while a row was open: forget, emit nothing */
    unsigned long commit_count;

    /* IMAGE tier: a copy of the runtime's profile verdict, refreshed at
     * every commit pass (copied, not borrowed: the runtime may be torn
     * down before the transcript, and emission reads it at freeze
     * time). image_seq assigns TuiBlock.image_id (monotonic for the
     * transcript's lifetime — never reset by clear, so kitty i= ids
     * are never re-used). */
    TuiTerminalProfile profile;
    unsigned long image_seq;

    /* deferred units (the IMAGE gate's hold), in freeze order */
    TuiDefer *defer;
    size_t defer_len, defer_cap;
    int draining; /* inside defer_drain: enqueue checks are bypassed */

    /* live planner scratch (reused; see memory-reuse principle) */
    DynamicBuffer *live_scratch;
    size_t *row_starts;
    size_t *row_ends;
    int row_ring_cap;
};

/* Forward declarations for the block-mode helpers (used by
 * process_line, defined below it). */
static void stream_open_block(TuiTranscript *t, TuiStream *s, size_t off,
                              size_t end, TuiBlockKind kind);
static void stream_extend_block(TuiTranscript *t, TuiStream *s, size_t end);
static void stream_finalize_block(TuiTranscript *t, TuiStream *s);
static void emit_unit(TuiTranscript *t, TuiStream *s, TuiBlockKind kind,
                      size_t off, size_t len, int image_id);
static void transcript_close_row(TuiTranscript *t);
static int defer_active(const TuiTranscript *t);
static int defer_push(TuiTranscript *t, int type, int stream_idx,
                      TuiBlockKind kind, size_t off, size_t len, int image_id);

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
        size_t cl = 0;
        int w = tui_next_cluster(bytes + i, len - i, &cl);
        if (cl == 0)
            break;
        col += w;
        i += cl;
    }
    if (esc_state)
        *esc_state = esc;
    return col;
}

/* ------------------------------------------------------------------ */
/* Byte-mode staging (system stream + unlabeled fences)                */
/* ------------------------------------------------------------------ */

/* Emit the logical line break (\n / \r\n normalized to \r\n). This is
 * the only break the commit path emits: the terminal owns wrapping. */
static void stage_row_break(TuiTranscript *t)
{
    dynamic_buffer_append(t->staging, "\r\n", 2);
    t->row_open = 0;
    t->row_col = 0;
}

/* Append glyph bytes. The commit path inserts NO width break — the
 * terminal owns soft-wrapping and reflow (see tui_row_text). */
static void stage_glyph(TuiTranscript *t, const char *bytes, size_t len,
                        int gw)
{
    dynamic_buffer_append(t->staging, bytes, len);
    t->row_open = 1;
    t->row_col += gw;
}

/* Consume escape bytes with the scan policy; only SGR is emitted (as
 * zero-width bytes). Resumes a sequence split across chunks. */
static void stage_escape(TuiTranscript *t, const char *bytes, size_t len,
                         size_t *i)
{
    while (*i < len) {
        unsigned char c = (unsigned char)bytes[*i];
        if (t->esc.state == 0 && c != 0x1b)
            return;
        int emit = 0;
        (*i)++;
        if (esc_scan_feed(&t->esc, c, &emit)) {
            if (emit) {
                dynamic_buffer_append(t->staging, t->esc.buf, t->esc.len);
                t->row_open = 1; /* bytes (even zero-width) are on the row */
            }
            t->esc.len = 0;
            t->esc.nostore = 0;
        }
    }
}

/* Normalize raw byte-run text into the staging buffer. Handles
 * LF/CRLF/CR -> \r\n, hard tabs (physical-column stops), ANSI
 * passthrough with no width wrap (the terminal soft-wraps), and no raw
 * \r on output. */
static void stage_bytes(TuiTranscript *t, const char *bytes, size_t len)
{
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)bytes[i];
        if (t->esc.state) {
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
            /* Tab stops sit at physical columns; the committed row may
             * have soft-wrapped, so derive the physical column from
             * the running display total. */
            int w = t->width > 1 ? t->width : 80;
            int stop = 8 - ((t->row_col % w) % 8);
            for (int k = 0; k < stop; k++)
                dynamic_buffer_append(t->staging, " ", 1);
            t->row_open = 1;
            t->row_col += stop;
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
        size_t cl = 0;
        int gw = tui_next_cluster(bytes + i, len - i, &cl);
        if (cl == 0)
            break;
        stage_glyph(t, bytes + i, cl, gw);
        i += cl;
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
        if (defer_active(t)) {
            /* the gate holds units in freeze order; byte ranges defer
             * with them (referencing the raw buffer, which cannot be
             * trimmed while the queue is non-empty) */
            defer_push(t, TDEFER_BYTES, (int)(s - t->streams),
                       TUI_BLOCK_RAW, s->stage_pos, upto - s->stage_pos, 0);
        } else {
            stage_bytes(t, s->raw->data + s->stage_pos, upto - s->stage_pos);
        }
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
            s->t->esc.state = 0;
            s->t->esc.len = 0;
        }
    }
    sink_record_row(s);
    s->open = 0;
    s->col = 0;
}

/* Consume escape bytes with the shared scan policy; only SGR survives
 * (zero-width, styling). Resumes a sequence split across calls. */
static void sink_escape(TuiRowSink *s, const char *utf8, size_t len,
                        size_t *i)
{
    while (*i < len) {
        unsigned char c = (unsigned char)utf8[*i];
        if (s->esc.state == 0 && c != 0x1b)
            return;
        int emit = 0;
        (*i)++;
        if (esc_scan_feed(&s->esc, c, &emit)) {
            if (emit && s->buf) {
                /* D9: note the row BEFORE appending — a leading attr
                 * opens the row range, so the frame's recorded range
                 * includes it (capture happens at row end). */
                sink_note_row_start(s);
                dynamic_buffer_append(s->buf, s->esc.buf, s->esc.len);
            }
            s->esc.len = 0;
            s->esc.nostore = 0;
        }
    }
}

/* Append UTF-8 text to the current row (public: tui_row_text).
 *
 * The commit path (SINK_COMMIT) inserts NO width break: committed
 * bytes are the scrollback's truth, and the terminal owns
 * soft-wrapping — a long logical line stays one byte run and the
 * terminal (portty) reflows it on resize. The live frame (SINK_FRAME
 * / SINK_COUNT) still wraps explicitly at the width: those bytes feed
 * the inline frame's own row geometry, which the cursor math counts
 * by row breaks. */
void tui_row_text(TuiRowSink *s, const char *utf8, size_t len)
{
    if (!s || !utf8)
        return;
    int wrap = s->dest != SINK_COMMIT;
    int w = s->width > 1 ? s->width : 1;
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)utf8[i];
        if (s->esc.state) {
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
            /* Emulated tab stops sit at physical columns. The commit
             * path may have soft-wrapped, so derive the physical
             * column from the running display total. */
            int phys = wrap ? s->col : (s->col % w);
            int stop = 8 - (phys % 8);
            if (wrap && s->col + stop > w)
                stop = w - s->col;
            sink_note_row_start(s);
            for (int k = 0; k < stop; k++) {
                if (s->buf)
                    dynamic_buffer_append(s->buf, " ", 1);
            }
            s->col += stop;
            if (wrap && s->col >= w)
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
        size_t cl = 0;
        int gw = tui_next_cluster(utf8 + i, len - i, &cl);
        if (cl == 0)
            break;
        if (wrap && s->col + gw > w)
            sink_row_break(s); /* explicit wrap, never soft-wrap */
        sink_note_row_start(s);
        if (s->buf)
            dynamic_buffer_append(s->buf, utf8 + i, cl);
        s->col += gw;
        if (wrap && s->col >= w)
            sink_row_break(s);
        i += cl;
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
    if (a.strikethrough)
        buf[n++] = ';', buf[n++] = '9';
    if (a.has_fg)
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, ";38;2;%d;%d;%d",
                              a.fg_r, a.fg_g, a.fg_b);
    if (a.has_bg)
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, ";48;2;%d;%d;%d",
                              a.bg_r, a.bg_g, a.bg_b);
    buf[n++] = 'm';
    /* D9: the attr may LEAD the row. Open the row range first, or the
     * frame's recorded range (captured at row end) starts after it and
     * the attr is dropped from the emitted bytes. */
    sink_note_row_start(s);
    dynamic_buffer_append(s->buf, buf, n);
}

void tui_row_attr_reset(TuiRowSink *s)
{
    static const char reset[] = "\x1b[0m";
    if (!s || !s->buf || s->dest == SINK_COUNT)
        return;
    if (!s->open && s->dest == SINK_FRAME && s->rstart && s->rcap > 0 &&
        s->nrows > 0) {
        /* D9: a reset after the row closed (tui_row_text broke at an
         * exact multiple of the width) terminates the row just
         * rendered. Extend that row's recorded end — the reset adds no
         * row and cannot inflate tui_transcript_live_rows. */
        int idx = (s->nrows - 1) % s->rcap;
        s->rend[idx] = s->buf->len + sizeof(reset) - 1;
    } else {
        sink_note_row_start(s);
    }
    dynamic_buffer_append(s->buf, reset, sizeof(reset) - 1);
}

void tui_row_pad_to(TuiRowSink *s, int display_col)
{
    if (!s)
        return;
    int wrap = s->dest != SINK_COMMIT;
    int w = s->width > 1 ? s->width : 1;
    if (display_col > w)
        display_col = w;
    if (display_col <= s->col)
        return;
    int pad = display_col - s->col;
    sink_note_row_start(s);
    if (s->buf) {
        for (int k = 0; k < pad; k++)
            dynamic_buffer_append(s->buf, " ", 1);
    }
    s->col += pad;
    if (wrap && s->col >= w)
        sink_row_break(s);
}

void tui_row_end(TuiRowSink *s)
{
    if (!s)
        return;
    if (s->dest == SINK_COMMIT) {
        /* An image row reserves N screen rows: the row terminators
         * (and the transcript's row accounting) cover all of them, so
         * the cursor lands exactly one row below the image and the
         * next unit starts under it. */
        int n = s->image_rows > 0 ? s->image_rows : 1;
        s->image_rows = 0;
        for (int k = 0; k < n; k++)
            sink_row_break(s); /* appends \r\n, closes the row */
        return;
    }
    sink_record_row(s);
    s->open = 0;
    s->col = 0;
}

void tui_image_spec_init(TuiImageSpec *spec, TuiImageTransport transport,
                         TuiImageFormat format, const unsigned char *data,
                         size_t data_len, int src_w, int src_h, int disp_cols,
                         int disp_rows, int image_id)
{
    if (!spec)
        return;
    memset(spec, 0, sizeof(*spec));
    spec->transport = transport;
    spec->format = format;
    spec->data = data;
    spec->data_len = data_len;
    spec->src_w = src_w;
    spec->src_h = src_h;
    spec->disp_cols = disp_cols > 0 ? disp_cols : 1;
    spec->disp_rows = disp_rows > 0 ? disp_rows : 1;
    spec->image_id = image_id;
}

/* ------------------------------------------------------------------ */
/* Image transports                                                    */
/* ------------------------------------------------------------------ */

/* Raw source bytes per 4096-byte base64 chunk (kitty's documented
 * chunk ceiling; base64 grows 3 -> 4, and every chunk but the last
 * must be a multiple of 4 bytes of encoded output, which 3072 raw
 * bytes guarantees). */
#define IMG_KITTY_CHUNK_RAW 3072

/* kitty graphics protocol: APC G <keys> ; <base64 chunk> ST, keys on
 * the first chunk only, m=1/m=0 chunk framing. f=100 = PNG (kitty
 * decodes the container; JPEG/GIF do not ride this protocol). a=T:
 * transmit AND display — the protocol's default action is a=t, which
 * only STORES the image, so an omitted a= reserves our rows and shows
 * nothing (kitty places on 'T' alone; the spec's own chunked sender
 * carries "a=T,f=100"). c/r = display size in cells — both given, so
 * the layout is ours, not the terminal's; kitty letterboxes any aspect
 * drift. s/v = the source pixels: kitty reads the size from the PNG
 * and ignores them, but they must be PRESENT — a receiver may require
 * the full transmission key set on the first chunk even for f=100
 * (coffer, and so portty, rejects a transfer without s/v as
 * "EINVAL:missing format/dimensions"). C=1: the terminal must NOT move
 * the cursor itself (we reserve rows with our own row terminators —
 * the byte accounting stays ours). q=2: no reply unless the load fails
 * (an unsolicited reply is dropped by the input parser's reply ring
 * anyway; the probe owns that slot). */
static void image_write_kitty(TuiRowSink *s, const TuiImageSpec *spec)
{
    if (spec->format != TUI_IMAGE_PNG)
        return; /* only PNG rides f=100; the app's tier choice guards it */

    char head[160];
    int more = spec->data_len > IMG_KITTY_CHUNK_RAW;
    int hl = snprintf(head, sizeof(head),
                      "\x1b_Ga=T,f=100,s=%d,v=%d,c=%d,r=%d,i=%d,q=2,C=1%s;",
                      spec->src_w, spec->src_h, spec->disp_cols,
                      spec->disp_rows,
                      spec->image_id > 0 ? spec->image_id : 1,
                      more ? ",m=1" : "");
    if (hl <= 0)
        return;

    size_t off = 0;
    for (;;) {
        size_t take = spec->data_len - off;
        if (take > IMG_KITTY_CHUNK_RAW)
            take = IMG_KITTY_CHUNK_RAW;
        dynamic_buffer_append(s->buf, head, (size_t)hl);
        char b64[BOBA_BASE64_ENCODED_LEN(IMG_KITTY_CHUNK_RAW)];
        size_t enc = boba_base64_encode(spec->data + off, take, b64,
                                        sizeof(b64));
        if (enc)
            dynamic_buffer_append(s->buf, b64, enc);
        dynamic_buffer_append_str(s->buf, "\x1b\\");
        off += take;
        if (off >= spec->data_len)
            break;
        /* subsequent chunks: only m (and optionally q) keys */
        int last = off + IMG_KITTY_CHUNK_RAW >= spec->data_len;
        hl = snprintf(head, sizeof(head), "\x1b_Gq=2,m=%s;",
                      last ? "0" : "1");
        if (hl <= 0)
            return;
    }
}

/* iTerm2 inline images: OSC 1337 ; File=inline=1 ; size=<decoded> ;
 * width=<cells> ; height=<cells> : <base64> BEL. width/height in
 * character cells, both explicit, so the row reservation matches what
 * we frame. iTerm2 decodes PNG/JPEG/GIF itself. One OSC carries the
 * whole payload (iTerm2 has no chunked form; the 4 MiB staged cap
 * bounds it). */
static void image_write_iterm2(TuiRowSink *s, const TuiImageSpec *spec)
{
    char head[128];
    int hl = snprintf(head, sizeof(head),
                      "\x1b]1337;File=inline=1;size=%zu;width=%d;height=%d:",
                      spec->data_len, spec->disp_cols, spec->disp_rows);
    if (hl <= 0)
        return;
    dynamic_buffer_append(s->buf, head, (size_t)hl);

    size_t off = 0;
    while (off < spec->data_len) {
        size_t take = spec->data_len - off;
        if (take > IMG_KITTY_CHUNK_RAW)
            take = IMG_KITTY_CHUNK_RAW; /* just an encoding batch size */
        char b64[BOBA_BASE64_ENCODED_LEN(IMG_KITTY_CHUNK_RAW)];
        size_t enc =
            boba_base64_encode(spec->data + off, take, b64, sizeof(b64));
        if (enc)
            dynamic_buffer_append(s->buf, b64, enc);
        off += take;
    }
    dynamic_buffer_append(s->buf, "\x07", 1);
}

void tui_row_image(TuiRowSink *s, const TuiImageSpec *spec)
{
    if (!s || !spec)
        return;
    if (s->dest != SINK_COMMIT) {
        /* The live region renders placeholders, never images: every
         * frame would re-transmit the payload. The assert names the
         * contract violation in debug builds; release drops it. */
        assert(!"tui_row_image called on the live/count sink");
        return;
    }
    sink_note_row_start(s);
    switch (spec->transport) {
    case TUI_IMAGE_KITTY:
        image_write_kitty(s, spec);
        break;
    case TUI_IMAGE_ITERM2:
        image_write_iterm2(s, spec);
        break;
    default:
        break;
    }
    s->open = 1;
    s->image_rows = spec->disp_rows > 0 ? spec->disp_rows : 1;
    s->col += spec->disp_cols > 0 ? spec->disp_cols : 1;
}

/* ------------------------------------------------------------------ */
/* Deferred emission units (the IMAGE gate's hold)                     */
/* ------------------------------------------------------------------ */

/* Enqueue one hold record. Returns 0 on success, -1 when the queue
 * cannot grow (the caller renders its degraded fallback instead —
 * guarantees beat OOM silence). */
static int defer_push(TuiTranscript *t, int type, int stream_idx,
                      TuiBlockKind kind, size_t off, size_t len, int image_id)
{
    if (t->defer_len == t->defer_cap) {
        size_t ncap = t->defer_cap ? t->defer_cap * 2 : 8;
        TuiDefer *nd = realloc(t->defer, ncap * sizeof(*nd));
        if (!nd)
            return -1;
        t->defer = nd;
        t->defer_cap = ncap;
    }
    TuiDefer *d = &t->defer[t->defer_len++];
    d->type = (unsigned char)type;
    d->stream_idx = stream_idx;
    d->kind = kind;
    d->off = off;
    d->len = len;
    d->image_id = image_id;
    return 0;
}

/* Should staging defer right now? TRUE only while the gate holds
 * units (and never inside the drain, which replays the queue). */
static int defer_active(const TuiTranscript *t)
{
    return !t->draining && t->defer_len > 0;
}

/* Replay every held unit in freeze order, now that the profile has a
 * verdict. Rendering and byte staging run through their normal paths
 * with the enqueue checks disarmed (t->draining). */
static void defer_drain(TuiTranscript *t)
{
    t->draining = 1;
    for (size_t i = 0; i < t->defer_len; i++) {
        TuiDefer *d = &t->defer[i];
        TuiStream *s = d->stream_idx >= 0 ? &t->streams[d->stream_idx] : NULL;
        switch (d->type) {
        case TDEFER_CLOSE:
            transcript_close_row(t);
            break;
        case TDEFER_BYTES:
            if (s && d->len > 0) {
                /* clamp to what the buffer still holds: a system-stream
                 * finalize may have cleared it while the unit was held */
                size_t avail = s->raw->len > d->off ? s->raw->len - d->off : 0;
                if (d->len <= avail)
                    stage_bytes(t, s->raw->data + d->off, d->len);
            }
            break;
        default:
            if (s)
                emit_unit(t, s, d->kind, d->off, d->len, d->image_id);
            break;
        }
    }
    t->defer_len = 0;
    t->draining = 0;
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
        t->esc.state = 0;
        t->esc.len = 0;
    }
}

static void sink_commit_end(TuiRowSink *s)
{
    if (s->open)
        tui_row_end(s);
}

/* Render one emission unit (a frozen line or a finalized block). The
 * stream is the config index, or -1 for the system stream (which is
 * byte-emitted and never reaches here). IMAGE units with a measure
 * callback cannot render until the terminal profile resolves, so an
 * unresolved profile defers the unit (and everything behind it) to
 * the commit pass — see the defer queue and commit gate. */
static void emit_unit(TuiTranscript *t, TuiStream *s, TuiBlockKind kind,
                      size_t off, size_t len, int image_id)
{
    /* order-preserving hold: anything freezing while an IMAGE unit
     * waits for the profile defers behind it (guarantee 4) */
    if (!t->draining &&
        (t->defer_len > 0 ||
         (kind == TUI_BLOCK_IMAGE && t->cfg.measure_image &&
          !t->profile.resolved))) {
        if (defer_push(t, TDEFER_UNIT, (int)(s - t->streams), kind, off, len,
                       image_id) == 0)
            return;
        /* fall through: allocation failed — render degraded below */
    }

    TuiBlock blk;
    memset(&blk, 0, sizeof(blk));
    blk.kind = kind;
    blk.state = TUI_BLOCK_FINAL;
    blk.off = off;
    blk.len = len;
    blk.image_id = image_id;
    blk.stream = s->cls ? (int)(s - t->streams) : -1;

    /* IMAGE tier: measure with the (resolved) profile, then render
     * through render_image. Measure's 0, or no measure callback,
     * degrades to render_block (the app's marker). */
    if (kind == TUI_BLOCK_IMAGE && t->cfg.measure_image &&
        t->profile.resolved) {
        int rows = 0;
        if (t->cfg.measure_image(&blk, s->raw->data + off, len, &t->profile,
                                 &rows, t->cfg.user_data) &&
            rows >= 1) {
            if (t->cfg.render_image) {
                TuiRowSink sink;
                sink_commit_begin(t, &sink);
                t->cfg.render_image(&blk, s->raw->data + off, len, sink.width,
                                    rows, &sink, t->cfg.user_data);
                sink_commit_end(&sink);
                /* unit rows always end terminated */
                t->row_open = 0;
                t->row_col = 0;
                t->esc.state = 0;
                t->esc.len = 0;
            }
            return;
        }
    }

    if (!t->cfg.render_block)
        return;
    TuiRowSink sink;
    sink_commit_begin(t, &sink);
    t->cfg.render_block(&blk, s->raw->data + off, len, sink.width, &sink,
                        t->cfg.user_data);
    sink_commit_end(&sink);
    /* unit rows always end terminated */
    t->row_open = 0;
    t->row_col = 0;
    t->esc.state = 0;
    t->esc.len = 0;
}

/* Freeze the pending line, if any. */
static void stream_freeze_pend(TuiTranscript *t, TuiStream *s)
{
    if (!s->has_pend)
        return;
    emit_unit(t, s, s->pend_kind, s->pend_off, s->pend_len, 0);
    s->has_pend = 0;
}

/* Block-mode (table, image) helpers. */
static void stream_open_block(TuiTranscript *t, TuiStream *s, size_t off,
                              size_t end, TuiBlockKind kind)
{
    if (end <= off) {
        s->has_block = 0;
        return;
    }
    s->has_block = 1;
    s->block_off = off;
    s->block_len = end - off;
    s->block_kind = kind;
    if (kind == TUI_BLOCK_IMAGE)
        s->block_image_id = (int)++t->image_seq;
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
    emit_unit(t, s, s->block_kind, s->block_off, s->block_len,
              s->block_image_id);
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
                emit_unit(t, s, s->container_kind, ls, llen, 0);
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
        if (okind == TUI_BLOCK_TABLE || okind == TUI_BLOCK_IMAGE) {
            stream_open_block(t, s, ls, end, okind);
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
            /* prev is the live block's last line: split the block —
             * finalize everything before prev as-is, then open a new
             * block of out_kind whose first line is prev. */
            size_t block_end = s->block_off + s->block_len;
            if (!s->has_prev || s->prev_off < s->block_off ||
                s->prev_off >= block_end) {
                /* contract violation (prev outside the live block):
                 * keep the block intact in release, assert in debug */
                assert(!"RECLASSIFY_PREV outside the live block");
                stream_extend_block(t, s, end);
                break;
            }
            size_t before = s->prev_off - s->block_off;
            if (before > 0)
                emit_unit(t, s, s->block_kind, s->block_off, before,
                          s->block_image_id);
            s->block_off = s->prev_off;
            s->block_len = end - s->block_off;
            s->block_kind = okind;
            if (okind == TUI_BLOCK_IMAGE)
                s->block_image_id = (int)++t->image_seq;
            s->cur_kind = TUI_BLOCK_PARAGRAPH;
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
            stream_open_block(t, s, s->pend_off, end, okind);
        } else {
            /* line-mode reclass (e.g. setext): the pair is one unit */
            size_t off = s->pend_off;
            emit_unit(t, s, okind, off, end - off, 0);
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
    t->esc.state = 0;
    t->esc.len = 0;
}

int tui_transcript_commit_pending(TuiTranscript *t, TuiRuntime *rt)
{
    if (!t || !rt)
        return 0;

    /* The profile is the runtime's; refresh our copy for this pass
     * (the IMAGE tier reads it at emission). */
    t->profile = *tui_runtime_terminal_profile(rt);

    /* The IMAGE gate: deferred IMAGE units cannot render until the
     * profile has a verdict (the transport choice comes from it), so
     * the held units — in freeze order, see the defer queue — wait
     * here. probe_ensure reaches one immediately when no probe was
     * declared (an embedding with no view); an outstanding probe
     * resolves on its 250 ms deadline via the tick, so the hold is
     * bounded. An app without measure_image never defers (nothing
     * about a text-degraded IMAGE unit depends on the profile), so
     * it is never gated. */
    if (t->defer_len > 0 && !t->profile.resolved) {
        tui_runtime_probe_ensure(rt);
        t->profile = *tui_runtime_terminal_profile(rt);
        if (!t->profile.resolved)
            return 0;
    }

    if (t->orphan_partial) {
        tui_runtime_transcript_orphan(rt);
        t->orphan_partial = 0;
        t->row_open = 0;
        t->row_col = 0;
        t->esc.state = 0;
        t->esc.len = 0;
    }
    if (t->defer_len > 0)
        defer_drain(t);
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
    s->block_image_id = 0;
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
        s->live_attr = t->cfg.streams ? t->cfg.streams[i].live_attr : NULL;
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
    free(t->defer);
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
        size_t off = s->raw->len;
        dynamic_buffer_append(s->raw, msg.data.stream.text,
                              msg.data.stream.len);
        if (defer_active(t)) {
            defer_push(t, TDEFER_BYTES, (int)t->n_user, TUI_BLOCK_RAW, off,
                       msg.data.stream.len, 0);
        } else {
            stage_bytes(t, msg.data.stream.text, msg.data.stream.len);
        }
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
        if (defer_active(t))
            defer_push(t, TDEFER_CLOSE, -1, TUI_BLOCK_PARAGRAPH, 0, 0, 0);
        else
            transcript_close_row(t);
        return tui_update_result_none();
    }

    case TUI_MSG_TRANSCRIPT_SUBMIT:
    {
        /* finalize every LIVE block across all streams; no echo */
        for (size_t i = 0; i <= t->n_user; i++)
            stream_finalize(t, &t->streams[i]);
        if (defer_active(t))
            defer_push(t, TDEFER_CLOSE, -1, TUI_BLOCK_PARAGRAPH, 0, 0, 0);
        else
            transcript_close_row(t);
        return tui_update_result_none();
    }

    case TUI_MSG_TRANSCRIPT_CLEAR:
    {
        /* new chat: reset everything; emit nothing. Held (deferred)
         * units are dropped with their bytes — the streams reset
         * below. image_seq is deliberately NOT reset: kitty i= ids
         * must never be re-used while the old images are still in the
         * scrollback. */
        if (t->row_open)
            t->orphan_partial = 1;
        t->row_open = 0;
        t->row_col = 0;
        t->esc.state = 0;
        t->esc.len = 0;
        t->defer_len = 0;
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
        /* Byte-emitted content (system stream, unlabeled fences) is
         * committed on receipt, including a trailing partial row — it
         * has no live representation; showing it here would paint the
         * same bytes twice (frame + scrollback). */
        if (!s->cls || (s->in_container && s->container_byte))
            continue;
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
                blk.image_id = s->block_image_id;
                blk.stream = (int)i;
                t->cfg.render_live(&blk, s->raw->data + s->block_off,
                                   s->block_len, w, cap, &sink,
                                   t->cfg.user_data);
                if (sink.open)
                    sink_row_break(&sink);
            }
        } else {
            /* boba-owned plain rows: lookahead line + partial tail.
             * The stream's app-declared attr (if any) wraps the run;
             * the reset is D9's closed-row case when the run wrapped
             * at an exact multiple of the width. */
            size_t start = s->has_pend ? s->pend_off : s->tail_off;
            if (start < s->raw->len) {
                if (s->live_attr)
                    tui_row_attr(&sink, *s->live_attr);
                tui_row_text(&sink, s->raw->data + start,
                             s->raw->len - start);
                if (s->live_attr)
                    tui_row_attr_reset(&sink);
            }
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

size_t tui_transcript_staged_bytes(const TuiTranscript *t)
{
    if (!t || !t->staging)
        return 0;
    return t->staging->len;
}

int tui_transcript_commit_gated(const TuiTranscript *t)
{
    if (!t)
        return 0;
    return t->defer_len > 0; /* IMAGE units held for the profile */
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
