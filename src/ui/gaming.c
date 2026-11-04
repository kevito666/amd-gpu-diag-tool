/* SPDX-License-Identifier: GPL-3.0-or-later
 * Gaming page. Read-only telemetry, including PCIe and BAR status.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "dashboards.h"
#include "draw.h"
#include "status.h"
/* Read-only gaming telemetry. No FPS or hardware feature statuses are inferred
 * from the UI's own redraw rate or unsupported kernel interfaces. */
void ui_draw_gaming_dashboard(Gui *ui, const Gpu *g, const Sysfs *s,
                                  const PowerTelemetry *p, const SystemTelemetry *sys) {
    int margin=UI_MARGIN, gap=UI_GAP, tile_h=96;
    int w=(ui->width-2*margin-2*gap)/3;
    int top=UI_PAGE_Y;
    Rect r[3]={{margin, top, w, tile_h},
               {margin+w+gap, top, w, tile_h},
               {margin+2*(w+gap), top, w, tile_h}};
    char label[128];
    double watts_scale=p->power_cap_w>0.0 ? p->power_cap_w : 400.0;
    if (watts_scale<200) watts_scale=200;

    /* Row 1: both measured powers, individual GPU temperatures, free memory. */
    panel_title(ui,r[0],"POWER CONSUMPTION");
    if (p->power_w>=0.0) {
        if (p->power_cap_w > 0.0)
            snprintf(label,sizeof(label),"GPU %.0f / %.0f W",p->power_w,p->power_cap_w);
        else snprintf(label,sizeof(label),"GPU %.0f W (board)",p->power_w);
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+31,w-18,21},p->power_w*100.0/watts_scale,label,ui->accent);
    } else gui_text(ui,r[0].x+9,r[0].y+47,ui->dim,false,"GPU power unavailable");
    if (sys->cpu_power_w>=0.0) {
        snprintf(label,sizeof(label),"CPU %.0f W (sensor)",sys->cpu_power_w);
        /* CPU is an independent 200 W reference scale, not the GPU's power cap. */
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+61,w-18,21},sys->cpu_power_w/2.0,label,ui->accent);
    } else gui_text(ui,r[0].x+9,r[0].y+76,ui->dim,false,"CPU power: sensor unavailable");

    panel_title(ui,r[1],"GPU TEMPERATURE");
    static const char *const temp_names[3]={"EDGE [temp1]","JUNCTION [temp2]","MEM [temp3]"};
    int shown=0;
    for (int i=0;i<MAX_GPU_TEMPS;i++) {
        if (p->temps_c[i]<0.0) continue;
        TuneZone zone=temperature_zone(p->temps_c[i],s->temp_crit_mc[i],s->temp_emergency_mc[i]);
        double limit=s->temp_emergency_mc[i]>0 ? s->temp_emergency_mc[i]/1000.0 :
                     s->temp_crit_mc[i]>0 ? s->temp_crit_mc[i]/1000.0+10 : 110;
        if (limit<80) limit=110;
        snprintf(label,sizeof(label),"%s %.0f C",temp_names[i],p->temps_c[i]);
        gui_bar_color(ui,(Rect){r[1].x+9,r[1].y+29+i*22,w-18,18},
                      p->temps_c[i]*100.0/limit,label,tune_zone_color(ui,zone));
        shown++;
    }
    if (!shown) gui_text(ui,r[1].x+9,r[1].y+45,ui->dim,false,"GPU sensors unavailable");

    panel_title(ui,r[2],"MEMORY");
    if (g->vram_bytes>0 && p->vram_usage_valid && p->vram_used<=g->vram_bytes) {
        uint64_t free=g->vram_bytes-p->vram_used;
        snprintf(label,sizeof(label),"GPU free %.1f / %.1f GiB",free/1073741824.0,g->vram_bytes/1073741824.0);
        gui_bar_color(ui,(Rect){r[2].x+9,r[2].y+31,w-18,21},
                      free*100.0/g->vram_bytes,label,free*100.0/g->vram_bytes<10?ui->warn:ui->accent);
    } else gui_text(ui,r[2].x+9,r[2].y+47,ui->dim,false,"GPU headroom unavailable");
    if (sys->ram_total_bytes && sys->ram_used_bytes<=sys->ram_total_bytes) {
        uint64_t free=sys->ram_total_bytes-sys->ram_used_bytes;
        snprintf(label,sizeof(label),"RAM free %.1f / %.1f GiB",free/1073741824.0,sys->ram_total_bytes/1073741824.0);
        gui_bar_color(ui,(Rect){r[2].x+9,r[2].y+61,w-18,21},
                      free*100.0/sys->ram_total_bytes,label,ui->accent);
    } else gui_text(ui,r[2].x+9,r[2].y+76,ui->dim,false,"System RAM headroom n/a");

    top+=tile_h+gap;
    for(int i=0;i<3;i++) r[i].y=top;
    panel_title(ui,r[0],"GPU ENGINE UTILIZATION");
    if (p->gpu_busy>=0) {
        snprintf(label,sizeof(label),"Graphics/compute %d%%",p->gpu_busy);
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+31,w-18,19},p->gpu_busy,label,ui->accent);
    } else gui_text(ui,r[0].x+9,r[0].y+45,ui->dim,false,"GPU busy unavailable");
    if (p->mem_busy>=0) {
        snprintf(label,sizeof(label),"Memory controller %d%%",p->mem_busy);
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+59,w-18,19},p->mem_busy,label,ui->accent);
    } else gui_text(ui,r[0].x+9,r[0].y+75,ui->dim,false,"Memory busy unavailable");

    panel_title(ui,r[1],"CPU LOAD PER CORE");
    int max_cores=sys->cpu_cores.core_count;
    if (max_cores>8) max_cores=8;
    if (max_cores<=0) gui_text(ui,r[1].x+9,r[1].y+45,ui->dim,false,"CPU load unavailable");
    else {
        int bw=(w-20-3*5)/4;
        for (int i=0;i<max_cores;i++) {
            int x=r[1].x+10+(i%4)*(bw+5), y=r[1].y+32+(i/4)*24;
            snprintf(label,sizeof(label),"C%d %.0f%%",i,sys->cpu_cores.load_pct[i]);
            gui_bar_color(ui,(Rect){x,y,bw,19},sys->cpu_cores.load_pct[i],label,ui->accent);
        }
    }
    panel_title(ui,r[2],"CPU CLOCKS");
    if (sys->cpu_clock_count>0) {
        double scale=sys->cpu_clock_cap_mhz>0?sys->cpu_clock_cap_mhz:sys->cpu_clock_max_mhz;
        const double f[]={sys->cpu_clock_min_mhz,sys->cpu_clock_avg_mhz,sys->cpu_clock_max_mhz};
        static const char *const names[]={"Min","Avg","Max"};
        for(int i=0;i<3;i++) {
            snprintf(label,sizeof(label),"%s %.0f MHz",names[i],f[i]);
            gui_bar_color(ui,(Rect){r[2].x+9,r[2].y+29+i*22,w-18,18},f[i]*100.0/scale,label,ui->accent);
        }
    } else gui_text(ui,r[2].x+9,r[2].y+45,ui->dim,false,"CPU cpufreq unavailable");

    top+=tile_h+gap;
    for(int i=0;i<3;i++) r[i].y=top;
    panel_title(ui,r[0],"CPU TEMPERATURE");
    if (sys->cpu_tctl_c>=0.0) {
        snprintf(label,sizeof(label),"Tctl [%s] %.1f C",
                 strcasestr(sys->cpu_tctl_label,"zenpower")?"zenpower":"k10temp",sys->cpu_tctl_c);
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+31,w-18,20},sys->cpu_tctl_c,label,ui->accent);
    } else gui_text(ui,r[0].x+9,r[0].y+45,ui->dim,false,"Tctl unavailable");
    if (sys->cpu_temp_c>=0.0) {
        snprintf(label,sizeof(label),"%s %.1f C",sys->cpu_temp_label[0]?sys->cpu_temp_label:"CPU",sys->cpu_temp_c);
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+61,w-18,19},sys->cpu_temp_c,label,ui->accent);
    }
    panel_title(ui,r[1],"MOTHERBOARD TEMP");
    if (sys->mb_temp_count>0) {
        int show=sys->mb_temp_count>3?3:sys->mb_temp_count;
        for(int i=0;i<show;i++) {
            if (sys->mb_temp_c[i]<0.0) continue;
            snprintf(label,sizeof(label),"%s %.1f C",sys->mb_temp_labels[i],sys->mb_temp_c[i]);
            gui_bar_color(ui,(Rect){r[1].x+9,r[1].y+29+i*22,w-18,18},sys->mb_temp_c[i],label,ui->accent);
        }
    } else gui_text(ui,r[1].x+9,r[1].y+45,ui->dim,false,"No motherboard sensors");
    panel_title(ui,r[2],"GPU CLOCKS / FAN");
    if (p->gfx_hz>=0) gui_text(ui,r[2].x+9,r[2].y+36,ui->fg,false,"GFX %.0f MHz",p->gfx_hz/1e6);
    else gui_text(ui,r[2].x+9,r[2].y+36,ui->dim,false,"GFX clock unavailable");
    if (p->mem_hz>=0) gui_text(ui,r[2].x+9,r[2].y+57,ui->fg,false,"VRAM %.0f MHz",p->mem_hz/1e6);
    else gui_text(ui,r[2].x+9,r[2].y+57,ui->dim,false,"VRAM clock unavailable");
    if (p->fan_rpm>=0) gui_text(ui,r[2].x+9,r[2].y+77,ui->fg,false,"Fan %d RPM",p->fan_rpm);
    else gui_text(ui,r[2].x+9,r[2].y+77,ui->dim,false,"Fan unavailable");

    top+=tile_h+gap;
    for(int i=0;i<3;i++) r[i].y=top;
    panel_title(ui,r[0],"PCIe LINK");
    if (p->pcie_link_valid) {
        gui_text(ui,r[0].x+9,r[0].y+38,ui->fg,false,"NOW %s x%d",p->pcie_current_speed,p->pcie_current_width);
        if (p->pcie_max_width>0 && p->pcie_max_speed[0])
            gui_text(ui,r[0].x+9,r[0].y+58,ui->dim,false,"MAX %s x%d",p->pcie_max_speed,p->pcie_max_width);
        gui_text(ui,r[0].x+9,r[0].y+80,p->pcie_link_loaded_low==1?ui->warn:ui->dim,false,
                 "Under load: %s",p->pcie_link_loaded_low<0?"UNKNOWN":p->pcie_link_loaded_low?"LINK DOWN-SHIFT":"OK / IDLE");
    } else gui_text(ui,r[0].x+9,r[0].y+44,ui->dim,false,"Link negotiation n/a");

    panel_title(ui,r[1],"RESIZABLE / LARGE BAR");
    if (p->bar0_valid) {
        bool large=p->bar0_bytes>=1073741824ULL;
        gui_text(ui,r[1].x+9,r[1].y+36,large?ui->good:ui->warn,false,
                 "Large BAR: %s",large?"ENABLED":"DISABLED");
        gui_text(ui,r[1].x+9,r[1].y+56,ui->fg,false,"BAR0 aperture %.1f GiB",p->bar0_bytes/1073741824.0);
    } else {
        gui_text(ui,r[1].x+9,r[1].y+38,ui->dim,false,"Large BAR: UNKNOWN");
        gui_text(ui,r[1].x+9,r[1].y+58,ui->dim,false,"BAR0 aperture unavailable");
    }
    gui_text(ui,r[1].x+9,r[1].y+79,ui->dim,false,
             "Resize sysfs: %s",p->rebar_supported?"EXPOSED":"NOT EXPOSED");

    panel_title(ui,r[2],"PERFORMANCE FLAGS");
    gui_text(ui,r[2].x+9,r[2].y+34,sys->cpu_boost<0?ui->dim:sys->cpu_boost?ui->good:ui->warn,
             false,"CPU Boost: %s",sys->cpu_boost<0?"UNKNOWN":sys->cpu_boost?"ENABLED":"DISABLED");
    gui_text(ui,r[2].x+9,r[2].y+53,ui->fg,false,"CPU governor: %s",
             sys->cpu_governor[0]?sys->cpu_governor:"unknown");
    gui_text(ui,r[2].x+9,r[2].y+72,p->amdgpu_dpm==0?ui->warn:ui->dim,false,"GPU DPM: %s",
             p->amdgpu_dpm==-2?"UNKNOWN":p->amdgpu_dpm==-1?"AUTO":p->amdgpu_dpm?"ENABLED":"DISABLED");
    gui_text(ui,r[2].x+9,r[2].y+89,ui->dim,false,"CPU SMT: %s",
             sys->cpu_smt<0?"UNKNOWN":sys->cpu_smt?"ENABLED":"DISABLED");
}
