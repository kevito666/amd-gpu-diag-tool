/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "log_parser.h"
#include "../core/util.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ai_log_timing_values(const char *p, double *ms, double *tokens,
                                 double *mspt, double *tps) {
    *ms = *tokens = *mspt = *tps = 0.0;
    const char *eq = strchr(p, '=');
    if (!eq) return;
    char *after;
    *ms = strtod(eq + 1, &after);
    const char *slash = strchr(after, '/');
    if (slash) *tokens = strtod(slash + 1, &after);
    const char *paren = strchr(after, '(');
    if (paren) {
        *mspt = strtod(paren + 1, &after);
        const char *comma = strchr(after, ',');
        if (comma) *tps = strtod(comma + 1, NULL);
    }
    if (*ms > 0 && *tokens > 0) {
        if (*mspt <= 0) *mspt = *ms / *tokens;
        if (*tps <= 0) *tps = 1000.0 * *tokens / *ms;
    }
}

/* Parse ALL timings in a bounded tail and merge prompt/eval/total lines by
 * (slot, task). Keep the most recently updated tasks, never the first N lines. */
int ai_log_parse_lines(char *buf, AiLogEvent *out, int max_events) {
    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strstr(line, "print_timing")) continue;
        const char *tp = strstr(line, "task ");
        const char *sp = strstr(line, "id ");
        if (!tp || !sp) continue;
        int task = atoi(tp + 5), slot = atoi(sp + 3);
        if (task < 0 || slot < 0) continue;
        const char *metric = NULL;
        int kind = 0;
        if ((metric = strstr(line, "prompt eval time"))) kind = 1;
        else if ((metric = strstr(line, "eval time"))) kind = 2;
        else if ((metric = strstr(line, "total time"))) kind = 3;
        if (!kind) continue;
        int idx = -1;
        for (int i = 0; i < count; i++)
            if (out[i].task_id == task && out[i].slot_id == slot) { idx = i; break; }
        AiLogEvent ev = {0};
        if (idx >= 0) {
            ev = out[idx];
            memmove(&out[idx], &out[idx+1], (size_t)(count - idx - 1) * sizeof(*out));
            count--;
        } else { ev.task_id = task; ev.slot_id = slot; }
        int yy=0,mm=0,dd=0,hh=0,mi=0,ss=0;
        if (sscanf(line, "[%d-%d-%d %d:%d:%d]", &yy, &mm, &dd, &hh, &mi, &ss) == 6)
            snprintf(ev.time_str, sizeof(ev.time_str), "%02d:%02d:%02d", hh, mi, ss);
        else if (!ev.time_str[0]) text_copy(ev.time_str, sizeof(ev.time_str), "--:--:--");
        double ms=0, tok=0, mspt=0, tps=0;
        ai_log_timing_values(metric, &ms, &tok, &mspt, &tps);
        if (kind == 1) {
            ev.prompt_eval_ms = ms; ev.prompt_tokens = tok;
            ev.prompt_ms_per_token = mspt; ev.prompt_tps = tps; ev.has_prompt = true;
        } else if (kind == 2) {
            ev.eval_ms = ms; ev.eval_tokens = tok;
            ev.eval_ms_per_token = mspt; ev.eval_tps = tps; ev.has_eval = true;
        } else {
            ev.total_ms = ms; ev.total_tokens = tok; ev.has_total = true;
        }
        if (!ev.has_total) {
            ev.total_ms = ev.prompt_eval_ms + ev.eval_ms;
            ev.total_tokens = ev.prompt_tokens + ev.eval_tokens;
        }
        if (count == max_events) {
            memmove(&out[0], &out[1], (size_t)(count - 1) * sizeof(*out));
            count--;
        }
        out[count++] = ev;
    }
    return count;
}

/* Return only a setting that is an actual number (in MiB), not a guessed
 * proportion of process RSS or GPU VRAM. -1 means unlimited, 0 disabled. */
bool ai_cache_parse_limit(const char *value, double *limit_mib) {
    if (!value || !*value) return false;
    errno = 0;
    char *end = NULL;
    long number = strtol(value, &end, 10);
    if (errno || end == value || (number < -1) || number > 1048576L) return false;
    if (*end && !isspace((unsigned char)*end)) return false;
    *limit_mib = (double)number;
    return true;
}

void ai_cache_parse_lines(const char *buf, AiPromptCache *out) {
    memset(out, 0, sizeof(*out));
    if (!buf) return;
    for (const char *p=buf; *p;) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end-p) : strlen(p);
        if (len < 1024) {
            char line[1024];
            memcpy(line, p, len);
            line[len] = '\0';
            const char *mark;
            int entries;
            double used, limit;
            if ((mark = strstr(line, "cache state:")) &&
                sscanf(mark, "cache state: %d prompts, %lf MiB (limits: %lf MiB", &entries, &used, &limit) == 3 &&
                entries >= 0 && used >= 0 && limit >= -1) {
                out->usage_valid = true;
                out->limit_valid = true;
                out->entries = entries;
                out->used_mib = used;
                out->limit_mib = limit;
            } else if ((mark = strstr(line, "prompt cache is enabled, size limit:"))) {
                mark += strlen("prompt cache is enabled, size limit:");
                while (*mark == ' ') mark++;
                if (strstr(mark, "no limit")) {
                    out->limit_valid = true;
                    out->limit_mib = -1;
                } else if (sscanf(mark, "%lf MiB", &limit)==1 && limit >= 0) {
                    out->limit_valid = true;
                    out->limit_mib = limit;
                }
            } else if (strstr(line, "prompt cache is disabled")) {
                out->limit_valid = true;
                out->limit_mib = 0;
            }
            if (strstr(line, "saving prompt with length") && strstr(line, "total state size"))
                out->save_attempts++;
            if (strstr(line, "found better prompt with f_keep")) out->matches++;
            if ((mark = strstr(line, "making room for prompt cache entry, removing oldest entry"))) {
                out->evictions++;
                if ((mark = strstr(mark, "size = ")) && sscanf(mark, "size = %lf MiB", &used)==1 && used>=0)
                    out->evicted_mib += used;
            }
            if ((mark = strstr(line, "prompt state size")) && strstr(mark, "exceeds cache size limit")) {
                out->rejected++;
                if (sscanf(mark, "prompt state size %lf MiB exceeds cache size limit %lf MiB", &used, &limit)==2) {
                    out->last_rejected_mib = used;
                    if (limit>=0) { out->limit_valid = true; out->limit_mib = limit; }
                }
            }
            if (strstr(line, "failed to load prompt from cache")) out->load_failures++;
            if (strstr(line, "forcing full prompt re-processing due to lack of cache data"))
                out->forced_reprocess++;
        }
        if (!end) break;
        p=end+1;
    }
}
