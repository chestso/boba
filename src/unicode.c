/* unicode.c - UTF-8 and Unicode utility functions */

#include <boba/unicode.h>

int tui_utf8_char_len(const char *ptr)
{
    unsigned char c = (unsigned char)*ptr;
    if ((c & 0x80) == 0)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1; /* Invalid, treat as single byte */
}

uint32_t tui_utf8_decode(const char *ptr, int len)
{
    unsigned char c = (unsigned char)*ptr;
    uint32_t cp;
    switch (len) {
    case 1:
        cp = c;
        break;
    case 2:
        cp = c & 0x1F;
        break;
    case 3:
        cp = c & 0x0F;
        break;
    case 4:
        cp = c & 0x07;
        break;
    default:
        return c;
    }
    for (int i = 1; i < len; i++)
        cp = (cp << 6) | ((unsigned char)ptr[i] & 0x3F);
    return cp;
}

int tui_utf8_encode(uint32_t cp, char buf[5])
{
    int len;
    if (cp < 0x80) {
        buf[0] = (char)cp;
        len = 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        len = 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        len = 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        len = 4;
    }
    buf[len] = '\0';
    return len;
}

size_t tui_utf8_prev_char(const char *text, size_t pos)
{
    if (pos == 0)
        return 0;
    pos--;
    while (pos > 0 && ((unsigned char)text[pos] & 0xC0) == 0x80) {
        pos--;
    }
    return pos;
}

int tui_utf8_codepoint_count(const char *text, size_t len)
{
    int count = 0;
    size_t i = 0;
    while (i < len) {
        i += tui_utf8_char_len(text + i);
        count++;
    }
    return count;
}

size_t tui_utf8_byte_offset(const char *text, size_t text_len, int cp_index)
{
    size_t offset = 0;
    int cp = 0;
    while (offset < text_len && cp < cp_index) {
        offset += tui_utf8_char_len(text + offset);
        cp++;
    }
    return offset;
}

int tui_utf8_cp_index(const char *text, size_t byte_pos)
{
    int cp = 0;
    size_t i = 0;
    while (i < byte_pos) {
        i += tui_utf8_char_len(text + i);
        cp++;
    }
    return cp;
}

int tui_codepoint_width(uint32_t cp)
{
    /* Zero-width characters */
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D) /* ZWSP, ZWNJ, ZWJ */
        return 0;
    if (cp >= 0x0300 && cp <= 0x036F) /* Combining diacritical marks */
        return 0;
    if (cp >= 0xFE00 && cp <= 0xFE0F) /* Variation selectors */
        return 0;

    /* East Asian Wide characters */
    if (cp >= 0x1100 && cp <= 0x115F) /* Hangul Jamo */
        return 2;
    if (cp >= 0x2E80 && cp <= 0x303E) /* CJK radicals, Kangxi, ideographic */
        return 2;
    if (cp >= 0x3040 && cp <= 0x33FF) /* Hiragana, Katakana, CJK compat */
        return 2;
    if (cp >= 0x3400 && cp <= 0x4DBF) /* CJK Unified Ext A */
        return 2;
    if (cp >= 0x4E00 && cp <= 0x9FFF) /* CJK Unified Ideographs */
        return 2;
    if (cp >= 0xAC00 && cp <= 0xD7AF) /* Hangul Syllables */
        return 2;
    if (cp >= 0xF900 && cp <= 0xFAFF) /* CJK Compatibility Ideographs */
        return 2;
    if (cp >= 0xFE30 && cp <= 0xFE6F) /* CJK Compatibility Forms */
        return 2;
    if (cp >= 0xFF01 && cp <= 0xFF60) /* Fullwidth forms */
        return 2;
    if (cp >= 0xFFE0 && cp <= 0xFFE6) /* Fullwidth signs */
        return 2;

    /* Misc symbols and dingbats (includes ⚓ U+2693) */
    if (cp >= 0x2600 && cp <= 0x27BF)
        return 2;

    /* Emoji ranges */
    if (cp >= 0x1F000 && cp <= 0x1FBFF)
        return 2;

    /* CJK Unified Ext B and beyond */
    if (cp >= 0x20000 && cp <= 0x2FA1F)
        return 2;

    return 1;
}

int tui_utf8_display_width(const char *str)
{
    if (!str)
        return 0;
    int width = 0;
    while (*str) {
        int char_len = tui_utf8_char_len(str);
        uint32_t cp = tui_utf8_decode(str, char_len);
        width += tui_codepoint_width(cp);
        str += char_len;
    }
    return width;
}

size_t tui_utf8_display_width_ansi(const char *text, size_t len)
{
    size_t width = 0;
    int in_escape = 0;

    for (size_t i = 0; i < len; i++) {
        if (in_escape) {
            /* End of CSI sequence */
            if ((text[i] >= 'A' && text[i] <= 'Z') ||
                (text[i] >= 'a' && text[i] <= 'z')) {
                in_escape = 0;
            }
        } else if (text[i] == '\033' && i + 1 < len && text[i + 1] == '[') {
            in_escape = 1;
            i++; /* Skip '[' */
        } else if ((unsigned char)text[i] >= 0x20) {
            int clen = tui_utf8_char_len(&text[i]);
            /* Clamp to remaining bytes */
            if (i + clen > len)
                clen = (int)(len - i);
            uint32_t cp = tui_utf8_decode(&text[i], clen);
            width += tui_codepoint_width(cp);
            i += clen - 1; /* -1 because loop increments */
        }
    }

    return width;
}
