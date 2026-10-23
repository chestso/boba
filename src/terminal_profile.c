/* terminal_profile.c - terminal capability probe (see
 * include/boba/terminal_profile.h).
 *
 * Two halves, deliberately kept apart:
 *
 *   - the decoder: functions over a captured reply payload. No
 *     runtime, no I/O, no globals — unit-testable by feeding strings.
 *   - the lifecycle: emit the query, decode replies until the profile
 *     is settled or the deadline passes, then fire the callback
 *     exactly once. That half lives in runtime.c (it owns the clock,
 *     the output stream and the input parser); this file owns the
 *     grammar.
 *
 * No regex (project principle): every reply is a character-level scan.
 */

#include <boba/terminal_profile.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "terminal_profile_internal.h"

/* ------------------------------------------------------------------ */
/* Query emission                                                      */
/* ------------------------------------------------------------------ */

/* The order matters and is kitty's documented support check: a terminal
 * with the graphics protocol answers the APC query immediately and the
 * DA1 after it; one without answers DA1 alone, which is what makes a
 * missing graphics reply conclusive rather than merely slow. DA1
 * arriving is therefore the probe's sequencing anchor — once it is in,
 * both the sixel verdict and the kitty verdict are settled.
 *
 *   ESC _ Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA ESC \   kitty, 1x1 RGB query
 *   ESC [ ? u                                      kitty keyboard flags
 *   ESC [ c                                        DA1 (sixel = attr 4)
 *   ESC [ 16 t                                     cell size in pixels
 *   ESC [ > 0 q                                    XTVERSION
 *
 * The kitty query uses the id kitty's own docs use (31) and transmits a
 * 1x1 dummy image in query mode, so nothing is stored by the terminal.
 * Cell size and XTVERSION are best-effort extras (the unknown-cell-size
 * consumer falls back to a nominal cell; the version only gates the
 * placeholder tier). The keyboard flags query rides the same rule as
 * the graphics one: ask, then DA1, so a terminal that does not answer
 * is distinguishable from one that answers nothing yet. */
size_t tui_term_probe_query(char *buf, size_t cap)
{
    static const char query[] = "\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\"
                                "\033[?u"
                                "\033[c"
                                "\033[16t"
                                "\033[>0q";
    size_t len = sizeof(query) - 1;
    if (!buf || cap == 0)
        return 0;
    if (len >= cap)
        len = cap - 1;
    memcpy(buf, query, len);
    buf[len] = '\0';
    return len;
}

/* ------------------------------------------------------------------ */
/* Environment hints                                                   */
/* ------------------------------------------------------------------ */

/* ASCII case-insensitive compare (strcasecmp is POSIX, _stricmp is
 * MSVC — neither is portable C, and boba's Windows build is MSVC
 * clean). */
static int ci_equal(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    for (; *a && *b; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
    }
    return *a == '\0' && *b == '\0';
}

/* Does `s` end with `suffix`, case-insensitively? */
static int ci_ends_with(const char *s, const char *suffix)
{
    if (!s || !suffix)
        return 0;
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && ci_equal(s + (ls - lx), suffix);
}

/* Does `s` begin with `prefix`, case-insensitively? A NULL `s` or
 * `prefix` is a miss. */
static int ci_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix)
        return 0;
    size_t lp = strlen(prefix);
    if (strlen(s) < lp)
        return 0;
    for (size_t i = 0; i < lp; i++) {
        char a = s[i], b = prefix[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return 1;
}

/* PORTTY_WIDTH's vocabulary. AUTO is the "not declared" answer (it is
 * never a measurement model; see unicode.h). */
static TuiWidthModel width_model_from_name(const char *name)
{
    if (ci_equal(name, "uts51"))
        return TUI_WIDTH_MODEL_UTS51;
    if (ci_equal(name, "grid"))
        return TUI_WIDTH_MODEL_GRID;
    if (ci_equal(name, "wcwidth"))
        return TUI_WIDTH_MODEL_WCWIDTH;
    return TUI_WIDTH_MODEL_AUTO;
}

TuiWidthModel tui_width_model_detect(const char *term, const char *tmux,
                                     const char *kitty_window_id,
                                     const char *portty,
                                     const char *portty_width)
{
    /* The innermost multiplexer owns the grid. Markers inherited from
     * the terminal outside tmux must not leak through it. */
    if ((tmux && *tmux) || ci_starts_with(term, "tmux") ||
        ci_starts_with(term, "screen"))
        return TUI_WIDTH_MODEL_GRID;

    /* An explicit engine contract beats identity — for the terminal
     * that DECLARED it (portty). */
    TuiWidthModel declared = width_model_from_name(portty_width);

    /* TERM names the terminal that owns the grid: that terminal wrote
     * it, so it is the innermost authority. The identity markers
     * (PORTTY, KITTY_WINDOW_ID) describe the terminal that SPAWNED
     * this shell, and that is an OUTER terminal whenever a nested
     * terminal was started from it — an xterm launched inside portty
     * inherits PORTTY=1 and PORTTY_WIDTH=uts51, and a grid measured
     * with the outer terminal's rule is a cell off the inner
     * terminal's: the VS15 cluster is measured one cell narrow, the
     * terminal draws it two cells wide, and the status row comes out
     * longer than the row the terminal lays out — it wraps. So a TERM
     * that names a terminal DECIDES, and the markers are the evidence
     * only when TERM names no terminal we know. */
    if (ci_starts_with(term, "portty"))
        return declared != TUI_WIDTH_MODEL_AUTO ? declared
                                                : TUI_WIDTH_MODEL_UTS51;

    /* Kitty: its terminfo name. */
    if (ci_starts_with(term, "xterm-kitty"))
        return TUI_WIDTH_MODEL_UTS51;

    /* The xterm aliases: xterm itself, and the terminals that share
     * them (iTerm2, wezterm, VTE, ghostty). The grid rule is the safe
     * one here: it measures the emoji-presentation cell, so a terminal
     * that draws it narrower leaves the row a column short — while
     * measuring narrower overflows the line. */
    if (ci_starts_with(term, "xterm"))
        return TUI_WIDTH_MODEL_GRID;

    /* TERM names no terminal we know: the markers are the evidence.
     * The declaration beats the identity markers (portty states its
     * engine's contract), and the identity markers come next. */
    if (declared != TUI_WIDTH_MODEL_AUTO)
        return declared;
    if (portty && *portty)
        return TUI_WIDTH_MODEL_UTS51;
    if (kitty_window_id && *kitty_window_id)
        return TUI_WIDTH_MODEL_UTS51;

    /* Windows Terminal, and everything unknown: the terminal grid
     * ignores VS15 for layout. */
    return TUI_WIDTH_MODEL_GRID;
}

TuiWidthModel tui_width_model_detect_env(void)
{
    return tui_width_model_detect(getenv("TERM"), getenv("TMUX"),
                                  getenv("KITTY_WINDOW_ID"), getenv("PORTTY"),
                                  getenv("PORTTY_WIDTH"));
}

/* 24-bit color, from the environment alone: COLORTERM is the de-facto
 * standard (kitty, foot, WezTerm, iTerm2, ghostty all set it), and the
 * `-direct` / `-truecolor` TERM variants (xterm-direct, foot-direct,
 * tmux-direct) name it in the terminal name itself. There is
 * deliberately no 256-color rung here: a consumer that needs to know
 * "how many colors" would have to guess, while "is it truecolor" is
 * the question every terminal actually answers. */
static int env_truecolor(const char *colorterm, const char *term)
{
    if (ci_equal(colorterm, "truecolor") || ci_equal(colorterm, "24bit"))
        return 1;
    if (ci_ends_with(term, "-direct") || ci_ends_with(term, "-truecolor"))
        return 1;
    return 0;
}

/* iTerm2 and WezTerm advertise themselves in the environment, and both
 * support a protocol family without answering a query for it (iTerm2's
 * own image protocol; WezTerm answers the kitty query but also
 * implements sixel). Only env-visible families are inferred here. */
void tui_term_profile_env(TuiTerminalProfile *p, const char *term_program,
                          const char *lc_terminal, const char *colorterm,
                          const char *term)
{
    if (!p)
        return;

    /* iTerm2: TERM_PROGRAM=iTerm.app (or LC_TERMINAL=iTerm2). */
    if ((term_program && strcmp(term_program, "iTerm.app") == 0) ||
        (lc_terminal && strcmp(lc_terminal, "iTerm2") == 0))
        p->iterm2_images = 1;

    /* WezTerm and foot implement sixel; so do xterm (>= 358, when
     * enabled) and mlterm, but those are not reliably env-visible, so
     * they are left to DA1. */
    if (term_program &&
        (strcmp(term_program, "WezTerm") == 0 || strcmp(term_program, "foot") == 0))
        p->sixel = 1;

    if (env_truecolor(colorterm, term))
        p->truecolor = 1;
}

/* Apply the process environment (called once, at probe start). */
void tui_term_profile_apply_env(TuiTerminalProfile *p)
{
    if (!p)
        return;
    tui_term_profile_env(p, getenv("TERM_PROGRAM"), getenv("LC_TERMINAL"),
                         getenv("COLORTERM"), getenv("TERM"));
}

/* ------------------------------------------------------------------ */
/* Reply decoding                                                      */
/* ------------------------------------------------------------------ */

/* Decimal scan: returns the value and advances *i past the digits.
 * *ok is 0 when no digit was seen. */
static int scan_dec(const char *s, size_t len, size_t *i, int *ok)
{
    int v = 0, seen = 0;
    while (*i < len && s[*i] >= '0' && s[*i] <= '9') {
        if (v < 100000000)
            v = v * 10 + (s[*i] - '0');
        seen = 1;
        (*i)++;
    }
    *ok = seen;
    return v;
}

static int match(const char *s, size_t len, const char *lit)
{
    size_t n = strlen(lit);
    return len >= n && memcmp(s, lit, n) == 0;
}

/* DA1: "[?1;2;4c" (attributes are ';'-separated; 4 = sixel). Any
 * attribute list is scanned for a standalone 4. */
static int decode_da1(TuiTerminalProfile *p, const char *s, size_t len)
{
    if (!match(s, len, "[?"))
        return 0;
    size_t i = 2;
    while (i < len) {
        int ok = 0;
        int v = scan_dec(s, len, &i, &ok);
        if (ok && v == 4)
            p->sixel = 1;
        while (i < len && s[i] != ';' && s[i] != 'c')
            i++;
        if (i < len && s[i] == ';')
            i++;
        else
            break;
    }
    return 1;
}

/* Cell size: "[6;<height>;<width>t" (`CSI 14 t` / `CSI 18 t` report
 * character cells and rows, not what the profile wants). */
static int decode_cellsize(TuiTerminalProfile *p, const char *s, size_t len)
{
    if (!match(s, len, "[6;"))
        return 0;
    size_t i = 3;
    int ok = 0;
    int h = scan_dec(s, len, &i, &ok);
    if (!ok || i >= len || s[i] != ';')
        return 0;
    i++;
    int w = scan_dec(s, len, &i, &ok);
    if (!ok || i >= len || s[i] != 't')
        return 0;
    if (w > 0 && h > 0) {
        p->cell_w_px = w;
        p->cell_h_px = h;
    }
    return 1;
}

/* Kitty keyboard flags reply: "[?<flags>u" (the answer to `CSI ? u`).
 * Any answer counts as support — the protocol's detection rule is the
 * presence of the reply, since a terminal with no flags set answers
 * `?0u`. The value is kept for a consumer that wants to see it. */
static int decode_kbd_flags(TuiTerminalProfile *p, const char *s, size_t len)
{
    if (!match(s, len, "[?"))
        return 0;
    size_t i = 2;
    int ok = 0;
    int flags = scan_dec(s, len, &i, &ok);
    if (!ok || i >= len || s[i] != 'u')
        return 0;
    p->kbd_protocol = 1;
    p->kbd_flags = flags;
    return 1;
}

/* kitty graphics query ack: "Gi=31;OK". Any payload after our id counts
 * as support — an error reply still proves the APC was parsed by a
 * terminal that knows the protocol. Another client's id is ignored. */
static int decode_kitty(TuiTerminalProfile *p, const char *s, size_t len)
{
    if (!match(s, len, "Gi="))
        return 0;
    size_t i = 3;
    int ok = 0;
    int id = scan_dec(s, len, &i, &ok);
    if (!ok)
        return 1;
    if (id == 31 && i < len && s[i] == ';')
        p->kitty_graphics = 1;
    return 1;
}

/* XTVERSION: ">|kitty(0.36.0)" / ">|kitty 0.36.0". Only the kitty
 * version matters here: placeholders (the tier this gates) arrived in
 * 0.28.0. */
static int decode_xtversion(TuiTerminalProfile *p, const char *s, size_t len)
{
    if (!match(s, len, ">|kitty"))
        return 0;
    size_t i = 7;
    if (i < len && (s[i] == '(' || s[i] == ' '))
        i++;
    int ok = 0;
    int major = scan_dec(s, len, &i, &ok);
    if (!ok)
        return 1;
    if (i >= len || s[i] != '.')
        return 1;
    i++;
    int minor = scan_dec(s, len, &i, &ok);
    if (!ok)
        return 1;
    if (major > 0 || minor >= 28)
        p->kitty_placeholders = 1;
    return 1;
}

int tui_term_profile_feed(TuiTerminalProfile *p, const char *payload,
                          size_t len)
{
    if (!p || !payload || len == 0)
        return 0;

    /* CSI replies carry their "[params final" shape; the string
     * sequences carry a payload prefix. */
    if (payload[0] == '[') {
        /* The keyboard flags answer is checked before DA1: both open
         * with "[?", and DA1's scan would otherwise claim it. */
        if (decode_kbd_flags(p, payload, len))
            return 1;
        if (decode_da1(p, payload, len))
            return 1;
        if (decode_cellsize(p, payload, len))
            return 1;
        return 0;
    }
    if (decode_kitty(p, payload, len))
        return 1;
    if (decode_xtversion(p, payload, len))
        return 1;

    /* Unknown payloads (OSC 52 echoes, private queries, other clients'
     * traffic) are ignored: the probe asks four questions and reads
     * only their answers. */
    return 0;
}
