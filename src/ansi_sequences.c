/* ansi_sequences.c - Helper functions for parameterized ANSI sequences */

#include "base64.h"
#include <boba/ansi_sequences.h>
#include <stdio.h>
#include <string.h>

/* SGR color parameter prefixes for 256-color and RGB modes */
#define SGR_FG_256_PREFIX                              \
    "38;5;" /* 256-color foreground - CSI 38;5;color m \
             */
#define SGR_BG_256_PREFIX                                                    \
    "48;5;"                       /* 256-color background - CSI 48;5;color m \
                                   */
#define SGR_FG_RGB_PREFIX "38;2;" /* RGB foreground - CSI 38;2;r;g;b m */
#define SGR_BG_RGB_PREFIX "48;2;" /* RGB background - CSI 48;2;r;g;b m */

void ansi_format_cursor_pos(char *buf, size_t size, int row, int col)
{
    if (!buf || size < 16)
        return;
    snprintf(buf, size, CSI "%d;%d" CUP_FINAL, row, col);
}

void ansi_format_scroll_region(char *buf, size_t size, int top, int bottom)
{
    if (!buf || size < 24)
        return;
    snprintf(buf, size, CSI "%d;%d" DECSTBM_FINAL, top, bottom);
}

void ansi_format_fg_color_256(char *buf, size_t size, int color)
{
    if (!buf || size < 16)
        return;
    snprintf(buf, size, CSI SGR_FG_256_PREFIX "%d" SGR_FINAL, color);
}

void ansi_format_bg_color_256(char *buf, size_t size, int color)
{
    if (!buf || size < 16)
        return;
    snprintf(buf, size, CSI SGR_BG_256_PREFIX "%d" SGR_FINAL, color);
}

void ansi_format_fg_color_rgb(char *buf, size_t size, int r, int g, int b)
{
    if (!buf || size < 24)
        return;
    snprintf(buf, size, CSI SGR_FG_RGB_PREFIX "%d;%d;%d" SGR_FINAL, r, g, b);
}

void ansi_format_bg_color_rgb(char *buf, size_t size, int r, int g, int b)
{
    if (!buf || size < 24)
        return;
    snprintf(buf, size, CSI SGR_BG_RGB_PREFIX "%d;%d;%d" SGR_FINAL, r, g, b);
}

void ansi_set_window_title(char *buf, size_t size, const char *title)
{
    if (!buf || size < 8 || !title)
        return;
    snprintf(buf, size, OSC "2;%s" ST, title);
}

size_t ansi_format_osc52(char *buf, size_t size, const char *text,
                         size_t text_len)
{
    if (!buf)
        return 0;
    if (!text && text_len > 0)
        return 0;

    /* Required: 7 (ESC ] 5 2 ; c ;) + b64_len + 2 (ESC \) ; we also write
     * a trailing null when there is room. */
    static const char prefix[] = OSC "52;c;";
    static const size_t prefix_len = sizeof(prefix) - 1;
    static const char suffix[] = ST;
    static const size_t suffix_len = sizeof(suffix) - 1;

    size_t b64_len = BOBA_BASE64_ENCODED_LEN(text_len);
    size_t needed = prefix_len + b64_len + suffix_len;

    if (size < needed)
        return 0;

    memcpy(buf, prefix, prefix_len);
    size_t written = boba_base64_encode((const unsigned char *)text,
                                              text_len, buf + prefix_len,
                                              size - prefix_len);
    if (written != b64_len)
        return 0;
    memcpy(buf + prefix_len + b64_len, suffix, suffix_len);

    size_t total = prefix_len + b64_len + suffix_len;
    if (size > total)
        buf[total] = '\0';

    return total;
}
