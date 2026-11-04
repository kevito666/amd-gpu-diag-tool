/* SPDX-License-Identifier: GPL-3.0-or-later
 * X11 keyboard translation and path-editor helpers. No GPU write authority.
 */
#ifndef AMD_GPU_DIAG_UI_INPUT_H
#define AMD_GPU_DIAG_UI_INPUT_H
#include <stdbool.h>
#include <stddef.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

/* Bound each dispatch batch so an X11 event flood cannot starve polling. */
#define UI_EVENT_BATCH_LIMIT 64

/* Translate an X11 key event into the existing single-character app actions. */
char ui_keysym_to_action(XKeyEvent *event);

/* Convert page navigation keys into a scroll delta; does not mutate GUI state. */
bool ui_navigation_delta(KeySym key, int content_height, int *delta);

/* Strict, bounded editing. Return true only if all bytes are printable. */
bool ui_path_insert(char *buffer, size_t capacity, const char *chars, size_t n);
bool ui_path_backspace(char *buffer, size_t capacity);
bool ui_path_is_absolute(const char *buffer, size_t capacity);
#endif
