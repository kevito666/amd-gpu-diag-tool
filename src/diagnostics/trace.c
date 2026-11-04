/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "trace.h"
#include "../core/util.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
static int count_event_dirs(const char *p) {
    DIR *d=opendir(p); if(!d)return 0;
    int n=0; struct dirent *de;
    while((de=readdir(d))) {
        if(de->d_name[0]=='.')continue;
        char q[MAX_PATH_LEN]; snprintf(q,sizeof(q),"%s/%s",p,de->d_name);
        struct stat st; if(stat(q,&st)==0 && S_ISDIR(st.st_mode)) n++;
    }
    closedir(d); return n;
}

static int write_path_literal(const char *p,const char *v) {
    int fd=open(p,O_WRONLY|O_CLOEXEC); if(fd<0)return -1;
    ssize_t n=write(fd,v,strlen(v)); close(fd); return n==(ssize_t)strlen(v)?0:-1;
}

void trace_discover(TraceState *tr) {
    memset(tr,0,sizeof(*tr)); tr->pipe_fd=-1;
    const char *roots[]={"/sys/kernel/tracing","/sys/kernel/debug/tracing"};
    for(size_t i=0;i<sizeof(roots)/sizeof(roots[0]);i++) {
        if(path_exists(roots[i])) { snprintf(tr->root,sizeof(tr->root),"%s",roots[i]); break; }
    }
    if(!tr->root[0])return;
    char p[MAX_PATH_LEN+128];
    snprintf(p,sizeof(p),"%s/events/amdgpu",tr->root); tr->amdgpu_events=count_event_dirs(p);
    snprintf(p,sizeof(p),"%s/events/drm_sched",tr->root); tr->drm_sched_events=count_event_dirs(p);
    snprintf(p,sizeof(p),"%s/events/drm",tr->root); tr->drm_events=count_event_dirs(p);
    tr->available = tr->amdgpu_events || tr->drm_sched_events || tr->drm_events;
}


static int trace_toggle_matching_drm_events(TraceState *tr, bool enable) {
    char dirp[MAX_PATH_LEN+128];
    snprintf(dirp,sizeof(dirp),"%s/events/drm",tr->instance);
    DIR *d=opendir(dirp);
    if(!d)return 0;
    int n=0; struct dirent *de;
    while((de=readdir(d))) {
        if(de->d_name[0]=='.')continue;
        if(!strstr(de->d_name,"vblank") && !strstr(de->d_name,"flip") &&
           !strstr(de->d_name,"atomic") && !strstr(de->d_name,"commit")) continue;
        char p[MAX_PATH_LEN+256];
        char evdir[MAX_PATH_LEN]; if (!path_join(evdir,sizeof(evdir),dirp,de->d_name) || !path_join(p,sizeof(p),evdir,"enable")) continue;
        if(path_exists(p) && write_path_literal(p,enable?"1":"0")==0)n++;
    }
    closedir(d);
    return n;
}

int trace_start(TraceState *tr) {
    if(!tr->available || tr->active)return tr->active?0:-1;
    char instances[MAX_PATH_LEN], leaf[96];
    snprintf(leaf,sizeof(leaf),"amd-gpu-diagnostic-%d",getpid());
    if (!path_join(instances,sizeof(instances),tr->root,"instances") || !path_join(tr->instance,sizeof(tr->instance),instances,leaf)) return -1;
    if(mkdir(tr->instance,0700)<0 && errno!=EEXIST)return -1;
    char p[MAX_PATH_LEN+128];
    snprintf(p,sizeof(p),"%s/events/amdgpu/enable",tr->instance); if(path_exists(p)) write_path_literal(p,"1");
    snprintf(p,sizeof(p),"%s/events/drm_sched/enable",tr->instance); if(path_exists(p)) write_path_literal(p,"1");
    tr->drm_display_events_enabled=trace_toggle_matching_drm_events(tr,true);
    snprintf(p,sizeof(p),"%s/tracing_on",tr->instance); write_path_literal(p,"1");
    snprintf(p,sizeof(p),"%s/trace_pipe",tr->instance);
    tr->pipe_fd=open(p,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(tr->pipe_fd<0) { rmdir(tr->instance); return -1; }
    tr->active=true;
    return 0;
}

void trace_stop(TraceState *tr) {
    if(!tr->active)return;
    char p[MAX_PATH_LEN+128];
    snprintf(p,sizeof(p),"%s/tracing_on",tr->instance); write_path_literal(p,"0");
    snprintf(p,sizeof(p),"%s/events/amdgpu/enable",tr->instance); if(path_exists(p)) write_path_literal(p,"0");
    snprintf(p,sizeof(p),"%s/events/drm_sched/enable",tr->instance); if(path_exists(p)) write_path_literal(p,"0");
    trace_toggle_matching_drm_events(tr,false);
    if(tr->pipe_fd>=0)close(tr->pipe_fd);
    tr->pipe_fd=-1; tr->active=false;
    rmdir(tr->instance);
}

static void parse_trace_actor(const char *line,char *out,size_t cap) {
    const char *lb=strchr(line,'[');
    if(!lb){out[0]=0;return;}
    const char *p=lb;
    while(p>line && isspace((unsigned char)p[-1]))p--;
    const char *end=p;
    while(p>line && isdigit((unsigned char)p[-1]))p--;
    if(p>line && p[-1]=='-')p--;
    const char *start=p;
    while(start>line && !isspace((unsigned char)start[-1]))start--;
    size_t n=(size_t)(end-start); if(n>=cap)n=cap-1;
    memcpy(out,start,n); out[n]=0; trim(out);
}

void trace_pump(TraceState *tr) {
    if(!tr->active || tr->pipe_fd<0)return;
    char buf[4096];
    for(int loops=0;loops<4;loops++) {
        ssize_t n=read(tr->pipe_fd,buf,sizeof(buf)-1);
        if(n<=0)break;
        buf[n]=0;
        char *save=NULL,*line=strtok_r(buf,"\n",&save);
        while(line) {
            tr->lines++;
            if(strstr(line,"amdgpu"))tr->amdgpu_lines++;
            if(strstr(line,"drm_sched"))tr->sched_lines++;
            if(strstr(line,"drm_") || strstr(line,"vblank") || strstr(line,"flip"))tr->drm_lines++;
            snprintf(tr->last_line,sizeof(tr->last_line),"%s",line);
            parse_trace_actor(line,tr->last_actor,sizeof(tr->last_actor));
            clock_gettime(CLOCK_MONOTONIC,&tr->last_seen);
            line=strtok_r(NULL,"\n",&save);
        }
    }
}
