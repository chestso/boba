/* statusbar.h - Status bar component for boba
 *
 * A lightweight status bar widget supporting:
 * - Mode indicator (left, persistent)
 * - Notification text (right, transient)
 * - Absolute cursor positioning
 * - Single-line rendering with clear-line semantics
 */

#ifndef BOBA_STATUSBAR_H
#define BOBA_STATUSBAR_H

#include "../component.h"
#include "../dynamic_buffer.h"
#include "../msg.h"

/* Status bar model */
typedef struct TuiStatusBar
{
    TuiModel base; /* Base model for component interface */

    /* Mode indicator (left, persistent) */
    char *mode_text;
    size_t mode_len;
    int mode_display_width;

    /* Notification (right, transient) */
    char *notification;
    size_t notification_len;
    int notification_display_width;

    /* Layout */
    int terminal_width;
    int terminal_row; /* Absolute row (1-indexed, 0 = not set) */
    int height;       /* Number of rows (default 1) */
} TuiStatusBar;

/* Create a new status bar component
 *
 * Returns: New status bar model, or NULL on failure
 */
TuiStatusBar *tui_statusbar_create(void);

/* Free status bar component */
void tui_statusbar_free(TuiStatusBar *sb);

/* Set mode text (left side, persistent)
 * Pass NULL or "" to clear.
 */
void tui_statusbar_set_mode(TuiStatusBar *sb, const char *text);

/* Get mode text (returns NULL if not set) */
const char *tui_statusbar_get_mode(const TuiStatusBar *sb);

/* Set notification text (right side, transient)
 * Pass NULL or "" to clear.
 */
void tui_statusbar_set_notification(TuiStatusBar *sb, const char *text);

/* Get notification text (returns NULL if not set) */
const char *tui_statusbar_get_notification(const TuiStatusBar *sb);

/* Clear notification text */
void tui_statusbar_clear_notification(TuiStatusBar *sb);

/* Set terminal width for layout calculations */
void tui_statusbar_set_terminal_width(TuiStatusBar *sb, int width);

/* Set terminal row for absolute positioning (1-indexed)
 * When set, view() uses absolute cursor positioning.
 */
void tui_statusbar_set_terminal_row(TuiStatusBar *sb, int row);

/* Get render height in rows (default 1) */
int tui_statusbar_get_height(const TuiStatusBar *sb);

/* Render status bar to output buffer
 *
 * Parameters:
 *   sb: Status bar model (const)
 *   out: Output buffer to append to
 */
void tui_statusbar_view(const TuiStatusBar *sb, DynamicBuffer *out);

/* Update status bar with message
 *
 * Parameters:
 *   sb: Status bar model
 *   msg: Message to process
 *
 * Returns: Update result (statusbar currently doesn't handle any messages)
 */
TuiUpdateResult tui_statusbar_update(TuiStatusBar *sb, TuiMsg msg);

/* Get component interface for status bar */
const TuiComponent *tui_statusbar_component(void);

#endif /* BOBA_STATUSBAR_H */
