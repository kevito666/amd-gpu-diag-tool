/* SPDX-License-Identifier: GPL-3.0-or-later
 * Bounded asynchronous, read-only telemetry sampling.
 * The worker MUST NOT access Xlib or perform any GPU control writes.
 * Ownership: the callback exclusively owns its persistent private context;
 * caller inputs/results are copied under the small mutex.
 */
#ifndef AMD_DIAG_MONITOR_WORKER_H
#define AMD_DIAG_MONITOR_WORKER_H
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include "system_state.h"
#include "nvme.h"
#include "../ai/maya_log.h"

typedef struct {
    AppSettings settings;
    double gpu_power_w; /* snapshot, not a pointer to mutable GPU state */
    uint64_t generation; /* advances whenever source settings change, including A->B->A */
} MonitorInput;

typedef struct {
    SystemTelemetry system;
    NvmeMonitor nvme;
    MayaLogMonitor maya;
    AiLogEvent events[AI_LOG_MAX_EVENTS];
    AppSettings settings; /* provenance: reject an outdated backend snapshot */
    uint64_t generation;
    uint64_t duration_ns;
    uint64_t finished_ns;
} MonitorOutput;

typedef void (*MonitorSampler)(const MonitorInput *, MonitorOutput *, void *);
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    MonitorInput queued;
    MonitorOutput latest;
    MonitorSampler sample;
    void *context;
    bool started, stop, pending, ready;
} MonitorWorker;

bool monitor_worker_start(MonitorWorker *w, MonitorSampler sample, void *context);
void monitor_worker_request(MonitorWorker *w, const MonitorInput *input);
bool monitor_worker_take(MonitorWorker *w, MonitorOutput *output);
void monitor_worker_stop(MonitorWorker *w);
/* Only source fields that affect AI interpretation cause stale results to be rejected. */
bool monitor_source_settings_equal(const AppSettings *a, const AppSettings *b);
/* Reject results from earlier source revisions, even if source settings reverted. */
bool monitor_result_matches(const MonitorOutput *out, const MonitorInput *current);
#endif
