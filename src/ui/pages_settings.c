/* SPDX-License-Identifier: GPL-3.0-or-later
 * Settings, Help and About presentation. All configuration values are passed
 * by the controller; the renderer never writes persistent settings.
 */
#include <stdio.h>
#include <string.h>
#include "pages.h"
#include "draw.h"
#include "layout.h"
void ui_draw_settings_page(Gui *ui, const UiSettingsView *v) {
    static const char *const sources[]={"Power heuristic","llama.cpp API","Ollama API","vLLM API","LM Studio API","Project Maya (log)"};
    static const char *const labels[]={
        "AI DATA SOURCE", "AI LOG PARSER", "AI LOG DISCOVERY", "AI LOG FILENAME",
        "AI NVME CACHE SSD", "COMPOSITOR SOURCE", "AMD GPU SELECTOR", "OVERCLOCK UNLOCK"
    };
    gui_text(ui,UI_MARGIN,UI_PAGE_Y+16,ui->accent,true,"SETTINGS / CONFIGURATION");
    gui_text(ui,UI_MARGIN,UI_PAGE_Y+33,ui->dim,false,
             "Select an option below each heading. Changes save automatically.");

    for(int i=0;i<8;i++) {
        Rect r = settings_option_rect(ui,i);
        gui_text(ui,r.x,r.y-10,ui->fg,true,"%s",labels[i]);
    }
    draw_button(ui,settings_option_rect(ui,0),sources[v->settings->token_source],false);
    draw_button(ui,settings_option_rect(ui,1),v->parser_names[v->settings->log_parser],false);
    draw_button(ui,settings_option_rect(ui,2),v->settings->ai_log_manual?"Manual file":"Auto detect newest",false);
    draw_button(ui,settings_path_rect(ui),v->editing_path?"Editing (Enter to save)":"EDIT FILENAME",v->editing_path);
    Rect path=settings_path_rect(ui);
    ui->text_right=path.x+path.w-5;
    gui_text(ui,path.x,path.y+43,ui->dim,false,"Path: %.190s",
             v->editing_path?v->edit_path_buffer:v->settings->ai_log_file[0]?v->settings->ai_log_file:"(not configured)");
    ui->text_right=0;
    char nv_label[190];
    snprintf(nv_label,sizeof(nv_label),"%s",v->nvme->selected[0]?v->nvme->selected:"No NVMe found");
    for(int i=0;i<v->nvme->device_count;i++) if(!strcmp(v->nvme->selected,v->nvme->devices[i].name)) {
        snprintf(nv_label,sizeof(nv_label),"%s | %.65s",v->nvme->selected,v->nvme->devices[i].model);
        break;
    }
    draw_button(ui,settings_option_rect(ui,4),nv_label,false);
    draw_button(ui,settings_option_rect(ui,5),v->compositor_names[v->settings->compositor_source],false);
    int desired=v->requested_gpu>=0?v->requested_gpu:v->gpu->card_num;
    char card[160];
    snprintf(card,sizeof(card),"card%d (unavailable)",desired);
    for(int i=0;i<v->gpu_count;i++) if(v->gpus[i].card_num==desired)
        snprintf(card,sizeof(card),"card%d - %.80s",desired,v->gpus[i].full_name);
    draw_button(ui,settings_option_rect(ui,6),card,false);
    Rect selected=settings_option_rect(ui,6);
    gui_text(ui,selected.x,selected.y+44,ui->dim,false,"%d card(s) | active: card%d",
             v->gpu_count,v->gpu->card_num);
    draw_button_disabled(ui,settings_unlock_rect(ui),"LOCKED - planned feature");
    Rect apply=settings_gpu_switch_rect(ui);
    draw_button(ui,apply,"APPLY GPU",false);
    gui_text(ui,apply.x+apply.w+12,apply.y+17,ui->warn,false,
             "Rebind pauses watchdog, clears locks and disarms tuning.");
    int y=apply.y+49;
    gui_text(ui,UI_MARGIN,y,ui->dim,false,
             "Config: ~/.config/amd-gpu-diagnostic/settings.ini (invoking user)");y+=22;
    gui_text(ui,UI_MARGIN,y,ui->dim,false,
             "Project Maya and llama.cpp/Bionic timing logs supported.");y+=22;
    gui_text(ui,UI_MARGIN,y,ui->dim,false,
             "Manual log: absolute readable path required. Settings auto-save.");
}

void ui_draw_help_page(Gui *ui) {
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"Keyboard / mouse controls"); y+=30;
    const char *lines[] = {
        "1 Auto    2 Low    3 High    4 Standard    5 Min-GFX    6 Min-MEM    7 Peak    8 Manual",
        "0 Home  P Clients  G Power  U Tuning  W Desktop  E Events  R Trace  F Settings  H Help  I About",
        "Space pause/resume watchdog    A adaptive burst    C DPM clock telemetry",
        "+ faster watchdog    - slower watchdog    D cycle DPM telemetry rate",
        "S cycle manual GFX level    M cycle manual memory level    X clear manual level locks",
        "U tuning page    T kernel trace on/off    L CSV event logging on/off    Q quit",
        "Tuning writes require ARM then APPLY. RESET STOCK uses driver reset and captured stock power cap.",
        "All tabs, power modes, and toggles are clickable. Help is the small ? tab.",
        "The GUI uses plain Xlib: no GTK, Qt, ncurses, Electron, XCB, or libdrm dependency."
    };
    for(size_t i=0;i<sizeof(lines)/sizeof(lines[0]);i++) { gui_text(ui,x,y,ui->fg,false,"%s",lines[i]); y+=28; }
}

void ui_draw_about_page(Gui *ui, const char *version, const char *build_date) {
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"AMD GPU Performance & Diagnostic Tool"); y+=28;
    gui_text(ui,x,y,ui->fg,true,"v%s  |  beta release  |  compiled %s",version,build_date); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Developer: kevito666   |   GitHub: github.com/kevito666"); y+=29;
    gui_text(ui,x,y,ui->accent,true,"How it started"); y+=25;
    gui_text(ui,x,y,ui->fg,false,"What started as a GPU coil-whine reducer / power-state controller"); y+=21;
    gui_text(ui,x,y,ui->fg,false,"morphed into a full-blown AI and gaming desktop monitoring"); y+=21;
    gui_text(ui,x,y,ui->fg,false,"and configuration tool!"); y+=27;
    gui_text(ui,x,y,ui->fg,true,"Built in Tohoku, Japan."); y+=32;
    gui_text(ui,x,y,ui->accent,true,"Technology"); y+=25;
    gui_text(ui,x,y,ui->fg,false,"C / Xlib / AMDGPU sysfs / procfs / debugfs / tracefs / DRM fdinfo"); y+=21;
    gui_text(ui,x,y,ui->fg,false,"Lightweight X11/XLibre; no GTK, Qt or Electron."); y+=28;
    gui_text(ui,x,y,ui->accent,true,"License"); y+=25;
    gui_text(ui,x,y,ui->fg,true,"SPDX: GPL-3.0-or-later"); y+=21;
    gui_text(ui,x,y,ui->fg,false,"GNU General Public License, version 3 or (at your option) later."); y+=22;
    gui_text(ui,x,y,ui->dim,false,"Copyright (C) 2026 Kevin Kolasa. See LICENSE in the source archive."); y+=26;
    gui_text(ui,x,y,ui->warn,false,"Overclock unlock remains disabled in this beta.");
}
