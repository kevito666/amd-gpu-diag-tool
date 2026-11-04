/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only monitoring pages. Collection and policy control remain outside UI.
 */
#include <stdio.h>
#include <string.h>
#include "pages.h"
#include "draw.h"
void ui_draw_clients_page(Gui *ui, const ClientMonitor *cm) {
    int y = UI_PAGE_Y+16;
    gui_text(ui,16,y,ui->accent,true,"Per-process DRM activity"); y += 23;
    gui_text(ui,16,y,ui->dim,false,"PID    PROCESS / ROLE                  TOTAL   GFX  COMP   DMA MEDIA   VRAM"); y += 18;
    int max = cm->count < 14 ? cm->count : 14;
    for (int i=0;i<max;i++) {
        const GpuClient *c=&cm->items[i];
        gui_text(ui,16,y,ui->fg,false,"%-6d %-29.29s %6.1f %5.1f %5.1f %5.1f %5.1f %6.0fM",
                 c->pid,c->role[0]?c->role:c->name,c->activity_pct,c->gfx_pct,c->compute_pct,c->dma_pct,c->media_pct,c->vram_bytes/1048576.0);
        y += 18;
    }
    gui_text(ui,16,y+18,ui->dim,false,"Scan CPU %.3f%%  scans %llu  fdinfo reads %llu  errors %llu",
             cm->scan_cpu_pct,(unsigned long long)cm->scans,(unsigned long long)cm->fdinfo_reads,(unsigned long long)cm->scan_errors);
}

void ui_draw_power_page(Gui *ui, const Target *t, const Stats *st, const PowerTelemetry *p,
                            const Sysfs *s, const SystemTelemetry *sys,
                            bool monitor_clocks, int dpm_interval_us) {
    (void)st; (void)monitor_clocks; (void)dpm_interval_us;
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"ELECTRICAL / POWER telemetry (read-only)"); y+=30;
    gui_text(ui,x,y,ui->fg,false,"GPU board power: %s",p->power_w>=0?"available":"not reported"); y+=21;
    if(p->power_w>=0) {gui_text(ui,x,y,ui->fg,true,"Measured board power  %.2f W",p->power_w); y+=22;}
    if(sys->cpu_power_w>=0) {gui_text(ui,x,y,ui->fg,false,"CPU package power    %.2f W (hwmon)",sys->cpu_power_w); y+=22;}
    else {gui_text(ui,x,y,ui->dim,false,"CPU package power    unavailable"); y+=22;}
    gui_text(ui,x,y,ui->fg,false,"GPU power cap         %.1f W",p->power_cap_w); y+=22;
    gui_text(ui,x,y,ui->dim,false,"Firmware cap range     %.1f - %.1f W",p->power_cap_min_w,p->power_cap_max_w); y+=30;
    gui_text(ui,x,y,ui->accent,true,"Voltage / current"); y+=26;
    if(p->voltage_v>=0) gui_text(ui,x,y,ui->fg,false,"%s: %.4f V",s->voltage_label[0]?s->voltage_label:"GPU voltage",p->voltage_v);
    else {gui_text(ui,x,y,ui->dim,false,"GPU voltage: not exposed");} y+=22;
    if(p->current_a>=0) gui_text(ui,x,y,ui->fg,false,"%s current: %.2f A%s",p->current_label,p->current_a,
                                p->current_is_estimate?" (derived estimate)":" (sensor)");
    else {gui_text(ui,x,y,ui->dim,false,"GPU current: unavailable");} y+=22;
    if(p->equivalent_input_ohm>=0) {
        gui_text(ui,x,y,ui->dim,false,"Equivalent 12 V load: %.3f ohm (estimated)",p->equivalent_input_ohm); y+=22;
    }
    gui_text(ui,x,y,ui->accent,true,"Electrical control state"); y+=26;
    gui_text(ui,x,y,ui->fg,false,"Power profile: %.160s",p->power_profile[0]?p->power_profile:"unavailable"); y+=22;
    gui_text(ui,x,y,ui->fg,false,"Power policy: %s   watchdog: %s",t->policy,t->enabled?"enforcing":"paused"); y+=23;
    gui_text(ui,x,y,ui->dim,false,"Thermals, clocks, link and AI metrics have their own dedicated pages.");
}

void ui_draw_desktop_page(Gui *ui, const SessionInfo *si, const char *compositor_detector) {
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"Desktop / X server software & display state"); y+=28;
    gui_text(ui,x,y,ui->fg,false,"Session: %s  |  X display: %s",si->session_type,si->display); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Session manager: %s   built %s",si->session_version[0]?si->session_version:"unknown",
             si->session_built[0]?si->session_built:"unknown");y+=24;
    gui_text(ui,x,y,ui->fg,false,"X server: %s   built %s",si->xserver_version,si->xserver_built[0]?si->xserver_built:"unknown");y+=24;
    gui_text(ui,x,y,ui->dim,false,"X vendor: %.110s",si->xserver_vendor);y+=24;
    gui_text(ui,x,y,ui->fg,false,"Desktop %s: %s   built %s",si->desktop,
             si->desktop_version[0]?si->desktop_version:"unknown",si->desktop_built[0]?si->desktop_built:"unknown");y+=24;
    gui_text(ui,x,y,ui->fg,false,"WM %s: %s   built %s",si->window_manager,
             si->wm_version[0]?si->wm_version:"unknown",si->wm_built[0]?si->wm_built:"unknown");y+=24;
    gui_text(ui,x,y,ui->fg,false,"Compositor %s: %s   built %s",si->compositor,
             si->compositor_version[0]?si->compositor_version:"unknown",si->compositor_built[0]?si->compositor_built:"unknown");y+=22;
    gui_text(ui,x,y,ui->dim,false,"Compositor PID %d  |  detector: %s",si->compositor_pid,
             compositor_detector); y+=34;
    gui_text(ui,x,y,ui->accent,true,"Display timing / presentation"); y+=27;
    gui_text(ui,x,y,ui->fg,false,"Refresh rate: %s",si->refresh);y+=22;
    gui_text(ui,x,y,ui->fg,false,"Active modes: %.155s",si->kms_modes);y+=22;
    gui_text(ui,x,y,ui->fg,false,"VRR / FreeSync / G-Sync: %s",si->vrr_state);y+=22;
    gui_text(ui,x,y,ui->dim,false,"VSync: %s",si->vsync_state);y+=22;
    gui_text(ui,x,y,ui->dim,false,"Buffering: %s",si->buffer_state);y+=28;
    gui_text(ui,x,y,ui->accent,true,"Outputs / KMS");y+=25;
    gui_text(ui,x,y,ui->fg,false,"%d connected: %.155s",si->connected_outputs,si->connector_summary);y+=22;
    gui_text(ui,x,y,ui->fg,false,"Active planes %d; allocators: %.150s",si->active_planes,si->plane_allocators);y+=28;
    gui_text(ui,x,y,ui->dim,false,"Version/build dates are from installed Arch packages (when available).");y+=21;
    gui_text(ui,x,y,ui->dim,false,"Refresh/VRR reflect DRM reports, not per-game presentation guarantees.");
}

void ui_draw_events_page(Gui *ui, const EventLog *el) {
    int y=UI_PAGE_Y+16;
    gui_text(ui,16,y,ui->accent,true,"Diagnostic flight recorder - newest first"); y+=24;
    int printed=0;
    for(int n=0;n<el->count && printed<15;n++) {
        int idx=el->head-1-n; while(idx<0) idx+=MAX_DIAG_EVENTS;
        const DiagEvent *ev=&el->ring[idx];
        /* High-frequency samples do not displace the meaningful fault/control history. */
        if(!strcmp(ev->kind,"DPM CHANGE") || !strcmp(ev->kind,"GFXOFF")) continue;
        gui_text(ui,16,y,ui->fg,false,"%2d  %-12.12s %-42.42s actor %-14.14s %5.1f%%",
                 ++printed,ev->kind,ev->detail,ev->actor,ev->actor_activity);
        y+=18;
    }
    gui_text(ui,16,y+20,ui->dim,false,"Last 15 major events | Total %llu   CSV logging %s%s%s",
             (unsigned long long)el->total,el->logging?"ON":"off",el->logging?"  ":"",el->logging?el->log_path:"");
}

void ui_draw_trace_page(Gui *ui, const TraceState *tr) {
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"Kernel tracefs diagnostics"); y+=30;
    gui_text(ui,x,y,ui->fg,false,"Available: %s   Active: %s   total lines %llu",tr->available?"yes":"no",tr->active?"YES":"no",(unsigned long long)tr->lines); y+=24;
    gui_text(ui,x,y,ui->fg,false,"AMDGPU events %d   scheduler events %d   DRM events %d",tr->amdgpu_events,tr->drm_sched_events,tr->drm_events); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Lines: amdgpu %llu   scheduler %llu   drm/display %llu",
             (unsigned long long)tr->amdgpu_lines,(unsigned long long)tr->sched_lines,(unsigned long long)tr->drm_lines); y+=28;
    gui_text(ui,x,y,ui->fg,false,"Last actor: %s",tr->last_actor[0]?tr->last_actor:"none"); y+=24;
    gui_text(ui,x,y,ui->dim,false,"Last trace: %.150s",tr->last_line[0]?tr->last_line:"none"); y+=36;
    gui_text(ui,x,y,ui->warn,true,"T toggles tracing. It is OFF by default because tracing is diagnostic, not normal monitoring.");
}
