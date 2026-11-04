/* SPDX-License-Identifier: GPL-3.0-or-later
 * Headless UI contract: rectangles used for both paint and input stay aligned.
 */
#include <assert.h>
#include <stdio.h>
#include "../src/ui/layout.h"

static void assert_hitbox(Rect rect) {
    assert(rect.w > 0 && rect.h > 0);
    assert(point_in(rect, rect.x, rect.y));
    assert(point_in(rect, rect.x + rect.w - 1, rect.y + rect.h - 1));
    assert(!point_in(rect, rect.x - 1, rect.y));
    assert(!point_in(rect, rect.x + rect.w, rect.y));
    assert(!point_in(rect, rect.x, rect.y + rect.h));
}

int main(void) {
    Gui ui = {.width=840, .height=660};
    assert(gui_content_bottom(&ui) == 598);
    assert(gui_content_height(&ui) == 323);
    for (int i=0; i<9; i++) {
        Rect t = tab_rect(&ui, i);
        assert_hitbox(t);
        if (i>0) assert(tab_rect(&ui, i-1).x + tab_rect(&ui, i-1).w <= t.x);
    }
    assert(tab_view(8) == VIEW_ABOUT);
    assert(tab_view(7) == VIEW_SETTINGS);
    assert(tab_view(9) == VIEW_DASHBOARD);
    assert(view_name(VIEW_HELP)[0] == '?');
    assert(view_name(VIEW_ABOUT)[0] == 'A');
    assert_hitbox(help_rect(&ui));
    assert(tab_rect(&ui, 8).x+tab_rect(&ui, 8).w <= help_rect(&ui).x);
    for (int i=0; i<8; i++) assert_hitbox(mode_rect(&ui, i));
    for (int i=0; i<5; i++) assert_hitbox(control_rect(&ui, i));
    for (int i=0; i<2; i++) assert_hitbox(workflow_rect(&ui, i));
    for (int i=0; i<6; i++) assert_hitbox(tuning_tile_rect(&ui, i));
    assert_hitbox(gpu_db_button_rect(&ui));
    assert_hitbox(gpu_db_back_rect(&ui));
    assert_hitbox(gpu_db_item_rect(&ui, 0));
    assert(gpu_db_button_rect(&ui).y+gpu_db_button_rect(&ui).h < tuning_tile_rect(&ui,0).y);
    assert(gui_page_end(VIEW_GPU_DATABASE, WORKFLOW_AI) > gui_page_end(VIEW_TUNING, WORKFLOW_AI));
    for (int i=0; i<5; i++) {
        Rect tile = tuning_tile_rect(&ui, i);
        Rect less = tuning_minus_rect(&ui, i);
        Rect more = tuning_plus_rect(&ui, i);
        assert_hitbox(less);
        assert_hitbox(more);
        assert(point_in(tile, less.x, less.y));
        assert(point_in(tile, more.x+more.w-1, more.y+more.h-1));
        assert(less.x+less.w <= more.x);
    }
    for (int i=0; i<4; i++) assert_hitbox(tuning_action_rect(&ui, i));
    assert_hitbox(ai_log_button_rect(&ui));
    for (int i=0; i<8; i++) assert_hitbox(settings_option_rect(&ui, i));
    assert(settings_option_rect(&ui,0).y == settings_option_rect(&ui,1).y);
    assert(settings_option_rect(&ui,0).x < settings_option_rect(&ui,1).x);
    assert(settings_option_rect(&ui,2).y > settings_option_rect(&ui,0).y);
    assert(settings_option_rect(&ui,7).y < settings_gpu_switch_rect(&ui).y);
    assert_hitbox(settings_gpu_switch_rect(&ui));
    assert(settings_path_rect(&ui).y == settings_option_rect(&ui, 3).y);
    assert(settings_unlock_rect(&ui).y == settings_option_rect(&ui, 7).y);
    assert(gui_page_end(VIEW_DASHBOARD, WORKFLOW_GAMING) >
           gui_page_end(VIEW_DASHBOARD, WORKFLOW_AI));
    int max = gui_max_scroll(&ui, VIEW_TUNING, WORKFLOW_AI);
    assert(max > 0);
    gui_scroll(&ui, VIEW_TUNING, WORKFLOW_AI, 100000);
    assert(ui.scroll_y == max && ui.dirty);
    gui_scroll(&ui, VIEW_TUNING, WORKFLOW_AI, -100000);
    assert(ui.scroll_y == 0);
    /* Resizing adjusts layout, not a cached set of stale hitboxes. */
    ui.width = 1024;
    assert(tab_rect(&ui, 7).w > tab_rect(&(Gui){.width=840}, 7).w);
    assert_hitbox(help_rect(&ui));
    printf("UI layout contract: PASS\n");
    return 0;
}
