/* cmd.h - Command types for bloom-boba TUI library
 *
 * Commands represent side effects in the Elm Architecture. The Update function
 * can return commands that will be executed asynchronously, producing new
 * messages.
 */

#ifndef BLOOM_BOBA_CMD_H
#define BLOOM_BOBA_CMD_H

#include "msg.h"
#include <stddef.h>

/* Forward declaration */
typedef struct TuiCmd TuiCmd;

/* Command type enumeration.
 *
 * Bubbletea v2 alignment: terminal-mode imperative commands have been
 * removed in favour of declarative TuiView fields. Components set
 * alt-screen, mouse mode, keyboard enhancements, cursor, focus
 * reporting, bracketed paste, and window title on the TuiView returned
 * from view(); the runtime reconciles each frame. */
typedef enum
{
    TUI_CMD_NONE = 0,           /* No command / null command */
    TUI_CMD_QUIT,               /* Quit the application */
    TUI_CMD_BATCH,              /* Batch of commands to execute */
    TUI_CMD_LINE_SUBMIT,        /* Line submitted (contains line text) */
    TUI_CMD_TAB_COMPLETE,       /* Tab pressed (contains prefix and word position) */
    TUI_CMD_CLIPBOARD_COPY,     /* Copy text to system clipboard */
    TUI_CMD_CUSTOM_BASE = 1000, /* Base for application-defined commands */
} TuiCmdType;

/* Command callback function type
 * Returns a TuiMsg that will be sent to the update function
 */
typedef TuiMsg (*TuiCmdCallback)(void *data);

/* Command structure */
struct TuiCmd
{
    TuiCmdType type;
    union
    {
        struct
        {
            TuiCmdCallback callback;
            void *data;
            void (*free_data)(void *); /* Optional cleanup function for data */
        } custom;
        struct
        {
            TuiCmd **cmds;
            int count;
        } batch;
        char *line; /* For TUI_CMD_LINE_SUBMIT - owned, must be freed */
        struct
        {
            char *prefix;   /* Word prefix at cursor - owned, must be freed */
            int word_start; /* Byte offset where the word starts in input */
        } tab_complete;
        struct
        {
            char *text; /* Owned, must be freed (binary-safe; not null-terminated reliance) */
            size_t len; /* Byte length of text */
        } clipboard;
    } payload;
};

/* Command constructor functions */

/* Create a null/none command */
TuiCmd *tui_cmd_none(void);

/* Create a quit command */
TuiCmd *tui_cmd_quit(void);

/* Create a batch of commands */
TuiCmd *tui_cmd_batch(TuiCmd **cmds, int count);

/* Convenience function: batch two commands together
 * Skips NULL commands: if only one is non-NULL, returns that one.
 * If both NULL, returns NULL.
 */
TuiCmd *tui_cmd_batch2(TuiCmd *cmd1, TuiCmd *cmd2);

/* Create a custom command with callback */
TuiCmd *tui_cmd_custom(TuiCmdCallback callback, void *data,
                       void (*free_data)(void *));

/* Create a line submit command (takes ownership of line string) */
TuiCmd *tui_cmd_line_submit(char *line);

/* Create a tab complete command (takes ownership of prefix string) */
TuiCmd *tui_cmd_tab_complete(char *prefix, int word_start);

/* Create a clipboard copy command. Copies `len` bytes of `text` into the
 * command (caller retains ownership of the input pointer). The runtime
 * either calls the configured TuiClipboardHandler (if set) or emits OSC 52
 * to the output. */
TuiCmd *tui_cmd_clipboard_copy(const char *text, size_t len);

/* Free a command and its associated resources */
void tui_cmd_free(TuiCmd *cmd);

/* Check if command is the none/null command */
int tui_cmd_is_none(TuiCmd *cmd);

#endif /* BLOOM_BOBA_CMD_H */
