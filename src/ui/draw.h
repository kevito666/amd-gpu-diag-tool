/* SPDX-License-Identifier: GPL-3.0-or-later
 * Stateless X11 drawing primitives, shared by all screens.
 */
#ifndef AMD_GPU_DIAG_UI_DRAW_H
#define AMD_GPU_DIAG_UI_DRAW_H
#include "types.h"

unsigned long gui_color(Gui *ui, const char *name, unsigned long fallback);
void gui_set_font(Gui *ui, bool bold);
void gui_text(Gui *ui, int x, int y, unsigned long color, bool bold,
              const char *fmt, ...) __attribute__((format(printf,6,7)));
/* Draw GPU title using the larger X11 font, honoring normal text clipping. */
void gui_text_title(Gui *ui, int x, int y, unsigned long color,
                    const char *fmt, ...) __attribute__((format(printf,5,6)));
void gui_fill(Gui *ui, Rect r, unsigned long color);
void gui_outline(Gui *ui, Rect r, unsigned long color);
void gui_bar(Gui *ui, Rect r, double pct, const char *label);
void gui_bar_color(Gui *ui, Rect r, double pct, const char *label,
                   unsigned long fill_color);
void gui_bar_color_text(Gui *ui, Rect r, double pct, const char *label,
                        unsigned long fill_color, unsigned long text_color);
void draw_button(Gui *ui, Rect r, const char *text, bool active);
void draw_button_disabled(Gui *ui, Rect r, const char *text);
void panel_title(Gui *ui, Rect r, const char *title);
#endif
