/* SPDX-License-Identifier: GPL-3.0-or-later
 * Diagnostic state shared by the collector and read-only views.
 * No X11 dependency and no sysfs writes.
 */
#ifndef AMD_GPU_DIAG_DIAGNOSTICS_TYPES_H
#define AMD_GPU_DIAG_DIAGNOSTICS_TYPES_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "../monitor/system_state.h"
#ifndef MAX_DIAG_EVENTS
#define MAX_DIAG_EVENTS 512
#endif
#ifndef MAX_TRACE_LINE
#define MAX_TRACE_LINE 512
#endif
typedef struct {
    struct timespec when;
    char kind[32];
    char detail[192];
    char actor[96];
    double actor_activity;
    char trace_actor[96];
} DiagEvent;

typedef struct {
    DiagEvent ring[MAX_DIAG_EVENTS];
    int head;
    int count;
    uint64_t total;
    FILE *logf;
    bool logging;
    char log_path[MAX_PATH_LEN];
} EventLog;

typedef struct {
    bool available;
    bool active;
    char root[MAX_PATH_LEN];
    char instance[MAX_PATH_LEN];
    int pipe_fd;
    int amdgpu_events;
    int drm_sched_events;
    int drm_events;
    int drm_display_events_enabled;
    uint64_t lines;
    uint64_t amdgpu_lines;
    uint64_t sched_lines;
    uint64_t drm_lines;
    char last_line[MAX_TRACE_LINE];
    char last_actor[96];
    struct timespec last_seen;
} TraceState;

#endif
