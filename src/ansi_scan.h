/* ansi_scan.h - the one escape-sequence grammar (internal).
 *
 * Two consumers need to walk the same bytes for different reasons:
 *
 *   - the transcript seam (stream.c) classifies a sequence to decide
 *     whether it may be emitted as SGR or must be dropped as framing;
 *   - the measuring helpers (unicode.c) and the row arithmetic
 *     (tui_rowcols_advance) only need to know that a sequence is not
 *     text, so it occupies no columns.
 *
 * They must agree on where a sequence begins and ends, or the same
 * string measures differently depending on which component sees it: the
 * old hand-rolled CSI-only scanners counted an OSC 8 hyperlink's payload
 * as visible columns, while the transcript dropped it. So the grammar
 * lives here, and the EMISSION POLICY stays with the caller (SGR kept
 * for the transcript, everything skipped for measurement).
 *
 * The scanner is one byte at a time and carries state across chunk
 * boundaries, so a sequence split between two reads is decided on the
 * whole sequence.
 */

#ifndef BOBA_ANSI_SCAN_H
#define BOBA_ANSI_SCAN_H

#include <stddef.h>

/* Longest sequence whose bytes are buffered. A longer sequence is still
 * parsed (so it measures as zero) but its bytes are not kept; the
 * transcript seam drops such an SGR rather than emit a truncated one. */
#define TUI_ANSI_SEQ_MAX 64

typedef struct TuiAnsiScan
{
    int state;  /* 0 ground, 1 after ESC, 2 CSI, 3 OSC, 4 string, 5 ESC-in-string */
    int resume; /* state to return to from 5 (OSC vs DCS/APC/PM/SOS) */
    int nostore;
    size_t len;
    char buf[TUI_ANSI_SEQ_MAX];
} TuiAnsiScan;

/* What a completed sequence is. NONE means the sequence is still open. */
typedef enum TuiAnsiKind
{
    TUI_ANSI_KIND_NONE = 0,
    TUI_ANSI_KIND_SGR,   /* CSI ... m: styling, keepable */
    TUI_ANSI_KIND_OTHER, /* any other complete sequence: framing */
} TuiAnsiKind;

/* Feed one byte. Call it while sc->state != 0, or for a byte that is
 * ESC in ground state. Returns 1 when the sequence is complete, with
 * *kind set (a two-byte escape and a string sequence are OTHER). The
 * caller clears sc->len/sc->nostore when it has consumed the bytes.
 * `kind` may be NULL for a caller that only needs completion. */
int tui_ansi_scan_feed(TuiAnsiScan *sc, unsigned char c, TuiAnsiKind *kind);

#endif /* BOBA_ANSI_SCAN_H */
