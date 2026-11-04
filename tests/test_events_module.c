/* SPDX-License-Identifier: GPL-3.0-or-later
 * Event ring and trace-actor metadata use pure, simulated inputs.
 */
#define _POSIX_C_SOURCE 200809L
#include "../src/diagnostics/events.h"
#include <assert.h>
#include <string.h>
#include <time.h>
int main(void) {
    EventLog events={0}; ClientMonitor clients={0}; TraceState trace={0};
    clients.count=1;
    strcpy(clients.items[0].name,"llama-server");
    strcpy(clients.items[0].role,"AI inference");
    clients.items[0].activity_pct=24.5;
    strcpy(trace.last_actor,"amdgpu-worker");
    clock_gettime(CLOCK_MONOTONIC,&trace.last_seen);
    event_log_add(&events,"POLICY RESET","test metadata",&clients,&trace);
    assert(events.count==1 && events.total==1);
    const DiagEvent *ev=event_last(&events);
    assert(ev && !strcmp(ev->actor,"AI inference"));
    assert(!strcmp(ev->trace_actor,"amdgpu-worker"));
    assert(ev->actor_activity==24.5);
    for(int i=0;i<MAX_DIAG_EVENTS+3;i++)
        event_log_add(&events,"DPM CHANGE","synthetic",NULL,NULL);
    assert(events.count==MAX_DIAG_EVENTS);
    assert(events.total==MAX_DIAG_EVENTS+4);
    assert(event_last(&events) && !strcmp(event_last(&events)->detail,"synthetic"));
    return 0;
}
