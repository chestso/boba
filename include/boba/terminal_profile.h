/* terminal_profile.h - terminal capability profile (async probe)
 *
 * Some layout decisions cannot be made without asking the terminal:
 * an image's row reservation depends on the graphics protocol and the
 * cell size, so a transcript block holding an image can only be
 * committed once the profile is known. Asking is inherently
 * asynchronous — the answer arrives on stdin whenever the terminal
 * feels like it — so this is a probe (a query out, a reply in), never a
 * blocking read at startup.
 *
 * Lifecycle, concretely:
 *
 *   1. An app declares `probe_terminal = 1` on its TuiView (see
 *      component.h) before its first flush.
 *   2. The runtime emits the query sequences once, ahead of that
 *      frame's content, and claims the input parser's reply slot.
 *   3. Replies are decoded as they arrive. When the profile is
 *      complete, on_term_reply (TuiRuntimeConfig) fires and the
 *      runtime stops claiming replies.
 *   4. If no complete profile arrives within
 *      TUI_TERM_PROBE_TIMEOUT_MS of wall clock (checked on the event
 *      loop's tick), the profile resolves conservatively — no graphics
 *      — and on_term_reply fires anyway. Reaching a verdict is
 *      guaranteed: exactly one on_term_reply per probe.
 *
 * The profile is only ever read through tui_runtime_terminal_profile(),
 * which never returns NULL. Before resolution it reports resolved = 0
 * and no capabilities, so consumers that ignore the gate degrade to
 * text rather than mis-laying-out.
 *
 * Windows note: ConPTY translates or drops these queries, so the probe
 * there resolves conservatively on timeout. That is the correct
 * degradation — the profile is an optimization, not a requirement.
 */

#ifndef BOBA_TERMINAL_PROFILE_H
#define BOBA_TERMINAL_PROFILE_H

#include <stddef.h>

/* The runtime (runtime.h) defines this; forward-declared so this header
 * stands alone (stream.h includes it, and stream.h is included by
 * runtime.h). */
struct TuiRuntime;

/* Bounded wait for a complete profile, measured from probe emission on
 * the runtime's monotonic clock. 250 ms is well under a frame's worth
 * of user-perceptible delay and comfortably longer than a local
 * terminal's reply latency. */
#define TUI_TERM_PROBE_TIMEOUT_MS 250

typedef struct TuiTerminalProfile
{
    int resolved; /* 0 until resolved (reply or timeout)          */

    /* Graphics tiers. At most one is normally set; a terminal that
     * answers both DA1-4 and the kitty query is reported as both (the
     * consumer picks its preferred tier). */
    int sixel;              /* DA1 attribute 4 (§8;4c)               */
    int kitty_graphics;     /* kitty graphics query acknowledged      */
    int kitty_placeholders; /* version >= 0.28 (XTVERSION)       */
    int iterm2_images;      /* TERM_PROGRAM / LC_TERMINAL env        */

    /* Cell geometry in pixels, 0 when unknown (`CSI 16 t` reply). */
    int cell_w_px, cell_h_px;
} TuiTerminalProfile;

/* The runtime's profile. Never NULL. `resolved` tells whether a probe
 * has reached its verdict. */
const TuiTerminalProfile *tui_runtime_terminal_profile(struct TuiRuntime *rt);

/* Probe query bytes (see step 2 above): kitty graphics query, DA1, cell
 * size, XTVERSION — in the order kitty documents (support query first,
 * then DA1, so a terminal without graphics answers DA1 alone). Exposed
 * for tests and for consumers embedding the runtime elsewhere. `buf`
 * must hold TUI_TERM_PROBE_QUERY_MAX bytes; returns the length. */
#define TUI_TERM_PROBE_QUERY_MAX 128
size_t tui_term_probe_query(char *buf, size_t cap);

/* Feed one captured reply payload (as returned by
 * tui_input_parser_next_reply) into the decoder. Returns 1 when the
 * payload was recognized, 0 when ignored. Exposed for tests. */
int tui_term_profile_feed(TuiTerminalProfile *p, const char *payload,
                          size_t len);

/* Environment-derived hints (TERM_PROGRAM=iTerm.app, LC_TERMINAL /
 * TERM_PROGRAM for others). Not a query; applied once at probe start.
 * Exposed for tests (which pass the values instead of touching the
 * environment). */
void tui_term_profile_env(TuiTerminalProfile *p, const char *term_program,
                          const char *lc_terminal);

#endif /* BOBA_TERMINAL_PROFILE_H */