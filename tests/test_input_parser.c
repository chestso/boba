/* test_input_parser.c - Tests for SGR mouse parsing, including v2-aligned
 * wheel left/right buttons and SGR modifier-bit extraction (Shift/Meta/Ctrl).
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boba/input_parser.h>
#include <boba/msg.h>

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn)                 \
    do {                             \
        tests_run++;                 \
        fn();                        \
        tests_passed++;              \
        printf("  PASS: %s\n", #fn); \
    } while (0)

/* Feed a NUL-terminated escape sequence and return the single resulting
 * message. Asserts exactly one message came out. */
static TuiMsg parse_one(const char *seq)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    TuiMsg msgs[4];
    int n = tui_input_parser_parse(p, (const unsigned char *)seq, strlen(seq),
                                   msgs, 4);
    assert(n == 1);
    tui_input_parser_free(p);
    return msgs[0];
}

/* Feed a sequence that must produce NO message at all (a capability
 * reply, a key the parser has no code for). */
static int parse_none(const char *seq)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    TuiMsg msgs[4];
    int n = tui_input_parser_parse(p, (const unsigned char *)seq, strlen(seq),
                                   msgs, 4);
    tui_input_parser_free(p);
    return n == 0;
}

/* ----- basic buttons ---------------------------------------------------- */

static void test_left_press(void)
{
    TuiMsg m = parse_one("\033[<0;5;10M");
    assert(m.type == TUI_MSG_MOUSE);
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_PRESS);
    assert(m.data.mouse.col == 5);
    assert(m.data.mouse.row == 10);
    assert(m.data.mouse.mods == 0);
}

static void test_left_release(void)
{
    TuiMsg m = parse_one("\033[<0;5;10m");
    assert(m.type == TUI_MSG_MOUSE);
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_RELEASE);
    assert(m.data.mouse.mods == 0);
}

static void test_middle_press(void)
{
    TuiMsg m = parse_one("\033[<1;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_MIDDLE);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_PRESS);
}

static void test_right_press(void)
{
    TuiMsg m = parse_one("\033[<2;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_RIGHT);
}

static void test_release_no_button(void)
{
    /* Button code 3 = release / no specific button (legacy X10 semantic). */
    TuiMsg m = parse_one("\033[<3;5;10m");
    assert(m.data.mouse.button == TUI_MOUSE_RELEASE);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_RELEASE);
}

/* ----- wheel (including new wheel left/right) --------------------------- */

static void test_wheel_up(void)
{
    TuiMsg m = parse_one("\033[<64;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_WHEEL_UP);
    assert(m.data.mouse.mods == 0);
}

static void test_wheel_down(void)
{
    TuiMsg m = parse_one("\033[<65;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_WHEEL_DOWN);
}

static void test_wheel_left(void)
{
    TuiMsg m = parse_one("\033[<66;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_WHEEL_LEFT);
}

static void test_wheel_right(void)
{
    TuiMsg m = parse_one("\033[<67;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_WHEEL_RIGHT);
}

/* ----- modifier bits ---------------------------------------------------- */

static void test_shift_left_click(void)
{
    /* button = 0 (left) | 4 (shift) = 4 */
    TuiMsg m = parse_one("\033[<4;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.mods == TUI_MOD_SHIFT);
}

static void test_meta_left_click(void)
{
    /* button = 0 | 8 (meta) = 8 */
    TuiMsg m = parse_one("\033[<8;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.mods == TUI_MOD_META);
}

static void test_ctrl_left_click(void)
{
    /* button = 0 | 16 (ctrl) = 16 */
    TuiMsg m = parse_one("\033[<16;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.mods == TUI_MOD_CTRL);
}

static void test_ctrl_shift_wheel_up(void)
{
    /* button = 64 (wheel up) | 4 (shift) | 16 (ctrl) = 84 */
    TuiMsg m = parse_one("\033[<84;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_WHEEL_UP);
    assert(m.data.mouse.mods == (TUI_MOD_SHIFT | TUI_MOD_CTRL));
}

/* ----- motion ----------------------------------------------------------- */

static void test_motion_left_held(void)
{
    /* button = 0 (left) | 32 (motion) = 32 */
    TuiMsg m = parse_one("\033[<32;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_MOTION);
    assert(m.data.mouse.mods == 0);
}

static void test_motion_with_ctrl(void)
{
    /* button = 0 | 32 (motion) | 16 (ctrl) = 48 */
    TuiMsg m = parse_one("\033[<48;5;10M");
    assert(m.data.mouse.button == TUI_MOUSE_LEFT);
    assert(m.data.mouse.action == TUI_MOUSE_ACTION_MOTION);
    assert(m.data.mouse.mods == TUI_MOD_CTRL);
}

/* ----- regression: regular keys still parse after these changes --------- */

static void test_regular_char_still_parses(void)
{
    TuiMsg m = parse_one("a");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.rune == 'a');
}

/* ----- Kitty keyboard protocol: press/release -------------------------- */

static void test_kitty_press_default_action(void)
{
    /* CSI 65 u — 'A' press. No event-type given → defaults to PRESS. */
    TuiMsg m = parse_one("\033[65u");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.rune == 'A');
    assert(m.data.key.action == TUI_KEY_ACTION_PRESS);
}

static void test_kitty_press_explicit_event_type(void)
{
    /* CSI 65;1:1 u — 'A' press with explicit event-type 1. */
    TuiMsg m = parse_one("\033[65;1:1u");
    assert(m.data.key.rune == 'A');
    assert(m.data.key.action == TUI_KEY_ACTION_PRESS);
}

static void test_kitty_repeat_maps_to_press(void)
{
    /* CSI 65;1:2 u — 'A' repeat. Folds into PRESS. */
    TuiMsg m = parse_one("\033[65;1:2u");
    assert(m.data.key.action == TUI_KEY_ACTION_PRESS);
}

static void test_kitty_release(void)
{
    /* CSI 65;1:3 u — 'A' release. Type stays KEY_PRESS; action discriminates. */
    TuiMsg m = parse_one("\033[65;1:3u");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.rune == 'A');
    assert(m.data.key.action == TUI_KEY_ACTION_RELEASE);
}

static void test_kitty_release_with_ctrl(void)
{
    /* CSI 65;5:3 u — Ctrl+'A' release. */
    TuiMsg m = parse_one("\033[65;5:3u");
    assert(m.data.key.rune == 'A');
    assert(m.data.key.mods == TUI_MOD_CTRL);
    assert(m.data.key.action == TUI_KEY_ACTION_RELEASE);
}

static void test_kitty_release_special_key(void)
{
    /* CSI 13;1:3 u — Enter release. */
    TuiMsg m = parse_one("\033[13;1:3u");
    assert(m.data.key.key == TUI_KEY_ENTER);
    assert(m.data.key.action == TUI_KEY_ACTION_RELEASE);
}

/* Ctrl+C and Ctrl+D keep their verdicts under the protocol. The legacy
 * path reads them from the control BYTES (0x03 / 0x04), which flag 1
 * stops sending — a key's meaning must not depend on its encoding. */
static void test_kitty_ctrl_c_is_interrupt(void)
{
    TuiMsg m = parse_one("\033[99;5u");
    assert(m.type == TUI_MSG_INTERRUPT);
}

static void test_kitty_ctrl_shift_c_is_interrupt(void)
{
    /* Shift does not change what the key means (the legacy byte for
     * Ctrl+Shift+C is 0x03 like Ctrl+C's). */
    TuiMsg m = parse_one("\033[99;6u");
    assert(m.type == TUI_MSG_INTERRUPT);
}

static void test_kitty_ctrl_d_is_eof(void)
{
    TuiMsg m = parse_one("\033[100;5u");
    assert(m.type == TUI_MSG_EOF);
}

static void test_kitty_alt_ctrl_c_is_a_key(void)
{
    /* Alt is a real distinction under the protocol: alt+ctrl+c is not a
     * quit. */
    TuiMsg m = parse_one("\033[99;7u");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.rune == 'c');
    assert(m.data.key.mods == (TUI_MOD_CTRL | TUI_MOD_ALT));
}

static void test_kitty_other_ctrl_keys_are_keys(void)
{
    /* The parity is for the two verdict keys only: Ctrl+A is still a
     * key (the textinput's line-start binding). */
    TuiMsg m = parse_one("\033[97;5u");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.rune == 'a');
    assert(m.data.key.mods == TUI_MOD_CTRL);
}

/* Associated text (the "report associated text" flag): the key code is
 * the UNSHIFTED key, so the text is what the key actually produced. */
static void test_kitty_associated_text_capital(void)
{
    /* CSI 97;2;65u — shift+a, text "A" (kitty's own example). */
    TuiMsg m = parse_one("\033[97;2;65u");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.key == TUI_KEY_NONE);
    assert(m.data.key.rune == 'A');
    assert(m.data.key.mods == TUI_MOD_SHIFT);
    assert(m.data.key.text_len == 1);
    assert(memcmp(m.data.key.text, "A", 1) == 0);
}

static void test_kitty_associated_text_grapheme(void)
{
    /* CSI 101;1;101:769u — 'e' plus COMBINING ACUTE, one key event
     * carrying both codepoints (a dead key's result). */
    TuiMsg m = parse_one("\033[101;1;101:769u");
    assert(m.data.key.key == TUI_KEY_NONE);
    assert(m.data.key.rune == 'e');
    assert(m.data.key.text_len == 3);
    assert(memcmp(m.data.key.text, "e\xcc\x81", 3) == 0);
}

static void test_kitty_pure_text_event(void)
{
    /* CSI 0;;229u — no key information at all (an IME result): the text
     * is the event. */
    TuiMsg m = parse_one("\033[0;;229u");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.key == TUI_KEY_NONE);
    assert(m.data.key.rune == 229);
    assert(m.data.key.text_len == 2);
    assert(memcmp(m.data.key.text, "\xc3\xa5", 2) == 0);
}

/* The event type is a SUB-PARAMETER: only a ':' field is one, so a ';'
 * field that happens to hold 3 is text, never a release. */
static void test_kitty_event_type_needs_the_colon(void)
{
    TuiMsg release = parse_one("\033[97;2:3u");
    assert(release.data.key.action == TUI_KEY_ACTION_RELEASE);

    TuiMsg press = parse_one("\033[97;2;3u");
    assert(press.data.key.action == TUI_KEY_ACTION_PRESS);
    /* 3 is a control code, which is not text: the field is empty. */
    assert(press.data.key.text_len == 0);
    assert(press.data.key.rune == 'a');
}

/* A special key keeps its code, and control codes are not text — so the
 * text field is not a character there. */
static void test_kitty_special_key_ignores_text(void)
{
    TuiMsg m = parse_one("\033[13;2;13u");
    assert(m.data.key.key == TUI_KEY_ENTER);
    assert(m.data.key.mods == TUI_MOD_SHIFT);
    assert(m.data.key.text_len == 0);
}

/* The modifier field is a BIT FIELD plus one, and the LOCK modifiers
 * are bits in it. The report-all flag makes a terminal report the lock
 * state for every key, so a user with Num Lock on sends `CSI 13;130u`
 * for Shift+Enter — and a decoder that only knows the values 2..8 reads
 * that as NO modifiers: the newline becomes a submit, and Ctrl+Shift+A
 * becomes a plain 'a'. The locks are state, not modifiers: masked off,
 * the real modifiers survive. */
static void test_kitty_lock_modifiers_do_not_eat_the_modifiers(void)
{
    /* Shift+Enter with Num Lock on: 1 + shift(1) + num_lock(128). */
    TuiMsg m = parse_one("\033[13;130u");
    assert(m.data.key.key == TUI_KEY_ENTER);
    assert(m.data.key.mods == TUI_MOD_SHIFT);

    /* Ctrl+Shift+a with Num Lock on: 1 + shift(1) + ctrl(4) + 128. */
    m = parse_one("\033[97;134u");
    assert(m.data.key.rune == 'a');
    assert(m.data.key.mods == (TUI_MOD_CTRL | TUI_MOD_SHIFT));

    /* Ctrl+a with Num Lock on (1 + ctrl(4) + 128). */
    m = parse_one("\033[97;133u");
    assert(m.data.key.mods == TUI_MOD_CTRL);

    /* Caps Lock on: 1 + shift(1) + caps_lock(64). */
    m = parse_one("\033[13;66u");
    assert(m.data.key.key == TUI_KEY_ENTER);
    assert(m.data.key.mods == TUI_MOD_SHIFT);

    /* A lock alone is not a modifier at all. */
    m = parse_one("\033[97;129u");
    assert(m.data.key.rune == 'a');
    assert(m.data.key.mods == TUI_MOD_NONE);

    /* Super, hyper and meta fold into the one extra flag boba has. */
    m = parse_one("\033[97;9u"); /* super */
    assert(m.data.key.mods == TUI_MOD_META);
    m = parse_one("\033[97;17u"); /* hyper */
    assert(m.data.key.mods == TUI_MOD_META);
    m = parse_one("\033[97;33u"); /* meta */
    assert(m.data.key.mods == TUI_MOD_META);

    /* And the plain combinations are exactly what they were. */
    assert(parse_one("\033[97;2u").data.key.mods == TUI_MOD_SHIFT);
    assert(parse_one("\033[97;3u").data.key.mods == TUI_MOD_ALT);
    assert(parse_one("\033[97;5u").data.key.mods == TUI_MOD_CTRL);
    assert(parse_one("\033[97;8u").data.key.mods ==
           (TUI_MOD_CTRL | TUI_MOD_ALT | TUI_MOD_SHIFT));
}

/* A functional key that produced TEXT is not dropped: a keypad digit
 * with Num Lock on is the PUA key code KP_1 with the digit in its text
 * field, and the digit is what the user typed. */
static void test_kitty_keypad_digit_keeps_its_text(void)
{
    /* CSI 57400;129;49u — KP_1, Num Lock on, text "1". */
    TuiMsg m = parse_one("\033[57400;129;49u");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.rune == '1');
    assert(m.data.key.mods == TUI_MOD_NONE);
    assert(m.data.key.text_len == 1);
    assert(m.data.key.text[0] == '1');
}

/* A functional key is not text. The "report all keys as escape codes"
 * flag reports the modifier keys THEMSELVES (a Shift press is `CSI
 * 57441;2u`) alongside the keys that have no character of their own,
 * and every one of them is encoded in the Private Use Area. Handing
 * that codepoint to a consumer as a rune types an invisible PUA
 * character — once per capital the user types. */
static void test_kitty_functional_keys_are_not_text(void)
{
    /* Left shift, left control, left super (the modifier keys). */
    assert(parse_none("\033[57441;2u"));
    assert(parse_none("\033[57442;5u"));
    assert(parse_none("\033[57444;9u"));
    /* A lock key and a key past F12 (no key code in boba to report). */
    assert(parse_none("\033[57358u"));
    assert(parse_none("\033[57376u"));
    /* A modifier key RELEASE (flag 2 + flag 8) is no more text. */
    assert(parse_none("\033[57441;2:3u"));
}

/* The other half of that rule: a PUA CHARACTER still arrives as text,
 * because text travels in the text field — a pure text event carries
 * key code 0 and the codepoint is text. */
static void test_kitty_pua_text_field_is_still_text(void)
{
    /* CSI 0;;57344u — a text event whose text is U+E000. */
    TuiMsg m = parse_one("\033[0;;57344u");
    assert(m.type == TUI_MSG_KEY_PRESS);
    assert(m.data.key.key == TUI_KEY_NONE);
    assert(m.data.key.rune == 0xE000);
    assert(m.data.key.text_len == 3);
    assert(memcmp(m.data.key.text, "\xee\x80\x80", 3) == 0);
}

/* A key event's text is a grapheme, not a paragraph: the buffer is
 * bounded, and what does not fit is dropped rather than overflowing. */
static void test_kitty_associated_text_is_bounded(void)
{
    /* 20 three-byte codepoints (U+0800) = 60 bytes, well past the cap. */
    char seq[256];
    int n = snprintf(seq, sizeof(seq), "\033[97;1;%u", 0x800u);
    for (int i = 1; i < 20; i++) {
        char part[16];
        snprintf(part, sizeof(part), ":%u", 0x800u);
        size_t len = strlen(seq);
        snprintf(seq + len, sizeof(seq) - len, "%s", part);
    }
    size_t len = strlen(seq);
    snprintf(seq + len, sizeof(seq) - len, "u");

    TuiMsg m = parse_one(seq);
    assert(m.data.key.text_len <= TUI_KEY_TEXT_MAX);
    /* Whole codepoints only: the cap is a multiple of three here. */
    assert(m.data.key.text_len == (TUI_KEY_TEXT_MAX / 3) * 3);
    assert(m.data.key.rune == 0x800u);
}

/* The kitty keyboard flags answer is a capability REPLY, not a key * press: `CSI ? <flags> u` (a key event is `CSI <code> u`, no `?`). A
 * probe that claimed replies gets it parked; without a claim it is
 * dropped, never typed. */
static void test_kitty_flags_reply_is_claimed_not_typed(void)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    tui_input_parser_claim_reply(p);

    const char *reply = "\033[?9u";
    TuiMsg msgs[4];
    int n = tui_input_parser_parse(p, (const unsigned char *)reply,
                                   strlen(reply), msgs, 4);
    assert(n == 0);

    char *payload = NULL;
    size_t len = 0;
    assert(tui_input_parser_next_reply(p, &payload, &len) == 1);
    assert(payload != NULL);
    assert(strcmp(payload, "[?9u") == 0);
    free(payload);

    /* The same bytes with no probe outstanding are dropped — never a
     * Tab (which is what a flat decode of "?9u" would produce). */
    tui_input_parser_release_reply(p);
    n = tui_input_parser_parse(p, (const unsigned char *)reply, strlen(reply),
                               msgs, 4);
    assert(n == 0);

    /* A real key event still parses. */
    const char *key = "\033[97u";
    n = tui_input_parser_parse(p, (const unsigned char *)key, strlen(key),
                               msgs, 4);
    assert(n == 1);
    assert(msgs[0].type == TUI_MSG_KEY_PRESS);
    assert(msgs[0].data.key.rune == 'a');
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);

    tui_input_parser_free(p);
}

/* Regression: standard CSI sequences with ';' separator still parse after
 * the param-array refactor. */
static void test_csi_ctrl_up_still_works(void)
{
    /* CSI 1;5A — Ctrl+Up arrow. */
    TuiMsg m = parse_one("\033[1;5A");
    assert(m.data.key.key == TUI_KEY_UP);
    assert(m.data.key.mods == TUI_MOD_CTRL);
    assert(m.data.key.action == TUI_KEY_ACTION_PRESS);
}

/* ----- bracketed paste ------------------------------------------------- */

/* Feed the whole input in one shot and assert exactly `expected` messages
 * were produced. Caller is responsible for freeing each msg with
 * tui_msg_free(). */
static int parse_into(const char *input, size_t input_len, TuiMsg *out,
                      int max)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    int n = tui_input_parser_parse(p, (const unsigned char *)input, input_len,
                                   out, max);
    tui_input_parser_free(p);
    return n;
}

static void test_paste_simple(void)
{
    const char *input = "\033[200~hello\033[201~";
    TuiMsg msgs[8];
    int n = parse_into(input, strlen(input), msgs, 8);
    assert(n == 3);
    assert(msgs[0].type == TUI_MSG_PASTE_START);
    assert(msgs[1].type == TUI_MSG_PASTE);
    assert(msgs[1].data.paste.len == 5);
    assert(strcmp(msgs[1].data.paste.text, "hello") == 0);
    assert(msgs[2].type == TUI_MSG_PASTE_END);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
}

static void test_paste_empty(void)
{
    /* 200~ immediately followed by 201~ — zero-length payload. */
    const char *input = "\033[200~\033[201~";
    TuiMsg msgs[8];
    int n = parse_into(input, strlen(input), msgs, 8);
    assert(n == 3);
    assert(msgs[0].type == TUI_MSG_PASTE_START);
    assert(msgs[1].type == TUI_MSG_PASTE);
    assert(msgs[1].data.paste.len == 0);
    assert(msgs[1].data.paste.text != NULL);
    assert(msgs[1].data.paste.text[0] == '\0');
    assert(msgs[2].type == TUI_MSG_PASTE_END);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
}

static void test_paste_split_across_feeds(void)
{
    /* Same paste, but fed in two batches via two parse() calls on the
     * same parser. The second call should drain pending PASTE_END from
     * the first via the queued-pending path. */
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    TuiMsg msgs[8];

    const char *part1 = "\033[200~hel";
    int n1 = tui_input_parser_parse(p, (const unsigned char *)part1,
                                    strlen(part1), msgs, 8);
    /* Only PASTE_START so far. */
    assert(n1 == 1);
    assert(msgs[0].type == TUI_MSG_PASTE_START);

    const char *part2 = "lo\033[201~";
    int n2 = tui_input_parser_parse(p, (const unsigned char *)part2,
                                    strlen(part2), msgs, 8);
    assert(n2 == 2);
    assert(msgs[0].type == TUI_MSG_PASTE);
    assert(msgs[0].data.paste.len == 5);
    assert(strcmp(msgs[0].data.paste.text, "hello") == 0);
    assert(msgs[1].type == TUI_MSG_PASTE_END);
    tui_msg_free(&msgs[0]);
    tui_msg_free(&msgs[1]);
    tui_input_parser_free(p);
}

static void test_paste_contains_csi_not_reparsed(void)
{
    /* Paste containing what looks like a CSI sequence: must NOT be
     * dispatched as a key/cursor msg. Payload is the literal bytes. */
    const char *input = "\033[200~ab\033[1;2Hcd\033[201~";
    TuiMsg msgs[8];
    int n = parse_into(input, strlen(input), msgs, 8);
    assert(n == 3);
    assert(msgs[1].type == TUI_MSG_PASTE);
    /* Payload: "ab\033[1;2Hcd" = 10 bytes */
    assert(msgs[1].data.paste.len == 10);
    assert(memcmp(msgs[1].data.paste.text, "ab\033[1;2Hcd", 10) == 0);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
}

static void test_paste_contains_bare_esc(void)
{
    /* Bare ESC (not followed by [) inside the paste. The terminator
     * scanner should match \033 then mismatch on the next byte and
     * flush \033 back into the buffer. */
    const char *input = "\033[200~x\033yz\033[201~";
    TuiMsg msgs[8];
    int n = parse_into(input, strlen(input), msgs, 8);
    assert(n == 3);
    assert(msgs[1].type == TUI_MSG_PASTE);
    assert(msgs[1].data.paste.len == 4);
    assert(memcmp(msgs[1].data.paste.text, "x\033yz", 4) == 0);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
}

static void test_paste_grows_beyond_initial_buf(void)
{
    /* Force the geometric grow path: paste payload exceeds initial 256
     * bytes. */
    char input[2048];
    char expected[1024];
    size_t pos = 0;
    memcpy(input + pos, "\033[200~", 6);
    pos += 6;
    /* 1000 'A's */
    memset(input + pos, 'A', 1000);
    memset(expected, 'A', 1000);
    expected[1000] = '\0';
    pos += 1000;
    memcpy(input + pos, "\033[201~", 6);
    pos += 6;

    TuiMsg msgs[8];
    int n = parse_into(input, pos, msgs, 8);
    assert(n == 3);
    assert(msgs[1].type == TUI_MSG_PASTE);
    assert(msgs[1].data.paste.len == 1000);
    assert(memcmp(msgs[1].data.paste.text, expected, 1000) == 0);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
}

/* ----- capability replies (OSC / DCS / APC captures) -------------------- */

/* Feed `input`, then pull replies the way the runtime does. Returns the
 * count; fills up to `max` payloads (caller frees each). */
static int capture_replies(const char *input, char *out[], int max)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    tui_input_parser_claim_reply(p);
    TuiMsg msgs[8];
    int n = tui_input_parser_parse(p, (const unsigned char *)input,
                                   strlen(input), msgs, 8);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
    int got = 0;
    char *payload = NULL;
    size_t len = 0;
    while (got < max && tui_input_parser_next_reply(p, &payload, &len)) {
        out[got++] = payload;
        payload = NULL;
    }
    tui_input_parser_free(p);
    return got;
}

static void test_kitty_query_reply_captured(void)
{
    char *r[4];
    int n = capture_replies("\033_Gi=31;OK\033\\", r, 4);
    assert(n == 1);
    assert(strcmp(r[0], "Gi=31;OK") == 0);
    free(r[0]);
}

static void test_da1_reply_captured(void)
{
    char *r[4];
    int n = capture_replies("\033[?1;2;4c", r, 4);
    assert(n == 1);
    assert(strcmp(r[0], "[?1;2;4c") == 0);
    free(r[0]);
}

static void test_cell_size_reply_captured(void)
{
    char *r[4];
    int n = capture_replies("\033[6;18;9t", r, 4);
    assert(n == 1);
    assert(strcmp(r[0], "[6;18;9t") == 0);
    free(r[0]);
}

static void test_xtversion_reply_captured(void)
{
    char *r[4];
    int n = capture_replies("\033P>|kitty(0.36.0)\033\\", r, 4);
    assert(n == 1);
    assert(strcmp(r[0], ">|kitty(0.36.0)") == 0);
    free(r[0]);
}

static void test_replies_are_not_key_input(void)
{
    /* A config reply must produce ZERO key messages — it used to leak
     * in as characters (the '?' and '1' of "?1;2;4c"). */
    TuiMsg msgs[8];
    const char *input = "\033[?1;2;4c";
    int n = parse_into(input, strlen(input), msgs, 8);
    assert(n == 0);
}

static void test_unclaimed_replies_are_dropped(void)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    TuiMsg msgs[4];
    const char *seq = "\033_Gi=31;OK\033\\";
    int n = tui_input_parser_parse(p, (const unsigned char *)seq, strlen(seq),
                                   msgs, 4);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
    char *payload = NULL;
    size_t len = 0;
    assert(tui_input_parser_next_reply(p, &payload, &len) == 0);
    tui_input_parser_free(p);
}

static void test_release_drops_queued_replies(void)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    tui_input_parser_claim_reply(p);
    TuiMsg msgs[4];
    const char *seq = "\033_Gi=31;OK\033\\";
    int n = tui_input_parser_parse(p, (const unsigned char *)seq, strlen(seq),
                                   msgs, 4);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
    tui_input_parser_release_reply(p);
    char *payload = NULL;
    size_t len = 0;
    assert(tui_input_parser_next_reply(p, &payload, &len) == 0);
    tui_input_parser_free(p);
}

static void test_reply_split_across_feeds(void)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    tui_input_parser_claim_reply(p);
    TuiMsg msgs[4];
    /* sequence split mid-payload and mid-terminator */
    const char *p1 = "\033_Gi=3";
    const char *p2 = "1;OK\033";
    const char *p3 = "\\";
    int n = tui_input_parser_parse(p, (const unsigned char *)p1, strlen(p1),
                                   msgs, 4);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
    n = tui_input_parser_parse(p, (const unsigned char *)p2, strlen(p2), msgs,
                               4);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
    n = tui_input_parser_parse(p, (const unsigned char *)p3, strlen(p3), msgs,
                               4);
    for (int i = 0; i < n; i++)
        tui_msg_free(&msgs[i]);
    char *payload = NULL;
    size_t len = 0;
    assert(tui_input_parser_next_reply(p, &payload, &len) == 1);
    assert(strcmp(payload, "Gi=31;OK") == 0);
    free(payload);
    tui_input_parser_free(p);
}

static void test_key_after_reply_still_parses(void)
{
    TuiInputParser *p = tui_input_parser_create();
    assert(p != NULL);
    tui_input_parser_claim_reply(p);
    TuiMsg msgs[4];
    const char *seq = "\033_Gi=31;OK\033\\x";
    int n = tui_input_parser_parse(p, (const unsigned char *)seq, strlen(seq),
                                   msgs, 4);
    assert(n == 1);
    assert(msgs[0].type == TUI_MSG_KEY_PRESS);
    assert(msgs[0].data.key.rune == 'x');
    tui_msg_free(&msgs[0]);
    char *payload = NULL;
    size_t len = 0;
    assert(tui_input_parser_next_reply(p, &payload, &len) == 1);
    free(payload);
    tui_input_parser_free(p);
}

static void test_osc_title_is_not_a_reply(void)
{
    /* Unrecognized string traffic is captured-and-dropped, never
     * surfaced as keys and never as a reply payload. */
    TuiMsg msgs[4];
    const char *input = "\033]0;my title\007x";
    int n = parse_into(input, strlen(input), msgs, 4);
    assert(n == 1);
    assert(msgs[0].type == TUI_MSG_KEY_PRESS);
    assert(msgs[0].data.key.rune == 'x');
    tui_msg_free(&msgs[0]);
}

int main(void)
{
    printf("Running input parser tests...\n");

    RUN_TEST(test_left_press);
    RUN_TEST(test_left_release);
    RUN_TEST(test_middle_press);
    RUN_TEST(test_right_press);
    RUN_TEST(test_release_no_button);

    RUN_TEST(test_wheel_up);
    RUN_TEST(test_wheel_down);
    RUN_TEST(test_wheel_left);
    RUN_TEST(test_wheel_right);

    RUN_TEST(test_shift_left_click);
    RUN_TEST(test_meta_left_click);
    RUN_TEST(test_ctrl_left_click);
    RUN_TEST(test_ctrl_shift_wheel_up);

    RUN_TEST(test_motion_left_held);
    RUN_TEST(test_motion_with_ctrl);

    RUN_TEST(test_regular_char_still_parses);

    RUN_TEST(test_kitty_press_default_action);
    RUN_TEST(test_kitty_press_explicit_event_type);
    RUN_TEST(test_kitty_repeat_maps_to_press);
    RUN_TEST(test_kitty_release);
    RUN_TEST(test_kitty_release_with_ctrl);
    RUN_TEST(test_kitty_release_special_key);
    RUN_TEST(test_kitty_associated_text_capital);
    RUN_TEST(test_kitty_associated_text_grapheme);
    RUN_TEST(test_kitty_pure_text_event);
    RUN_TEST(test_kitty_event_type_needs_the_colon);
    RUN_TEST(test_kitty_special_key_ignores_text);
    RUN_TEST(test_kitty_lock_modifiers_do_not_eat_the_modifiers);
    RUN_TEST(test_kitty_keypad_digit_keeps_its_text);
    RUN_TEST(test_kitty_functional_keys_are_not_text);
    RUN_TEST(test_kitty_pua_text_field_is_still_text);
    RUN_TEST(test_kitty_associated_text_is_bounded);
    RUN_TEST(test_kitty_ctrl_c_is_interrupt);
    RUN_TEST(test_kitty_ctrl_shift_c_is_interrupt);
    RUN_TEST(test_kitty_ctrl_d_is_eof);
    RUN_TEST(test_kitty_alt_ctrl_c_is_a_key);
    RUN_TEST(test_kitty_other_ctrl_keys_are_keys);
    RUN_TEST(test_kitty_flags_reply_is_claimed_not_typed);
    RUN_TEST(test_csi_ctrl_up_still_works);

    RUN_TEST(test_paste_simple);
    RUN_TEST(test_paste_empty);
    RUN_TEST(test_paste_split_across_feeds);
    RUN_TEST(test_paste_contains_csi_not_reparsed);
    RUN_TEST(test_paste_contains_bare_esc);
    RUN_TEST(test_paste_grows_beyond_initial_buf);

    RUN_TEST(test_kitty_query_reply_captured);
    RUN_TEST(test_da1_reply_captured);
    RUN_TEST(test_cell_size_reply_captured);
    RUN_TEST(test_xtversion_reply_captured);
    RUN_TEST(test_replies_are_not_key_input);
    RUN_TEST(test_unclaimed_replies_are_dropped);
    RUN_TEST(test_release_drops_queued_replies);
    RUN_TEST(test_reply_split_across_feeds);
    RUN_TEST(test_key_after_reply_still_parses);
    RUN_TEST(test_osc_title_is_not_a_reply);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
