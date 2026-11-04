/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "chrome.h"
#include "draw.h"
#include "layout.h"
#include "status.h"
#include "../core/util.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
/* Historical bar ranges kept for exact dev.8 visual parity. */
#define POWER_BAR_MAX_W 800.0
#define VOLTAGE_BAR_MAX_V 1.20
#define CURRENT_BAR_MAX_A 50.0
void ui_draw_header(Gui *ui, const Gpu *g, const Sysfs *s, const Target *t,
                        const PowerTelemetry *p, const SystemTelemetry *sys,
                        const Stats *st, WorkflowMode workflow) {
    ui->text_right = workflow_rect(ui,0).x - 8;
    gui_text_title(ui, UI_MARGIN, 20, ui->fg, "%s", g->full_name);
    ui->text_right = 0;

    /* Workflow mode buttons */
    Rect ai_btn = workflow_rect(ui, 0);
    Rect game_btn = workflow_rect(ui, 1);
    bool ai_active = (workflow == WORKFLOW_AI);
    bool game_active = (workflow == WORKFLOW_GAMING);
    draw_button(ui, ai_btn, "AI", ai_active);
    draw_button(ui, game_btn, "GAMING", game_active);

    /* Data sources are configured in Settings (not the header). */
    gui_text(ui, UI_MARGIN, 35, ui->dim, false,
             "%s  |  PCI %s  |  ID 1002:%s  |  VRAM %.1f GiB",
             g->card_name, g->pci_addr, g->device_id, g->vram_bytes / 1073741824.0);

    char actual[128] = "unknown";
    (void)read_fd_text(s->fd_policy_r, actual, sizeof(actual));
    unsigned long lock_color = strcmp(actual, t->policy) == 0 ? ui->good : ui->bad;
    gui_text(ui, UI_MARGIN, 50, lock_color, true,
             "Target = %s | Actual = %s | %s | Corrections = %llu",
             t->policy, actual, t->enabled ? "LOCKED" : "PAUSED",
             (unsigned long long)st->corrections);

    const char *modes[] = {
        "1 Auto", "2 Low", "3 High", "4 Standard",
        "5 Min-GFX", "6 Min-MEM", "7 Peak", "8 Manual"
    };
    for (int i = 0; i < 8; i++)
        draw_button(ui, mode_rect(ui, i), modes[i], strcmp(t->policy, POLICIES[i]) == 0);

    char label[192];
    double power_scale = POWER_BAR_MAX_W;
    if (p->power_cap_max_w > 1.0 && p->power_cap_max_w < power_scale)
        power_scale = p->power_cap_max_w;
    if (power_scale < 400.0) power_scale = 400.0;
    Rect power_bar = {UI_MARGIN, 86, ui->width - UI_MARGIN * 2, 26};
    double power_pct = p->power_w >= 0.0 ? p->power_w * 100.0 / power_scale : 0.0;
    if (p->power_w >= 0.0 && p->power_cap_w > 0.0)
        snprintf(label, sizeof(label), "BOARD POWER   %.1f W    POWER CAP %.0f W    firmware scale %.0f W",
                 p->power_w, p->power_cap_w, power_scale);
    else if (p->power_w >= 0.0)
        snprintf(label, sizeof(label), "BOARD POWER   %.1f W    scale %.0f W", p->power_w, power_scale);
    else
        snprintf(label, sizeof(label), "BOARD POWER   unavailable");
    if (power_pct >= 95.0)
        gui_bar_color(ui, power_bar, power_pct, label, ui->bad);
    else if (power_pct >= 80.0)
        gui_bar_color_text(ui, power_bar, power_pct, label, ui->warn, ui->text_dark);
    else
        gui_bar_color(ui, power_bar, power_pct, label, ui->accent);

    /* Label the physical source explicitly; temp2 is junction, not another edge.
     * Tctl is read independently of the preferred CPU die sensor (Tdie). */
    double temp_values[MAX_GPU_TEMPS + 1];
    long temp_crit[MAX_GPU_TEMPS + 1];
    long temp_emergency[MAX_GPU_TEMPS + 1];
    const char *temp_names[MAX_GPU_TEMPS + 1];
    const char *const gpu_names[] = {"GPU Edge (t1)", "GPU Junction (t2)", "GPU Mem (t3)"};
    int nt = 0;
    for (int i = 0; i < MAX_GPU_TEMPS; i++) {
        if (p->temps_c[i] < 0.0) continue;
        temp_values[nt] = p->temps_c[i];
        temp_crit[nt] = s->temp_crit_mc[i];
        temp_emergency[nt] = s->temp_emergency_mc[i];
        /* The kernel's actual label wins when it differs from the standard mapping. */
        const char *sensor_label = s->temp_label[i];
        if (sensor_label[0] && strcasecmp(sensor_label, "edge") && strcasecmp(sensor_label, "junction") &&
            strcasecmp(sensor_label, "hotspot") && strcasecmp(sensor_label, "mem") &&
            strcasecmp(sensor_label, "memory") && strcasecmp(sensor_label, "GPU Edge") &&
            strcasecmp(sensor_label, "GPU Junction") && strcasecmp(sensor_label, "GPU Memory"))
            temp_names[nt] = sensor_label;
        else temp_names[nt] = gpu_names[i];
        nt++;
    }
    double cpu_label_temp = sys->cpu_tctl_c >= 0.0 ? sys->cpu_tctl_c : sys->cpu_temp_c;
    if (cpu_label_temp >= 0.0 && nt < MAX_GPU_TEMPS + 1) {
        temp_values[nt] = cpu_label_temp;
        temp_crit[nt] = sys->cpu_temp_crit_mc;
        temp_emergency[nt] = -1;
        temp_names[nt] = sys->cpu_tctl_c >= 0.0 ?
                         (sys->cpu_tctl_label[0] ? sys->cpu_tctl_label : "CPU Tctl (hwmon)") :
                         strcasestr(sys->cpu_temp_label, "tdie") ? "CPU Tdie (hwmon)" : "CPU temp (hwmon)";
        nt++;
    }

    int margin = UI_MARGIN, gap = UI_GAP;
    /* Bars start below the board power bar */
    int bar_top = 118;
    if (nt > 0) {
        int tw = (ui->width - margin * 2 - gap * (nt - 1)) / nt;
        for (int i = 0; i < nt; i++) {
            double max_c = temp_emergency[i] > 0 ? temp_emergency[i] / 1000.0 :
                           temp_crit[i] > 0 ? temp_crit[i] / 1000.0 + 10.0 : 110.0;
            if (max_c < 80.0) max_c = 110.0;
            snprintf(label, sizeof(label), "%s   %.1f C", temp_names[i], temp_values[i]);
            TuneZone z = temperature_zone(temp_values[i], temp_crit[i], temp_emergency[i]);
            Rect temp_bar = {margin + i * (tw + gap), bar_top, tw, 21};
            unsigned long fill = tune_zone_color(ui, z);
            if (z == TUNE_ZONE_SAFE || z == TUNE_ZONE_WARNING)
                gui_bar_color_text(ui, temp_bar, temp_values[i] * 100.0 / max_c, label, fill, ui->text_dark);
            else
                gui_bar_color(ui, temp_bar, temp_values[i] * 100.0 / max_c, label, fill);
        }
    } else {
        gui_bar_color(ui, (Rect){margin, bar_top, ui->width - margin * 2, 21}, 0.0,
                      "Temperatures unavailable", ui->dim);
    }

    const int half_gap = 8;
    int half = (ui->width - margin * 2 - half_gap) / 2;
    Rect left = {margin, bar_top + 26, half, 19};
    Rect right = {margin + half + half_gap, bar_top + 26, half, 19};

    snprintf(label, sizeof(label), "GPU load %d%%", p->gpu_busy >= 0 ? p->gpu_busy : 0);
    gui_bar(ui, left, p->gpu_busy >= 0 ? p->gpu_busy : 0.0, label);
    double vram_pct = g->vram_bytes ? (double)p->vram_used * 100.0 / (double)g->vram_bytes : 0.0;
    snprintf(label, sizeof(label), "VRAM %.2f / %.2f GiB (%.1f%%)",
             p->vram_used / 1073741824.0, g->vram_bytes / 1073741824.0, vram_pct);
    gui_bar(ui, right, vram_pct, label);

    left.y = right.y = bar_top + 49;
    snprintf(label, sizeof(label), "CPU %.1f%%  (%u threads)", sys->cpu_pct, sys->cpu_threads);
    gui_bar(ui, left, sys->cpu_pct, label);
    snprintf(label, sizeof(label), "System RAM %.2f / %.2f GiB (%.1f%%)",
             sys->ram_used_bytes / 1073741824.0, sys->ram_total_bytes / 1073741824.0, sys->ram_pct);
    gui_bar(ui, right, sys->ram_pct, label);

    left.y = right.y = bar_top + 72;
    double voltage_pct = p->voltage_v >= 0.0 ? p->voltage_v * 100.0 / VOLTAGE_BAR_MAX_V : 0.0;
    if (p->voltage_v >= 0.0)
        snprintf(label, sizeof(label), "%s %.3f V",
                 s->voltage_label[0] ? s->voltage_label : "GPU voltage", p->voltage_v);
    else
        snprintf(label, sizeof(label), "GPU voltage unavailable");
    gui_bar(ui, left, voltage_pct, label);

    double current_pct = p->current_a >= 0.0 ? p->current_a * 100.0 / CURRENT_BAR_MAX_A : 0.0;
    if (p->current_a >= 0.0)
        snprintf(label, sizeof(label), "Current %s %.2f A%s", p->current_label, p->current_a,
                 p->current_is_estimate ? " (est.)" : "");
    else
        snprintf(label, sizeof(label), "Current unavailable");
    gui_bar(ui, right, current_pct, label);

    char fan[80], gfx[80], mem[80];
    if (p->fan_rpm >= 0 && p->fan_pct >= 0) snprintf(fan, sizeof(fan), "%d RPM / %.0f%%", p->fan_rpm, p->fan_pct);
    else if (p->fan_rpm >= 0) snprintf(fan, sizeof(fan), "%d RPM", p->fan_rpm);
    else snprintf(fan, sizeof(fan), "n/a");
    if (p->gfx_hz >= 0) snprintf(gfx, sizeof(gfx), "%.0f MHz", p->gfx_hz / 1000000.0); else snprintf(gfx, sizeof(gfx), "n/a");
    if (p->mem_hz >= 0) snprintf(mem, sizeof(mem), "%.0f MHz", p->mem_hz / 1000000.0); else snprintf(mem, sizeof(mem), "n/a");
    gui_text(ui, UI_MARGIN, 236, ui->fg, false,
             "Fan %s   GFX %s   MEM %s   watchdog %.0f Hz", fan, gfx, mem, st->loop_hz);
}

void ui_draw_tabs(Gui *ui, View view) {
    for (int i=0;i<9;i++) {
        View tab = tab_view(i);
        draw_button(ui,tab_rect(ui,i),view_name(tab),view==tab);
    }
    draw_button(ui,help_rect(ui),"?",view==VIEW_HELP);
}


void ui_draw_bottom_controls(Gui *ui, const Target *t, bool adaptive, bool monitor_clocks,
                                 const TraceState *tr, const EventLog *el) {
    char b0[64],b1[64],b2[64],b3[64],b4[64];
    snprintf(b0,sizeof(b0),"Space Watchdog %s",t->enabled?"ON":"PAUSED");
    snprintf(b1,sizeof(b1),"A Adaptive %s",adaptive?"ON":"off");
    snprintf(b2,sizeof(b2),"C DPM %s",monitor_clocks?"ON":"off");
    snprintf(b3,sizeof(b3),"T Trace %s",tr->active?"ON":"off");
    snprintf(b4,sizeof(b4),"L Log %s",el->logging?"ON":"off");
    const char *labels[]={b0,b1,b2,b3,b4};
    bool act[]={t->enabled,adaptive,monitor_clocks,tr->active,el->logging};
    for(int i=0;i<5;i++) draw_button(ui,control_rect(ui,i),labels[i],act[i]);

}
