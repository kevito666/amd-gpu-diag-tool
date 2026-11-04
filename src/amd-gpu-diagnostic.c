/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Kevin Kolasa
 *
 * AMD GPU Performance and Diagnostic Tool
 * Lightweight X11/XLibre AMDGPU diagnostics and power-policy control.
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <pwd.h>
#include <grp.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/time.h>

#include <curl/curl.h>

#include "gpu_specs.h"
#include "core/util.h"
#include "monitor/nvme.h"
#include "ai/maya_log.h"
#include "ai/log_parser.h"
#include "ai/http.h"
#include "ai/llama_process.h"
#include "ai/log_files.h"
#include "config/settings.h"
#include "monitor/cpu.h"

#define VERSION "0.8.0-dev.16"
#define MAX_PATH_LEN 512
#define MAX_TEXT 4096
#define MAX_LEVELS 32
#define UI_REFRESH_NS 500000000LL
#define DEFAULT_INTERVAL_US 5000
#define DEFAULT_DPM_INTERVAL_US 250000
#define MIN_DPM_INTERVAL_US 50000
#define MAX_DPM_INTERVAL_US 2000000
#define MIN_INTERVAL_US 500
#define UNSAFE_MIN_INTERVAL_US 10
#define MAX_INTERVAL_US 1000000
#define ADAPTIVE_BURST_INTERVAL_US 500
#define ADAPTIVE_BURST_NS 250000000LL

#define CLIENT_SAMPLE_NS 1000000000LL
#define POWER_SAMPLE_NS 100000000LL
#define SESSION_SAMPLE_NS 10000000000LL
#define SYSTEM_SAMPLE_NS 500000000LL
#define VOLTAGE_BAR_MAX_V 1.20
#define CURRENT_BAR_MAX_A 50.0
#define POWER_BAR_MAX_W 800.0
#define MAX_GPU_CLIENTS 128
#define MAX_CONNECTORS 12
#define MAX_GPU_TEMPS 3
#include "monitor/system_state.h"
#include "monitor/sensors.h"
#include "monitor/desktop_probe.h"



#include "gpu/readback.h"
#include "gpu/device.h"
#include "gpu/policy.h"
#include "gpu/tuning.h"
#include "monitor/session.h"
#include "ui/draw.h"
#include "ui/layout.h"
#include "ui/window.h"
#include "ui/status.h"
#include "ui/dashboards.h"
#include "ui/pages.h"
#include "diagnostics/types.h"
#include "diagnostics/events.h"
#include "diagnostics/trace.h"
#include "ui/chrome.h"
#include "ui/input.h"
#include "app/poll_timing.h"
#include "monitor/worker.h"
#include "monitor/clients.h"
#include "monitor/scan_worker.h"


static AiLogEvent g_ai_log_events[AI_LOG_MAX_EVENTS];




static volatile sig_atomic_t g_stop = 0;
static WorkflowMode g_workflow = WORKFLOW_AI;
static TokenSource g_token_source = TOKEN_SOURCE_LLAMA;
static char g_ai_log_path[MAX_PATH_LEN] = {0};
/* Settings are persisted for the *invoking desktop user*, never /root with sudo. */

static AppSettings g_settings = {.token_source=TOKEN_SOURCE_LLAMA,.log_parser=LOG_AUTO,
                                 .compositor_source=COMP_AUTO,.preferred_card=-1};
static const char *const g_parser_names[]={"Auto (llama compatible)","llama.cpp","Bionic / llama.cpp",
    "Ollama (experimental)","vLLM (experimental)","LM Studio (experimental)","Disabled",
    "Project Maya (engine log)"};
/* Read-only aggregate block-device throughput. Linux /sys/block/DEVICE/stat counts
 * 512-byte sectors regardless of the drive's advertised logical sector size.
 * It includes ALL host I/O, not exclusively inference traffic. */
static NvmeMonitor g_nvme;

/* Project Maya writes maya-<model>.log beside its model's .json config.
 * Engine lines vary by version and backend: raw provenance is retained.
 * Speeds are populated only from explicit known 'tokens in milliseconds'
 * prefill entries, never inferred from unrelated statistic lines. */
static MayaLogMonitor g_maya;

static const char *const g_compositor_names[]={"Auto", "X11 selection", "DRM client", "Process scan", "Disabled"};
static int g_requested_gpu=-1;
static bool g_gpu_switch_requested=false;
static Gpu g_gpu_inventory[16];
static int g_gpu_inventory_count=0;
static bool g_edit_ai_path=false;
static char g_edit_path_buffer[MAX_PATH_LEN]={0};



static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}










/* App-owned runtime synchronization; file I/O lives in config/settings.c. */
static void settings_load(void) {
    (void)config_settings_load(&g_settings);
    g_token_source = g_settings.token_source;
}
static bool settings_save(void) {
    return config_settings_save(&g_settings);
}


/* Detect available AI servers */
static void detect_ai_servers(SystemTelemetry *sys) {
    sys->llama_available = check_port(find_llama_port());
    sys->ollama_available = check_port(11434);
    sys->vllm_available = check_port(8000);
    /* Check for LM Studio/Bionic via lms CLI or HTTP */
    sys->lmstudio_available = check_port(1234);
}

/* Heuristic token rate estimation from GPU power */
static void estimate_tokens_heuristic(SystemTelemetry *sys, double gpu_power_w) {
    TokenRate *t = &sys->tokens;

    /* Store power in history */
    t->power_history[t->power_history_idx] = gpu_power_w;
    t->power_history_idx = (t->power_history_idx + 1) % 100;

    /* Calculate recent peak (last 2 seconds = 20 samples at 10Hz) */
    double peak = 0;
    for (int i = 0; i < 20; i++) {
        int idx = (t->power_history_idx - i + 100) % 100;
        if (t->power_history[idx] > peak) peak = t->power_history[idx];
    }
    t->recent_peak_w = peak;

    /* Detect decode phase: power is 40-80% of recent peak and steady */
    double ratio = (peak > 0) ? gpu_power_w / peak : 0;
    if (ratio >= 0.4 && ratio <= 0.8 && peak > 50) {
        t->in_decode_phase = true;
        t->decode_power_w = gpu_power_w;
    } else if (ratio > 0.85) {
        t->in_decode_phase = false;
    }

    if (t->in_decode_phase) {
        /* Estimate tokens/sec based on power
         * Typical decode: ~30-50W per token/sec for modern GPUs
         * This is a rough heuristic */
        double decode_w = gpu_power_w;
        double base_w = peak * 0.3;  /* Base power when idle */
        double compute_w = decode_w - base_w;
        t->tokens_per_sec = compute_w / 40.0;  /* ~40W per tok/s */
        if (t->tokens_per_sec < 1) t->tokens_per_sec = 1;
        if (t->tokens_per_sec > 200) t->tokens_per_sec = 200;
        t->has_data = true;
    } else {
        t->tokens_per_sec = 0;
        t->has_data = false;
    }
}

/* Poll llama.cpp API for token stats */
static void poll_llama_tokens(SystemTelemetry *sys) {
    TokenRate *t = &sys->tokens;
    int port = find_llama_port();
    char api_key[256];
    find_llama_api_key(api_key, sizeof(api_key));
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/slots", port);
    char resp[4096];
    if (http_get_auth(url, resp, sizeof(resp), 500, api_key) != 0) {
        t->has_data = false;
        t->is_processing = false;
        t->tokens_per_sec = 0;
        t->prev_decoded = 0;
        t->last_api_poll = (struct timespec){0};
        text_copy(sys->ai_model_activity, sizeof(sys->ai_model_activity), "API unavailable");
        return;
    }
    /* Parse JSON - look for n_decoded in next_token */
    /* Response format: [{"id":0,...,"is_processing":true/false,...,"next_token":[{"n_decoded":N,...}]}] */
    char *p = strstr(resp, "\"n_decoded\"");
    int64_t n_decoded = 0;
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            n_decoded = strtoll(p, NULL, 10);
        }
    }
    if (!p || !strstr(resp, "\"n_decoded\"")) {
        t->has_data = false;
        t->is_processing = false;
        t->tokens_per_sec = 0;
        return;
    }
    /* Parse is_processing */
    p = strstr(resp, "\"is_processing\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            t->is_processing = (*p == 't');
        }
    }

    /* Parse n_prompt_tokens */
    p = strstr(resp, "\"n_prompt_tokens\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            t->n_prompt_tokens = strtoll(p, NULL, 10);
        }
    }

    /* Parse n_prompt_tokens_processed */
    p = strstr(resp, "\"n_prompt_tokens_processed\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            t->n_prompt_tokens_processed = strtoll(p, NULL, 10);
        }
    }

    /* Parse n_prompt_tokens_cache */
    p = strstr(resp, "\"n_prompt_tokens_cache\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            t->n_prompt_tokens_cache = strtoll(p, NULL, 10);
        }
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (t->last_api_poll.tv_sec > 0 || t->last_api_poll.tv_nsec > 0) {
        double dt = (double)(now.tv_sec - t->last_api_poll.tv_sec) + (double)(now.tv_nsec - t->last_api_poll.tv_nsec) / 1e9;
        double delta = (double)(n_decoded - t->prev_decoded);
        if (dt > 0.0) {
            t->tokens_per_sec = delta / dt;
            if (t->tokens_per_sec < 0) t->tokens_per_sec = 0;
            t->has_data = true;
        }
    }

    /* Determine activity state from /slots data */
    if (!t->is_processing) {
        snprintf(sys->ai_model_activity, sizeof(sys->ai_model_activity), "Idle");
    } else if (t->n_prompt_tokens_processed < t->n_prompt_tokens) {
        snprintf(sys->ai_model_activity, sizeof(sys->ai_model_activity), "Reading");
    } else if (n_decoded != t->prev_decoded) {
        snprintf(sys->ai_model_activity, sizeof(sys->ai_model_activity), "Outputting");
    } else {
        snprintf(sys->ai_model_activity, sizeof(sys->ai_model_activity), "Thinking");
    }

    t->prev_decoded = n_decoded;
    t->last_api_poll = now;
    t->total_tokens = n_decoded;
}

/* Poll Ollama API for token stats */
static void poll_ollama_tokens(SystemTelemetry *sys) {
    TokenRate *t = &sys->tokens;
    char resp[4096];
    if (http_get("http://127.0.0.1:11434/api/ps", resp, sizeof(resp), 500) != 0) {
        t->has_data = false;
        t->tokens_per_sec = 0;
        return;
    }
    /* Ollama /api/ps returns loaded models but not token rates
     * We need to use /api/generate or track during generation
     * For now, just mark as unavailable for real-time stats */
    t->has_data = false;
}

/* Poll vLLM API for token stats */
static void poll_vllm_tokens(SystemTelemetry *sys) {
    TokenRate *t = &sys->tokens;
    char resp[8192];
    if (http_get("http://127.0.0.1:8000/metrics", resp, sizeof(resp), 500) != 0) {
        t->has_data = false;
        t->tokens_per_sec = 0;
        return;
    }
    /* Parse Prometheus metrics for vllm:generate_tokens_total or similar */
    /* Look for vllm:num_requests_running or throughput metrics */
    char *p = strstr(resp, "vllm:e2e_request_latency_seconds");
    if (!p) {
        /* Try alternative metric names */
        p = strstr(resp, "vllm:time_to_first_token_seconds");
    }
    if (p) {
        /* Found some vLLM metrics - estimate from request count */
        t->has_data = false;
        t->tokens_per_sec = 0; /* Request metrics alone cannot establish token rate. */
    } else {
        t->has_data = false;
    }
}

/* The LM Studio model-list API does not report real-time token rates.
 * Previously this called `lms ps` via a synchronous shell pipeline, which
 * could block an entire read-only worker (including shutdown) indefinitely.
 * Never launch an unbounded subprocess just to report "rate unavailable".
 * Bionic/llama.cpp token rates remain sourced from /slots and engine logs. */
static void poll_lmstudio_tokens(SystemTelemetry *sys) {
    TokenRate *t = &sys->tokens;
    t->has_data = false;
    t->is_processing = false;
    t->tokens_per_sec = 0;
    t->prompt_tokens_per_sec = 0;
}

/* Backend counters and cached rates have no meaning after a source change.
 * This includes a previous /slots decode counter and heuristic power history. */
static void token_rate_prepare_source(SystemTelemetry *sys) {
    TokenRate *t = &sys->tokens;
    if (!t->source_initialized || t->source_in_use != sys->token_source) {
        memset(t, 0, sizeof(*t));
        t->source_initialized = true;
        t->source_in_use = sys->token_source;
        sys->ai_model_activity[0] = '\0';
    }
}

/* Main token rate sampling function */
static void sample_tokens(SystemTelemetry *sys, double gpu_power_w) {
    token_rate_prepare_source(sys);
    switch (sys->token_source) {
        case TOKEN_SOURCE_HEURISTIC:
            estimate_tokens_heuristic(sys, gpu_power_w);
            snprintf(sys->tokens.source_label, sizeof(sys->tokens.source_label), "Heuristic");
            break;
        case TOKEN_SOURCE_LLAMA:
            poll_llama_tokens(sys);
            snprintf(sys->tokens.source_label, sizeof(sys->tokens.source_label), "llama.cpp");
            break;
        case TOKEN_SOURCE_OLLAMA:
            poll_ollama_tokens(sys);
            snprintf(sys->tokens.source_label, sizeof(sys->tokens.source_label), "Ollama");
            break;
        case TOKEN_SOURCE_VLLM:
            poll_vllm_tokens(sys);
            snprintf(sys->tokens.source_label, sizeof(sys->tokens.source_label), "vLLM");
            break;
        case TOKEN_SOURCE_LMSTUDIO:
            poll_lmstudio_tokens(sys);
            snprintf(sys->tokens.source_label, sizeof(sys->tokens.source_label), "LM Studio");
            break;
        case TOKEN_SOURCE_MAYA:
            sys->tokens.has_data=false;sys->tokens.is_processing=false;
            snprintf(sys->tokens.source_label,sizeof(sys->tokens.source_label),"Project Maya log");
            break;
        default:
            estimate_tokens_heuristic(sys, gpu_power_w);
            break;
    }
}

/* Read model metadata from the same cached /proc command line used for API
 * discovery. Unknown information is left unavailable, never guessed. */
static void detect_ai_model_info(SystemTelemetry *sys) {
    const LlamaProcessInfo *p = llama_process_info();
    sys->ai_model_info_valid = p->found;
    if (!p->found) {
        sys->ai_model_name[0] = '\0';
        sys->ai_ctx_size = 0;
        sys->ai_cache_type_k[0] = '\0';
        sys->ai_cache_type_v[0] = '\0';
        sys->ai_flash_attn = false;
        sys->ai_mtp = false;
        return;
    }
    text_copy(sys->ai_model_name, sizeof(sys->ai_model_name), p->model_name);
    if (strlen(sys->ai_model_name) > 45)
        memcpy(sys->ai_model_name+42, "...", 4);
    sys->ai_ctx_size = p->context_size;
    text_copy(sys->ai_cache_type_k, sizeof(sys->ai_cache_type_k), p->cache_k);
    text_copy(sys->ai_cache_type_v, sizeof(sys->ai_cache_type_v), p->cache_v);
    sys->ai_flash_attn = p->flash_attn;
    sys->ai_mtp = p->mtp;
}

/* Scan the same bounded log tail as the timing parser. Unlike estimated RAM
 * figures, 'cache state' reports real cache occupancy and number of entries.
 * Counters reflect the *visible log tail*, not lifetime cumulative totals.
 * Keep this parser non-destructive; the timing parser reuses the same buffer. */
/* Keep parser configuration coherent without waiting for the 30-second log
 * discovery timer or leaving historical values visible when logging is off. */
typedef struct {
    LogParser last_parser;
    bool last_manual;
    char last_filename[MAX_PATH_LEN];
} AiLogParseContext;

static void parse_ai_log(SystemTelemetry *sys, const AppSettings *config,
                         MayaLogMonitor *maya, AiLogEvent *events, AiLogParseContext *ctx) {
    LogParser last_parser = ctx->last_parser;
    bool last_manual = ctx->last_manual;
    const char *last_filename = ctx->last_filename;
    if (last_parser != config->log_parser ||
        last_manual != config->ai_log_manual ||
        strcmp(last_filename, config->ai_log_file) != 0) {
        ctx->last_parser = config->log_parser;
        ctx->last_manual = config->ai_log_manual;
        text_copy(ctx->last_filename, sizeof(ctx->last_filename), config->ai_log_file);
        sys->ai_log_last_detect = 0;
        sys->ai_log_last_update = 0;
        sys->ai_log_path[0] = '\0';
        sys->ai_log_event_count = 0;
        memset(&sys->ai_prompt_cache, 0, sizeof(sys->ai_prompt_cache));
        memset(events, 0, sizeof(AiLogEvent) * AI_LOG_MAX_EVENTS);
        memset(maya,0,sizeof(*maya));
    }
    if (config->log_parser == LOG_OFF) {
        sys->ai_log_event_count = 0;
        sys->ai_log_path[0] = '\0';
        memset(&sys->ai_prompt_cache, 0, sizeof(sys->ai_prompt_cache));
        memset(events, 0, sizeof(AiLogEvent) * AI_LOG_MAX_EVENTS);
        memset(maya,0,sizeof(*maya));
        return;
    }
    time_t now = time(NULL);
    if (!sys->ai_log_path[0] || now - sys->ai_log_last_detect >= 30) {
        ai_log_discover(config, sys->ai_log_path, sizeof(sys->ai_log_path),
                        sys->ai_log_source, sizeof(sys->ai_log_source));
        sys->ai_log_last_detect = now;
    }
    if (now - sys->ai_log_last_update < 2) return;
    sys->ai_log_last_update = now;
    sys->ai_log_event_count = 0;
    memset(&sys->ai_prompt_cache, 0, sizeof(sys->ai_prompt_cache));
    if (now - sys->ai_cache_process_last_check >= 30 || !sys->ai_cache_process_last_check) {
        sys->ai_cache_process_limit_valid = ai_cache_process_limit(&sys->ai_cache_process_limit_mib);
        sys->ai_cache_process_last_check = now;
    }
    if (sys->ai_cache_process_limit_valid) {
        sys->ai_prompt_cache.limit_valid = true;
        sys->ai_prompt_cache.limit_mib = sys->ai_cache_process_limit_mib;
    }
    if (!sys->ai_log_path[0]) {memset(maya,0,sizeof(*maya));return;}
    time_t log_mtime = 0;
    char *buffer = ai_log_read_tail(sys->ai_log_path, &log_mtime);
    if (!buffer) { memset(maya, 0, sizeof(*maya)); return; }
    char *start = buffer;
    if (start) {
        memset(maya,0,sizeof(*maya));
        bool maya_mode = config->log_parser==LOG_MAYA ||
                         (config->log_parser==LOG_AUTO && !strcmp(sys->ai_log_source,"Project Maya"));
        if (maya_mode) {
            maya_parse_lines(start,maya);
            if(config->token_source==TOKEN_SOURCE_MAYA) {
                sys->tokens.has_data=false;sys->tokens.is_processing=false;
                sys->tokens.tokens_per_sec=0;
                sys->tokens.prompt_tokens_per_sec=maya->prefill_valid?maya->prefill_tps:0;
                if(now>=log_mtime && now-log_mtime<=8 && maya->count)
                    text_copy(sys->ai_model_activity,sizeof(sys->ai_model_activity),"Recent Maya log");
                else text_copy(sys->ai_model_activity,sizeof(sys->ai_model_activity),"Idle / no recent log");
            }
        }
        bool compatible = config->log_parser==LOG_AUTO || config->log_parser==LOG_LLAMA ||
                          config->log_parser==LOG_BIONIC;
        if (compatible) ai_cache_parse_lines(start, &sys->ai_prompt_cache);
        AiLogEvent ordered[AI_LOG_MAX_EVENTS] = {{0}};
        int count = compatible ? ai_log_parse_lines(start, ordered, AI_LOG_MAX_EVENTS) : 0;
        sys->ai_log_event_count = count;
        for (int i = 0; i < count; i++) events[i] = ordered[count - 1 - i];
        /* A finished timing line is history, not proof of a live inference.
         * Prefer live /slots processing state; otherwise classify fresh logs as recent. */
        if (!sys->tokens.is_processing) {
            const char *activity = "Idle";
            if (now >= log_mtime && now - log_mtime <= 8 && count > 0) {
                activity = events[0].has_eval ? "Recent decoding" :
                           events[0].has_prompt ? "Recent prompt" : "Recent task";
            }
            snprintf(sys->ai_model_activity, sizeof(sys->ai_model_activity), "%s", activity);
        }
    }
    free(buffer);
    if (sys->ai_cache_process_limit_valid) {
        /* A different limit in the log suggests old startup records or a
         * previous backend. Never combine those stale usages with the new
         * server's current limit. */
        if (sys->ai_prompt_cache.limit_valid &&
            fabs(sys->ai_prompt_cache.limit_mib - sys->ai_cache_process_limit_mib) > 0.1)
            memset(&sys->ai_prompt_cache, 0, sizeof(sys->ai_prompt_cache));
        sys->ai_prompt_cache.limit_valid = true;
        sys->ai_prompt_cache.limit_mib = sys->ai_cache_process_limit_mib;
    }
}
static void sample_system(SystemTelemetry *s);

/* Worker-private state: no X11 state, GPU sysfs handle or tuning target. */
typedef struct {
    SystemTelemetry system;
    NvmeMonitor nvme;
    MayaLogMonitor maya;
    AiLogEvent events[AI_LOG_MAX_EVENTS];
    AiLogParseContext parser;
    time_t last_server_check, last_model_check;
    uint64_t source_generation;
    bool have_source_generation;
} BackgroundMonitor;

static void background_monitor_init(BackgroundMonitor *state, const NvmeMonitor *nvme) {
    memset(state, 0, sizeof(*state));
    state->parser.last_parser = LOG_COUNT;
    state->nvme = *nvme;
    state->system.fd_cpu_temp = -1;
    state->system.fd_cpu_tctl = -1;
    state->system.cpu_power_w = -1;
    state->system.cpu_temp_c = -1;
    state->system.cpu_tctl_c = -1;
    state->system.cpu_boost = -1;
    state->system.cpu_smt = -1;
}

/* The same backend can be selected again after A->B->A while a slow read
 * is in flight. The foreground rejects old generations, but its rejection
 * alone does not reset the worker's own token counters and AI log history.
 * Retain CPU sensor descriptors and CPU samples: only AI state belongs to
 * this generation. This helper is worker-private and never touches Xlib. */
static void background_monitor_reset_generation(BackgroundMonitor *state,
                                                uint64_t generation) {
    if (state->have_source_generation && state->source_generation == generation) return;
    state->source_generation = generation;
    state->have_source_generation = true;
    memset(&state->system.tokens, 0, sizeof(state->system.tokens));
    state->system.ai_model_activity[0] = '\0';
    state->system.ai_log_path[0] = '\0';
    state->system.ai_log_source[0] = '\0';
    state->system.ai_log_last_update = 0;
    state->system.ai_log_last_detect = 0;
    state->system.ai_log_event_count = 0;
    state->system.ai_cache_process_last_check = 0;
    state->system.ai_cache_process_limit_valid = false;
    memset(&state->system.ai_prompt_cache, 0, sizeof(state->system.ai_prompt_cache));
    memset(&state->maya, 0, sizeof(state->maya));
    memset(state->events, 0, sizeof(state->events));
    memset(&state->parser, 0, sizeof(state->parser));
    state->parser.last_parser = LOG_COUNT;
    state->last_model_check = state->last_server_check = 0;
}

/* All expensive CPU, /proc, NVMe, AI HTTP and AI-log I/O takes place here.
 * Only the worker accesses this private state. Never write to AMDGPU sysfs. */
static void background_monitor_sample(const MonitorInput *input, MonitorOutput *out, void *userdata) {
    BackgroundMonitor *state = userdata;
    background_monitor_reset_generation(state, input->generation);
    SystemTelemetry *sys = &state->system;
    time_t now = time(NULL);
    sample_system(sys);
    if (input->settings.ai_nvme_device[0] &&
        strcmp(state->nvme.selected, input->settings.ai_nvme_device) != 0)
        nvme_select(&state->nvme, input->settings.ai_nvme_device);
    nvme_sample(&state->nvme);
    sys->token_source = input->settings.token_source;
    sample_tokens(sys, input->gpu_power_w);
    parse_ai_log(sys, &input->settings, &state->maya, state->events, &state->parser);
    if (!state->last_server_check || now - state->last_server_check >= 10) {
        detect_ai_servers(sys);
        state->last_server_check = now;
    }
    if (!state->last_model_check || now - state->last_model_check >= 10) {
        detect_ai_model_info(sys);
        state->last_model_check = now;
    }
    out->system = *sys;
    /* The GUI receives values only, never usable worker-owned descriptor IDs. */
    monitor_sensors_detach_snapshot(&out->system);
    out->nvme = state->nvme;
    out->maya = state->maya;
    memcpy(out->events, state->events, sizeof(out->events));
}

/* Worker-private client history; never share the selected device's live
 * Sysfs file descriptors with another thread. Only DRM debug state pathname is
 * copied, which remains safe across GPU rebinding. */
typedef struct {
    ClientMonitor clients;
    SessionInfo session;
    int card_num;
    char pci_addr[64];
    CompositorSource compositor_source;
    uint64_t generation, last_session_ns;
} BackgroundDiscovery;

static void background_discovery_sample(const DiscoveryInput *in,
                                        DiscoveryOutput *out, void *userdata) {
    BackgroundDiscovery *ctx = userdata;
    bool changed = ctx->generation != in->generation ||
                   ctx->card_num != in->gpu.card_num ||
                   strcmp(ctx->pci_addr, in->gpu.pci_addr) != 0 ||
                   ctx->compositor_source != in->compositor_source;
    if (changed) {
        memset(&ctx->clients, 0, sizeof(ctx->clients));
        memset(&ctx->session, 0, sizeof(ctx->session));
        ctx->card_num = in->gpu.card_num;
        ctx->generation = in->generation;
        text_copy(ctx->pci_addr, sizeof(ctx->pci_addr), in->gpu.pci_addr);
        ctx->compositor_source = in->compositor_source;
        ctx->last_session_ns = 0;
    }
    uint64_t started = poll_timing_now_ns();
    client_monitor_sample(&in->gpu, &ctx->clients);
    out->clients_duration_ns = poll_timing_now_ns() - started;
    out->clients = ctx->clients;

    /* Package/KMS and compositor discovery have a slower independent cadence.
     * A GPU switch or compositor-source change forces rediscovery. */
    uint64_t now = poll_timing_now_ns();
    if (!ctx->last_session_ns || now - ctx->last_session_ns >= UINT64_C(10000000000)) {
        Sysfs readonly_sys = {0};
        text_copy(readonly_sys.drm_state_path, sizeof(readonly_sys.drm_state_path),
                  in->drm_state_path);
        started = poll_timing_now_ns();
        desktop_sample_session(&in->gpu, &readonly_sys, &ctx->session,
                               &ctx->clients, in->compositor_source);
        text_copy(ctx->session.window_manager, sizeof(ctx->session.window_manager),
                  in->window_manager);
        desktop_session_package_info(&ctx->session, in->xserver_vendor_release);
        out->session_duration_ns = poll_timing_now_ns() - started;
        ctx->last_session_ns = poll_timing_now_ns();
        out->session_valid = true;
        out->session = ctx->session;
    }
}

static DiscoveryInput discovery_input(const Gpu *gpu, const Sysfs *sys,
                                      const SessionInfo *session, int vendor_release,
                                      CompositorSource source, uint64_t generation) {
    DiscoveryInput input = {.gpu = *gpu, .compositor_source = source,
                            .generation = generation};
    text_copy(input.drm_state_path, sizeof(input.drm_state_path), sys->drm_state_path);
    text_copy(input.window_manager, sizeof(input.window_manager), session->window_manager);
    input.xserver_vendor_release = vendor_release;
    return input;
}

static void background_monitor_close(BackgroundMonitor *state) {
    monitor_sensors_close(&state->system);
}

static void background_monitor_apply(const MonitorOutput *out, SystemTelemetry *system) {
    *system = out->system; /* copied snapshot only; FDs remain worker-owned */
    g_nvme = out->nvme;
    g_maya = out->maya;
    memcpy(g_ai_log_events, out->events, sizeof(g_ai_log_events));
    text_copy(g_ai_log_path, sizeof(g_ai_log_path), system->ai_log_path);
}





typedef struct {
    unsigned device;
    const char *name;
} AmdGpuName;

/* Marketing names for common discrete cards. This is intentionally only a
   small exact-name table; unknown devices fall back to the system pci.ids
   database so the program remains useful on newer hardware. */
static const AmdGpuName AMD_GPU_NAMES[] = {
    {0x744c, "AMD Radeon RX 7900 XTX"},
    {0x7448, "AMD Radeon RX 7900 XTX"},
    {0x744e, "AMD Radeon RX 7900 XT"},
    {0x747e, "AMD Radeon RX 7800 XT"},
    {0x7480, "AMD Radeon RX 7700 XT"},
    {0x7483, "AMD Radeon RX 7600 XT"},
    {0x73bf, "AMD Radeon RX 6800 XT / RX 6800"},
    {0x73df, "AMD Radeon RX 6700 XT"},
    {0x73ff, "AMD Radeon RX 6900 XT"},
    {0x73ef, "AMD Radeon RX 6650 XT"},
    {0x1638, "AMD Radeon RX 6600 XT"},
    {0x731f, "AMD Radeon RX 5700 XT / RX 5700"},
    {0x687f, "AMD Radeon RX Vega 56 / Vega 64"},
    {0x66af, "AMD Radeon VII"},
};

static bool lookup_pci_ids_name(unsigned device, char *out, size_t cap) {
    static const char *paths[] = {
        "/usr/share/hwdata/pci.ids",
        "/usr/share/misc/pci.ids",
        "/usr/share/pci.ids"
    };
    FILE *f = NULL;
    for (size_t i = 0; i < sizeof(paths)/sizeof(paths[0]); i++) {
        f = fopen(paths[i], "r");
        if (f) break;
    }
    if (!f) return false;

    char line[512];
    bool in_amd = false;
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        if (line[0] != '\t' && !isspace((unsigned char)line[0])) {
            unsigned vendor = 0;
            if (sscanf(line, "%4x", &vendor) == 1) in_amd = (vendor == 0x1002);
            else in_amd = false;
            continue;
        }
        if (!in_amd || line[0] != '\t' || line[1] == '\t') continue;

        unsigned dev = 0;
        char name[400] = {0};
        if (sscanf(line + 1, "%4x %399[^\n]", &dev, name) == 2 && dev == device) {
            trim(name);
            snprintf(out, cap, "AMD %.150s", name);
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

static void resolve_gpu_name(Gpu *g) {
    char p[MAX_PATH_LEN], txt[256];
    unsigned device = (unsigned)strtoul(g->device_id, NULL, 0);

    /* Prefer our exact marketing-name table for known cards. A generic board
       product string such as "Navi 31" is less useful in the UI. */
    for (size_t i = 0; i < sizeof(AMD_GPU_NAMES)/sizeof(AMD_GPU_NAMES[0]); i++) {
        if (AMD_GPU_NAMES[i].device == device) {
            text_copy(g->full_name, sizeof(g->full_name), AMD_GPU_NAMES[i].name);
            return;
        }
    }

    /* Some boards/drivers expose a useful product string. */
    if (!path_join(p, sizeof(p), g->base, "product_name")) return;
    if (read_path_text(p, txt, sizeof(txt)) >= 0 && txt[0]) {
        text_copy(g->full_name, sizeof(g->full_name), txt);
        return;
    }

    if (lookup_pci_ids_name(device, g->full_name, sizeof(g->full_name))) return;
    snprintf(g->full_name, sizeof(g->full_name), "AMD Radeon GPU");
}

static int detect_amd_gpus(Gpu *out, int max) {
    int count = 0;
    for (int n = 0; n < 64 && count < max; n++) {
        char dev[MAX_PATH_LEN], vendor[MAX_PATH_LEN], txt[128];
        snprintf(dev, sizeof(dev), "/sys/class/drm/card%d/device", n);
        if (!path_join(vendor, sizeof(vendor), dev, "vendor")) continue;
        if (read_path_text(vendor, txt, sizeof(txt)) < 0) continue;
        if (strcmp(txt, "0x1002") != 0) continue;

        Gpu *g = &out[count++];
        memset(g, 0, sizeof(*g));
        g->card_num = n;
        snprintf(g->card_name, sizeof(g->card_name), "card%d", n);
        text_copy(g->base, sizeof(g->base), dev);

        char p[MAX_PATH_LEN];
        if (!path_join(p, sizeof(p), dev, "device")) continue;
        if (read_path_text(p, txt, sizeof(txt)) >= 0)
            text_copy(g->device_id, sizeof(g->device_id), txt);
        else
            snprintf(g->device_id, sizeof(g->device_id), "unknown");

        char *resolved = realpath(dev, NULL);
        if (resolved) {
            const char *last = strrchr(resolved, '/');
            text_copy(g->pci_addr, sizeof(g->pci_addr), last ? last + 1 : resolved);
            free(resolved);
        } else {
            snprintf(g->pci_addr, sizeof(g->pci_addr), "unknown");
        }

        if (!path_join(p, sizeof(p), dev, "mem_info_vram_total")) continue;
        if (read_path_text(p, txt, sizeof(txt)) >= 0)
            g->vram_bytes = strtoull(txt, NULL, 10);
        resolve_gpu_name(g);
    }
    return count;
}

static int choose_gpu(Gpu *gpus, int count, int requested_card) {
    if (count <= 0) return -1;
    if (requested_card >= 0) {
        for (int i = 0; i < count; i++) if (gpus[i].card_num == requested_card) return i;
        return -1;
    }
    if (count == 1) return 0;

    int best = 0;
    for (int i = 1; i < count; i++) {
        if (gpus[i].vram_bytes > gpus[best].vram_bytes) best = i;
    }
    return best;
}

/* The process and core CPU telemetry samplers are independent of GPU and X11. */
static void sample_top_processes(SystemTelemetry *s) {
    cpu_sample_top_processes(s->top_procs, &s->top_proc_count);
}
static void sample_cpu_cores(SystemTelemetry *s) {
    cpu_sample_core_load(&s->cpu_cores);
}

static void sample_cpu_clocks(SystemTelemetry *s) {
    double min_mhz=0.0, max_mhz=0.0, total_mhz=0.0, cap_mhz=0.0;
    int count=0;
    char path[MAX_PATH_LEN], base[128], label[64];
    for (int core=0; core<MAX_CPU_CORES; core++) {
        snprintf(base, sizeof(base), "/sys/devices/system/cpu/cpu%d/cpufreq", core);
        if (!path_join(path, sizeof(path), base, "scaling_cur_freq")) continue;
        long khz=read_optional_long_path(path, -1);
        if (khz <= 0) {
            if (!path_join(path, sizeof(path), base, "cpuinfo_cur_freq")) continue;
            khz=read_optional_long_path(path, -1);
        }
        if (khz <= 0) continue;
        double mhz=khz / 1000.0;
        if (!count || mhz < min_mhz) min_mhz=mhz;
        if (mhz > max_mhz) max_mhz=mhz;
        total_mhz+=mhz;
        count++;
        if (core == 0) {
            if (path_join(path, sizeof(path), base, "scaling_governor") &&
                read_path_text(path, label, sizeof(label)) >= 0)
                text_copy(s->cpu_governor, sizeof(s->cpu_governor), label);
        }
        if (path_join(path, sizeof(path), base, "cpuinfo_max_freq")) {
            long maxkhz=read_optional_long_path(path, -1);
            if (maxkhz > 0 && maxkhz / 1000.0 > cap_mhz) cap_mhz=maxkhz / 1000.0;
        }
    }
    s->cpu_clock_count=count;
    s->cpu_clock_min_mhz=count ? min_mhz : -1.0;
    s->cpu_clock_avg_mhz=count ? total_mhz/count : -1.0;
    s->cpu_clock_max_mhz=count ? max_mhz : -1.0;
    s->cpu_clock_cap_mhz=cap_mhz;
    /* cpufreq boost flag may be absent on modern amd-pstate systems. */
    long boost=read_optional_long_path("/sys/devices/system/cpu/cpufreq/boost", -1);
    s->cpu_boost=boost < 0 ? -1 : !!boost;
    long smt=read_optional_long_path("/sys/devices/system/cpu/smt/active", -1);
    s->cpu_smt=smt < 0 ? -1 : !!smt;
}

/* Resolve a sensor sibling without changing the saved pwm path.  Reading via
 * read_optional_long_path closes its descriptor before returning. */
static void sample_system(SystemTelemetry *s) {
    FILE *f = fopen("/proc/stat", "r");
    if (f) {
        char line[512];
        if (fgets(line, sizeof(line), f)) {
            unsigned long long user=0,nice=0,system=0,idle=0,iowait=0,irq=0,softirq=0,steal=0;
            int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                           &user,&nice,&system,&idle,&iowait,&irq,&softirq,&steal);
            if (n >= 4) {
                unsigned long long idle_all = idle + (n >= 5 ? iowait : 0);
                unsigned long long total = user + nice + system + idle_all + irq + softirq + steal;
                if (s->have_cpu_sample && total >= s->prev_cpu_total && idle_all >= s->prev_cpu_idle) {
                    unsigned long long dt = total - s->prev_cpu_total;
                    unsigned long long di = idle_all - s->prev_cpu_idle;
                    s->cpu_pct = (dt && di <= dt) ? (double)(dt - di) * 100.0 / (double)dt : 0.0;
                }
                s->prev_cpu_total = total;
                s->prev_cpu_idle = idle_all;
                s->have_cpu_sample = true;
            }
        }
        fclose(f);
    }

    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        unsigned long long total_kb = 0, avail_kb = 0;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "MemTotal: %llu kB", &total_kb) == 1) continue;
            if (sscanf(line, "MemAvailable: %llu kB", &avail_kb) == 1) continue;
        }
        fclose(f);
        if (total_kb > 0) {
            if (avail_kb > total_kb) avail_kb = total_kb;
            s->ram_total_bytes = total_kb * 1024ULL;
            s->ram_used_bytes = (total_kb - avail_kb) * 1024ULL;
            s->ram_pct = (double)s->ram_used_bytes * 100.0 / (double)s->ram_total_bytes;
        }
    }
    long threads = sysconf(_SC_NPROCESSORS_ONLN);
    s->cpu_threads = threads > 0 ? (unsigned)threads : 0U;

    discover_cpu_temp(s);
    long cpu_mc = read_long_fd(s->fd_cpu_temp, -1);
    s->cpu_temp_c = cpu_mc >= 0 ? cpu_mc / 1000.0 : -1.0;
    long tctl_mc = read_long_fd(s->fd_cpu_tctl, -1);
    s->cpu_tctl_c = tctl_mc >= 0 ? tctl_mc / 1000.0 : -1.0;

    /* CPU CCX temps */
    discover_cpu_ccx_temps(s);
    for (int i = 0; i < s->cpu_ccx_temp_count; i++) {
        long mc = read_long_fd(s->fd_cpu_ccx_temp[i], -1);
        s->cpu_ccx_temp_c[i] = mc >= 0 ? mc / 1000.0 : -1.0;
    }

    /* CPU fan */
    discover_cpu_fan(s);
    if (s->fd_cpu_fan_rpm >= 0) {
        long rpm = read_long_fd(s->fd_cpu_fan_rpm, -1);
        s->cpu_fan_rpm = rpm >= 0 ? (int)rpm : 0;
    }
    if (s->fd_cpu_fan_pct >= 0) {
        long pwm = read_long_fd(s->fd_cpu_fan_pct, -1);
        long pwm_max = read_cpu_fan_pwm_max(s->cpu_fan_pct_path);
        s->cpu_fan_pct = pwm >= 0 && pwm_max > 0 ? (double)pwm * 100.0 / (double)pwm_max : 0.0;
    }

    /* CPU power: never show zero watts when the sensor does not exist. */
    s->cpu_power_w=-1.0;
    discover_cpu_power(s);
    if (s->fd_cpu_power >= 0) {
        long uw = read_long_fd(s->fd_cpu_power, -1);
        s->cpu_power_w = uw >= 0 ? uw / 1000000.0 : -1.0;
    }

    /* Motherboard temps */
    discover_motherboard_temps(s);
    for (int i = 0; i < s->mb_temp_count; i++) {
        long mc = read_long_fd(s->mb_temp_fds[i], -1);
        s->mb_temp_c[i] = mc >= 0 ? mc / 1000.0 : -1.0;
    }

    /* Top processes */
    sample_top_processes(s);

    /* Per-core load is inexpensive and remains at 500 ms. CPU frequency
     * collection probes many sysfs files; keep it to a 2-second cadence. */
    sample_cpu_cores(s);
    struct timespec clock_now;
    clock_gettime(CLOCK_MONOTONIC, &clock_now);
    if ((!s->last_cpu_clock_scan.tv_sec && !s->last_cpu_clock_scan.tv_nsec) ||
        diff_ns(clock_now, s->last_cpu_clock_scan) >= 2000000000LL) {
        sample_cpu_clocks(s);
        s->last_cpu_clock_scan = clock_now;
    }
}


static const char *gfxoff_text(int status) {
    switch (status) {
        case 0: return "GFXOFF";
        case 1: return "exit transition";
        case 2: return "awake";
        case 3: return "entry transition";
        default: return "unavailable";
    }
}

/* ------------------------------------------------------------------------- */
/* X11 / XLibre GUI                                                           */
/* ------------------------------------------------------------------------- */

/* Dashboard pages consume immutable snapshots, never application globals. */
static void draw_dashboard(Gui *ui, const Gpu *g, const Sysfs *s, const Target *t,
                           const Stats *st, int interval_us, bool adaptive, bool monitor_clocks,
                           const ClientMonitor *cm, const PowerTelemetry *p,
                           const SessionInfo *si, const EventLog *el,
                           const TraceState *tr, const SystemTelemetry *sys) {
    (void)monitor_clocks; (void)si; (void)tr;
    if (g_workflow == WORKFLOW_GAMING) {
        ui_draw_gaming_dashboard(ui,g,s,p,sys);
        return;
    }
    const DiagEvent *recent = event_last(el);
    UiAiSources sources = {
        .settings = &g_settings, .maya = &g_maya, .nvme = &g_nvme,
        .log_events = g_ai_log_events,
        .event = {
            .kind = recent ? recent->kind : NULL,
            .detail = recent ? recent->detail : NULL,
            .actor = recent ? recent->actor : NULL,
            .actor_activity = recent ? recent->actor_activity : 0.0,
            .available = recent != NULL
        }
    };
    ui_draw_ai_dashboard(ui,g,t,st,interval_us,adaptive,cm,p,sys,&sources);
}

static void gui_draw(Gui *ui, const Gpu *g, Sysfs *s, const Target *t, const Stats *st,
                     int interval_us, int dpm_interval_us, bool monitor_clocks, bool adaptive,
                     const ClientMonitor *clients, const PowerTelemetry *power, const SystemTelemetry *system, const SessionInfo *session,
                     const EventLog *events, const TraceState *trace, const TuningState *tuning,
                     View view, const char *message) {
    gui_fill(ui,(Rect){0,0,ui->width,ui->height},ui->bg);
    ui_draw_header(ui,g,s,t,power,system,st,g_workflow);
    ui_draw_tabs(ui,view);
    ui->draw_offset_y = UI_CONTENT_TOP - UI_PAGE_Y - ui->scroll_y;
    XRectangle clip = {(short)0, (short)UI_CONTENT_TOP, (unsigned short)ui->width,
                       (unsigned short)gui_content_height(ui)};
    XSetClipRectangles(ui->dpy,ui->gc,0,0,&clip,1,Unsorted);
    ui->text_right = 0;
    switch(view) {
        case VIEW_DASHBOARD: draw_dashboard(ui,g,s,t,st,interval_us,adaptive,monitor_clocks,clients,power,session,events,trace,system); break;
        case VIEW_CLIENTS: ui_draw_clients_page(ui,clients); break;
        case VIEW_POWER: ui_draw_power_page(ui,t,st,power,s,system,monitor_clocks,dpm_interval_us); break;
        case VIEW_TUNING: ui_draw_tuning_page(ui,tuning); break;
        case VIEW_GPU_DATABASE: ui_draw_gpu_database_page(ui); break;
        case VIEW_DESKTOP: ui_draw_desktop_page(ui,session,g_compositor_names[g_settings.compositor_source]); break;
        case VIEW_EVENTS: ui_draw_events_page(ui,events); break;
        case VIEW_TRACE: ui_draw_trace_page(ui,trace); break;
        case VIEW_SETTINGS: {
            const UiSettingsView settings_view = {
                .gpu = g, .gpus = g_gpu_inventory, .gpu_count = g_gpu_inventory_count,
                .requested_gpu = g_requested_gpu, .settings = &g_settings,
                .nvme = &g_nvme, .parser_names = g_parser_names,
                .compositor_names = g_compositor_names, .editing_path = g_edit_ai_path,
                .edit_path_buffer = g_edit_path_buffer
            };
            ui_draw_settings_page(ui,&settings_view);
            break;
        }
        case VIEW_HELP: ui_draw_help_page(ui); break;
        case VIEW_ABOUT: ui_draw_about_page(ui,VERSION,__DATE__); break;
    }
    XSetClipMask(ui->dpy,ui->gc,None);
    ui->draw_offset_y = 0;
    ui->text_right = 0;
    if(message && *message) gui_text(ui,UI_MARGIN,ui->height-45,ui->warn,false,"%s",message);
    ui_draw_bottom_controls(ui,t,adaptive,monitor_clocks,trace,events);
    /* Subtle vertical scroll thumb; content continues below when this is visible. */
    int max_scroll = gui_max_scroll(ui, view, g_workflow);
    if (max_scroll > 0) {
        int track_h = gui_content_height(ui) - 10;
        int thumb_h = (track_h * gui_content_height(ui)) /
                      (gui_content_height(ui) + max_scroll);
        if (thumb_h < 24) thumb_h = 24;
        int thumb_y = UI_CONTENT_TOP + 5 +
                      (track_h - thumb_h) * ui->scroll_y / max_scroll;
        gui_fill(ui,(Rect){ui->width-6,UI_CONTENT_TOP+5,3,track_h},ui->panel_alt);
        gui_fill(ui,(Rect){ui->width-6,thumb_y,3,thumb_h},ui->accent);
    }
    XFlush(ui->dpy);
    ui->dirty=false;
}

static int next_interval(int current, bool faster) {
    static const int vals[] = {500,1000,2000,5000,10000,20000,50000,100000,250000,500000,1000000};
    int n=(int)(sizeof(vals)/sizeof(vals[0])), nearest=0;
    long best=labs((long)current-vals[0]);
    for(int i=1;i<n;i++){long d=labs((long)current-vals[i]);if(d<best){best=d;nearest=i;}}
    if(faster&&nearest>0)nearest--; else if(!faster&&nearest<n-1)nearest++;
    return vals[nearest];
}

static int next_dpm_interval(int current) {
    static const int vals[] = {50000,100000,250000,500000,1000000,2000000};
    int n=(int)(sizeof(vals)/sizeof(vals[0]));
    for(int i=0;i<n;i++) if(vals[i]==current) return vals[(i+1)%n];
    return DEFAULT_DPM_INTERVAL_US;
}

static int benchmark_reads(Sysfs *s, int duration_ms, int interval_us,
                           double *cpu_pct, double *actual_hz, double *avg_read_us) {
    struct timespec wall0,wall1,cpu0,cpu1,next;
    clock_gettime(CLOCK_MONOTONIC,&wall0); clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu0); next=wall0;
    uint64_t loops=0; long long read_cpu_ns=0; char buf[256];
    long long end_ns=ts_ns(wall0)+(long long)duration_ms*1000000LL;
    while(1){
        struct timespec r0,r1,now; clock_gettime(CLOCK_THREAD_CPUTIME_ID,&r0);
        if(read_fd_text(s->fd_policy_r,buf,sizeof(buf))<0)return -1;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID,&r1); read_cpu_ns+=diff_ns(r1,r0); loops++;
        add_ns(&next,(long long)interval_us*1000LL); clock_gettime(CLOCK_MONOTONIC,&now);
        if(ts_ns(now)>=end_ns)break;
        if(ts_ns(next)>ts_ns(now))clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&next,NULL);else next=now;
    }
    clock_gettime(CLOCK_MONOTONIC,&wall1); clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu1);
    double wall=diff_ns(wall1,wall0)/1e9,cpu=diff_ns(cpu1,cpu0)/1e9;
    *cpu_pct=wall>0?cpu/wall*100.0:0.0; *actual_hz=wall>0?loops/wall:0.0; *avg_read_us=loops?(read_cpu_ns/1000.0)/loops:0.0;
    return 0;
}

static void run_benchmark_cli(Sysfs *s) {
    static const int intervals[]={20000,5000,2000,1000,500};
    printf("Policy-only amdgpu sysfs benchmark\n\n");
    printf(" interval      target Hz     actual Hz     thread CPU     read CPU/check\n");
    printf(" ----------------------------------------------------------------------\n");
    for(size_t i=0;i<sizeof(intervals)/sizeof(intervals[0]);i++){
        double cpu=0,hz=0,rus=0;
        if(benchmark_reads(s,350,intervals[i],&cpu,&hz,&rus)<0){printf(" %7d us      ERROR\n",intervals[i]);break;}
        printf(" %7d us   %10.0f   %10.1f     %7.3f%%      %8.3f us\n",intervals[i],1000000.0/intervals[i],hz,cpu,rus);
    }
}

static int parse_int_arg(const char *s, int minv, int maxv, int *out) {
    if(!s||!*s)return -1;
    errno=0; char *end=NULL; long v=strtol(s,&end,10);
    if(errno||!end||*end||v<minv||v>maxv)return -1;
    *out=(int)v; return 0;
}

static void usage(const char *argv0) {
    printf("AMD GPU Performance and Diagnostic Tool %s - X11/XLibre GUI\n",VERSION);
    printf("Usage: %s [--card N] [--interval-us N] [--dpm-interval-us N] [--mode MODE]\n",argv0);
    printf("          [--monitor-dpm] [--fixed] [--unsafe-fast] [--normal-priority] [--benchmark]\n");
    printf("Modes: auto low high manual profile_standard profile_min_sclk profile_min_mclk profile_peak\n");
    printf("Launch GUI through sudo with X11 credentials preserved: sudo -E %s\n",argv0);
}

static void handle_action(char key, Sysfs *s, Target *target, int *interval_us, int *dpm_interval_us,
                          bool *monitor_clocks, bool *adaptive, long long *burst_until_ns,
                          TraceState *trace, EventLog *events, View *view, char *message, size_t message_cap) {
    message[0]='\0';
    if(key>='1'&&key<='8') set_mode(s,target,key-'1',message,message_cap);
    else if(key=='0') *view=VIEW_DASHBOARD;
    else if(key=='p'||key=='P') *view=VIEW_CLIENTS;
    else if(key=='g'||key=='G') *view=VIEW_POWER;
    else if(key=='u'||key=='U') *view=VIEW_TUNING;
    else if(key=='w'||key=='W') *view=VIEW_DESKTOP;
    else if(key=='e'||key=='E') *view=VIEW_EVENTS;
    else if(key=='r'||key=='R') *view=VIEW_TRACE;
    else if(key=='h'||key=='H'||key=='?') *view=VIEW_HELP;
    else if(key=='f'||key=='F') *view=VIEW_SETTINGS;
    else if(key=='i'||key=='I') *view=VIEW_ABOUT;
    else if(key==' '){target->enabled=!target->enabled;snprintf(message,message_cap,"Watchdog %s",target->enabled?"enabled":"paused");}
    else if(key=='a'||key=='A'){*adaptive=!*adaptive;*burst_until_ns=0;snprintf(message,message_cap,"Adaptive correction burst %s",*adaptive?"enabled":"disabled");}
    else if(key=='c'||key=='C'){*monitor_clocks=!*monitor_clocks;snprintf(message,message_cap,"DPM clock monitoring %s",*monitor_clocks?"enabled":"disabled");}
    else if(key=='+'){*interval_us=next_interval(*interval_us,true);snprintf(message,message_cap,"Watchdog interval %d us",*interval_us);}
    else if(key=='-'){*interval_us=next_interval(*interval_us,false);snprintf(message,message_cap,"Watchdog interval %d us",*interval_us);}
    else if(key=='d'||key=='D'){*dpm_interval_us=next_dpm_interval(*dpm_interval_us);snprintf(message,message_cap,"DPM interval %d us",*dpm_interval_us);}
    else if(key=='s'||key=='S'){(void)cycle_manual_level(s,target,true,message,message_cap);*monitor_clocks=true;}
    else if(key=='m'||key=='M'){(void)cycle_manual_level(s,target,false,message,message_cap);*monitor_clocks=true;}
    else if(key=='x'||key=='X'){target->lock_sclk=target->lock_mclk=false;snprintf(message,message_cap,"Manual clock locks cleared");}
    else if(key=='l'||key=='L'){toggle_event_logging(events);snprintf(message,message_cap,"CSV logging %s",events->logging?"enabled":"disabled");}
    else if(key=='t'||key=='T'){
        if(trace->active){trace_stop(trace);snprintf(message,message_cap,"Kernel trace stopped");}
        else if(trace_start(trace)==0)snprintf(message,message_cap,"Kernel trace started");
        else snprintf(message,message_cap,"Kernel trace unavailable or failed to start");
    }
}

static bool tuning_row_available(const TuningState *t, int row) {
    switch (row) {
        case 0: return t->has_sclk || t->has_sclk_offset;
        case 1: return t->has_mclk;
        case 2: return t->has_vdd_offset;
        case 3: return t->has_power_cap;
        case 4: return t->has_fan;
        default: return false;
    }
}

/* Launch viewer without a root shell or shell interpolation of filenames. */
static bool open_ai_log_viewer(const char *path) {
    if (!path || path[0] != '/') return false;
    pid_t starter = fork();
    if (starter < 0) return false;
    if (starter == 0) {
        pid_t viewer = fork();
        if (viewer < 0) _exit(127);
        if (viewer > 0) _exit(0); /* double fork: no retained GUI zombie */
        const char *sudo_user = getenv("SUDO_USER");
        if (geteuid() == 0) {
            /* Never launch xdg-open with root privileges. Require a genuine
             * invoking-user identity, even on a direct-root invocation. */
            if (!sudo_user || !*sudo_user) _exit(126);
            struct passwd *pw = getpwnam(sudo_user);
            if (!pw || pw->pw_uid == 0 || setgroups(0, NULL) != 0 ||
                setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) _exit(126);
            (void)setenv("HOME", pw->pw_dir, 1);
        }
        execlp("xdg-open", "xdg-open", path, (char *)NULL);
        _exit(127);
    }
    (void)waitpid(starter, NULL, 0);
    return true;
}

static void handle_click(Gui *ui, int x, int y, Sysfs *s, Target *target, TuningState *tuning,
                         int *interval_us, int *dpm_interval_us, bool *monitor_clocks, bool *adaptive,
                         long long *burst_until_ns, TraceState *trace, EventLog *events,
                         View *view, char *message, size_t message_cap) {
    /* Workflow mode buttons */
    if(point_in(workflow_rect(ui, 0), x, y)) {
        g_workflow = WORKFLOW_AI;
        ui->scroll_y = 0;
        snprintf(message, message_cap, "AI Workflow mode");
        return;
    }
    if(point_in(workflow_rect(ui, 1), x, y)) {
        g_workflow = WORKFLOW_GAMING;
        ui->scroll_y = 0;
        snprintf(message, message_cap, "Gaming Workflow mode");
        return;
    }

    if(*view==VIEW_SETTINGS && y>=UI_CONTENT_TOP && y<gui_content_bottom(ui)) {
        int ly=y-(UI_CONTENT_TOP-UI_PAGE_Y-ui->scroll_y);
        if(point_in(settings_option_rect(ui,0),x,ly)) {
            g_settings.token_source=(TokenSource)((g_settings.token_source+1)%TOKEN_SOURCE_COUNT);
            g_token_source=g_settings.token_source;
        } else if(point_in(settings_option_rect(ui,1),x,ly)) {
            g_settings.log_parser=(LogParser)((g_settings.log_parser+1)%LOG_COUNT);
        } else if(point_in(settings_option_rect(ui,2),x,ly)) {
            g_settings.ai_log_manual=!g_settings.ai_log_manual;
        } else if(point_in(settings_path_rect(ui),x,ly)) {
            g_edit_ai_path=true;
            text_copy(g_edit_path_buffer,sizeof(g_edit_path_buffer),g_settings.ai_log_file);
            snprintf(message,message_cap,"Type an absolute path; Enter saves, Esc cancels.");
            return;
        } else if(point_in(settings_option_rect(ui,4),x,ly)) {
            if(g_nvme.device_count) {
                int idx=-1;
                for(int i=0;i<g_nvme.device_count;i++) if(!strcmp(g_nvme.selected,g_nvme.devices[i].name))idx=i;
                nvme_select(&g_nvme,g_nvme.devices[(idx+1)%g_nvme.device_count].name);
                text_copy(g_settings.ai_nvme_device,sizeof(g_settings.ai_nvme_device),g_nvme.selected);
            }
        } else if(point_in(settings_option_rect(ui,5),x,ly)) {
            g_settings.compositor_source=(CompositorSource)((g_settings.compositor_source+1)%COMP_COUNT);
        } else if(point_in(settings_option_rect(ui,6),x,ly)) {
            int pos=0;
            for(int i=0;i<g_gpu_inventory_count;i++) if(g_gpu_inventory[i].card_num==g_requested_gpu) pos=i;
            if(g_gpu_inventory_count>0) g_requested_gpu=g_gpu_inventory[(pos+1)%g_gpu_inventory_count].card_num;
        } else if(point_in(settings_unlock_rect(ui),x,ly)) {
            snprintf(message,message_cap,"Overclock unlock is not implemented in v0.7.0b (safe placeholder).");
            return;
        } else if(point_in(settings_gpu_switch_rect(ui),x,ly)) {
            g_gpu_switch_requested=true;
            snprintf(message,message_cap,"GPU switch requested; tuning will be disarmed.");
            return;
        } else return;
        if(settings_save()) snprintf(message,message_cap,"Settings saved.");
        else snprintf(message,message_cap,"Could not save settings to user config directory.");
        return;
    }

    /* View AI log button */
    if (g_ai_log_path[0] != 0 && *view == VIEW_DASHBOARD && g_workflow == WORKFLOW_AI) {
        Rect log_btn = ai_log_button_rect(ui);
        int content_y = y - (UI_CONTENT_TOP - UI_PAGE_Y - ui->scroll_y);
        if (y >= UI_CONTENT_TOP && y < gui_content_bottom(ui) && point_in(log_btn, x, content_y)) {
            snprintf(message, message_cap, "%s", open_ai_log_viewer(g_ai_log_path) ?
                     "Opening AI log" : "Unable to start AI log viewer");
            return;
        }
    }

    if(point_in(help_rect(ui),x,y)) { *view=VIEW_HELP; ui->scroll_y=0; message[0]=0; return; }
    for(int i=0;i<9;i++) {
        if(point_in(tab_rect(ui,i),x,y)) {
            *view=tab_view(i);
            ui->scroll_y=0;
            message[0]=0;
            return;
        }
    }
    for(int i=0;i<8;i++) {
        if(point_in(mode_rect(ui,i),x,y)) {
            set_mode(s,target,i,message,message_cap);
            return;
        }
    }

    if (*view == VIEW_GPU_DATABASE && y >= UI_CONTENT_TOP && y < gui_content_bottom(ui)) {
        int logical_y = y - (UI_CONTENT_TOP - UI_PAGE_Y - ui->scroll_y);
        if (point_in(gpu_db_back_rect(ui), x, logical_y)) {
            *view = VIEW_TUNING;
            ui->scroll_y = 0;
            message[0] = '\0';
        }
        return;
    }

    if (*view == VIEW_TUNING && y >= UI_CONTENT_TOP && y < gui_content_bottom(ui)) {
        int logical_y = y - (UI_CONTENT_TOP - UI_PAGE_Y - ui->scroll_y);
        if (point_in(gpu_db_button_rect(ui),x,logical_y)) {
            *view = VIEW_GPU_DATABASE;
            ui->scroll_y = 0;
            message[0] = '\0';
            return;
        }
        for (int row=0; row<5; row++) {
            if (point_in(tuning_minus_rect(ui,row),x,logical_y) || point_in(tuning_plus_rect(ui,row),x,logical_y)) {
                if (!tuning_row_available(tuning,row)) {
                    snprintf(message,message_cap,"That tuning control is not exposed by this ASIC/driver.");
                    return;
                }
                int dir=point_in(tuning_minus_rect(ui,row),x,logical_y)?-1:+1;
                tuning_adjust(tuning,row,dir);
                snprintf(message,message_cap,"Tuning edit changed; ARM + APPLY required to write.");
                return;
            }
        }
        if (point_in(tuning_action_rect(ui,0),x,logical_y)) {
            tuning->armed = !tuning->armed;
            snprintf(message,message_cap,"Tuning writes %s.",tuning->armed?"ARMED":"disarmed");
            return;
        }
        if (point_in(tuning_action_rect(ui,1),x,logical_y)) {
            (void)tuning_apply(s,target,tuning,message,message_cap);
            return;
        }
        if (point_in(tuning_action_rect(ui,2),x,logical_y)) {
            (void)tuning_reset(s,target,tuning,message,message_cap);
            return;
        }
        if (point_in(tuning_action_rect(ui,3),x,logical_y)) {
            tuning_refresh(s,tuning,false);
            snprintf(message,message_cap,"Tuning state refreshed from driver.");
            return;
        }
    }

    for(int i=0;i<5;i++) if(point_in(control_rect(ui,i),x,y)){
        char key=i==0?' ':i==1?'A':i==2?'C':i==3?'T':'L';
        handle_action(key,s,target,interval_us,dpm_interval_us,monitor_clocks,adaptive,burst_until_ns,trace,events,view,message,message_cap);
        return;
    }
}

int main(int argc, char **argv) {
    int requested_card=-1, interval_us=DEFAULT_INTERVAL_US, dpm_interval_us=DEFAULT_DPM_INTERVAL_US;
    bool monitor_clocks=false, adaptive=true, unsafe_fast=false, lower_priority=true, bench_at_start=false;
    Target target={.policy="profile_standard",.enabled=true,.lock_sclk=false,.lock_mclk=false,.sclk_level=0,.mclk_level=0};
    settings_load();
    if(g_settings.ai_nvme_device[0]) nvme_select(&g_nvme,g_settings.ai_nvme_device);
    nvme_discover(&g_nvme);
    requested_card=g_settings.preferred_card;

    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--help")==0||strcmp(argv[i],"-h")==0){usage(argv[0]);return 0;}
        if(strcmp(argv[i],"--version")==0){printf("%s\n",VERSION);return 0;}
        if(strcmp(argv[i],"--card")==0){if(++i>=argc||parse_int_arg(argv[i],0,255,&requested_card)<0){fprintf(stderr,"Invalid --card\n");return 2;}}
        else if(strcmp(argv[i],"--interval-us")==0){if(++i>=argc||parse_int_arg(argv[i],1,MAX_INTERVAL_US,&interval_us)<0){fprintf(stderr,"Invalid --interval-us\n");return 2;}}
        else if(strcmp(argv[i],"--dpm-interval-us")==0){if(++i>=argc||parse_int_arg(argv[i],1,MAX_DPM_INTERVAL_US,&dpm_interval_us)<0){fprintf(stderr,"Invalid --dpm-interval-us\n");return 2;}}
        else if(strcmp(argv[i],"--mode")==0){if(++i>=argc||!valid_policy(argv[i])){fprintf(stderr,"Invalid --mode\n");return 2;}snprintf(target.policy,sizeof(target.policy),"%s",argv[i]);}
        else if(strcmp(argv[i],"--monitor-dpm")==0)monitor_clocks=true;
        else if(strcmp(argv[i],"--policy-only")==0)monitor_clocks=false;
        else if(strcmp(argv[i],"--fixed")==0)adaptive=false;
        else if(strcmp(argv[i],"--unsafe-fast")==0)unsafe_fast=true;
        else if(strcmp(argv[i],"--normal-priority")==0)lower_priority=false;
        else if(strcmp(argv[i],"--benchmark")==0)bench_at_start=true;
        else if(argv[i][0]=='-'){fprintf(stderr,"Unknown option: %s\n",argv[i]);usage(argv[0]);return 2;}
    }
    int min_interval=unsafe_fast?UNSAFE_MIN_INTERVAL_US:MIN_INTERVAL_US;
    if(interval_us<min_interval)interval_us=min_interval;
    if(dpm_interval_us<MIN_DPM_INTERVAL_US)dpm_interval_us=MIN_DPM_INTERVAL_US;

    if(geteuid()!=0){
        fprintf(stderr,"This build must run as root to write AMDGPU power controls.\n");
        fprintf(stderr,"On X11/XLibre use: sudo -E %s\n",argv[0]);
        return 1;
    }
    if(lower_priority)(void)setpriority(PRIO_PROCESS,0,10);
    struct sigaction sa={0}; sa.sa_handler=on_signal; sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL); sigaction(SIGTERM,&sa,NULL); sigaction(SIGHUP,&sa,NULL);

    Gpu gpus[16]; int ngpu=detect_amd_gpus(gpus,16);
    g_gpu_inventory_count=ngpu>0?ngpu:0;
    if(ngpu>0) memcpy(g_gpu_inventory,gpus,(size_t)ngpu*sizeof(Gpu));
    if(ngpu<=0){fprintf(stderr,"No AMD DRM GPUs found.\n");return 1;}
    int gi=choose_gpu(gpus,ngpu,requested_card);
    if(gi<0){fprintf(stderr,"Requested AMD GPU not found.\n");return 1;}
    Gpu *g=&gpus[gi]; Sysfs s;
    g_requested_gpu=g->card_num;
    if(sysfs_open(g,&s)<0){fprintf(stderr,"Cannot open AMDGPU controls: %s\n",strerror(errno));return 1;}
    if(apply_policy(&s,target.policy)<0){fprintf(stderr,"Cannot set initial policy %s: %s\n",target.policy,strerror(errno));sysfs_close(&s);return 1;}
    if(bench_at_start){run_benchmark_cli(&s);sysfs_close(&s);return 0;}

    Stats st={0}; st.last_active_sclk=st.last_active_mclk=INT_MIN;
    ClientMonitor clients={0};
    PowerTelemetry power={.gpu_busy=-1,.mem_busy=-1,.temp_c=-1,.power_w=-1,.power_cap_w=-1,.power_cap_min_w=-1,.power_cap_max_w=-1,
                          .voltage_v=-1,.current_a=-1,.equivalent_input_ohm=-1,.fan_rpm=-1,.fan_pct=-1,
                          .gfx_hz=-1,.mem_hz=-1,.gfxoff_enabled=-1,.gfxoff_status=-1,.last_gfxoff_status=-1};
    for (int i=0;i<MAX_GPU_TEMPS;i++) power.temps_c[i]=-1.0;
    SystemTelemetry system={0};
    system.cpu_power_w=-1;
    system.fd_cpu_temp=-1;
    system.fd_cpu_tctl=-1;
    system.cpu_tctl_c=-1;
    system.cpu_boost=-1;
    system.cpu_smt=-1;
    system.cpu_temp_c=-1.0;
    system.cpu_temp_crit_mc=-1;
    TuningState tuning={0};
    tuning.spec=gpu_spec_match(g->full_name,g->vram_bytes);
    SessionInfo session={0}; EventLog events={0}; TraceState trace; trace_discover(&trace);

    /* Initialize curl and detect AI servers */
    curl_global_init(CURL_GLOBAL_DEFAULT);
    system.token_source = g_token_source;
    /* Worker owns CPU sensor descriptors and AI backend state. */
    gpu_sample_power(&s,&power);
    /* Client and desktop scans begin asynchronously after the window opens. */
    tuning_refresh(&s,&tuning,true);

    Gui ui;
    if(ui_window_open(&ui,&session,VERSION,g_settings.compositor_source)<0){sysfs_close(&s);return 1;}
    ui_window_probe(&ui,&session,g_settings.compositor_source);
    View view=VIEW_DASHBOARD; char message[256]="";

    struct timespec next,next_dpm,next_clients,next_power,next_system,last_ui,stat_wall0,stat_cpu0;
    clock_gettime(CLOCK_MONOTONIC,&next); next_dpm=next; add_ns(&next_dpm,(long long)dpm_interval_us*1000LL);
    next_clients=next;add_ns(&next_clients,CLIENT_SAMPLE_NS); next_power=next;add_ns(&next_power,POWER_SAMPLE_NS);
    next_system=next;add_ns(&next_system,SYSTEM_SAMPLE_NS);
    last_ui=next; stat_wall0=next; clock_gettime(CLOCK_THREAD_CPUTIME_ID,&stat_cpu0);
    uint64_t stat_checks0=0; long long burst_until_ns=0; int active_interval_us=interval_us;

    BackgroundMonitor background;
    background_monitor_init(&background, &g_nvme);
    MonitorWorker worker;
    bool worker_started = monitor_worker_start(&worker, background_monitor_sample, &background);
    uint64_t monitor_generation = 1;
    MonitorInput initial_input = {.settings = g_settings, .gpu_power_w = power.power_w,
                                  .generation = monitor_generation};
    if (worker_started) monitor_worker_request(&worker, &initial_input);
    else fprintf(stderr, "warning: telemetry worker unavailable; using synchronous fallback\n");
    BackgroundDiscovery discovery_context = {.card_num = -1};
    DiscoveryWorker discovery_worker;
    bool discovery_started = discovery_worker_start(&discovery_worker,
                                                    background_discovery_sample,
                                                    &discovery_context);
    uint64_t last_fallback_session_ns = 0;
    uint64_t discovery_generation = 1;
    CompositorSource last_discovery_source = g_settings.compositor_source;
    if (discovery_started) {
        DiscoveryInput request = discovery_input(g, &s, &session, VendorRelease(ui.dpy),
                                                 g_settings.compositor_source, discovery_generation);
        discovery_worker_request(&discovery_worker, &request);
    } else {
        fprintf(stderr, "warning: GPU client/session discovery worker unavailable; synchronous fallback active\n");
    }
    uint64_t last_monitor_finished_ns = 0;
    AppSettings visible_source_settings = g_settings;
    bool monitor_data_stale = false;

    PollTiming poll_timing={0};
    const char *timing_flag=getenv("AMD_GPU_DIAG_TIMING");
    bool collect_timing=timing_flag && strcmp(timing_flag,"1")==0;
    while(!g_stop){
        if (collect_timing)
            poll_timing_watchdog_tick(&poll_timing, poll_timing_now_ns(),
                                     (uint64_t)active_interval_us * 1000ULL);
        struct timespec loop_now; clock_gettime(CLOCK_MONOTONIC,&loop_now);
        int enforce_rc=target.enabled?enforce_policy_once(&s,&target,&st):0;
        clock_gettime(CLOCK_MONOTONIC,&loop_now);
        if(adaptive&&target.enabled&&enforce_rc>0){if(ts_ns(loop_now)>=burst_until_ns)st.burst_entries++;burst_until_ns=ts_ns(loop_now)+ADAPTIVE_BURST_NS;}
        if(enforce_rc>0){char d[160];snprintf(d,sizeof(d),"restored target %s",target.policy);event_log_add(&events,"POLICY RESET",d,&clients,&trace);ui.dirty=true;}
        bool burst_active=adaptive&&target.enabled&&ts_ns(loop_now)<burst_until_ns;
        active_interval_us=burst_active?ADAPTIVE_BURST_INTERVAL_US:interval_us;

        if(monitor_clocks&&diff_ns(loop_now,next_dpm)>=0){
            uint64_t pm=collect_timing?poll_timing_now_ns():0;
            uint64_t os=st.observed_sclk_changes,om=st.observed_mclk_changes; monitor_dpm_once(&s,&target,&st,target.enabled);
            if(st.observed_sclk_changes!=os||st.observed_mclk_changes!=om){char d[180];snprintf(d,sizeof(d),"SCLK %.78s | MCLK %.78s",st.sclk_display,st.mclk_display);event_log_add(&events,"DPM CHANGE",d,&clients,&trace);}
            if(collect_timing)poll_timing_record(&poll_timing,POLL_DPM,poll_timing_now_ns()-pm);
            next_dpm=loop_now;add_ns(&next_dpm,(long long)dpm_interval_us*1000LL);
        }
        if (diff_ns(loop_now, next_clients) >= 0 ||
            last_discovery_source != g_settings.compositor_source) {
            bool source_changed = last_discovery_source != g_settings.compositor_source;
            last_discovery_source = g_settings.compositor_source;
            /* Invalidate an in-flight A->B->A scan; matching only its final
             * compositor value would otherwise admit old desktop data. */
            if (source_changed) discovery_generation++;
            if (discovery_started) {
                DiscoveryInput request = discovery_input(g, &s, &session,
                    VendorRelease(ui.dpy), g_settings.compositor_source, discovery_generation);
                discovery_worker_request(&discovery_worker, &request);
            } else {
                uint64_t pm = collect_timing ? poll_timing_now_ns() : 0;
                client_monitor_sample(g, &clients);
                if (collect_timing) poll_timing_record(&poll_timing, POLL_CLIENTS,
                    poll_timing_now_ns() - pm);
                if (source_changed || !last_fallback_session_ns ||
                    poll_timing_now_ns() - last_fallback_session_ns >= UINT64_C(10000000000)) {
                    pm = collect_timing ? poll_timing_now_ns() : 0;
                    desktop_sample_session(g, &s, &session, &clients, g_settings.compositor_source);
                    ui_window_probe(&ui, &session, g_settings.compositor_source);
                    desktop_session_package_info(&session, VendorRelease(ui.dpy));
                    if (collect_timing) poll_timing_record(&poll_timing, POLL_SESSION,
                        poll_timing_now_ns() - pm);
                    last_fallback_session_ns = poll_timing_now_ns();
                }
                ui.dirty = true;
            }
            next_clients = loop_now;
            add_ns(&next_clients, CLIENT_SAMPLE_NS);
        }
        if (discovery_started) {
            DiscoveryOutput result;
            if (discovery_worker_take(&discovery_worker, &result)) {
                DiscoveryInput expected = discovery_input(g, &s, &session,
                    VendorRelease(ui.dpy), g_settings.compositor_source, discovery_generation);
                if (discovery_result_matches(&result, &expected)) {
                    clients = result.clients;
                    if (result.session_valid) {
                        session = result.session;
                        ui_window_probe(&ui, &session, g_settings.compositor_source);
                    }
                    if (collect_timing) {
                        poll_timing_record(&poll_timing, POLL_CLIENTS, result.clients_duration_ns);
                        if (result.session_valid)
                            poll_timing_record(&poll_timing, POLL_SESSION, result.session_duration_ns);
                    }
                    ui.dirty = true;
                }
            }
        }
        if(diff_ns(loop_now,next_power)>=0){
            uint64_t pm=collect_timing?poll_timing_now_ns():0;
            uint64_t oldgo=power.gfxoff_status_changes;double oldpw=power.power_w;gpu_sample_power(&s,&power);
            if(power.gfxoff_status_changes!=oldgo){char d[128];snprintf(d,sizeof(d),"status %s",gfxoff_text(power.gfxoff_status));event_log_add(&events,"GFXOFF",d,&clients,&trace);}
            if(oldpw>=0&&power.power_w>=0&&fabs(power.power_w-oldpw)>30.0){char d[128];snprintf(d,sizeof(d),"%.1f W -> %.1f W",oldpw,power.power_w);event_log_add(&events,"POWER STEP",d,&clients,&trace);}
            if(collect_timing)poll_timing_record(&poll_timing,POLL_POWER,poll_timing_now_ns()-pm);
            next_power=loop_now;add_ns(&next_power,POWER_SAMPLE_NS);ui.dirty=true;
        }
        if (diff_ns(loop_now, next_system) >= 0) {
            MonitorInput request = {.settings = g_settings, .gpu_power_w = power.power_w,
                                    .generation = monitor_generation};
            if (worker_started) monitor_worker_request(&worker, &request);
            else {
                /* Explicit degraded mode: still safe, but not watchdog-isolated. */
                MonitorOutput result = {0};
                uint64_t began = poll_timing_now_ns();
                background_monitor_sample(&request, &result, &background);
                result.duration_ns = poll_timing_now_ns() - began;
                result.finished_ns = poll_timing_now_ns();
                if (collect_timing) poll_timing_record(&poll_timing, POLL_SYSTEM_AI, result.duration_ns);
                background_monitor_apply(&result, &system);
                last_monitor_finished_ns = result.finished_ns;
                monitor_data_stale = false;
                ui.dirty = true;
            }
            next_system = loop_now;
            add_ns(&next_system, SYSTEM_SAMPLE_NS);
        }
        if (worker_started) {
            MonitorOutput result;
            MonitorInput expected = {.settings = g_settings,
                                     .generation = monitor_generation};
            if (monitor_worker_take(&worker, &result) &&
                monitor_result_matches(&result, &expected)) {
                background_monitor_apply(&result, &system);
                last_monitor_finished_ns = result.finished_ns;
                monitor_data_stale = false;
                if (collect_timing) poll_timing_record(&poll_timing, POLL_SYSTEM_AI, result.duration_ns);
                ui.dirty = true;
            }
            /* Never display an old token rate as if the backend is live. */
            if (!monitor_data_stale && last_monitor_finished_ns &&
                poll_timing_now_ns() - last_monitor_finished_ns > UINT64_C(5000000000)) {
                monitor_data_stale = true;
                system.tokens.has_data = false;
                system.tokens.is_processing = false;
                system.tokens.tokens_per_sec = 0;
                text_copy(system.ai_model_activity, sizeof(system.ai_model_activity), "Monitor delayed");
                ui.dirty = true;
            }
        }

        uint64_t pm_trace=collect_timing?poll_timing_now_ns():0;
        trace_pump(&trace);
        if(collect_timing)poll_timing_record(&poll_timing,POLL_TRACE,poll_timing_now_ns()-pm_trace);

        uint64_t pm_input=collect_timing?poll_timing_now_ns():0;
        for(int handled=0;handled<UI_EVENT_BATCH_LIMIT && XPending(ui.dpy)>0;handled++){
            XEvent ev;XNextEvent(ui.dpy,&ev);
            if(ev.type==Expose){ui.dirty=true;}
            else if(ev.type==ConfigureNotify){
                ui.width=ev.xconfigure.width;ui.height=ev.xconfigure.height;
                gui_scroll(&ui,view,g_workflow,0);
            }
            else if(ev.type==ClientMessage&&(Atom)ev.xclient.data.l[0]==ui.wm_delete){g_stop=1;break;}
            else if(ev.type==KeyPress){
                KeySym ks = XLookupKeysym(&ev.xkey,0);
                if(g_edit_ai_path) {
                    if(ks==XK_Escape) { g_edit_ai_path=false; snprintf(message,sizeof(message),"Log path edit cancelled."); }
                    else if(ks==XK_Return || ks==XK_KP_Enter) {
                        if(ui_path_is_absolute(g_edit_path_buffer,sizeof(g_edit_path_buffer))) {
                            text_copy(g_settings.ai_log_file,sizeof(g_settings.ai_log_file),g_edit_path_buffer);
                            g_settings.ai_log_manual=true;
                            g_edit_ai_path=false;
                            snprintf(message,sizeof(message),settings_save()?"AI log path saved.":"Failed to save AI log path.");
                            system.ai_log_last_detect=0;
                        } else snprintf(message,sizeof(message),"Absolute path required (begins with /). Esc to cancel.");
                    } else if(ks==XK_BackSpace) {
                        (void)ui_path_backspace(g_edit_path_buffer,sizeof(g_edit_path_buffer));
                    } else {
                        char ch[16]={0}; KeySym temp;
                        int len=XLookupString(&ev.xkey,ch,sizeof(ch),&temp,NULL);
                        if(len>0) (void)ui_path_insert(g_edit_path_buffer,sizeof(g_edit_path_buffer),ch,(size_t)len);
                    }
                    ui.dirty=true;
                    continue;
                }

                int delta=0;
                if(ui_navigation_delta(ks,gui_content_height(&ui),&delta)) {
                    gui_scroll(&ui,view,g_workflow,delta);
                    continue;
                }
                char key=ui_keysym_to_action(&ev.xkey); if(key=='q'||key=='Q'){g_stop=1;break;}
                if(key){
                    View old_view = view;
                    handle_action(key,&s,&target,&interval_us,&dpm_interval_us,&monitor_clocks,&adaptive,&burst_until_ns,&trace,&events,&view,message,sizeof(message));
                    if (view != old_view) ui.scroll_y = 0;
                    ui.dirty=true;
                }
            } else if(ev.type==ButtonPress){
                if ((ev.xbutton.button == Button4 || ev.xbutton.button == Button5) &&
                    ev.xbutton.y >= UI_CONTENT_TOP && ev.xbutton.y < gui_content_bottom(&ui)) {
                    gui_scroll(&ui,view,g_workflow,ev.xbutton.button == Button4 ? -54 : 54);
                    continue;
                }
                if (ev.xbutton.button != Button1) continue;
                handle_click(&ui,ev.xbutton.x,ev.xbutton.y,&s,&target,&tuning,&interval_us,&dpm_interval_us,&monitor_clocks,&adaptive,&burst_until_ns,&trace,&events,&view,message,sizeof(message));ui.dirty=true;
            }
        }

        if(collect_timing)poll_timing_record(&poll_timing,POLL_INPUT,poll_timing_now_ns()-pm_input);
        /* Invalidate displayed data immediately on source/parser/device changes.
         * The worker may still be completing a request from the previous source. */
        if (!monitor_source_settings_equal(&visible_source_settings, &g_settings)) {
            visible_source_settings = g_settings;
            monitor_generation++;
            if (worker_started) {
                MonitorInput fresh = {.settings = g_settings,
                                      .gpu_power_w = power.power_w,
                                      .generation = monitor_generation};
                monitor_worker_request(&worker, &fresh);
            }
            system.tokens.has_data = false;
            system.tokens.is_processing = false;
            system.tokens.tokens_per_sec = 0;
            system.ai_log_event_count = 0;
            system.ai_log_path[0] = '\0';
            system.ai_model_activity[0] = '\0';
            g_ai_log_path[0] = '\0';
            memset(g_ai_log_events, 0, sizeof(g_ai_log_events));
            memset(&g_maya, 0, sizeof(g_maya));
            memset(&system.ai_prompt_cache, 0, sizeof(system.ai_prompt_cache));
            last_monitor_finished_ns = 0;
            ui.dirty = true;
        }

        if(g_gpu_switch_requested) {
            uint64_t pm_switch=collect_timing?poll_timing_now_ns():0;
            g_gpu_switch_requested=false;
            int newgi=-1;
            for(int i=0;i<ngpu;i++) if(gpus[i].card_num==g_requested_gpu) newgi=i;
            if(newgi<0) snprintf(message,sizeof(message),"Selected AMD GPU no longer available.");
            else if(newgi==gi) snprintf(message,sizeof(message),"Already monitoring selected GPU.");
            else {
                Sysfs nextsys;
                if(sysfs_open(&gpus[newgi],&nextsys)!=0)
                    snprintf(message,sizeof(message),"Cannot open selected card%d: %s",g_requested_gpu,strerror(errno));
                else {
                    /* No new GPU policy writes until the user re-enables the watchdog. */
                    target.enabled=false; target.lock_sclk=false; target.lock_mclk=false;
                    tuning.armed=false;
                    sysfs_close(&s); s=nextsys;
                    gi=newgi;g=&gpus[gi];
                    memset(&power,0,sizeof(power));power.gpu_busy=power.mem_busy=-1;
                    power.power_w=power.voltage_v=power.current_a=-1;
                    power.gfx_hz=power.mem_hz=-1;power.fan_rpm=-1;
                    power.gfxoff_enabled=power.gfxoff_status=-1;
                    for(int i=0;i<MAX_GPU_TEMPS;i++) power.temps_c[i]=-1;
                    memset(&clients,0,sizeof(clients));memset(&tuning,0,sizeof(tuning));
                    /* Do not display DRM/desktop data associated with the old
                     * card while the new read-only scan is in flight. */
                    memset(&session,0,sizeof(session));
                    ui_window_probe(&ui,&session,g_settings.compositor_source);
                    last_fallback_session_ns=0;
                    tuning.spec=gpu_spec_match(g->full_name,g->vram_bytes);
                    gpu_sample_power(&s,&power);
                    tuning_refresh(&s,&tuning,true);
                    discovery_generation++;
                    if (discovery_started) {
                        DiscoveryInput request = discovery_input(g, &s, &session,
                            VendorRelease(ui.dpy), g_settings.compositor_source, discovery_generation);
                        discovery_worker_request(&discovery_worker, &request);
                    }
                    g_settings.preferred_card=g->card_num;
                    (void)settings_save();
                    event_log_add(&events,"GPU SWITCH","Monitoring rebound; watchdog paused",&clients,&trace);
                    snprintf(message,sizeof(message),"Switched to card%d. Watchdog PAUSED; tuning DISARMED.",g->card_num);
                }
            }
            ui.dirty=true;
            if(collect_timing)poll_timing_record(&poll_timing,POLL_GPU_SWITCH,poll_timing_now_ns()-pm_switch);
        }
        clock_gettime(CLOCK_MONOTONIC,&loop_now);
        if(diff_ns(loop_now,stat_wall0)>=1000000000LL){
            struct timespec cpu1;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu1);double wall=diff_ns(loop_now,stat_wall0)/1e9,cpu=diff_ns(cpu1,stat_cpu0)/1e9;uint64_t dc=st.checks-stat_checks0;
            st.loop_cpu_pct=wall>0?cpu/wall*100.0:0.0;st.loop_hz=wall>0?dc/wall:0.0;stat_wall0=loop_now;stat_cpu0=cpu1;stat_checks0=st.checks;ui.dirty=true;
        }
        if(ui.dirty&&diff_ns(loop_now,last_ui)>=250000000LL){
            uint64_t pm=collect_timing?poll_timing_now_ns():0;
            gui_draw(&ui,g,&s,&target,&st,interval_us,dpm_interval_us,monitor_clocks,adaptive,&clients,&power,&system,&session,&events,&trace,&tuning,view,message);
            if(collect_timing)poll_timing_record(&poll_timing,POLL_RENDER,poll_timing_now_ns()-pm);
            last_ui=loop_now;
        }

        /* Preserve the existing single-threaded scheduler: measure how far its
         * intended deadline was exceeded before resetting it. A missed tick is
         * a diagnostic of latency, not a real-time scheduling guarantee. */
        add_ns(&next,(long long)active_interval_us*1000LL);
        struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
        long long remain_ns=diff_ns(next,now);
        if(remain_ns>0)clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&next,NULL);
        else {
            st.deadline_misses++;
            if(collect_timing)poll_timing_watchdog_late(&poll_timing,
                remain_ns<0?(uint64_t)(-remain_ns):0,(uint64_t)active_interval_us*1000ULL);
            next=now;
        }
    }

    if (discovery_started) discovery_worker_stop(&discovery_worker);
    if (worker_started) monitor_worker_stop(&worker);
    background_monitor_close(&background);
    if(collect_timing)poll_timing_report(&poll_timing,stderr,st.deadline_misses);
    trace_stop(&trace);if(events.logf)fclose(events.logf);cleanup_curl_handle();curl_global_cleanup();ui_window_close(&ui);
    sysfs_close(&s);return 0;
}
