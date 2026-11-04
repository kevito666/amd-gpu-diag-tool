/* SPDX-License-Identifier: GPL-3.0-or-later
 * Deterministic and side-effect-free llama.cpp/Bionic log parsing.
 */
#ifndef AMD_GPU_DIAG_AI_LOG_PARSER_H
#define AMD_GPU_DIAG_AI_LOG_PARSER_H
#include <stdbool.h>
#include <time.h>
#define AI_LOG_MAX_EVENTS 12
/* Host-RAM prompt-state cache from llama.cpp/Bionic's --cache-ram.
 * These are NOT GPU VRAM, GPU KV-cache or system-RSS measurements.
 * Usage is reported only when the backend logs an actual cache-state summary. */
typedef struct {
    bool usage_valid;
    bool limit_valid;
    double used_mib;
    double limit_mib;   /* 0 = disabled; -1 = unlimited */
    int entries;
    int save_attempts;
    int matches;       /* f_keep matches; not proof that a load succeeded */
    int evictions;
    int rejected;
    int load_failures;
    int forced_reprocess;
    double evicted_mib;
    double last_rejected_mib;
} AiPromptCache;

typedef struct {
    time_t timestamp;
    char time_str[16];
    double prompt_eval_ms;
    double prompt_tokens;
    double prompt_ms_per_token;
    double prompt_tps;
    double eval_ms;
    double eval_tokens;
    double eval_ms_per_token;
    double eval_tps;
    double total_ms;
    double total_tokens;
    int slot_id;
    int task_id;
    bool has_prompt;
    bool has_eval;
    bool has_total;
} AiLogEvent;


/* ai_log_parse_lines mutates its caller-owned input buffer (strtok_r).
 * The cache parser does not modify input. */
int ai_log_parse_lines(char *buf, AiLogEvent *out, int max_events);
bool ai_cache_parse_limit(const char *value, double *limit_mib);
void ai_cache_parse_lines(const char *buf, AiPromptCache *out);
#endif
