/* SPDX-License-Identifier: GPL-3.0-or-later
 * Shared X11 view state, coordinates and page identifiers.
 * Keep this header free of hardware control and application-global state.
 */
#ifndef AMD_GPU_DIAG_UI_TYPES_H
#define AMD_GPU_DIAG_UI_TYPES_H
#include <stdbool.h>
#include <X11/Xlib.h>

typedef enum {
    VIEW_DASHBOARD = 0,
    VIEW_CLIENTS,
    VIEW_POWER,
    VIEW_TUNING,
    VIEW_DESKTOP,
    VIEW_EVENTS,
    VIEW_TRACE,
    VIEW_SETTINGS,
    VIEW_HELP,
    VIEW_ABOUT,
    VIEW_GPU_DATABASE
} View;

typedef enum {
    WORKFLOW_AI = 0,
    WORKFLOW_GAMING
} WorkflowMode;

typedef struct {
    Display *dpy;
    int screen;
    Window root;
    Window win;
    GC gc;
    XFontStruct *font;
    XFontStruct *font_bold;
    XFontStruct *font_title; /* One step larger than the standard UI font. */
    Atom wm_delete;
    int width;
    int height;
    unsigned long bg;
    unsigned long panel;
    unsigned long panel_alt;
    unsigned long fg;
    unsigned long dim;
    unsigned long accent;
    unsigned long good;
    unsigned long warn;
    unsigned long bad;
    unsigned long text_dark;
    bool dirty;
    /* Scroll only the content viewport; header and footer are fixed. */
    int scroll_y;
    int draw_offset_y;
    int text_right;
} Gui;

typedef struct {
    int x, y, w, h;
} Rect;

/* Logical page geometry. Content is translated into the clip viewport. */
enum { UI_MARGIN = 12, UI_GAP = 7, UI_PAGE_Y = 407,
       UI_CONTENT_TOP = 275, UI_FOOTER_SPACE = 62 };

#endif
