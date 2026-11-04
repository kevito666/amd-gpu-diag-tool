/* SPDX-License-Identifier: GPL-3.0-or-later
 * Deterministic offline handoff tests. No GPU, network or X11 calls.
 */
#define _GNU_SOURCE
#include "../src/monitor/worker.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* A caller can enqueue during a slow read without waiting for that read. */
static void slow_sample(const MonitorInput *input, MonitorOutput *output, void *ctx) {
    (void)ctx;
    struct timespec pause = {.tv_sec=0,.tv_nsec=30000000};
    nanosleep(&pause,NULL);
    output->system.tokens.total_tokens = (int64_t)input->gpu_power_w;
    output->system.tokens.has_data = true;
}

static uint64_t nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void) {
    MonitorWorker worker;
    assert(!monitor_worker_start(NULL, slow_sample, NULL));
    assert(monitor_worker_start(&worker, slow_sample, NULL));
    MonitorInput in = {.gpu_power_w=11};
    in.settings.token_source=TOKEN_SOURCE_LLAMA;
    in.generation = 1;
    monitor_worker_request(&worker,&in);
    uint64_t started = nanos();
    for (int i=0;i<100;i++) {
        in.gpu_power_w=20+i;
        monitor_worker_request(&worker,&in);
    }
    assert(nanos()-started < 20000000ULL); /* queueing is not a 30ms sample */
    MonitorOutput out;
    bool seen=false;
    for (int i=0;i<100;i++) {
        if (monitor_worker_take(&worker,&out)) {
            seen=true;
            if (out.system.tokens.total_tokens==119) break;
        }
        struct timespec pause = {.tv_sec=0,.tv_nsec=5000000};
        nanosleep(&pause,NULL);
    }
    assert(seen);
    assert(out.system.tokens.total_tokens == 119);
    assert(out.duration_ns>=20000000ULL);
    assert(out.finished_ns > 0);
    MonitorInput current = in;
    assert(monitor_result_matches(&out, &current));
    current.generation++;
    assert(!monitor_result_matches(&out, &current));
    /* Source A->B->A looks the same, but old A result must be rejected. */
    current.settings.token_source = TOKEN_SOURCE_MAYA;
    current.settings.token_source = TOKEN_SOURCE_LLAMA;
    assert(!monitor_result_matches(&out, &current));
    current.generation = out.generation;
    assert(monitor_result_matches(&out, &current));
    assert(!monitor_result_matches(NULL, &current));
    AppSettings before={.token_source=TOKEN_SOURCE_LLAMA,.log_parser=LOG_MAYA};
    AppSettings after=before;
    assert(monitor_source_settings_equal(&before,&after));
    after.token_source=TOKEN_SOURCE_MAYA;
    assert(!monitor_source_settings_equal(&before,&after));
    after=before;
    strcpy(after.ai_nvme_device,"nvme2n1");
    assert(!monitor_source_settings_equal(&before,&after));
    after=before;
    after.log_parser=LOG_OFF;
    assert(!monitor_source_settings_equal(&before,&after));
    monitor_worker_stop(&worker);
    puts("Monitor worker PASS: nonblocking queue, latest state, provenance, clean join");
    return 0;
}
