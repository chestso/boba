/* input_parser.c - Terminal input parsing for bloom-boba TUI library */

#include <bloom-boba/input_parser.h>
#include <stdlib.h>
#include <string.h>

/* Parser states */
typedef enum
{
    PARSER_STATE_GROUND,    /* Normal state, waiting for input */
    PARSER_STATE_ESCAPE,    /* Got ESC, waiting for next char */
    PARSER_STATE_CSI,       /* In CSI sequence (ESC [) */
    PARSER_STATE_CSI_MOUSE, /* In SGR mouse sequence (ESC [ <) */
    PARSER_STATE_SS3,       /* In SS3 sequence (ESC O) */
    PARSER_STATE_UTF8,      /* In UTF-8 multi-byte sequence */
} ParserState;

/* Input parser structure */
struct TuiInputParser
{
    ParserState state;
    unsigned char seq_buf[32]; /* Buffer for escape sequences */
    int seq_len;
    int utf8_remaining;      /* Remaining bytes in UTF-8 sequence */
    uint32_t utf8_codepoint; /* Accumulated UTF-8 codepoint */
};

/* Create a new input parser */
TuiInputParser *tui_input_parser_create(void)
{
    TuiInputParser *parser = (TuiInputParser *)malloc(sizeof(TuiInputParser));
    if (!parser)
        return NULL;

    memset(parser, 0, sizeof(TuiInputParser));
    parser->state = PARSER_STATE_GROUND;
    return parser;
}

/* Free input parser */
void tui_input_parser_free(TuiInputParser *parser)
{
    if (parser) {
        free(parser);
    }
}

/* Reset parser state */
void tui_input_parser_reset(TuiInputParser *parser)
{
    if (!parser)
        return;
    parser->state = PARSER_STATE_GROUND;
    parser->seq_len = 0;
    parser->utf8_remaining = 0;
    parser->utf8_codepoint = 0;
}

/* Parse CSI sequence and return appropriate key message */
static TuiMsg parse_csi_sequence(const unsigned char *seq, int len)
{
    TuiMsg msg = tui_msg_none();
    int mods = TUI_MOD_NONE;

    if (len == 0)
        return msg;

    /* Check for modifier parameters (e.g., ESC[1;5A for Ctrl+Up) */
    int param1 = 0, param2 = 0;
    const char *p = (const char *)seq;
    char final = seq[len - 1];

    /* Parse numeric parameters */
    while (*p >= '0' && *p <= '9') {
        param1 = param1 * 10 + (*p - '0');
        p++;
    }
    if (*p == ';') {
        p++;
        while (*p >= '0' && *p <= '9') {
            param2 = param2 * 10 + (*p - '0');
            p++;
        }
        /* param2 encodes modifiers: 1=none, 2=shift, 3=alt, etc */
        if (param2 >= 2) {
            if (param2 == 2)
                mods |= TUI_MOD_SHIFT;
            else if (param2 == 3)
                mods |= TUI_MOD_ALT;
            else if (param2 == 4)
                mods |= TUI_MOD_ALT | TUI_MOD_SHIFT;
            else if (param2 == 5)
                mods |= TUI_MOD_CTRL;
            else if (param2 == 6)
                mods |= TUI_MOD_CTRL | TUI_MOD_SHIFT;
            else if (param2 == 7)
                mods |= TUI_MOD_CTRL | TUI_MOD_ALT;
            else if (param2 == 8)
                mods |= TUI_MOD_CTRL | TUI_MOD_ALT | TUI_MOD_SHIFT;
        }
    }

    /* Map final character to key */
    switch (final) {
    case 'A':
        return tui_msg_key(TUI_KEY_UP, 0, mods);
    case 'B':
        return tui_msg_key(TUI_KEY_DOWN, 0, mods);
    case 'C':
        return tui_msg_key(TUI_KEY_RIGHT, 0, mods);
    case 'D':
        return tui_msg_key(TUI_KEY_LEFT, 0, mods);
    case 'H':
        return tui_msg_key(TUI_KEY_HOME, 0, mods);
    case 'F':
        return tui_msg_key(TUI_KEY_END, 0, mods);
    case '~':
        /* Extended keys: ESC[1~ = Home, ESC[2~ = Insert, etc */
        switch (param1) {
        case 1:
            return tui_msg_key(TUI_KEY_HOME, 0, mods);
        case 2:
            return tui_msg_key(TUI_KEY_INSERT, 0, mods);
        case 3:
            return tui_msg_key(TUI_KEY_DELETE, 0, mods);
        case 4:
            return tui_msg_key(TUI_KEY_END, 0, mods);
        case 5:
            return tui_msg_key(TUI_KEY_PAGE_UP, 0, mods);
        case 6:
            return tui_msg_key(TUI_KEY_PAGE_DOWN, 0, mods);
        case 15:
            return tui_msg_key(TUI_KEY_F5, 0, mods);
        case 17:
            return tui_msg_key(TUI_KEY_F6, 0, mods);
        case 18:
            return tui_msg_key(TUI_KEY_F7, 0, mods);
        case 19:
            return tui_msg_key(TUI_KEY_F8, 0, mods);
        case 20:
            return tui_msg_key(TUI_KEY_F9, 0, mods);
        case 21:
            return tui_msg_key(TUI_KEY_F10, 0, mods);
        case 23:
            return tui_msg_key(TUI_KEY_F11, 0, mods);
        case 24:
            return tui_msg_key(TUI_KEY_F12, 0, mods);
        }
        break;
    case 'P':
        return tui_msg_key(TUI_KEY_F1, 0, mods);
    case 'Q':
        return tui_msg_key(TUI_KEY_F2, 0, mods);
    case 'R':
        return tui_msg_key(TUI_KEY_F3, 0, mods);
    case 'S':
        return tui_msg_key(TUI_KEY_F4, 0, mods);
    case 'Z':
        /* xterm legacy Shift+Tab: CSI Z */
        return tui_msg_key(TUI_KEY_TAB, 0, mods | TUI_MOD_SHIFT);
    case 'u':
        /* CSI u - kitty keyboard protocol: ESC[keycode;modifiers u */
        if (param1 == 13) {
            return tui_msg_key(TUI_KEY_ENTER, 0, mods);
        }
        if (param1 == 9) {
            return tui_msg_key(TUI_KEY_TAB, 0, mods);
        }
        if (param1 == 27) {
            return tui_msg_key(TUI_KEY_ESCAPE, 0, mods);
        }
        if (param1 == 127) {
            return tui_msg_key(TUI_KEY_BACKSPACE, 0, mods);
        }
        /* Regular codepoint with modifiers */
        if (param1 >= 0x20) {
            return tui_msg_key(TUI_KEY_NONE, (uint32_t)param1, mods);
        }
        break;
    }

    return msg;
}

/* Parse SS3 sequence (ESC O) */
static TuiMsg parse_ss3_sequence(unsigned char c)
{
    switch (c) {
    case 'A':
        return tui_msg_key(TUI_KEY_UP, 0, TUI_MOD_NONE);
    case 'B':
        return tui_msg_key(TUI_KEY_DOWN, 0, TUI_MOD_NONE);
    case 'C':
        return tui_msg_key(TUI_KEY_RIGHT, 0, TUI_MOD_NONE);
    case 'D':
        return tui_msg_key(TUI_KEY_LEFT, 0, TUI_MOD_NONE);
    case 'H':
        return tui_msg_key(TUI_KEY_HOME, 0, TUI_MOD_NONE);
    case 'F':
        return tui_msg_key(TUI_KEY_END, 0, TUI_MOD_NONE);
    case 'P':
        return tui_msg_key(TUI_KEY_F1, 0, TUI_MOD_NONE);
    case 'Q':
        return tui_msg_key(TUI_KEY_F2, 0, TUI_MOD_NONE);
    case 'R':
        return tui_msg_key(TUI_KEY_F3, 0, TUI_MOD_NONE);
    case 'S':
        return tui_msg_key(TUI_KEY_F4, 0, TUI_MOD_NONE);
    default:
        return tui_msg_none();
    }
}

/* Parse SGR mouse sequence: <Cb;Cx;CyM or <Cb;Cx;Cym
 * Where Cb = button code, Cx = column, Cy = row
 * M = press, m = release
 */
static TuiMsg parse_sgr_mouse_sequence(const unsigned char *seq, int len)
{
    if (len < 5) /* Minimum: "0;1;1M" */
        return tui_msg_none();

    int button = 0, col = 0, row = 0;
    const char *p = (const char *)seq;
    char final = seq[len - 1];

    /* Parse button code */
    while (*p >= '0' && *p <= '9') {
        button = button * 10 + (*p - '0');
        p++;
    }
    if (*p != ';')
        return tui_msg_none();
    p++;

    /* Parse column */
    while (*p >= '0' && *p <= '9') {
        col = col * 10 + (*p - '0');
        p++;
    }
    if (*p != ';')
        return tui_msg_none();
    p++;

    /* Parse row */
    while (*p >= '0' && *p <= '9') {
        row = row * 10 + (*p - '0');
        p++;
    }

    /* Determine action from final character */
    TuiMouseAction action;
    if (final == 'M') {
        action = TUI_MOUSE_ACTION_PRESS;
    } else if (final == 'm') {
        action = TUI_MOUSE_ACTION_RELEASE;
    } else {
        return tui_msg_none();
    }

    /* Decode button code:
     *   bits 0-1: button (0=L, 1=M, 2=R, 3=release/no-button)
     *   bit  2 (4):  Shift
     *   bit  3 (8):  Meta/Alt
     *   bit  4 (16): Ctrl
     *   bit  5 (32): motion event flag
     *   bit  6 (64): wheel/extra block (bits 0-1 give wheel direction)
     */
    int mods = 0;
    if (button & 4)
        mods |= TUI_MOD_SHIFT;
    if (button & 8)
        mods |= TUI_MOD_META;
    if (button & 16)
        mods |= TUI_MOD_CTRL;

    int motion = (button & 32) != 0;
    int wheel = (button & 64) != 0;
    int btn_low = button & 3;

    TuiMouseButton mouse_button;
    if (wheel) {
        /* 64=up, 65=down, 66=left, 67=right */
        if (btn_low == 0)
            mouse_button = TUI_MOUSE_WHEEL_UP;
        else if (btn_low == 1)
            mouse_button = TUI_MOUSE_WHEEL_DOWN;
        else if (btn_low == 2)
            mouse_button = TUI_MOUSE_WHEEL_LEFT;
        else
            mouse_button = TUI_MOUSE_WHEEL_RIGHT;
    } else {
        if (btn_low == 0)
            mouse_button = TUI_MOUSE_LEFT;
        else if (btn_low == 1)
            mouse_button = TUI_MOUSE_MIDDLE;
        else if (btn_low == 2)
            mouse_button = TUI_MOUSE_RIGHT;
        else
            mouse_button = TUI_MOUSE_RELEASE;

        if (motion)
            action = TUI_MOUSE_ACTION_MOTION;
    }

    return tui_msg_mouse(mouse_button, action, col, row, mods);
}

/* Parse a single byte and return message if complete */
int tui_input_parser_feed(TuiInputParser *parser, unsigned char byte,
                          TuiMsg *msg)
{
    if (!parser || !msg)
        return 0;

    *msg = tui_msg_none();

    switch (parser->state) {
    case PARSER_STATE_GROUND:
        if (byte == 0x1B) {
            /* ESC - start escape sequence */
            parser->state = PARSER_STATE_ESCAPE;
            parser->seq_len = 0;
            return 0;
        } else if (byte < 0x20) {
            /* Control character */
            switch (byte) {
            case 0x00: /* Ctrl+Space (NUL) */
                *msg = tui_msg_key(TUI_KEY_NONE, ' ', TUI_MOD_CTRL);
                return 1;
            case 0x0D: /* CR */
                *msg = tui_msg_key(TUI_KEY_ENTER, 0, TUI_MOD_NONE);
                return 1;
            case 0x09: /* Tab */
                *msg = tui_msg_key(TUI_KEY_TAB, 0, TUI_MOD_NONE);
                return 1;
            case 0x08: /* BS (Ctrl+H) */
                *msg = tui_msg_key(TUI_KEY_BACKSPACE, 0, TUI_MOD_NONE);
                return 1;
            case 0x1F: /* Ctrl+_ */
                *msg = tui_msg_key(TUI_KEY_NONE, '_', TUI_MOD_CTRL);
                return 1;
            default:
                /* Ctrl+letter */
                if (byte >= 1 && byte <= 26) {
                    *msg = tui_msg_key(TUI_KEY_NONE, 'a' + byte - 1, TUI_MOD_CTRL);
                    return 1;
                }
                return 0;
            }
        } else if (byte == 0x7F) {
            /* DEL - backspace on most terminals */
            *msg = tui_msg_key(TUI_KEY_BACKSPACE, 0, TUI_MOD_NONE);
            return 1;
        } else if ((byte & 0x80) == 0) {
            /* ASCII character */
            *msg = tui_msg_char(byte, TUI_MOD_NONE);
            return 1;
        } else if ((byte & 0xE0) == 0xC0) {
            /* UTF-8 2-byte sequence start */
            parser->state = PARSER_STATE_UTF8;
            parser->utf8_remaining = 1;
            parser->utf8_codepoint = byte & 0x1F;
            return 0;
        } else if ((byte & 0xF0) == 0xE0) {
            /* UTF-8 3-byte sequence start */
            parser->state = PARSER_STATE_UTF8;
            parser->utf8_remaining = 2;
            parser->utf8_codepoint = byte & 0x0F;
            return 0;
        } else if ((byte & 0xF8) == 0xF0) {
            /* UTF-8 4-byte sequence start */
            parser->state = PARSER_STATE_UTF8;
            parser->utf8_remaining = 3;
            parser->utf8_codepoint = byte & 0x07;
            return 0;
        }
        break;

    case PARSER_STATE_ESCAPE:
        if (byte == '[') {
            /* CSI sequence */
            parser->state = PARSER_STATE_CSI;
            parser->seq_len = 0;
            return 0;
        } else if (byte == 'O') {
            /* SS3 sequence */
            parser->state = PARSER_STATE_SS3;
            return 0;
        } else {
            /* Alt+key or unknown sequence */
            parser->state = PARSER_STATE_GROUND;
            if (byte >= 0x20 && byte < 0x7F) {
                *msg = tui_msg_char(byte, TUI_MOD_ALT);
                return 1;
            }
            /* Treat bare ESC as escape key */
            *msg = tui_msg_key(TUI_KEY_ESCAPE, 0, TUI_MOD_NONE);
            return 1;
        }
        break;

    case PARSER_STATE_CSI:
        /* Check for SGR mouse sequence: ESC [ < ... */
        if (parser->seq_len == 0 && byte == '<') {
            parser->state = PARSER_STATE_CSI_MOUSE;
            parser->seq_len = 0;
            return 0;
        }
        if (parser->seq_len < (int)sizeof(parser->seq_buf) - 1) {
            parser->seq_buf[parser->seq_len++] = byte;
        }
        /* CSI sequences end with a byte in range 0x40-0x7E */
        if (byte >= 0x40 && byte <= 0x7E) {
            parser->seq_buf[parser->seq_len] = '\0';
            *msg = parse_csi_sequence(parser->seq_buf, parser->seq_len);
            parser->state = PARSER_STATE_GROUND;
            return msg->type != TUI_MSG_NONE;
        }
        return 0;

    case PARSER_STATE_CSI_MOUSE:
        if (parser->seq_len < (int)sizeof(parser->seq_buf) - 1) {
            parser->seq_buf[parser->seq_len++] = byte;
        }
        /* SGR mouse sequences end with 'M' (press) or 'm' (release) */
        if (byte == 'M' || byte == 'm') {
            parser->seq_buf[parser->seq_len] = '\0';
            *msg = parse_sgr_mouse_sequence(parser->seq_buf, parser->seq_len);
            parser->state = PARSER_STATE_GROUND;
            return msg->type != TUI_MSG_NONE;
        }
        return 0;

    case PARSER_STATE_SS3:
        *msg = parse_ss3_sequence(byte);
        parser->state = PARSER_STATE_GROUND;
        return msg->type != TUI_MSG_NONE;

    case PARSER_STATE_UTF8:
        if ((byte & 0xC0) != 0x80) {
            /* Invalid UTF-8 continuation, reset */
            parser->state = PARSER_STATE_GROUND;
            return 0;
        }
        parser->utf8_codepoint = (parser->utf8_codepoint << 6) | (byte & 0x3F);
        parser->utf8_remaining--;
        if (parser->utf8_remaining == 0) {
            parser->state = PARSER_STATE_GROUND;
            *msg = tui_msg_char(parser->utf8_codepoint, TUI_MOD_NONE);
            return 1;
        }
        return 0;
    }

    return 0;
}

/* Parse input bytes and return messages */
int tui_input_parser_parse(TuiInputParser *parser, const unsigned char *input,
                           size_t input_len, TuiMsg *msgs, int max_msgs)
{
    if (!parser || !input || !msgs || max_msgs <= 0)
        return 0;

    int count = 0;
    for (size_t i = 0; i < input_len && count < max_msgs; i++) {
        TuiMsg msg;
        if (tui_input_parser_feed(parser, input[i], &msg)) {
            msgs[count++] = msg;
        }
    }

    return count;
}
