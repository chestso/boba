/* stream.h - streaming transcript component for boba
 *
 * A transcript is a set of named text streams (content, reasoning,
 * system) that the app feeds deltas into. boba turns the deltas into
 * blocks and guarantees the one invariant that makes inline chat
 * transcripts correct:
 *
 *     the scrollback receives only bytes whose rendering can no
 *     longer change; everything provisional lives in a windowed,
 *     repaintable live region.
 *
 * Ownership boundary: boba owns the terminal, the app owns the
 * meaning. boba never parses markdown (or any other grammar). The
 * app supplies, per stream:
 *
 *   - a classifier: one question per completed line — continues /
 *     blank / block-start (with kind) / reclassify-previous (with
 *     kind) / container-open / container-close. boba ships a
 *     blank-line default (tui_classifier_default) correct for logs,
 *     diffs, and JSON lines.
 *   - renderers: render_block() for finalized emission units (line
 *     or block granularity), render_live() for provisional block
 *     content. Both write rows through an opaque TuiRowSink — the
 *     app cannot emit framing bytes, cursor movement, or raw
 *     buffer access. boba owns terminators, EL, CRLF
 *     normalization, capability sequences and cursor placement.
 *
 * Emission granularity is keyed by block kind, because retention is
 * only needed where rendering can still change:
 *
 *   byte  - RAW (the system stream: tool panels, error bodies,
 *           command replies) and unlabeled fences. Rendering is
 *           byte-identity; deltas stage straight to the commit path
 *           mid-line. Nothing is retained, no live block exists.
 *   line  - paragraph, heading, list, quote, and labeled fences.
 *           A line freezes when the NEXT line completes with a
 *           non-reclassifying verdict (one-line lookahead), so a
 *           prose answer scrolls into the scrollback as it streams
 *           with ~one line of lag. Render_block is called exactly
 *           once per frozen line.
 *   block - table (column layout depends on every cell): the live
 *           block is windowed to its tail and rendered once, at
 *           finalize.
 *
 * Fence labeling flows through the classifier: CONTAINER_OPEN's
 * out_kind names TUI_BLOCK_FENCE (labeled; line-emitted and
 * app-rendered per line so a resumable highlighter can run) or
 * TUI_BLOCK_FENCE_PLAIN (unlabeled; byte-emitted verbatim). This
 * is the one verdict besides BLOCK_START/RECLASSIFY_PREV that
 * honors out_kind.
 *
 * Committing: boba owns the commit path. Attach the transcript to
 * the runtime with tui_runtime_set_transcript(); the runtime runs
 * the commit pass at the top of tui_runtime_flush(), so all units
 * finalized within one update/drain coalesce into a single
 * tui_runtime_transcript_write — one erase/write/re-render with one
 * geometry baseline per batch. boba is the only caller of that
 * seam; chat applications never print to the scrollback themselves.
 *
 * Inline mode only: "commit to the scrollback" has no meaning in an
 * alt-screen layout; this component targets boba's inline frame.
 */

#ifndef BOBA_STREAM_H
#define BOBA_STREAM_H

#include <stddef.h>

#include "component.h"
#include "dynamic_buffer.h"
#include "msg.h"

/* ------------------------------------------------------------------ */
/* Blocks                                                              */
/* ------------------------------------------------------------------ */

typedef enum
{
    TUI_BLOCK_PARAGRAPH,
    TUI_BLOCK_HEADING,
    TUI_BLOCK_FENCE,       /* verbatim body; labeled: line-emitted   */
    TUI_BLOCK_FENCE_PLAIN, /* verbatim body; unlabeled: byte-emitted */
    TUI_BLOCK_TABLE,
    TUI_BLOCK_QUOTE,
    TUI_BLOCK_LIST,
    TUI_BLOCK_IMAGE,
    TUI_BLOCK_RAW, /* app-defined via classifier; byte-emitted */
} TuiBlockKind;

typedef enum
{
    TUI_BLOCK_LIVE,      /* mutable; renders in the live region, windowed */
    TUI_BLOCK_FINAL,     /* layout frozen; not yet emitted                */
    TUI_BLOCK_COMMITTED, /* bytes are in the scrollback                   */
} TuiBlockState;

/* A block record. `off`/`len` are a byte range into the stream's raw
 * buffer (owned by the transcript; valid for the duration of the
 * render callback). Records are borrowed — never retained, never
 * freed, never mutated by the app. */
typedef struct TuiBlock
{
    TuiBlockKind kind;
    TuiBlockState state;
    size_t off, len;         /* byte range into the stream's raw buffer */
    int n_cols;              /* TABLE: locked at delimiter              */
    unsigned char col_align; /* TABLE: 2 bits per column                */
    int image_id;            /* IMAGE: app-assigned                     */
} TuiBlock;

/* ------------------------------------------------------------------ */
/* Row sink                                                            */
/* ------------------------------------------------------------------ */

/* Opaque. boba owns the destination buffer and every framing byte.
 * The handle is valid only for the duration of the callback and must
 * not be retained. The same sink type backs both the commit path and
 * the live path; the app cannot tell them apart (and must not try —
 * destination-dependent code is a design error). */
typedef struct TuiRowSink TuiRowSink;

/* Append UTF-8 text to the current row. Hard tabs expand to the next
 * multiple of 8 display columns; rows wrap explicitly at the
 * terminal width (never terminal soft-wrap), so boba's row math is
 * always exact. Control bytes other than \t are not allowed; rows
 * are ended with tui_row_end().
 *
 * Escape bytes in text are scanned, not trusted: SGR sequences
 * (styling) pass through; every framing sequence — cursor movement,
 * EL, OSC, APC/DCS — is dropped. Framing is unrepresentable by
 * construction; sequences split across calls are buffered until
 * complete. */
void tui_row_text(TuiRowSink *s, const char *utf8, size_t len);

/* Apply / reset an SGR attribute for subsequent text on this row.
 * boba maps TuiAttr to SGR byte sequences. */
typedef struct TuiAttr
{
    int bold;
    int dim;
    int italic;
    int underline;
    int has_fg;
    int fg_r, fg_g, fg_b;
    int has_bg;
    int bg_r, bg_g, bg_b;
} TuiAttr;

void tui_row_attr(TuiRowSink *s, TuiAttr a);
void tui_row_attr_reset(TuiRowSink *s);

/* Pad the current row with spaces up to display column `col` (0-based).
 * boba applies glyph widths, so wide-character alignment works without
 * the app owning a second width table. No-op when already past. */
void tui_row_pad_to(TuiRowSink *s, int display_col);

/* End the current row: terminator + EL, framed for the destination
 * (commit rows are \r\n-terminated; frame rows are EL-prefixed and
 * pre-wrapped). Exactly one row is emitted per call. */
void tui_row_end(TuiRowSink *s);

/* Emit an image at the current position. `spec` is a boba-side type
 * (frozen when the image tier lands); the app builds it with boba's
 * emit helpers and never writes capability sequences itself. */
typedef struct TuiImageSpec TuiImageSpec;
void tui_row_image(TuiRowSink *s, const TuiImageSpec *spec);

/* ------------------------------------------------------------------ */
/* Classifier seam                                                     */
/* ------------------------------------------------------------------ */

typedef enum
{
    TUI_LINE_CONTINUES,       /* extends the current block             */
    TUI_LINE_BLANK,           /* block separator                       */
    TUI_LINE_BLOCK_START,     /* opens a new block (app names the kind) */
    TUI_LINE_RECLASSIFY_PREV, /* prev line's meaning changed (para->table) */
    TUI_LINE_CONTAINER_OPEN,  /* verbatim until CLOSE (fence)          */
    TUI_LINE_CONTAINER_CLOSE,
} TuiLineClass;

/* One classifier instance per stream (state is per stream; content
 * and reasoning must not share fence state). */
typedef struct TuiClassifier
{
    void *state; /* app-owned */

    /* Returns the verdict for `line`. `prev` is the previous completed
     * line's bytes (borrowed, boba-owned, NULL for the stream's first
     * line). out_kind must be written for BLOCK_START,
     * RECLASSIFY_PREV and CONTAINER_OPEN; ignored otherwise. */
    TuiLineClass (*classify)(void *state, const char *line, size_t len,
                             const char *prev, size_t prev_len,
                             TuiBlockKind *out_kind);

    /* Called at stream end and on transcript clear. */
    void (*reset)(void *state);
} TuiClassifier;

/* boba's default: blank-line separated blocks, kind = paragraph.
 * Stateless; correct for logs, diffs, JSON lines. */
const TuiClassifier *tui_classifier_default(void);

/* ------------------------------------------------------------------ */
/* Transcript component                                                */
/* ------------------------------------------------------------------ */

typedef struct TuiTranscript TuiTranscript;

/* Terminal capability profile (defined in terminal_profile.h when the
 * probe lands; opaque here). */
typedef struct TuiTerminalProfile TuiTerminalProfile;

typedef struct TuiStreamSpec
{
    const char *name; /* "content", "reasoning", "system" */
} TuiStreamSpec;

typedef struct TuiTranscriptConfig
{
    /* Finalized emission unit -> rows. Called exactly once per unit
     * (a line in line mode; a block in block mode; never in byte
     * mode — those bytes are verbatim). */
    void (*render_block)(const TuiBlock *blk, const char *text, size_t len,
                         int width, TuiRowSink *sink, void *user_data);

    /* Provisional content of block-granular kinds, clipped to
     * `rows_cap`. Must be idempotent. */
    void (*render_live)(const TuiBlock *live, const char *text, size_t len,
                        int width, int rows_cap, TuiRowSink *sink,
                        void *user_data);

    /* NULL measure_image => IMAGE blocks degrade to their text. */
    int (*measure_image)(const TuiBlock *, const char *text, size_t len,
                         const TuiTerminalProfile *profile, int *out_rows,
                         void *user_data);
    void (*render_image)(const TuiBlock *, const char *text, size_t len,
                         int col_span, int rows, TuiRowSink *sink,
                         void *user_data);

    /* Per-stream, parallel arrays. A NULL classifier entry uses the
     * blank-line default. Stream -1 is the system stream (raw
     * pass-through; its entry is ignored). */
    const TuiStreamSpec *streams;
    const TuiClassifier **classifiers;
    size_t n_streams;

    void *user_data;
} TuiTranscriptConfig;

TuiTranscript *tui_transcript_create(const TuiTranscriptConfig *cfg);
void tui_transcript_free(TuiTranscript *t);
const TuiComponent *tui_transcript_component(TuiTranscript *t);

/* ----- Messages: the only way state changes (Elm) ----- */

/* A content delta for stream_id. Text is copied into the message
 * (freed by tui_msg_free); safe to post across callback boundaries. */
TuiMsg tui_msg_stream_delta(int stream_id, const char *text, size_t len);

/* Raw transcript entry, system stream only (stream_id < 0). One RAW
 * unit per call, finalized immediately: the escape hatch for tool
 * panels, command replies, error bodies. LF is normalized to CRLF by
 * boba, and escape bytes are scanned: SGR styling survives, framing
 * (cursor movement, EL, OSC, APC/DCS) is dropped — no raw \r and no
 * app-controlled cursor bytes ever reach the scrollback. */
TuiMsg tui_msg_stream_text(int stream_id, const char *text, size_t len);

/* Finalize the stream's live content (stream end / turn end). */
TuiMsg tui_msg_stream_end(int stream_id);

/* Records the user line (reserved for a future session log; no echo)
 * and finalizes every LIVE block across all streams. The echo owner
 * is the runtime: tui_runtime_finish_inline persists the rendered
 * input line, exactly once between the two. */
TuiMsg tui_msg_transcript_submit(const char *text, size_t len);

/* New chat: resets every stream (buffers, blocks, classifiers,
 * watermarks) and emits nothing to the scrollback. A cleared chat
 * simply starts writing below whatever the terminal still shows. */
TuiMsg tui_msg_transcript_clear(void);

/* ----- Direct API (embedding) and component wrapper ----- */

/* Feed one message to the transcript. Embedding parents (e.g. a chat
 * app composing this with a textinput) route stream messages and
 * TUI_MSG_WINDOW_SIZE here; returns no commands. */
TuiUpdateResult tui_transcript_update(TuiTranscript *t, TuiMsg msg);

/* Live region for the current frame: rows the next view() will emit,
 * never the transcript. `rows_cap` is the parent's row budget; the
 * planner clips to the TAIL of the live content. */
void tui_transcript_view(const TuiTranscript *t, DynamicBuffer *out, int width,
                         int rows_cap);

/* Row count of exactly what tui_transcript_view() would emit at the
 * same width/rows_cap (same internal planner; no second
 * implementation to drift), for footer/cursor placement. */
int tui_transcript_live_rows(const TuiTranscript *t, int width, int rows_cap);

/* ----- Test / introspection seams ----- */

/* Number of transcript_write batches driven by the commit pass. */
unsigned long tui_transcript_commit_count(const TuiTranscript *t);

/* Bytes retained in a stream's raw buffer (after trim). stream_id -1
 * addresses the system stream. */
size_t tui_transcript_stream_raw_len(const TuiTranscript *t, int stream_id);

#endif /* BOBA_STREAM_H */
