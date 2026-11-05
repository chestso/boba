/* fit.h - "how much of the screen do I get?" arithmetic, shared
 *
 * Every component that paints a box of rows (textinput, viewport,
 * statusline, list_popup) has to answer the same two questions:
 *
 *     given a total number of rows (or columns) of content, a terminal
 *     size, and the space the app keeps for itself around me, how many
 *     rows (columns) may I actually paint?
 *
 * Answering it in one place keeps the clamp rules identical everywhere:
 * a layout that is too tall never paints past the screen, a degenerate
 * screen never yields zero rows, and the app's floor (min_rows) can never
 * smuggle a frame off the bottom.
 *
 * Internal to boba (not installed): these are building blocks, not API. */

#ifndef BOBA_FIT_H
#define BOBA_FIT_H

/* Rows/columns the app reserves for itself around a component's area.
 * For an absolutely-placed component, "above" is also how a first screen
 * row is expressed: a component whose first row is screen row `row`
 * (1-based) is one with above == row - 1. */
typedef struct TuiFitInsets
{
    int above; /* rows (or columns) the app paints before the component */
    int below; /* rows (or columns) the app paints after it              */
    int left;  /* columns the app paints to the left of it               */
    int right; /* columns the app paints to the right of it              */
} TuiFitInsets;

/* Rows a component may paint.
 *
 *   total     natural number of rows the content wants (>= 0)
 *   term_h    terminal height in rows; 0 = unknown (nothing to clamp to)
 *   insets    space the app keeps for itself; NULL = nothing reserved
 *   min_rows  floor the component wants (<= 0 means 1); never lifts the
 *             result above total or above what the screen allows
 *   max_rows  cap the component or the app set (<= 0 means no cap)
 *
 * Returns at least 1 and never more than min(total, cap, available).
 * A screen too small for its own reserves yields 1, not 0 and not a
 * negative value: a component with one row can still be looked at. */
int tui_fit_rows(int total, int term_h, const TuiFitInsets *insets,
                 int min_rows, int max_rows);

/* Columns a component may paint. Same rules as tui_fit_rows.
 *
 *   term_w     terminal width in columns; 0 = unknown
 *   max_cols   cap the component or the app set (<= 0 means no cap)
 *
 * Returns 0 when nothing is known (term_w <= 0 and no cap): the caller
 * reads 0 as "no width constraint", which is what an unwrapped,
 * unclipped component wants. Otherwise returns at least 1. */
int tui_fit_cols(int term_w, const TuiFitInsets *insets, int max_cols);

#endif /* BOBA_FIT_H */
