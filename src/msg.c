/* msg.c - Message implementation for boba TUI library */

#include <boba/msg.h>
#include <stdlib.h>
#include <string.h>

/* Create a null/empty message */
TuiMsg tui_msg_none(void)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_NONE;
    return msg;
}

/* Create a key press message with special key code */
TuiMsg tui_msg_key(int key, uint32_t rune, int mods)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_KEY_PRESS;
    msg.data.key.key = key;
    msg.data.key.rune = rune;
    msg.data.key.mods = mods;
    return msg;
}

/* Create a key release message */
TuiMsg tui_msg_key_release(int key, uint32_t rune, int mods)
{
    TuiMsg msg = tui_msg_key(key, rune, mods);
    msg.data.key.action = TUI_KEY_ACTION_RELEASE;
    return msg;
}

/* Create a key press message for a regular character */
TuiMsg tui_msg_char(uint32_t rune, int mods)
{
    return tui_msg_key(TUI_KEY_NONE, rune, mods);
}

/* Create a window size message */
TuiMsg tui_msg_window_size(int width, int height)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_WINDOW_SIZE;
    msg.data.size.width = width;
    msg.data.size.height = height;
    return msg;
}

/* Create a focus message */
TuiMsg tui_msg_focus(void)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_FOCUS;
    return msg;
}

/* Create a blur message */
TuiMsg tui_msg_blur(void)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_BLUR;
    return msg;
}

/* Create an interrupt message (Ctrl+C) */
TuiMsg tui_msg_interrupt(void)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_INTERRUPT;
    return msg;
}

/* Create a custom message */
TuiMsg tui_msg_custom(int type, void *data)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = (TuiMsgType)type;
    msg.data.custom = data;
    return msg;
}

/* Create a mouse message */
TuiMsg tui_msg_mouse(TuiMouseButton button, TuiMouseAction action, int col,
                     int row, int mods)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_MOUSE;
    msg.data.mouse.button = button;
    msg.data.mouse.action = action;
    msg.data.mouse.col = col;
    msg.data.mouse.row = row;
    msg.data.mouse.mods = mods;
    return msg;
}

/* Create a paste-start message */
TuiMsg tui_msg_paste_start(void)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_PASTE_START;
    return msg;
}

/* Create a paste message (takes ownership of text) */
TuiMsg tui_msg_paste(char *text, size_t len)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_PASTE;
    msg.data.paste.text = text;
    msg.data.paste.len = len;
    return msg;
}

/* Create a paste-end message */
TuiMsg tui_msg_paste_end(void)
{
    TuiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = TUI_MSG_PASTE_END;
    return msg;
}

/* Free any heap data owned by a message */
void tui_msg_free(TuiMsg *msg)
{
    if (!msg)
        return;
    if (msg->type == TUI_MSG_PASTE) {
        free(msg->data.paste.text);
        msg->data.paste.text = NULL;
        msg->data.paste.len = 0;
    }
}

/* Check if message is a key press of specific type */
int tui_msg_is_key(TuiMsg msg, int key)
{
    return msg.type == TUI_MSG_KEY_PRESS && msg.data.key.key == key;
}

/* Check if message is a specific character */
int tui_msg_is_char(TuiMsg msg, uint32_t rune)
{
    return msg.type == TUI_MSG_KEY_PRESS && msg.data.key.key == TUI_KEY_NONE &&
           msg.data.key.rune == rune;
}

/* Check if message has specific modifier */
int tui_msg_has_mod(TuiMsg msg, int mod)
{
    if (msg.type != TUI_MSG_KEY_PRESS) {
        return 0;
    }
    return (msg.data.key.mods & mod) != 0;
}
