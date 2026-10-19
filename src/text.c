/* text.c - the one width-aware fit/emit walk (see text.h). */

#include <boba/text.h>
#include <boba/unicode.h>

#include "ansi_scan.h"

static TuiTextFit fit_walk(DynamicBuffer *out, const char *text, size_t len,
                           int cols, int at_least_one,
                           TuiTextClusterFn on_cluster, void *user)
{
    TuiTextFit fit = { 0, 0, 0 };
    if (!text || len == 0)
        return fit;
    if (cols <= 0) {
        fit.clipped = 1;
        return fit;
    }

    TuiAnsiScan scan = { 0 };
    size_t i = 0;
    int took_cluster = 0;

    while (i < len) {
        unsigned char c = (unsigned char)text[i];
        if (scan.state != 0 || c == 0x1b) {
            /* A sequence occupies no columns but is part of the prefix
             * (viewport and list_popup pass styling through; measuring
             * simply skips it). */
            TuiAnsiKind kind = TUI_ANSI_KIND_NONE;
            if (out)
                dynamic_buffer_append(out, &text[i], 1);
            i++;
            if (tui_ansi_scan_feed(&scan, c, &kind)) {
                scan.len = 0;
                scan.nostore = 0;
            }
            continue;
        }

        size_t clen = 0;
        int w = tui_next_cluster(text + i, len - i, &clen);
        if (clen == 0) {
            /* Invalid tail: consume one byte so the walk progresses. */
            clen = 1;
            w = 0;
        }
        if (fit.cols + w > cols && (took_cluster || !at_least_one))
            break;

        if (out) {
            if (on_cluster)
                on_cluster(out, fit.cols, w, user);
            dynamic_buffer_append(out, text + i, clen);
        }
        fit.cols += w;
        took_cluster = 1;
        i += clen;
    }

    fit.bytes = i;
    fit.clipped = i < len;
    return fit;
}

TuiTextFit tui_text_fit(const char *text, size_t len, int cols,
                        int at_least_one)
{
    return fit_walk(NULL, text, len, cols, at_least_one, NULL, NULL);
}

TuiTextFit tui_text_emit(DynamicBuffer *out, const char *text, size_t len,
                         int cols, int at_least_one,
                         TuiTextClusterFn on_cluster, void *user)
{
    return fit_walk(out, text, len, cols, at_least_one, on_cluster, user);
}
