/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "events.h"
#include "../core/util.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
void event_log_add(EventLog *el, const char *kind, const char *detail,
                          const ClientMonitor *cm, const TraceState *tr) {
    DiagEvent ev;
    memset(&ev,0,sizeof(ev));
    clock_gettime(CLOCK_MONOTONIC,&ev.when);
    snprintf(ev.kind,sizeof(ev.kind),"%s",kind);
    snprintf(ev.detail,sizeof(ev.detail),"%s",detail?detail:"");
    const GpuClient *tc=(cm && cm->count > 0) ? &cm->items[0] : NULL;
    if(tc) {
        snprintf(ev.actor,sizeof(ev.actor),"%s",tc->role[0]?tc->role:tc->name);
        ev.actor_activity=tc->activity_pct;
    } else snprintf(ev.actor,sizeof(ev.actor),"unknown");
    if(tr && tr->last_actor[0] && diff_ns(ev.when,tr->last_seen) < 500000000LL)
        snprintf(ev.trace_actor,sizeof(ev.trace_actor),"%s",tr->last_actor);

    el->ring[el->head]=ev;
    el->head=(el->head+1)%MAX_DIAG_EVENTS;
    if(el->count<MAX_DIAG_EVENTS)el->count++;
    el->total++;
    if(el->logging && el->logf) {
        fprintf(el->logf,"%lld.%09ld,%s,%s,%s,%.3f,%s\n",
                (long long)ev.when.tv_sec,ev.when.tv_nsec,ev.kind,ev.detail,ev.actor,ev.actor_activity,ev.trace_actor);
        fflush(el->logf);
    }
}

const DiagEvent *event_last(const EventLog *el) {
    if(el->count<=0)return NULL;
    int i=el->head-1; if(i<0)i+=MAX_DIAG_EVENTS;
    return &el->ring[i];
}

void toggle_event_logging(EventLog *el) {
    if(el->logging) {
        if(el->logf) fclose(el->logf);
        el->logf=NULL; el->logging=false;
        return;
    }
    snprintf(el->log_path,sizeof(el->log_path),"/tmp/amd-gpu-diagnostic-events-%d.csv",getpid());
    el->logf=fopen(el->log_path,"w");
    if(el->logf) {
        fprintf(el->logf,"monotonic_time,kind,detail,top_actor,actor_activity_pct,trace_actor\n");
        el->logging=true;
    }
}
