/* fit.c - see fit.h */

#include "fit.h"

/* Sentinel for "no limit from this input". INT_MAX rather than a large
 * constant so min() chains stay exact. */
#define FIT_NO_LIMIT 0x7fffffff

int tui_fit_rows(int total, int term_h, const TuiFitInsets *insets,
                 int min_rows, int max_rows)
{
    if (total < 0)
        total = 0;

    int avail = FIT_NO_LIMIT;
    if (term_h > 0) {
        int above = insets ? insets->above : 0;
        int below = insets ? insets->below : 0;
        if (above < 0)
            above = 0;
        if (below < 0)
            below = 0;
        avail = term_h - above - below;
    }

    int cap = max_rows > 0 ? max_rows : FIT_NO_LIMIT;

    int rows = total;
    if (cap < rows)
        rows = cap;
    if (avail < rows)
        rows = avail;

    /* The floor is a *preference*, not a licence to paint off-screen:
     * it is clamped by the same two bounds. A component that asks for
     * more rows than the text has, or more than the screen can give,
     * gets what exists. */
    int floor = min_rows > 0 ? min_rows : 1;
    if (floor > total)
        floor = total;
    if (floor > avail)
        floor = avail;
    if (rows < floor)
        rows = floor;

    return rows < 1 ? 1 : rows;
}

int tui_fit_cols(int term_w, const TuiFitInsets *insets, int max_cols)
{
    int avail = FIT_NO_LIMIT;
    int known = 0;

    if (term_w > 0) {
        int left = insets ? insets->left : 0;
        int right = insets ? insets->right : 0;
        if (left < 0)
            left = 0;
        if (right < 0)
            right = 0;
        avail = term_w - left - right;
        known = 1;
    }

    if (max_cols > 0) {
        if (max_cols < avail)
            avail = max_cols;
        known = 1;
    }

    if (!known)
        return 0; /* nothing known: "no constraint" */
    return avail < 1 ? 1 : avail;
}
