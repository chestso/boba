/* terminal_profile_internal.h - internal seam between the terminal
 * profile probe (terminal_profile.c) and the runtime.
 *
 * Not installed, not public API. runtime.c owns the probe lifecycle
 * (emission, the deadline, the callback); terminal_profile.c owns the
 * query grammar and the reply decoder.
 */

#ifndef BOBA_TERMINAL_PROFILE_INTERNAL_H
#define BOBA_TERMINAL_PROFILE_INTERNAL_H

#include <boba/terminal_profile.h>

/* Apply the process environment hints (TERM_PROGRAM / LC_TERMINAL).
 * Called once, at probe start. */
void tui_term_profile_apply_env(TuiTerminalProfile *p);

#endif /* BOBA_TERMINAL_PROFILE_INTERNAL_H */