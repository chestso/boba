/* text.h - width-aware slicing of styled text
 *
 * Fitting text into a column budget is the same operation in every
 * component: walk grapheme clusters, skip escape sequences (they occupy
 * no columns), stop on a cluster boundary. viewport clipping, list_popup
 * truncation, statusline elision and textinput's wrap/scroll budget each
 * grew their own copy, and they disagreed about escapes — a CSI-only
 * scanner counted an OSC 8 hyperlink's URL as visible columns.
 *
 * One walk serves them all. The unit is the grapheme cluster (unicode.h);
 * the escape grammar is ansi_scan.h; the budget is display columns under
 * the ambient TuiWidthModel. `tui_text_fit` measures the prefix,
 * `tui_text_emit` writes it (or measures, with a NULL output), and an
 * optional per-cluster hook lets a caller paint styling at cluster
 * boundaries (viewport's selection reversal) without a second walk.
 */

#ifndef BOBA_TEXT_H
#define BOBA_TEXT_H

#include "dynamic_buffer.h"

#include <stddef.h>

/* The longest whole-cluster prefix of `text` that fits a column budget.
 *
 *   bytes   input bytes consumed by the prefix, including the escape
 *           sequences it passed through on the way (they occupy no
 *           columns, but they are part of the prefix)
 *   cols    display columns those bytes occupy
 *   clipped 1 when input bytes remain after the prefix */
typedef struct TuiTextFit
{
    size_t bytes;
    int cols;
    int clipped;
} TuiTextFit;

/* Called before each TEXT cluster is emitted, with the column it starts
 * at and its display width (a zero-width cluster still reports a call).
 * Only called by tui_text_emit() with a non-NULL output. */
typedef void (*TuiTextClusterFn)(DynamicBuffer *out, int col, int width,
                                 void *user);

/* Measure the longest prefix of text[0..len) that fits in `cols` display
 * columns, cutting on a grapheme-cluster boundary. A non-positive budget
 * fits nothing (bytes 0, clipped when there is input). Escape sequences
 * are passed over and included in `bytes`.
 *
 * `at_least_one` takes the first cluster even when it alone exceeds the
 * budget, which is what a chunked walk needs to guarantee progress; a
 * strict fit (the default, 0) leaves it out. */
TuiTextFit tui_text_fit(const char *text, size_t len, int cols,
                        int at_least_one);

/* Emit that prefix into `out`: text clusters whole, escape sequences
 * verbatim, nothing that would exceed the budget. `out` may be NULL to
 * measure without emitting (the hook is then not called). Returns the
 * same TuiTextFit tui_text_fit() would. */
TuiTextFit tui_text_emit(DynamicBuffer *out, const char *text, size_t len,
                         int cols, int at_least_one,
                         TuiTextClusterFn on_cluster, void *user);

#endif /* BOBA_TEXT_H */
