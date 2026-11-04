/* SPDX-License-Identifier: GPL-3.0-or-later
 * X11 window lifetime and read-only session enrichment.
 * No GPU policy writes or application-global state.
 */
#ifndef AMD_GPU_DIAG_UI_WINDOW_H
#define AMD_GPU_DIAG_UI_WINDOW_H
#include "types.h"
#include "../monitor/session.h"
int ui_window_open(Gui *ui, SessionInfo *session, const char *version,
                   CompositorSource compositor_source);
void ui_window_probe(Gui *ui, SessionInfo *session, CompositorSource compositor_source);
void ui_window_close(Gui *ui);
#endif
