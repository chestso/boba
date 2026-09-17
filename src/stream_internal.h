/* stream_internal.h - internal seam between the stream component and
 * the runtime.
 *
 * Not installed, not public API. stream.c implements the commit pass
 * the runtime calls at the top of tui_runtime_flush(); runtime.c
 * implements the partial-row hooks the transcript needs.
 */

#ifndef BOBA_STREAM_INTERNAL_H
#define BOBA_STREAM_INTERNAL_H

#include <boba/runtime.h>
#include <boba/stream.h>

/* Commit pass: write staged bytes (if any) through the atomic
 * transcript seam. Returns 1 when a batch was written (the seam
 * already re-rendered the live region), 0 when there was nothing to
 * do. Called by tui_runtime_flush(). */
int tui_transcript_commit_pending(TuiTranscript *t, TuiRuntime *rt);

/* Forget an open partial row without emitting anything (used on
 * transcript clear: the row above stays as-is; the next write starts
 * below it instead of extending it). */
void tui_runtime_transcript_orphan(TuiRuntime *rt);

/* Advance a display column over a byte run. \n resets the column, \t
 * expands to the next multiple of 8, ESC sequences count zero, UTF-8
 * counts by codepoint width. `esc_state` carries a split escape
 * sequence across calls (0 = none). The result is the running DISPLAY
 * total of the current logical line (no width wrap here): callers
 * derive the physical row/column against the terminal width, since a
 * committed row may soft-wrap. */
int tui_rowcols_advance(int col, const char *bytes, size_t len,
                        int *esc_state);

#endif /* BOBA_STREAM_INTERNAL_H */
