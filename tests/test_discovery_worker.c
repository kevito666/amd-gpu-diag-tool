/* SPDX-License-Identifier: GPL-3.0-or-later
 * Offline worker contracts: bounded queue, generation and compositor isolation,
 * copied input lifetime, and clean shutdown. No X server or GPU required.
 */
#define _POSIX_C_SOURCE 200809L
#include "../src/monitor/scan_worker.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct { atomic_int calls; } Fixture;
static uint64_t nanoseconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
static void snooze(long ns) {
    struct timespec ts = {.tv_sec=0, .tv_nsec=ns};
    nanosleep(&ts,NULL);
}
static void fake_discovery(const DiscoveryInput *in, DiscoveryOutput *out, void *v) {
    Fixture *fixture = v;
    atomic_fetch_add(&fixture->calls, 1);
    snooze(25000000L);  /* simulate an expensive /proc traversal */
    out->clients.count = 1;
    out->clients.items[0].pid = (pid_t)in->gpu.card_num;
    out->session_valid = true;
    snprintf(out->session.compositor, sizeof(out->session.compositor),
             "source-%d", (int)in->compositor_source);
    out->clients_duration_ns=25000000ULL;
}
int main(void) {
    Fixture fixture;
    atomic_init(&fixture.calls, 0);
    DiscoveryWorker w;
    assert(!discovery_worker_start(NULL, fake_discovery, &fixture));
    assert(!discovery_worker_start(&w, NULL, &fixture));
    assert(discovery_worker_start(&w, fake_discovery, &fixture));
    DiscoveryInput req = {0};
    req.gpu.card_num=1; req.generation=1;
    req.compositor_source=COMP_PROCESS;
    discovery_worker_request(&w, &req);
    uint64_t start=nanoseconds();
    while (atomic_load(&fixture.calls)==0 && nanoseconds()-start<1000000000ULL)
        snooze(1000000L);
    assert(atomic_load(&fixture.calls)>0);
    start=nanoseconds();
    for (int i=0;i<250;i++) {
        req.gpu.card_num=2;
        req.generation=2;
        req.compositor_source=COMP_DRM;
        discovery_worker_request(&w, &req);
    }
    assert(nanoseconds()-start < 20000000ULL); /* enqueue doesn't await scan */
    DiscoveryOutput out;
    bool have_final=false;
    for (int i=0;i<300;i++) {
        if (discovery_worker_take(&w,&out)) {
            if (out.generation==1) {
                assert(!discovery_result_matches(&out,&req));
            } else {
                assert(discovery_result_matches(&out,&req));
                assert(out.clients.items[0].pid==2);
                assert(strcmp(out.session.compositor,"source-2")==0);
                assert(out.finished_ns>0);
                have_final=true;
                break;
            }
        }
        snooze(2000000L);
    }
    assert(have_final);
    assert(atomic_load(&fixture.calls)<=3); /* coalesced rather than queued 250 */
    DiscoveryInput changed=req;
    changed.compositor_source=COMP_X11;
    assert(!discovery_result_matches(&out,&changed));
    changed=req;
    changed.compositor_source=COMP_X11;
    changed.compositor_source=COMP_DRM; /* A->B->A must advance generation */
    changed.generation++;
    assert(!discovery_result_matches(&out,&changed));
    changed=req;
    changed.generation=3;
    assert(!discovery_result_matches(&out,&changed));
    changed=req;
    changed.gpu.card_num=3;
    assert(!discovery_result_matches(&out,&changed));
    discovery_worker_stop(&w);
    puts("Discovery worker PASS: nonblocking, latest coalescing, provenance, join");
    return 0;
}
