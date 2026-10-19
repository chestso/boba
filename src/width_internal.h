/* width_internal.h - runtime ownership of the ambient width model.
 *
 * The public setter (unicode.h) is for standalone callers and tests.
 * A runtime installs its model through pin(), which also locks the
 * value: the model decides committed scrollback geometry, so it must
 * not change after the first byte is emitted.
 */

#ifndef BOBA_WIDTH_INTERNAL_H
#define BOBA_WIDTH_INTERNAL_H

#include <boba/unicode.h>

/* Install `model` and lock it. Returns 1 on success, 0 when the model
 * is not a measurement model or another lock is already held. */
int tui_width_model_pin(TuiWidthModel model);

/* Release the lock. The installed value is left as-is; the caller
 * restores the previous model with tui_width_model_set(). */
void tui_width_model_unpin(void);

#endif /* BOBA_WIDTH_INTERNAL_H */
