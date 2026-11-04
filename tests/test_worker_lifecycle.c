/* SPDX-License-Identifier: GPL-3.0-or-later
 * Hardware-free shutdown/restart test for both read-only worker queues.
 * The in-flight sampler is deliberately held at a gate so a pending request
 * cannot sneak through before shutdown. Never requires an X server or GPU.
 */
#define _POSIX_C_SOURCE 200809L
#include "../src/monitor/worker.h"
#include "../src/monitor/scan_worker.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

typedef struct {
    atomic_bool entered, release;
    atomic_int calls;
} Gate;

static void tick(void) {
    const struct timespec nap = {.tv_sec=0, .tv_nsec=1000000L};
    nanosleep(&nap, NULL);
}
static void await_true(atomic_bool *flag) {
    for (int i = 0; i < 3000; ++i) {
        if (atomic_load(flag)) return;
        tick();
    }
    assert(!"worker failed to reach test gate");
}
static void gate_wait(Gate *gate) {
    atomic_fetch_add(&gate->calls, 1);
    atomic_store(&gate->entered, true);
    while (!atomic_load(&gate->release)) tick();
}
static void monitor_fixture(const MonitorInput *input, MonitorOutput *output, void *ctx) {
    gate_wait(ctx);
    output->generation = input->generation;
}
static void discovery_fixture(const DiscoveryInput *input, DiscoveryOutput *output, void *ctx) {
    gate_wait(ctx);
    output->card_num = input->gpu.card_num;
}
static void *stop_monitor(void *ptr) { monitor_worker_stop(ptr); return NULL; }
static void *stop_discovery(void *ptr) { discovery_worker_stop(ptr); return NULL; }

static void test_monitor(void) {
    Gate gate = {0};
    MonitorWorker worker;
    assert(monitor_worker_start(&worker, monitor_fixture, &gate));
    MonitorInput in = {.generation=1};
    monitor_worker_request(&worker, &in);
    await_true(&gate.entered);
    in.generation = 2;
    monitor_worker_request(&worker, &in);
    pthread_t stopper;
    assert(pthread_create(&stopper, NULL, stop_monitor, &worker) == 0);
    bool stopping = false;
    for (int i = 0; i < 3000 && !stopping; ++i) {
        pthread_mutex_lock(&worker.mutex);
        stopping = worker.stop;
        pthread_mutex_unlock(&worker.mutex);
        if (!stopping) tick();
    }
    assert(stopping);
    atomic_store(&gate.release, true);
    assert(pthread_join(stopper, NULL) == 0);
    assert(atomic_load(&gate.calls) == 1); /* queued request cancelled */
    assert(!worker.started);
    monitor_worker_stop(&worker);           /* idempotent */
    assert(monitor_worker_start(&worker, monitor_fixture, &gate));
    monitor_worker_stop(&worker);           /* idle shutdown */
}
static void test_discovery(void) {
    Gate gate = {0};
    DiscoveryWorker worker;
    assert(discovery_worker_start(&worker, discovery_fixture, &gate));
    DiscoveryInput in = {.generation=1};
    discovery_worker_request(&worker, &in);
    await_true(&gate.entered);
    in.generation = 2;
    discovery_worker_request(&worker, &in);
    pthread_t stopper;
    assert(pthread_create(&stopper, NULL, stop_discovery, &worker) == 0);
    bool stopping = false;
    for (int i = 0; i < 3000 && !stopping; ++i) {
        pthread_mutex_lock(&worker.mutex);
        stopping = worker.stop;
        pthread_mutex_unlock(&worker.mutex);
        if (!stopping) tick();
    }
    assert(stopping);
    atomic_store(&gate.release, true);
    assert(pthread_join(stopper, NULL) == 0);
    assert(atomic_load(&gate.calls) == 1);
    assert(!worker.started);
    discovery_worker_stop(&worker);
    assert(discovery_worker_start(&worker, discovery_fixture, &gate));
    discovery_worker_stop(&worker);
}
int main(void) {
    test_monitor();
    test_discovery();
    puts("Worker lifecycle PASS: in-flight shutdown, pending cancellation, idle restart");
    return 0;
}
