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
 *   ESC [ c                                        DA1 (sixel = attr 4)
 *   ESC [ 16 t                                     cell size in pixels
 *   ESC [ > 0 q                                    XTVERSION
 *
 * The kitty query uses the id kitty's own docs use (31) and transmits a
 * 1x1 dummy image in query mode, so nothing is stored by the terminal.
 * Cell size and XTVERSION are best-effort extras (the unknown-cell-size
 * consumer falls back to a nominal cell; the version only gates the
 * placeholder tier). */
size_t tui_term_probe_query(char *buf, size_t cap)
{
    static const char query[] = "\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\"
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

/* iTerm2 and WezTerm advertise themselves in the environment, and both
 * support a protocol family without answering a query for it (iTerm2's
 * own image protocol; WezTerm answers the kitty query but also
 * implements sixel). Only env-visible families are inferred here. */
void tui_term_profile_env(TuiTerminalProfile *p, const char *term_program,
                          const char *lc_terminal)
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
}

/* Apply the process environment (called once, at probe start). */
void tui_term_profile_apply_env(TuiTerminalProfile *p)
{
    if (!p)
        return;
    tui_term_profile_env(p, getenv("TERM_PROGRAM"), getenv("LC_TERMINAL"));
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
