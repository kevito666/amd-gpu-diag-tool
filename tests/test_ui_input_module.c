/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/ui/input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    assert(UI_EVENT_BATCH_LIMIT >= 16 && UI_EVENT_BATCH_LIMIT <= 128);
    int d = 0;
    assert(ui_navigation_delta(XK_Page_Down, 350, &d) && d == 310);
    assert(ui_navigation_delta(XK_Page_Up, 350, &d) && d == -310);
    assert(ui_navigation_delta(XK_Home, 350, &d) && d == -100000);
    assert(ui_navigation_delta(XK_End, 350, &d) && d == 100000);
    assert(!ui_navigation_delta(XK_space, 350, &d));
    assert(!ui_navigation_delta(XK_End, 350, NULL));
    char path[16] = "/tmp";
    assert(ui_path_insert(path, sizeof(path), "/a", 2));
    assert(!strcmp(path, "/tmp/a"));
    assert(!ui_path_insert(path, sizeof(path), "\n", 1));
    assert(!strcmp(path, "/tmp/a"));
    assert(!ui_path_insert(path, sizeof(path), "123456789abcdef", 15));
    assert(ui_path_backspace(path, sizeof(path)));
    assert(!strcmp(path, "/tmp/"));
    assert(ui_path_is_absolute(path, sizeof(path)));
    assert(!ui_path_is_absolute("file.log", 16));
    assert(!ui_path_is_absolute(path, 2));
    assert(!ui_path_backspace(NULL, sizeof(path)));
    puts("UI input contract: PASS");
    return 0;
}
