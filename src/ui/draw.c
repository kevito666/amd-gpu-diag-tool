/* SPDX-License-Identifier: GPL-3.0-or-later
 * Shared compact Xlib rendering. No sysfs or model-backend dependencies.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <X11/Xlib.h>
#include "draw.h"

unsigned long gui_color(Gui *ui, const char *name, unsigned long fallback) {
    XColor exact, screen;
    Colormap cmap = DefaultColormap(ui->dpy, ui->screen);
    if (XAllocNamedColor(ui->dpy, cmap, name, &screen, &exact)) return screen.pixel;
    return fallback;
}

void gui_set_font(Gui *ui, bool bold) {
    XFontStruct *f = bold && ui->font_bold ? ui->font_bold : ui->font;
    if (f) XSetFont(ui->dpy, ui->gc, f->fid);
}

/* Shared text writer: the GPU title uses the same clipping as ordinary labels. */
static void gui_text_vfont(Gui *ui, int x, int y, unsigned long color,
                           XFontStruct *font, const char *fmt, va_list ap) {
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    int right = ui->text_right ? ui->text_right : ui->width - UI_MARGIN;
    int room = right - x;
    if (room <= 4 || !font) return;
    int n = (int)strlen(buf);
    if (XTextWidth(font, buf, n) > room) {
        const char *dots = "..";
        int dots_w = XTextWidth(font, dots, 2);
        while (n > 0 && XTextWidth(font, buf, n) + dots_w > room) n--;
        if (n <= 0) return;
        buf[n] = '\0';
        if (n + 2 < (int)sizeof(buf)) strcat(buf, dots);
    }
    XSetForeground(ui->dpy, ui->gc, color);
    XSetFont(ui->dpy, ui->gc, font->fid);
    XDrawString(ui->dpy, ui->win, ui->gc, x, y + ui->draw_offset_y,
                buf, (int)strlen(buf));
}

void gui_text(Gui *ui, int x, int y, unsigned long color, bool bold,
              const char *fmt, ...) {
    XFontStruct *font = bold && ui->font_bold ? ui->font_bold : ui->font;
    va_list ap;
    va_start(ap, fmt);
    gui_text_vfont(ui, x, y, color, font, fmt, ap);
    va_end(ap);
}

void gui_text_title(Gui *ui, int x, int y, unsigned long color,
                    const char *fmt, ...) {
    XFontStruct *font = ui->font_title ? ui->font_title : ui->font_bold;
    va_list ap;
    va_start(ap, fmt);
    gui_text_vfont(ui, x, y, color, font, fmt, ap);
    va_end(ap);
}

void gui_fill(Gui *ui, Rect r, unsigned long color) {
    if (r.w <= 0 || r.h <= 0) return;
    XSetForeground(ui->dpy, ui->gc, color);
    XFillRectangle(ui->dpy, ui->win, ui->gc, r.x, r.y + ui->draw_offset_y, (unsigned)r.w, (unsigned)r.h);
}

void gui_outline(Gui *ui, Rect r, unsigned long color) {
    if (r.w <= 0 || r.h <= 0) return;
    XSetForeground(ui->dpy, ui->gc, color);
    XDrawRectangle(ui->dpy, ui->win, ui->gc, r.x, r.y + ui->draw_offset_y, (unsigned)(r.w - 1), (unsigned)(r.h - 1));
}

/* Outlined light labels remain legible on both filled and unfilled bar sections. */
static void gui_bar_label(Gui *ui, Rect r, const char *label) {
    if (!label || !*label) return;
    int old_right = ui->text_right;
    ui->text_right = r.x + r.w - 3;
    XFontStruct *font = ui->font_bold ? ui->font_bold : ui->font;
    int asc = font ? font->ascent : 10, desc = font ? font->descent : 3;
    int y = r.y + (r.h + asc - desc) / 2;
    int x = r.x + 5;
    gui_text(ui, x+1, y+1, ui->bg, true, "%s", label);
    gui_text(ui, x, y, ui->fg, true, "%s", label);
    ui->text_right = old_right;
}

void gui_bar(Gui *ui, Rect r, double pct, const char *label) {
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    gui_fill(ui, r, ui->panel_alt);
    Rect fill = r;
    fill.w = (int)((double)r.w * pct / 100.0);
    gui_fill(ui, fill, ui->accent);
    gui_outline(ui, r, ui->dim);
    gui_bar_label(ui, r, label);
}

void gui_bar_color(Gui *ui, Rect r, double pct, const char *label,
                          unsigned long fill_color) {
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    gui_fill(ui, r, ui->panel_alt);
    Rect fill = r;
    fill.w = (int)((double)r.w * pct / 100.0);
    gui_fill(ui, fill, fill_color);
    gui_outline(ui, r, ui->dim);
    gui_bar_label(ui, r, label);
}

void gui_bar_color_text(Gui *ui, Rect r, double pct, const char *label,
                               unsigned long fill_color, unsigned long text_color) {
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    gui_fill(ui, r, ui->panel_alt);
    Rect fill = r;
    fill.w = (int)((double)r.w * pct / 100.0);
    gui_fill(ui, fill, fill_color);
    gui_outline(ui, r, ui->dim);
    (void)text_color;
    gui_bar_label(ui, r, label);
}

/* Center labels using measured X11 font metrics, including disabled steppers. */
static void draw_button_label(Gui *ui, Rect r, const char *text,
                              bool bold, unsigned long color) {
    XFontStruct *font = bold && ui->font_bold ? ui->font_bold : ui->font;
    int len = (int)strlen(text);
    int text_w = font ? XTextWidth(font, text, len) : len * 8;
    int ascent = font ? font->ascent : 10;
    int descent = font ? font->descent : 3;
    int tx = r.x + (r.w - text_w) / 2;
    int ty = r.y + (r.h + ascent - descent) / 2;
    if (tx < r.x + 3) tx = r.x + 3;
    int old_right = ui->text_right;
    ui->text_right = r.x + r.w - 3;
    gui_text(ui, tx, ty, color, bold, "%s", text);
    ui->text_right = old_right;
}

void draw_button(Gui *ui, Rect r, const char *text, bool active) {
    gui_fill(ui, r, active ? ui->accent : ui->panel_alt);
    gui_outline(ui, r, active ? ui->fg : ui->dim);
    draw_button_label(ui, r, text, active, active ? ui->bg : ui->fg);
}

void draw_button_disabled(Gui *ui, Rect r, const char *text) {
    gui_fill(ui, r, ui->panel_alt);
    gui_outline(ui, r, ui->dim);
    draw_button_label(ui, r, text, false, ui->dim);
}


void panel_title(Gui *ui, Rect r, const char *title) {
    gui_fill(ui, r, ui->panel);
    gui_outline(ui, r, ui->panel_alt);
    ui->text_right = r.x + r.w - 8;
    gui_text(ui, r.x + 8, r.y + 17, ui->accent, true, "%s", title);
}
