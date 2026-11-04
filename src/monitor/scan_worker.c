/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "scan_worker.h"
#include <string.h>
#include <time.h>

static uint64_t monotonic_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

bool discovery_result_matches(const DiscoveryOutput *out, const DiscoveryInput *in) {
    return out && in && out->generation == in->generation &&
           out->card_num == in->gpu.card_num &&
           out->finished_ns != 0 &&
           out->compositor_source == in->compositor_source;
}

static void *discovery_thread(void *userdata) {
    DiscoveryWorker *w = userdata;
    for (;;) {
        pthread_mutex_lock(&w->mutex);
        while (!w->stop && !w->pending)
            pthread_cond_wait(&w->wake, &w->mutex);
        if (w->stop) { pthread_mutex_unlock(&w->mutex); return NULL; }
        DiscoveryInput request = w->queued;
        w->pending = false;
        pthread_mutex_unlock(&w->mutex);

        DiscoveryOutput output = {0};
        w->sampler(&request, &output, w->context);
        output.generation = request.generation;
        output.card_num = request.gpu.card_num;
        output.compositor_source = request.compositor_source;
        output.finished_ns = monotonic_ns();
        pthread_mutex_lock(&w->mutex);
        if (!w->stop) { w->latest = output; w->ready = true; }
        pthread_mutex_unlock(&w->mutex);
    }
}

bool discovery_worker_start(DiscoveryWorker *w, DiscoverySampler sampler, void *context) {
    if (!w || !sampler) return false;
    memset(w, 0, sizeof(*w));
    if (pthread_mutex_init(&w->mutex, NULL) != 0) return false;
    if (pthread_cond_init(&w->wake, NULL) != 0) {
        pthread_mutex_destroy(&w->mutex);
        return false;
    }
    w->sampler = sampler;
    w->context = context;
    if (pthread_create(&w->thread, NULL, discovery_thread, w) != 0) {
        pthread_cond_destroy(&w->wake);
        pthread_mutex_destroy(&w->mutex);
        return false;
    }
    w->started = true;
    return true;
}

void discovery_worker_request(DiscoveryWorker *w, const DiscoveryInput *request) {
    if (!w || !w->started || !request) return;
    pthread_mutex_lock(&w->mutex);
    if (!w->stop) {
        w->queued = *request;
        w->pending = true; /* replace stale unstarted work rather than build backlog */
        pthread_cond_signal(&w->wake);
    }
    pthread_mutex_unlock(&w->mutex);
}

bool discovery_worker_take(DiscoveryWorker *w, DiscoveryOutput *out) {
    if (!w || !w->started || !out) return false;
    pthread_mutex_lock(&w->mutex);
    bool ready = w->ready;
    if (ready) { *out = w->latest; w->ready = false; }
    pthread_mutex_unlock(&w->mutex);
    return ready;
}

void discovery_worker_stop(DiscoveryWorker *w) {
    if (!w || !w->started) return;
    pthread_mutex_lock(&w->mutex);
    w->stop = true;
    w->pending = false;
    pthread_cond_signal(&w->wake);
    pthread_mutex_unlock(&w->mutex);
    pthread_join(w->thread, NULL); /* only bounded read-only work permitted */
    pthread_cond_destroy(&w->wake);
    pthread_mutex_destroy(&w->mutex);
    w->started = false;
}
