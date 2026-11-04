/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "worker.h"
#include <string.h>
#include <time.h>

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

bool monitor_source_settings_equal(const AppSettings *a, const AppSettings *b) {
    if (!a || !b) return false;
    return a->token_source == b->token_source &&
           a->log_parser == b->log_parser &&
           a->ai_log_manual == b->ai_log_manual &&
           strcmp(a->ai_log_file, b->ai_log_file) == 0 &&
           strcmp(a->ai_nvme_device, b->ai_nvme_device) == 0;
}

bool monitor_result_matches(const MonitorOutput *out, const MonitorInput *current) {
    return out && current && out->generation == current->generation &&
           monitor_source_settings_equal(&out->settings, &current->settings);
}

static void *worker_main(void *arg) {
    MonitorWorker *w = arg;
    for (;;) {
        pthread_mutex_lock(&w->mutex);
        while (!w->stop && !w->pending)
            pthread_cond_wait(&w->wake, &w->mutex);
        if (w->stop) { pthread_mutex_unlock(&w->mutex); break; }
        MonitorInput in = w->queued;
        w->pending = false;
        pthread_mutex_unlock(&w->mutex);

        /* Perform all potentially blocking reads without holding a mutex. */
        MonitorOutput result = {0};
        const uint64_t before = monotonic_ns();
        w->sample(&in, &result, w->context);
        result.duration_ns = monotonic_ns() - before;
        result.finished_ns = monotonic_ns();
        result.settings = in.settings;
        result.generation = in.generation;
        pthread_mutex_lock(&w->mutex);
        if (!w->stop) { w->latest = result; w->ready = true; }
        pthread_mutex_unlock(&w->mutex);
    }
    return NULL;
}

bool monitor_worker_start(MonitorWorker *w, MonitorSampler sample, void *context) {
    if (!w || !sample) return false;
    memset(w, 0, sizeof(*w));
    if (pthread_mutex_init(&w->mutex, NULL) != 0) return false;
    if (pthread_cond_init(&w->wake, NULL) != 0) {
        pthread_mutex_destroy(&w->mutex); return false;
    }
    w->sample = sample;
    w->context = context;
    if (pthread_create(&w->thread, NULL, worker_main, w) != 0) {
        pthread_cond_destroy(&w->wake);
        pthread_mutex_destroy(&w->mutex);
        return false;
    }
    w->started = true;
    return true;
}

void monitor_worker_request(MonitorWorker *w, const MonitorInput *input) {
    if (!w || !w->started || !input) return;
    pthread_mutex_lock(&w->mutex);
    if (!w->stop) {
        w->queued = *input; /* only the newest request is retained */
        w->pending = true;
        pthread_cond_signal(&w->wake);
    }
    pthread_mutex_unlock(&w->mutex);
}

bool monitor_worker_take(MonitorWorker *w, MonitorOutput *output) {
    if (!w || !w->started || !output) return false;
    pthread_mutex_lock(&w->mutex);
    bool result = w->ready;
    if (result) { *output = w->latest; w->ready = false; }
    pthread_mutex_unlock(&w->mutex);
    return result;
}

void monitor_worker_stop(MonitorWorker *w) {
    if (!w || !w->started) return;
    pthread_mutex_lock(&w->mutex);
    w->stop = true;
    w->pending = false;
    pthread_cond_signal(&w->wake);
    pthread_mutex_unlock(&w->mutex);
    pthread_join(w->thread, NULL); /* callback must use bounded I/O */
    pthread_cond_destroy(&w->wake);
    pthread_mutex_destroy(&w->mutex);
    w->started = false;
}
