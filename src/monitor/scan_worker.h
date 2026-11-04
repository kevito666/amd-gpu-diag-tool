/* SPDX-License-Identifier: GPL-3.0-or-later
 * Bounded, read-only GPU-client and desktop-session discovery.
 * No X11 calls and no access to live GPU device descriptors or write paths.
 */
#ifndef AMD_GPU_DIAG_SCAN_WORKER_H
#define AMD_GPU_DIAG_SCAN_WORKER_H
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include "session.h"

typedef struct {
    Gpu gpu;                    /* copied identity; never a mutable Gpu* */
    char drm_state_path[MAX_PATH_LEN]; /* copied read-only debugfs path */
    char window_manager[64];
    CompositorSource compositor_source;
    int xserver_vendor_release;
    uint64_t generation;       /* increment after GPU or compositor source changes */
} DiscoveryInput;

typedef struct {
    ClientMonitor clients;
    SessionInfo session;
    bool session_valid;
    uint64_t clients_duration_ns, session_duration_ns, finished_ns;
    uint64_t generation;
    int card_num;
    CompositorSource compositor_source;
} DiscoveryOutput;

typedef void (*DiscoverySampler)(const DiscoveryInput *, DiscoveryOutput *, void *);
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    DiscoveryInput queued;
    DiscoveryOutput latest;
    DiscoverySampler sampler;
    void *context;
    bool started, stop, pending, ready;
} DiscoveryWorker;

bool discovery_worker_start(DiscoveryWorker *worker, DiscoverySampler sampler, void *context);
void discovery_worker_request(DiscoveryWorker *worker, const DiscoveryInput *input);
bool discovery_worker_take(DiscoveryWorker *worker, DiscoveryOutput *output);
void discovery_worker_stop(DiscoveryWorker *worker);
/* Use the current foreground provenance to reject old card/compositor scans. */
bool discovery_result_matches(const DiscoveryOutput *output, const DiscoveryInput *current);
#endif
