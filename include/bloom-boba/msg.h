/* msg.h - Message types for bloom-boba TUI library
 *
 * Messages represent events in the Elm Architecture. The Update function
 * receives messages and produces new state plus optional commands.
 */

#ifndef BLOOM_BOBA_MSG_H
#define BLOOM_BOBA_MSG_H

#include <stdint.h>

/* Message type enumeration */
typedef enum
{
    TUI_MSG_NONE = 0,           /* No message / null message */
    TUI_MSG_KEY_PRESS,          /* Key press event */
    TUI_MSG_MOUSE,              /* Mouse event */
    TUI_MSG_WINDOW_SIZE,        /* Terminal window size changed */
    TUI_MSG_FOCUS,              /* Component gained focus */
    TUI_MSG_BLUR,               /* Component lost focus */
    TUI_MSG_LINE_SUBMIT,        /* Line submitted (Enter pressed in single-line mode) */
    TUI_MSG_EOF,                /* End of input (Ctrl+D with empty input) */
    TUI_MSG_PASTE_START,        /* Bracketed paste begins (no payload) */
    TUI_MSG_PASTE,              /* Bracketed paste payload */
    TUI_MSG_PASTE_END,          /* Bracketed paste ends (no payload) */
    TUI_MSG_CUSTOM_BASE = 1000, /* Base for application-defined messages */
} TuiMsgType;

/* Modifier key flags */
typedef enum
{
    TUI_MOD_NONE = 0,
    TUI_MOD_CTRL = 1 << 0,
    TUI_MOD_ALT = 1 << 1,
    TUI_MOD_SHIFT = 1 << 2,
    TUI_MOD_META = 1 << 3,
} TuiKeyMod;

/* Key event action — press vs release.
 *
 * TUI_KEY_ACTION_PRESS is the default and what most apps want; release
 * events only arrive when the consumer enables the Kitty keyboard
 * protocol (set TuiView.kbd_enhancements = TUI_KBD_KITTY) and the
 * terminal supports it. Repeat events are folded into PRESS to match
 * Bubbletea v2's KeyMsg semantics. */
typedef enum
{
    TUI_KEY_ACTION_PRESS = 0,
    TUI_KEY_ACTION_RELEASE,
} TuiKeyAction;

/* Mouse button codes (SGR extended mode) */
typedef enum
{
    TUI_MOUSE_LEFT = 0,
    TUI_MOUSE_MIDDLE = 1,
    TUI_MOUSE_RIGHT = 2,
    TUI_MOUSE_RELEASE = 3, /* Button release (no specific button) */
    TUI_MOUSE_WHEEL_UP = 64,
    TUI_MOUSE_WHEEL_DOWN = 65,
    TUI_MOUSE_WHEEL_LEFT = 66,
    TUI_MOUSE_WHEEL_RIGHT = 67,
} TuiMouseButton;

/* Mouse action types */
typedef enum
{
    TUI_MOUSE_ACTION_PRESS,
    TUI_MOUSE_ACTION_RELEASE,
    TUI_MOUSE_ACTION_MOTION,
} TuiMouseAction;

/* Mouse message data */
typedef struct
{
    TuiMouseButton button;
    TuiMouseAction action;
    int col;  /* 1-indexed column */
    int row;  /* 1-indexed row */
    int mods; /* Modifier flags (TuiKeyMod) */
} TuiMouseMsg;

/* Special key codes (non-printable keys) */
typedef enum
{
    TUI_KEY_NONE = 0,
    TUI_KEY_ENTER = 1,
    TUI_KEY_TAB = 2,
    TUI_KEY_BACKSPACE = 3,
    TUI_KEY_DELETE = 4,
    TUI_KEY_INSERT = 5,
    TUI_KEY_HOME = 6,
    TUI_KEY_END = 7,
    TUI_KEY_PAGE_UP = 8,
    TUI_KEY_PAGE_DOWN = 9,
    TUI_KEY_UP = 10,
    TUI_KEY_DOWN = 11,
    TUI_KEY_LEFT = 12,
    TUI_KEY_RIGHT = 13,
    TUI_KEY_ESCAPE = 14,
    TUI_KEY_F1 = 15,
    TUI_KEY_F2 = 16,
    TUI_KEY_F3 = 17,
    TUI_KEY_F4 = 18,
    TUI_KEY_F5 = 19,
    TUI_KEY_F6 = 20,
    TUI_KEY_F7 = 21,
    TUI_KEY_F8 = 22,
    TUI_KEY_F9 = 23,
    TUI_KEY_F10 = 24,
    TUI_KEY_F11 = 25,
    TUI_KEY_F12 = 26,
} TuiKeyCode;

/* Key press message data */
typedef struct
{
    int key;             /* Special key code (TuiKeyCode) or 0 for regular char */
    uint32_t rune;       /* Unicode codepoint for regular characters */
    int mods;            /* Modifier flags (TuiKeyMod) */
    TuiKeyAction action; /* Press (default) vs release */
} TuiKeyMsg;

/* Window size message data */
typedef struct
{
    int width;
    int height;
} TuiWindowSizeMsg;

#include <stddef.h> /* size_t */

/* Bracketed-paste payload.
 *
 * Memory ownership: text is heap-allocated and owned by the message. The
 * runtime calls tui_msg_free() on each TuiMsg after dispatch, which frees
 * text. If a component needs the text beyond its own update() call, it
 * must copy the bytes. */
typedef struct
{
    char *text; /* UTF-8 paste payload (also null-terminated for convenience) */
    size_t len; /* Byte length, not counting trailing null */
} TuiPasteMsg;

/* Main message structure (tagged union) */
typedef struct
{
    TuiMsgType type;
    union
    {
        TuiKeyMsg key;
        TuiMouseMsg mouse;
        TuiWindowSizeMsg size;
        TuiPasteMsg paste;
        void *custom; /* For application-defined message data */
    } data;
} TuiMsg;

/* Message constructor functions */

/* Create a null/empty message */
TuiMsg tui_msg_none(void);

/* Create a key press message with special key code */
TuiMsg tui_msg_key(int key, uint32_t rune, int mods);

/* Create a key release message — same shape as tui_msg_key but with
 * action = TUI_KEY_ACTION_RELEASE. Only emitted by the parser when the
 * Kitty keyboard protocol is in use. */
TuiMsg tui_msg_key_release(int key, uint32_t rune, int mods);

/* Create a key press message for a regular character */
TuiMsg tui_msg_char(uint32_t rune, int mods);

/* Create a window size message */
TuiMsg tui_msg_window_size(int width, int height);

/* Create a focus message */
TuiMsg tui_msg_focus(void);

/* Create a blur message */
TuiMsg tui_msg_blur(void);

/* Create a custom message */
TuiMsg tui_msg_custom(int type, void *data);

/* Create a mouse message */
TuiMsg tui_msg_mouse(TuiMouseButton button, TuiMouseAction action, int col,
                     int row, int mods);

/* Create a paste-start message (no payload) */
TuiMsg tui_msg_paste_start(void);

/* Create a paste message. Takes ownership of `text`; tui_msg_free() will
 * free it. `text` must be heap-allocated (malloc/realloc). */
TuiMsg tui_msg_paste(char *text, size_t len);

/* Create a paste-end message (no payload) */
TuiMsg tui_msg_paste_end(void);

/* Free any heap data owned by a TuiMsg (currently: paste text). No-op for
 * messages without owned data. Safe to call multiple times. */
void tui_msg_free(TuiMsg *msg);

/* Check if message is a key press of specific type */
int tui_msg_is_key(TuiMsg msg, int key);

/* Check if message is a specific character */
int tui_msg_is_char(TuiMsg msg, uint32_t rune);

/* Check if message has specific modifier */
int tui_msg_has_mod(TuiMsg msg, int mod);

#endif /* BLOOM_BOBA_MSG_H */
