/* SPDX-License-Identifier: GPL-3.0-or-later
 * Canonical geometry: drawing and pointer handling use the SAME rectangles.
 */
#ifndef AMD_GPU_DIAG_UI_LAYOUT_H
#define AMD_GPU_DIAG_UI_LAYOUT_H
#include "types.h"

const char *view_name(View v);
View tab_view(int index);
int gui_content_bottom(const Gui *ui);
int gui_content_height(const Gui *ui);
bool point_in(Rect r, int x, int y);
Rect tab_rect(const Gui *ui, int idx);
Rect help_rect(const Gui *ui);
Rect control_rect(const Gui *ui, int idx);
Rect mode_rect(const Gui *ui, int idx);
Rect workflow_rect(const Gui *ui, int idx);
Rect gpu_db_button_rect(const Gui *ui);
Rect gpu_db_back_rect(const Gui *ui);
Rect gpu_db_item_rect(const Gui *ui, size_t index);
Rect tuning_tile_rect(const Gui *ui, int row);
Rect tuning_minus_rect(const Gui *ui, int row);
Rect tuning_plus_rect(const Gui *ui, int row);
Rect tuning_action_rect(const Gui *ui, int idx);
Rect ai_log_button_rect(const Gui *ui);
Rect settings_option_rect(const Gui *ui, int row);
Rect settings_path_rect(const Gui *ui);
Rect settings_gpu_switch_rect(const Gui *ui);
Rect settings_unlock_rect(const Gui *ui);
int gui_page_end(View view, WorkflowMode workflow);
int gui_max_scroll(const Gui *ui, View view, WorkflowMode workflow);
void gui_scroll(Gui *ui, View view, WorkflowMode workflow, int delta);
#endif
