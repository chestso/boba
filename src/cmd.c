/* cmd.c - Command implementation for boba TUI library */

#include <boba/cmd.h>
#include <stdlib.h>
#include <string.h>

/* Create a null/none command */
TuiCmd *tui_cmd_none(void) { return NULL; /* NULL represents no command */ }

/* Create a quit command */
TuiCmd *tui_cmd_quit(void)
{
    TuiCmd *cmd = (TuiCmd *)malloc(sizeof(TuiCmd));
    if (!cmd)
        return NULL;

    memset(cmd, 0, sizeof(TuiCmd));
    cmd->type = TUI_CMD_QUIT;
    return cmd;
}

/* Create a batch of commands */
TuiCmd *tui_cmd_batch(TuiCmd **cmds, int count)
{
    if (!cmds || count <= 0)
        return NULL;

    TuiCmd *cmd = (TuiCmd *)malloc(sizeof(TuiCmd));
    if (!cmd)
        return NULL;

    memset(cmd, 0, sizeof(TuiCmd));
    cmd->type = TUI_CMD_BATCH;

    /* Copy command pointers */
    cmd->payload.batch.cmds = (TuiCmd **)malloc(count * sizeof(TuiCmd *));
    if (!cmd->payload.batch.cmds) {
        free(cmd);
        return NULL;
    }

    memcpy(cmd->payload.batch.cmds, cmds, count * sizeof(TuiCmd *));
    cmd->payload.batch.count = count;

    return cmd;
}

/* Convenience function: batch two commands together */
TuiCmd *tui_cmd_batch2(TuiCmd *cmd1, TuiCmd *cmd2)
{
    /* Handle NULL cases */
    if (!cmd1 && !cmd2)
        return NULL;
    if (!cmd1)
        return cmd2;
    if (!cmd2)
        return cmd1;

    /* Both non-NULL, create batch */
    TuiCmd *cmds[2] = { cmd1, cmd2 };
    return tui_cmd_batch(cmds, 2);
}

/* Create a custom command with callback */
TuiCmd *tui_cmd_custom(TuiCmdCallback callback, void *data,
                       void (*free_data)(void *))
{
    if (!callback)
        return NULL;

    TuiCmd *cmd = (TuiCmd *)malloc(sizeof(TuiCmd));
    if (!cmd)
        return NULL;

    memset(cmd, 0, sizeof(TuiCmd));
    cmd->type = TUI_CMD_CUSTOM_BASE;
    cmd->payload.custom.callback = callback;
    cmd->payload.custom.data = data;
    cmd->payload.custom.free_data = free_data;

    return cmd;
}

/* Create a line submit command (takes ownership of line string) */
TuiCmd *tui_cmd_line_submit(char *line)
{
    TuiCmd *cmd = (TuiCmd *)malloc(sizeof(TuiCmd));
    if (!cmd)
        return NULL;

    memset(cmd, 0, sizeof(TuiCmd));
    cmd->type = TUI_CMD_LINE_SUBMIT;
    cmd->payload.line = line;

    return cmd;
}

/* Create a tab complete command (takes ownership of prefix string) */
TuiCmd *tui_cmd_tab_complete(char *prefix, int word_start)
{
    TuiCmd *cmd = (TuiCmd *)malloc(sizeof(TuiCmd));
    if (!cmd)
        return NULL;

    memset(cmd, 0, sizeof(TuiCmd));
    cmd->type = TUI_CMD_TAB_COMPLETE;
    cmd->payload.tab_complete.prefix = prefix;
    cmd->payload.tab_complete.word_start = word_start;

    return cmd;
}

TuiCmd *tui_cmd_clipboard_copy(const char *text, size_t len)
{
    TuiCmd *cmd = (TuiCmd *)malloc(sizeof(TuiCmd));
    if (!cmd)
        return NULL;
    memset(cmd, 0, sizeof(TuiCmd));
    cmd->type = TUI_CMD_CLIPBOARD_COPY;
    if (text && len > 0) {
        cmd->payload.clipboard.text = (char *)malloc(len);
        if (!cmd->payload.clipboard.text) {
            free(cmd);
            return NULL;
        }
        memcpy(cmd->payload.clipboard.text, text, len);
        cmd->payload.clipboard.len = len;
    } else {
        cmd->payload.clipboard.text = NULL;
        cmd->payload.clipboard.len = 0;
    }
    return cmd;
}

/* Free a command and its associated resources */
void tui_cmd_free(TuiCmd *cmd)
{
    if (!cmd)
        return;

    switch (cmd->type) {
    case TUI_CMD_BATCH:
        if (cmd->payload.batch.cmds) {
            for (int i = 0; i < cmd->payload.batch.count; i++) {
                tui_cmd_free(cmd->payload.batch.cmds[i]);
            }
            free(cmd->payload.batch.cmds);
        }
        break;

    case TUI_CMD_CUSTOM_BASE:
    default:
        if (cmd->type >= TUI_CMD_CUSTOM_BASE) {
            if (cmd->payload.custom.free_data && cmd->payload.custom.data) {
                cmd->payload.custom.free_data(cmd->payload.custom.data);
            }
        }
        break;

    case TUI_CMD_LINE_SUBMIT:
        if (cmd->payload.line) {
            free(cmd->payload.line);
        }
        break;

    case TUI_CMD_TAB_COMPLETE:
        if (cmd->payload.tab_complete.prefix) {
            free(cmd->payload.tab_complete.prefix);
        }
        break;

    case TUI_CMD_CLIPBOARD_COPY:
        if (cmd->payload.clipboard.text) {
            free(cmd->payload.clipboard.text);
        }
        break;

    case TUI_CMD_NONE:
    case TUI_CMD_QUIT:
        /* No resources to free */
        break;
    }

    free(cmd);
}

/* Check if command is the none/null command */
int tui_cmd_is_none(TuiCmd *cmd)
{
    return cmd == NULL || cmd->type == TUI_CMD_NONE;
}
