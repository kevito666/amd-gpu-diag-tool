/* SPDX-License-Identifier: GPL-3.0-or-later
 * Project Maya / DS4 engine-log parsing; no external process execution.
 */
#ifndef AMD_GPU_DIAG_AI_MAYA_LOG_H
#define AMD_GPU_DIAG_AI_MAYA_LOG_H
#include <stdbool.h>

typedef struct {
    char recent[3][160];
    int count;
    unsigned long recognized;
    bool prefill_valid;
    double prefill_tps;
    long prompt_tokens;
    bool decode_valid;
    double decode_tps, decode_ms_per_tok;
    bool vram_hit_valid, disk_valid, ram_fetch_valid;
    double vram_hit_pct, disk_reads_per_token, disk_wait_ms_per_token;
    double ram_fetches_per_token;
} MayaLogMonitor;

void maya_parse_lines(const char *log, MayaLogMonitor *out);
#endif
