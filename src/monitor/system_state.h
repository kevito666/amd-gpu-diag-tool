/* SPDX-License-Identifier: GPL-3.0-or-later
 * Internal sampling state shared by the main application and Linux sensors.
 * Not a public ABI: the GUI must not own or mutate hwmon file descriptors.
 */
#ifndef AMD_DIAG_SYSTEM_STATE_H
#define AMD_DIAG_SYSTEM_STATE_H
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "cpu.h"
#include "../config/settings.h"
#include "../ai/log_parser.h"
#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 512
#endif
#ifndef MAX_GPU_TEMPS
#define MAX_GPU_TEMPS 3
#endif
typedef struct {
    double tokens_per_sec;
    double prompt_tokens_per_sec;
    int64_t total_tokens;
    int64_t prompt_tokens;
    int64_t eval_tokens;
    bool has_data;
    bool is_processing;
    /* Prevent counters from one backend being reused by another. */
    bool source_initialized;
    TokenSource source_in_use;
    char source_label[32];
    /* Heuristic state */
    double power_history[100];
    int power_history_idx;
    double recent_peak_w;
    bool in_decode_phase;
    double decode_power_w;
    /* API state */
    struct timespec last_api_poll;
    int64_t prev_decoded;
    int64_t n_prompt_tokens;
    int64_t n_prompt_tokens_processed;
    int64_t n_prompt_tokens_cache;
} TokenRate;

typedef struct {
    double cpu_pct;
    unsigned cpu_threads;
    unsigned long long ram_total_bytes;
    unsigned long long ram_used_bytes;
    double ram_pct;
    unsigned long long prev_cpu_total;
    unsigned long long prev_cpu_idle;
    bool have_cpu_sample;
    int fd_cpu_temp;
    int fd_cpu_tctl;
    char cpu_tctl_path[MAX_PATH_LEN];
    char cpu_tctl_label[48];
    double cpu_tctl_c;
    char cpu_temp_path[MAX_PATH_LEN];
    char cpu_temp_label[32];
    double cpu_temp_c;
    long cpu_temp_crit_mc;
    bool cpu_temp_discovered;

    /* CPU per-CCX temps (k10temp) */
    int fd_cpu_ccx_temp[4];
    char cpu_ccx_temp_paths[4][MAX_PATH_LEN];
    char cpu_ccx_temp_labels[4][32];
    double cpu_ccx_temp_c[4];
    int cpu_ccx_temp_count;
    bool cpu_ccx_temp_discovered;

    /* CPU fan */
    int fd_cpu_fan_rpm;
    char cpu_fan_rpm_path[MAX_PATH_LEN];
    int cpu_fan_rpm;
    int fd_cpu_fan_pct;
    char cpu_fan_pct_path[MAX_PATH_LEN];
    double cpu_fan_pct;
    bool cpu_fan_discovered;

    /* CPU power */
    int fd_cpu_power;
    char cpu_power_path[MAX_PATH_LEN];
    double cpu_power_w;
    bool cpu_power_discovered;

    /* Motherboard temperatures */
    int mb_temp_count;
    int mb_temp_fds[4];
    char mb_temp_paths[4][MAX_PATH_LEN];
    char mb_temp_labels[4][32];
    double mb_temp_c[4];
    bool mb_temp_discovered;

    /* Top CPU processes */
    TopProcess top_procs[3];
    int top_proc_count;

    /* Token rate estimation */
    TokenRate tokens;
    TokenSource token_source;
    bool llama_available;
    bool ollama_available;
    bool vllm_available;
    bool lmstudio_available;

    /* Gaming mode metrics */
    CpuCoreLoad cpu_cores;
    double cpu_clock_min_mhz, cpu_clock_avg_mhz, cpu_clock_max_mhz;
    double cpu_clock_cap_mhz;
    int cpu_clock_count;
    struct timespec last_cpu_clock_scan;
    int cpu_boost; /* -1 unknown, 0 disabled, 1 enabled */
    char cpu_governor[32];
    int cpu_smt; /* -1 unknown, otherwise actual online SMT state */

    /* AI log events */
    char ai_log_path[MAX_PATH_LEN];
    char ai_log_source[32];
    int ai_log_event_count;
    time_t ai_log_last_update;
    time_t ai_log_last_detect;

    /* AI model info */
    char ai_model_name[128];
    char ai_model_type[32];
    int ai_ctx_size;
    char ai_cache_type_k[16];
    char ai_cache_type_v[16];
    bool ai_flash_attn;
    bool ai_mtp;
    AiPromptCache ai_prompt_cache;
    /* Prefer the currently running server's setting over old log lines. */
    bool ai_cache_process_limit_valid;
    double ai_cache_process_limit_mib;
    time_t ai_cache_process_last_check;
    bool ai_model_info_valid;
    char ai_model_activity[32];  /* "Idle", "Reading", "Thinking", "Outputting" */
} SystemTelemetry;


#endif
