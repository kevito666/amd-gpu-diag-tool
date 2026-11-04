/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only tuning controls. ARM/APPLY and sysfs writes owned by gpu/tuning.c.
 */
#include <stdio.h>
#include "pages.h"
#include "draw.h"
#include "layout.h"
#include "status.h"
#include "../gpu_specs.h"
static void draw_tuning_row(Gui *ui, int row, const char *name, const char *value,
                            const char *range, TuneZone zone, bool available) {
    Rect panel = tuning_tile_rect(ui, row);
    int y = panel.y;
    gui_fill(ui, panel, ui->panel);
    gui_outline(ui, panel, available ? tune_zone_color(ui, zone) : ui->dim);
    gui_text(ui, panel.x+9, y + 18, available ? ui->fg : ui->dim, true, "%s", name);
    gui_text(ui, panel.x+9, y + 40, available ? tune_zone_color(ui, zone) : ui->dim, true, "%s", value);
    ui->text_right = panel.x + panel.w - 9;
    gui_text(ui, panel.x+9, y + 62, ui->dim, false, "%s", range);
    ui->text_right = 0;
    if (available) {
        draw_button(ui, tuning_minus_rect(ui,row), "-", false);
        draw_button(ui, tuning_plus_rect(ui,row), "+", false);
    } else {
        draw_button_disabled(ui, tuning_minus_rect(ui,row), "-");
        draw_button_disabled(ui, tuning_plus_rect(ui,row), "+");
    }
}

void ui_draw_tuning_page(Gui *ui, const TuningState *t) {
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"Software tuning - firmware bounded"); y+=28;
    gui_text(ui,x,y,ui->warn,true,
             "Green/amber/red = aggressiveness relative to captured stock, NOT a hardware-safety guarantee."); y+=26;

    if (t->spec) {
        gui_text(ui,x,y,ui->fg,true,"%s   %s / %s",t->spec->name,t->spec->series,t->spec->architecture); y+=23;
        gui_text(ui,x,y,ui->dim,false,
                 "AMD reference: game %d MHz   boost %d MHz   TBP %d W   %d MB %s @ %.1f Gbps / %d-bit",
                 t->spec->game_mhz,t->spec->boost_mhz,t->spec->tbp_w,t->spec->vram_mb,t->spec->memory_type,
                 t->spec->memory_mbps/1000.0,t->spec->memory_bus_bits); y+=23;
    } else {
        gui_text(ui,x,y,ui->dim,false,"No exact reference-card match; driver/firmware ranges remain fully authoritative."); y+=23;
    }
    char db_label[96];
    snprintf(db_label, sizeof(db_label), "BROWSE AMD REFERENCE DATABASE (%zu)", gpu_spec_count());
    draw_button(ui, gpu_db_button_rect(ui), db_label, false);

    char value[96], range[160];
    TuneZone z;

    if (t->has_sclk) {
        snprintf(value,sizeof(value),"%ld MHz",t->edit_sclk_max_mhz);
        snprintf(range,sizeof(range),"stock %ld | firmware %ld..%ld MHz",
                 t->default_sclk_max_mhz,t->sclk_limit_min_mhz,t->sclk_limit_max_mhz);
        z=tuning_zone_long(t->edit_sclk_max_mhz,t->default_sclk_max_mhz,t->sclk_limit_max_mhz);
        draw_tuning_row(ui,0,"GFX MAX CLOCK",value,range,z,true);
    } else if (t->has_sclk_offset) {
        snprintf(value,sizeof(value),"%+ld MHz",t->edit_sclk_offset_mhz);
        snprintf(range,sizeof(range),"stock %+ld | firmware %+ld..%+ld MHz",
                 t->default_sclk_offset_mhz,t->sclk_offset_min_mhz,t->sclk_offset_max_mhz);
        z=tuning_zone_offset(t->edit_sclk_offset_mhz,t->sclk_offset_max_mhz);
        draw_tuning_row(ui,0,"GFX CLOCK OFFSET",value,range,z,true);
    } else {
        draw_tuning_row(ui,0,"GFX CLOCK","unavailable","driver exposes no supported modern OD clock control",TUNE_ZONE_UNAVAILABLE,false);
    }

    if (t->has_mclk) {
        snprintf(value,sizeof(value),"%ld MHz controller",t->edit_mclk_max_mhz);
        snprintf(range,sizeof(range),"stock %ld | firmware %ld..%ld MHz",
                 t->default_mclk_max_mhz,t->mclk_limit_min_mhz,t->mclk_limit_max_mhz);
        z=tuning_zone_long(t->edit_mclk_max_mhz,t->default_mclk_max_mhz,t->mclk_limit_max_mhz);
        draw_tuning_row(ui,1,"MEMORY MAX CLOCK",value,range,z,true);
    } else {
        draw_tuning_row(ui,1,"MEMORY CLOCK","unavailable","driver exposes no supported modern MCLK control",TUNE_ZONE_UNAVAILABLE,false);
    }

    if (t->has_vdd_offset) {
        snprintf(value,sizeof(value),"%+ld mV",t->edit_vdd_offset_mv);
        snprintf(range,sizeof(range),"stock %+ld | firmware %+ld..%+ld mV",
                 t->default_vdd_offset_mv,t->vdd_offset_min_mv,t->vdd_offset_max_mv);
        z=tuning_zone_offset(t->edit_vdd_offset_mv,t->vdd_offset_max_mv);
        draw_tuning_row(ui,2,"VDDGFX OFFSET",value,range,z,true);
    } else {
        draw_tuning_row(ui,2,"VDDGFX OFFSET","unavailable","not exposed by this ASIC/driver",TUNE_ZONE_UNAVAILABLE,false);
    }

    if (t->has_power_cap) {
        snprintf(value,sizeof(value),"%.0f W",t->edit_power_cap_w);
        snprintf(range,sizeof(range),"stock %.0f | firmware %.0f..%.0f W%s%d%s",
                 t->default_power_cap_w,t->power_cap_min_w,t->power_cap_max_w,
                 t->spec?" | AMD ref TBP ":"",t->spec?t->spec->tbp_w:0,t->spec?" W":"");
        z=tuning_zone_double(t->edit_power_cap_w,t->default_power_cap_w,t->power_cap_max_w);
        draw_tuning_row(ui,3,"POWER CAP",value,range,z,true);
    } else {
        draw_tuning_row(ui,3,"POWER CAP","unavailable","power1_cap is not exposed by this driver/board",TUNE_ZONE_UNAVAILABLE,false);
    }

    if (t->has_fan) {
        snprintf(value,sizeof(value),"%ld%% (%d RPM)",
                 t->fan_pwm_max > 0 ? (t->edit_fan_pwm * 100 / t->fan_pwm_max) : 0,
                 t->fan_rpm);
        snprintf(range,sizeof(range),"PWM %ld..%ld | current %ld",
                 (long)0,t->fan_pwm_max,t->fan_pwm);
        draw_tuning_row(ui,4,"FAN SPEED",value,range,TUNE_ZONE_SAFE,true);
    } else {
        draw_tuning_row(ui,4,"FAN SPEED","unavailable","fan PWM not exposed by this driver/board",TUNE_ZONE_UNAVAILABLE,false);
    }

    char arm[64];
    snprintf(arm,sizeof(arm),"ARM: %s",t->armed?"ON":"OFF");
    Rect safe=tuning_tile_rect(ui,5);
    panel_title(ui,safe,"SAFETY / WORKFLOW");
    draw_button(ui,tuning_action_rect(ui,0),arm,t->armed);
    draw_button(ui,tuning_action_rect(ui,1),"APPLY",false);
    draw_button(ui,tuning_action_rect(ui,2),"RESET STOCK",false);
    draw_button(ui,tuning_action_rect(ui,3),"REFRESH",false);
    gui_text(ui,UI_MARGIN,776,ui->warn,false,
             "ARM then APPLY required. RESET STOCK changes device settings; firmware limits apply.");
    gui_text(ui,UI_MARGIN,798,ui->dim,false,
             "Clock/voltage: pp_od_clk_voltage; power cap: hwmon. Unsupported settings are read-only.");
}


/* Read-only database browser: these are reference-card marketing specifications,
 * never firmware limits or values that can be applied to the active GPU. */
void ui_draw_gpu_database_page(Gui *ui) {
    draw_button(ui, gpu_db_back_rect(ui), "< BACK TO TUNING", false);
    gui_text(ui, UI_MARGIN + 143, UI_PAGE_Y + 29, ui->accent, true,
             "AMD REFERENCE GPU DATABASE (%zu)", gpu_spec_count());
    gui_text(ui, UI_MARGIN, UI_PAGE_Y + 61, ui->dim, false,
             "Reference values only; installed card firmware limits always take precedence.");
    for (size_t i = 0; i < gpu_spec_count(); i++) {
        const GpuSpec *spec = gpu_spec_at(i);
        if (!spec) continue;
        Rect card = gpu_db_item_rect(ui, i);
        gui_fill(ui, card, ui->panel);
        gui_outline(ui, card, ui->panel_alt);
        int saved_right = ui->text_right;
        ui->text_right = card.x + card.w - 8;
        gui_text(ui, card.x + 8, card.y + 17, ui->fg, true, "%s", spec->name);
        if (spec->game_mhz > 0 && spec->boost_mhz > 0)
            gui_text(ui, card.x + 8, card.y + 36, ui->dim, false,
                     "%s | %d/%d MHz | %d W | %d GiB %s",
                     spec->architecture, spec->game_mhz, spec->boost_mhz,
                     spec->tbp_w, spec->vram_mb / 1024, spec->memory_type);
        else if (spec->boost_mhz > 0)
            gui_text(ui, card.x + 8, card.y + 36, ui->dim, false,
                     "%s | Boost %d MHz | %d W | %d GiB %s",
                     spec->architecture, spec->boost_mhz,
                     spec->tbp_w, spec->vram_mb / 1024, spec->memory_type);
        else
            gui_text(ui, card.x + 8, card.y + 36, ui->dim, false,
                     "%s | Clocks n/a | %d W | %d GiB %s",
                     spec->architecture, spec->tbp_w,
                     spec->vram_mb / 1024, spec->memory_type);
        ui->text_right = saved_right;
    }
}
