/* SPDX-License-Identifier: GPL-3.0-or-later
 * AI page: explicit log, Maya, prompt cache and NVMe snapshots.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "dashboards.h"
#include "draw.h"
#include "layout.h"
/* Unified, dense tile grid shared in size/spacing with the gaming dashboard. */
void ui_draw_ai_dashboard(Gui *ui, const Gpu *g, const Target *t, const Stats *st, int interval_us,
                              bool adaptive, const ClientMonitor *cm,
                              const PowerTelemetry *p, const SystemTelemetry *sys,
                              const UiAiSources *sources) {
    const int margin = UI_MARGIN, gap = UI_GAP, top = UI_PAGE_Y;
    const int half = (ui->width - margin * 2 - gap) / 2;
    const int lead_h = 180, tile_h = 96;
    const int third = (ui->width - margin * 2 - gap * 2) / 3;
    const int tile_top = top + lead_h + gap;
    Rect perf = {margin, top, half, lead_h};
    Rect log = {margin + half + gap, top, half, lead_h};
    Rect r[6];
    for (int i = 0; i < 6; i++)
        r[i] = (Rect){margin + (i % 3) * (third + gap), tile_top + (i / 3) * (tile_h + gap), third, tile_h};

    panel_title(ui, perf, "AI PERFORMANCE");
    if (((*sources->settings).log_parser==LOG_MAYA ||
         ((*sources->settings).log_parser==LOG_AUTO && !strcmp(sys->ai_log_source,"Project Maya"))) &&
        (!sys->tokens.has_data || (*sources->settings).token_source==TOKEN_SOURCE_MAYA) &&
        ((*sources->maya).prefill_valid || (*sources->maya).decode_valid)) {
        if((*sources->maya).decode_valid) gui_text(ui,perf.x+9,top+36,ui->accent,true,
            "Maya decode %.1f tok/s (completed log)",(*sources->maya).decode_tps);
        else gui_text(ui,perf.x+9,top+36,ui->dim,true,"Maya decode: no completed stat");
        if((*sources->maya).prefill_valid) gui_text(ui,perf.x+9,top+53,ui->fg,false,
            "Prefill %.0f tok/s | %ld tokens",(*sources->maya).prefill_tps,(*sources->maya).prompt_tokens);
        else gui_text(ui,perf.x+9,top+53,ui->dim,false,"Prefill: no measured timing in log");
    } else if (sys->tokens.has_data && sys->tokens.tokens_per_sec > 0.0) {
        gui_text(ui, perf.x+9, top+36, ui->accent, true, "%s: %.1f tok/s (%s)",
                 sys->token_source == TOKEN_SOURCE_HEURISTIC ? "Est." : "Live",
                 sys->tokens.tokens_per_sec, sys->tokens.source_label);
        if (p->power_w > 0)
            gui_text(ui, perf.x+9, top+53, ui->fg, false, "%.2f tok/s/W | %.1f W",
                     sys->tokens.tokens_per_sec / p->power_w, p->power_w);
    } else if (sys->ai_log_event_count > 0 && (sources->log_events)[0].has_eval) {
        gui_text(ui,perf.x+9,top+36,ui->accent,true,"Last decode: %.1f tok/s (log)",(sources->log_events)[0].eval_tps);
        gui_text(ui,perf.x+9,top+53,ui->dim,false,"Completed timing - not live throughput");
    } else {
        gui_text(ui,perf.x+9,top+36,ui->dim,false,"No live token rate available");
        gui_text(ui,perf.x+9,top+53,ui->fg,false,"GPU %d%% | %.1f W",p->gpu_busy,p->power_w);
    }
    /* Host RAM prompt-state cache; only the cache-state log provides usage.
     * Never pass the full GPU VRAM reading as 'prompt cache used'. */
    const AiPromptCache *cache = &sys->ai_prompt_cache;
    bool maya_active=(*sources->settings).log_parser==LOG_MAYA ||
        ((*sources->settings).log_parser==LOG_AUTO && !strcmp(sys->ai_log_source,"Project Maya"));
    if (maya_active) {
        if((*sources->maya).vram_hit_valid)
            gui_text(ui,perf.x+9,top+82,ui->fg,false,"Maya VRAM expert hit %.1f%%",(*sources->maya).vram_hit_pct);
        else gui_text(ui,perf.x+9,top+82,ui->dim,false,"Maya VRAM hit: not reported");
        if((*sources->maya).disk_valid)
            gui_text(ui,perf.x+9,top+101,ui->fg,false,"Disk %.1f reads/tok | %.1f ms/tok",
                     (*sources->maya).disk_reads_per_token,(*sources->maya).disk_wait_ms_per_token);
        else gui_text(ui,perf.x+9,top+101,ui->dim,false,"Disk reads/tok: not reported");
        if((*sources->maya).ram_fetch_valid)
            gui_text(ui,perf.x+9,top+119,ui->dim,false,"RAM fetches %.1f/tok | engine statistics",
                     (*sources->maya).ram_fetches_per_token);
        else gui_text(ui,perf.x+9,top+119,ui->dim,false,"Maya stats: available when logged");
    } else if (cache->limit_valid && cache->limit_mib == 0) {
        gui_text(ui,perf.x+9,top+82,ui->dim,false,"Host prompt cache: disabled");
    } else if (cache->usage_valid && cache->limit_valid && cache->limit_mib > 0) {
        char cache_label[120];
        double pct = 100.0 * cache->used_mib / cache->limit_mib;
        snprintf(cache_label,sizeof(cache_label),"Prompt RAM (log) %.2f/%.2f GiB (%.0f%%)",
                 cache->used_mib/1024.0, cache->limit_mib/1024.0, pct);
        gui_bar_color(ui,(Rect){perf.x+9,top+71,perf.w-18,19},pct,cache_label,
                      pct >= 95 ? ui->bad : pct >= 85 ? ui->warn : ui->accent);
    } else if (cache->usage_valid) {
        gui_text(ui,perf.x+9,top+82,ui->fg,false,"Prompt RAM %.2f GiB | limit %s",
                 cache->used_mib/1024.0,cache->limit_valid?"unlimited":"unknown");
    } else if (cache->limit_valid && cache->limit_mib > 0) {
        gui_text(ui,perf.x+9,top+82,ui->dim,false,"Prompt RAM usage: -- / %.2f GiB",
                 cache->limit_mib/1024.0);
    } else {
        gui_text(ui,perf.x+9,top+82,ui->dim,false,"Prompt RAM cache: usage unavailable");
    }
    if (maya_active) {
        /* Project Maya has its own expert tiers; llama.cpp host prompt-cache
           counters are not meaningful and must not be shown here. */
    } else if (cache->usage_valid || cache->save_attempts || cache->matches ||
        cache->evictions || cache->rejected || cache->load_failures || cache->forced_reprocess) {
        if (cache->usage_valid)
            gui_text(ui,perf.x+9,top+101,ui->fg,false,"States %d | matches %d | evicts %d (%.1f GiB)",
                     cache->entries,cache->matches,cache->evictions,cache->evicted_mib/1024.0);
        else
            gui_text(ui,perf.x+9,top+101,ui->fg,false,"States -- | matches %d | evicts %d (%.1f GiB)",
                     cache->matches,cache->evictions,cache->evicted_mib/1024.0);
        gui_text(ui,perf.x+9,top+119,ui->dim,false,"Save tries %d | skip %d | fail %d | redo %d",
                 cache->save_attempts,cache->rejected,cache->load_failures,cache->forced_reprocess);
    } else {
        gui_text(ui,perf.x+9,top+101,ui->dim,false,"Cache events: unavailable from current log");
        gui_text(ui,perf.x+9,top+119,ui->dim,false,"Enable Bionic cache-state logging for stats");
    }
    const char *activity = sys->ai_model_activity[0] ? sys->ai_model_activity : "Idle";
    unsigned long activity_color = strcmp(activity,"Idle") == 0 ? ui->dim : ui->good;
    gui_text(ui,perf.x+9,top+139,activity_color,true,"Activity: %s",activity);
    if (sys->tokens.is_processing) {
        gui_text(ui,perf.x+9,top+158,ui->fg,false,"Prompt processed %lld / %lld tokens",
                 (long long)sys->tokens.n_prompt_tokens_processed,(long long)sys->tokens.n_prompt_tokens);
    } else if (sys->ai_log_event_count) {
        const AiLogEvent *ev = &(sources->log_events)[0];
        gui_text(ui,perf.x+9,top+158,ui->dim,false,"Task #%d | %s | P %.0f / D %.0f tok",
                 ev->task_id,ev->time_str,ev->prompt_tokens,ev->eval_tokens);
    } else {
        gui_text(ui,perf.x+9,top+158,ui->dim,false,"Counters: recent log tail, not lifetime");
    }

    panel_title(ui, log, "AI PERFORMANCE LOG");
    bool maya_log = (*sources->settings).log_parser==LOG_MAYA ||
                    ((*sources->settings).log_parser==LOG_AUTO && !strcmp(sys->ai_log_source,"Project Maya"));
    if(maya_log && (*sources->maya).count) {
        for(int i=0;i<(*sources->maya).count;i++) {
            gui_text(ui,log.x+9,top+36+i*42,ui->dim,false,"%.56s",(*sources->maya).recent[i]);
        }
    } else if (sys->ai_log_event_count) {
        int n = sys->ai_log_event_count < 3 ? sys->ai_log_event_count : 3;
        for (int i=0; i<n; ++i) {
            const AiLogEvent *ev=&(sources->log_events)[i];
            int y=top+34+i*37;
            gui_text(ui,log.x+9,y,i==0?ui->fg:ui->dim,false,
                     "%s  task %d / slot %d  %s",ev->time_str,ev->task_id,ev->slot_id,
                     ev->has_total?"done":ev->has_eval?"decode":"prompt");
            gui_text(ui,log.x+9,y+16,ui->dim,false,
                     "Prompt %.0f @ %.1f/s | Decode %.0f @ %.1f/s",
                     ev->prompt_tokens,ev->prompt_tps,ev->eval_tokens,ev->eval_tps);
        }
    } else gui_text(ui,log.x+9,top+37,ui->dim,false,
                    maya_log?"No Maya engine lines in log tail":"No recent timing entries in log");
    if (sys->ai_log_path[0]) draw_button(ui, ai_log_button_rect(ui), "View Log", false);

    panel_title(ui, r[0], "LOCK / WATCHDOG");
    gui_text(ui,r[0].x+9,r[0].y+36,ui->fg,false,"%s",t->enabled?"ENFORCING":"PAUSED");
    gui_text(ui,r[0].x+9,r[0].y+54,ui->dim,false,"Target %s",t->policy);
    gui_text(ui,r[0].x+9,r[0].y+72,ui->dim,false,"%.0f Hz | adaptive %s | resets %llu",
             1000000.0/interval_us,adaptive?"on":"off",(unsigned long long)st->policy_mismatches);

    panel_title(ui, r[1], "MODEL INFO");
    if (maya_active) {
        gui_text(ui,r[1].x+9,r[1].y+35,ui->fg,false,"Project Maya engine log");
        gui_text(ui,r[1].x+9,r[1].y+52,ui->dim,false,"Engine stats: %lu entries in tail",(*sources->maya).recognized);
        const char *base=strrchr(sys->ai_log_path,'/');
        gui_text(ui,r[1].x+9,r[1].y+71,ui->dim,false,"%.25s",base?base+1:
                 sys->ai_log_path[0]?sys->ai_log_path:"No log discovered");
    } else if (sys->ai_model_info_valid) {
        gui_text(ui,r[1].x+9,r[1].y+35,ui->fg,false,"Context %d",sys->ai_ctx_size);
        gui_text(ui,r[1].x+9,r[1].y+52,ui->fg,false,"KV %s / %s",sys->ai_cache_type_k,sys->ai_cache_type_v);
        gui_text(ui,r[1].x+9,r[1].y+69,ui->dim,false,"Flash %s | MTP %s",sys->ai_flash_attn?"on":"off",sys->ai_mtp?"on":"off");
    } else gui_text(ui,r[1].x+9,r[1].y+42,ui->dim,false,"No model data");

    panel_title(ui, r[2], "GPU CLIENTS / VRAM");
    const GpuClient *c = cm && cm->count > 0 ? &cm->items[0] : NULL;
    if (c) {
        gui_text(ui,r[2].x+9,r[2].y+36,ui->fg,false,"%s",c->role[0]?c->role:c->name);
        gui_text(ui,r[2].x+9,r[2].y+54,ui->dim,false,"PID %d | activity %.1f%%",c->pid,c->activity_pct);
    } else gui_text(ui,r[2].x+9,r[2].y+36,ui->dim,false,"No active DRM clients");
    if (g->vram_bytes)
        gui_text(ui,r[2].x+9,r[2].y+74,ui->fg,false,"VRAM %.1f / %.1f GiB",p->vram_used/1073741824.0,g->vram_bytes/1073741824.0);

    panel_title(ui, r[3], "TOP CPU PROCESSES / 2s");
    if (sys->top_proc_count > 0) {
        for (int i=0; i<sys->top_proc_count && i<3; ++i)
            gui_text(ui,r[3].x+9,r[3].y+35+i*18,ui->fg,false,"#%d %5.1f%% %.17s",
                     i+1,sys->top_procs[i].cpu_pct,sys->top_procs[i].name);
    } else gui_text(ui,r[3].x+9,r[3].y+40,ui->dim,false,"No CPU activity in sample yet");

    panel_title(ui, r[4], "LAST EVENT");
    const UiEventSummary *ev = &sources->event;
    if (ev->available) {
        gui_text(ui,r[4].x+9,r[4].y+36,ui->fg,false,"%s",ev->kind);
        gui_text(ui,r[4].x+9,r[4].y+54,ui->fg,false,"%s",ev->detail);
        gui_text(ui,r[4].x+9,r[4].y+73,ui->dim,false,"Actor %s (%.1f%%)",ev->actor,ev->actor_activity);
    } else gui_text(ui,r[4].x+9,r[4].y+40,ui->dim,false,"No diagnostic events");

    panel_title(ui,r[5],"AI NVME CACHE I/O");
    gui_text(ui,r[5].x+9,r[5].y+35,ui->fg,false,"Drive: %s",(*sources->nvme).selected[0]?(*sources->nvme).selected:"None selected");
    if((*sources->nvme).valid) {
        gui_text(ui,r[5].x+9,r[5].y+53,ui->accent,false,"Read %.1f | Write %.1f MiB/s",(*sources->nvme).read_mib_s,(*sources->nvme).write_mib_s);
        gui_text(ui,r[5].x+9,r[5].y+72,ui->dim,false,"Peak R %.0f W %.0f | Busy %.0f%%",
                 (*sources->nvme).peak_read_mib_s,(*sources->nvme).peak_write_mib_s,(*sources->nvme).busy_pct);
    } else gui_text(ui,r[5].x+9,r[5].y+57,ui->dim,false,"Waiting for selected NVMe counters");
    gui_text(ui,r[5].x+9,r[5].y+89,ui->dim,false,"Whole drive traffic, not AI-only");
}
