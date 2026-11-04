/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "input.h"
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <X11/Xutil.h>

char ui_keysym_to_action(XKeyEvent *event) {
    if (!event) return 0;
    KeySym key = NoSymbol;
    char bytes[8] = {0};
    int count = XLookupString(event, bytes, (int)sizeof(bytes), &key, NULL);
    if (key == XK_Escape) return 'q';
    if (key == XK_space) return ' ';
    if (key == XK_plus || key == XK_equal) return '+';
    if (key == XK_minus || key == XK_underscore) return '-';
    return count > 0 ? bytes[0] : 0;
}

bool ui_navigation_delta(KeySym key, int content_height, int *delta) {
    if (!delta) return false;
    int page = content_height > 40 ? content_height - 40 : 1;
    switch (key) {
        case XK_Page_Down: *delta = page; return true;
        case XK_Page_Up: *delta = -page; return true;
        case XK_Home: *delta = -100000; return true;
        case XK_End: *delta = 100000; return true;
        default: return false;
    }
}

bool ui_path_insert(char *buffer, size_t capacity, const char *chars, size_t n) {
    if (!buffer || !chars || capacity == 0 || n == 0) return false;
    size_t len = strnlen(buffer, capacity);
    if (len == capacity || n >= capacity - len) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)chars[i];
        if (ch < 32 || ch == 127) return false;
    }
    memcpy(buffer + len, chars, n);
    buffer[len + n] = '\0';
    return true;
}

bool ui_path_backspace(char *buffer, size_t capacity) {
    if (!buffer || capacity == 0) return false;
    size_t len = strnlen(buffer, capacity);
    if (len == 0 || len == capacity) return false;
    buffer[len - 1] = '\0';
    return true;
}

bool ui_path_is_absolute(const char *buffer, size_t capacity) {
    if (!buffer || capacity < 2) return false;
    size_t len = strnlen(buffer, capacity);
    return len > 0 && len < capacity - 1 && buffer[0] == '/';
}
