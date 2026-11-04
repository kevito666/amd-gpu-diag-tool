/* SPDX-License-Identifier: GPL-3.0-or-later
 * Pure geometry; independent of the X11 server and GPU device state.
 */
#include "layout.h"
#include "../gpu_specs.h"

int gui_content_bottom(const Gui *ui) { return ui->height - UI_FOOTER_SPACE; }
int gui_content_height(const Gui *ui) { return gui_content_bottom(ui) - UI_CONTENT_TOP; }

bool point_in(Rect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static Rect compact_row_rect(const Gui *ui, int idx, int count,
                             int y, int h, int gap, int preferred_w) {
    int full = ui->width - UI_MARGIN * 2;
    int w = (full - (count - 1) * gap) / count;
    if (w > preferred_w) w = preferred_w;
    int group = count * w + (count - 1) * gap;
    int x = (ui->width - group) / 2 + idx * (w + gap);
    return (Rect){x, y, w, h};
}

Rect tab_rect(const Gui *ui, int idx) {
    int total = ui->width - 2*UI_MARGIN;
    int help_w=26, gap=4;
    int w=(total-help_w-9*gap)/9;
    return (Rect){UI_MARGIN+idx*(w+gap),248,w,23};
}
Rect help_rect(const Gui *ui) {
    int total=ui->width-2*UI_MARGIN, gap=4, help_w=26;
    int w=(total-help_w-9*gap)/9;
    return (Rect){UI_MARGIN+9*(w+gap),248,help_w,23};
}

Rect control_rect(const Gui *ui, int idx) {
    return compact_row_rect(ui, idx, 5, ui->height - 33, 23, 6, 135);
}

Rect mode_rect(const Gui *ui, int idx) {
    return compact_row_rect(ui, idx, 8, 56, 23, 3, 103);
}

Rect workflow_rect(const Gui *ui, int idx) {
    int w = 102, h = 22, gap = 5;
    return (Rect){ui->width - UI_MARGIN - (2 - idx) * (w + gap) + gap, 8, w, h};
}

Rect gpu_db_button_rect(const Gui *ui) {
    (void)ui;
    return (Rect){UI_MARGIN, UI_PAGE_Y + 108, 268, 24};
}

Rect gpu_db_back_rect(const Gui *ui) {
    (void)ui;
    return (Rect){UI_MARGIN, UI_PAGE_Y + 12, 125, 24};
}

Rect gpu_db_item_rect(const Gui *ui, size_t index) {
    int width = (ui->width - 2 * UI_MARGIN - UI_GAP) / 2;
    int col = (int)(index % 2), row = (int)(index / 2);
    return (Rect){UI_MARGIN + col * (width + UI_GAP),
                  UI_PAGE_Y + 84 + row * 52, width, 48};
}

Rect tuning_tile_rect(const Gui *ui, int row) {
    int gap = UI_GAP, w = (ui->width - UI_MARGIN * 2 - 2 * gap) / 3;
    return (Rect){UI_MARGIN + (row % 3) * (w + gap), 562 + (row / 3) * 100, w, 91};
}

Rect tuning_minus_rect(const Gui *ui, int row) {
    Rect r = tuning_tile_rect(ui, row);
    return (Rect){r.x + r.w - 53, r.y + 30, 20, 20};
}

Rect tuning_plus_rect(const Gui *ui, int row) {
    Rect r = tuning_tile_rect(ui, row);
    return (Rect){r.x + r.w - 28, r.y + 30, 20, 20};
}

Rect tuning_action_rect(const Gui *ui, int idx) {
    Rect safe=tuning_tile_rect(ui,5);
    int w=(safe.w-24)/2;
    return (Rect){safe.x+8+(idx%2)*(w+8),safe.y+29+(idx/2)*28,w,22};
}

Rect ai_log_button_rect(const Gui *ui) {
    int margin = UI_MARGIN, gap = UI_GAP;
    int w = (ui->width - margin * 2 - gap) / 2;
    return (Rect){margin + w + gap + w - 78, UI_PAGE_Y + 149, 70, 21};
}

Rect settings_option_rect(const Gui *ui,int row) {
    int w = (ui->width - 2 * UI_MARGIN - UI_GAP) / 2;
    return (Rect){UI_MARGIN + (row % 2) * (w + UI_GAP),
                  UI_PAGE_Y + 60 + (row / 2) * 88, w, 25};
}
Rect settings_path_rect(const Gui *ui) {return settings_option_rect(ui,3);}
Rect settings_gpu_switch_rect(const Gui *ui) {
    (void)ui;
    return (Rect){UI_MARGIN,UI_PAGE_Y + 420, 132, 25};
}
Rect settings_unlock_rect(const Gui *ui) {return settings_option_rect(ui,7);}
/* Full logical content extent per page. All content retains its original coordinates. */
int gui_page_end(View view, WorkflowMode workflow) {
    switch (view) {
        case VIEW_DASHBOARD: return workflow == WORKFLOW_GAMING ? UI_PAGE_Y+4*103+8 : UI_PAGE_Y+180+7+2*103+8;
        case VIEW_CLIENTS: return 760;
        case VIEW_POWER: return 1020;
        case VIEW_TUNING: return 842;
        case VIEW_GPU_DATABASE: return gpu_db_item_rect(&(Gui){.width=840}, gpu_spec_count() ? gpu_spec_count()-1 : 0).y + 72;
        case VIEW_DESKTOP: return UI_PAGE_Y+560;
        case VIEW_EVENTS: return UI_PAGE_Y+15*18+75;
        case VIEW_SETTINGS: return UI_PAGE_Y + 530;
        case VIEW_TRACE: return 670;
        case VIEW_HELP: return 715;
        case VIEW_ABOUT: return UI_PAGE_Y+430;
        default: return 745;
    }
}

int gui_max_scroll(const Gui *ui, View view, WorkflowMode workflow) {
    int excess = gui_page_end(view, workflow) - UI_PAGE_Y - gui_content_height(ui);
    return excess > 0 ? excess : 0;
}

void gui_scroll(Gui *ui, View view, WorkflowMode workflow, int delta) {
    ui->scroll_y += delta;
    if (ui->scroll_y < 0) ui->scroll_y = 0;
    int max = gui_max_scroll(ui, view, workflow);
    if (ui->scroll_y > max) ui->scroll_y = max;
    ui->dirty = true;
}


View tab_view(int index) {
    /* Help is the separate '?' button. The ninth full tab is About. */
    if (index >= 0 && index < 8) return (View)index;
    if (index == 8) return VIEW_ABOUT;
    return VIEW_DASHBOARD;
}

const char *view_name(View v) {
    static const char *names[] = {
        "Home", "Clients", "Power", "Tuning", "Desktop", "Events",
        "Trace", "Settings", "?", "About", "Reference"
    };
    return (v >= VIEW_DASHBOARD && v <= VIEW_GPU_DATABASE) ? names[v] : "Home";
}
