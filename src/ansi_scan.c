/* ansi_scan.c - the shared escape-sequence grammar (see ansi_scan.h). */

#include "ansi_scan.h"

int tui_ansi_scan_feed(TuiAnsiScan *sc, unsigned char c, TuiAnsiKind *kind)
{
    if (kind)
        *kind = TUI_ANSI_KIND_NONE;

    if (!sc->nostore && sc->len < TUI_ANSI_SEQ_MAX)
        sc->buf[sc->len++] = (char)c;
    else
        sc->nostore = 1;

    switch (sc->state) {
    case 0: /* the ESC that starts the sequence */
        sc->state = 1;
        return 0;
    case 1: /* after ESC: kind selector */
        if (c == '[')
            sc->state = 2;
        else if (c == ']')
            sc->state = 3;
        else if (c == 'P' || c == '_' || c == '^' || c == 'X')
            sc->state = 4;
        else {
            sc->state = 0;
            if (kind)
                *kind = TUI_ANSI_KIND_OTHER; /* two-byte escape */
            return 1;
        }
        return 0;
    case 2: /* CSI: final byte in 0x40..0x7E; only SGR (m) is styling */
        if (c >= 0x40 && c <= 0x7E) {
            sc->state = 0;
            if (kind)
                *kind = (c == 'm' && !sc->nostore) ? TUI_ANSI_KIND_SGR
                                                   : TUI_ANSI_KIND_OTHER;
            return 1;
        }
        return 0;
    case 3: /* OSC: BEL or ESC \ terminates */
        if (c == 0x07) {
            sc->state = 0;
            if (kind)
                *kind = TUI_ANSI_KIND_OTHER;
            return 1;
        }
        if (c == 0x1b) {
            sc->resume = 3;
            sc->state = 5;
        }
        return 0;
    case 4: /* DCS / APC / PM / SOS: only ESC \ terminates */
        if (c == 0x1b) {
            sc->resume = 4;
            sc->state = 5;
        }
        return 0;
    default: /* 5: ESC seen inside a string */
        if (c == '\\') {
            sc->state = 0;
            if (kind)
                *kind = TUI_ANSI_KIND_OTHER;
            return 1;
        }
        sc->state = sc->resume;
        return 0;
    }
}
