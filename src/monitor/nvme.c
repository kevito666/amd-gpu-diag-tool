/* SPDX-License-Identifier: GPL-3.0-or-later
 * Linux /sys/block/nvmeXnY/stat provides aggregate sector and busy counters.
 * The selected namespace is never replaced silently when it disappears.
 */
#define _GNU_SOURCE
#include "nvme.h"
#include "../core/util.h"
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_PATH_LEN 512

bool nvme_namespace_name(const char *name) {
    if(strncmp(name,"nvme",4)) return false;
    const char *p=name+4;
    if(!isdigit((unsigned char)*p)) return false;
    while(isdigit((unsigned char)*p)) ++p;
    if(*p++!='n' || !isdigit((unsigned char)*p)) return false;
    while(isdigit((unsigned char)*p)) ++p;
    return !*p;
}

bool nvme_parse_stat(const char *line, uint64_t *rd, uint64_t *wr, uint64_t *busy) {
    unsigned long long a[11]={0};
    if(sscanf(line,"%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
              &a[0],&a[1],&a[2],&a[3],&a[4],&a[5],&a[6],&a[7],&a[8],&a[9],&a[10])!=11) return false;
    *rd=a[2]; *wr=a[6]; *busy=a[9];
    return true;
}

void nvme_discover(NvmeMonitor *nv) {
    DIR *d=opendir("/sys/block");
    if(!d) return;
    nv->device_count=0;
    struct dirent *e;
    while((e=readdir(d)) && nv->device_count<(int)(sizeof(nv->devices)/sizeof(nv->devices[0]))) {
        if(!nvme_namespace_name(e->d_name)) continue;
        NvmeDevice *dev=&nv->devices[nv->device_count];
        text_copy(dev->name,sizeof(dev->name),e->d_name);
        char file[MAX_PATH_LEN];
        snprintf(file,sizeof(file),"/sys/block/%s/device/model",e->d_name);
        FILE *f=fopen(file,"r");
        if(f){ if(!fgets(dev->model,sizeof(dev->model),f)) dev->model[0]=0; fclose(f); trim(dev->model); }
        else text_copy(dev->model,sizeof(dev->model),"NVMe namespace");
        nv->device_count++;
    }
    closedir(d);
    /* If a selected device disappears, do not silently switch to a different
       SSD and mislabel its activity. User must explicitly choose a new one. */
    if(!nv->selected[0] && nv->device_count)
        text_copy(nv->selected,sizeof(nv->selected),nv->devices[0].name);
}

void nvme_select(NvmeMonitor *nv,const char *name) {
    if(strcmp(nv->selected,name)) {
        text_copy(nv->selected,sizeof(nv->selected),name);
        nv->have_prev=false; nv->valid=false;
        nv->read_mib_s=nv->write_mib_s=nv->busy_pct=0;
        nv->peak_read_mib_s=nv->peak_write_mib_s=0;
    }
}

void nvme_sample(NvmeMonitor *nv) {
    time_t now_wall=time(NULL);
    if(!nv->last_discover || now_wall-nv->last_discover>=30) {
        nvme_discover(nv); nv->last_discover=now_wall;
    }
    if(!nv->selected[0] || !nvme_namespace_name(nv->selected)) {nv->valid=false;nv->have_prev=false;return;}
    char path[128];
    snprintf(path,sizeof(path),"/sys/block/%s/stat",nv->selected);
    char line[512];
    FILE *f=fopen(path,"r");
    if(!f) {nv->valid=false;nv->have_prev=false;return;}
    bool ok=fgets(line,sizeof(line),f)!=NULL;
    fclose(f);
    uint64_t rd=0,wr=0,busy=0;
    if(!ok || !nvme_parse_stat(line,&rd,&wr,&busy)) {nv->valid=false;nv->have_prev=false;return;}
    struct timespec tm;
    clock_gettime(CLOCK_MONOTONIC,&tm);
    if(nv->have_prev) {
        double dt=(tm.tv_sec-nv->prev_time.tv_sec)+(tm.tv_nsec-nv->prev_time.tv_nsec)/1e9;
        if(dt>=0.5 && rd>=nv->prev_read_sectors && wr>=nv->prev_write_sectors && busy>=nv->prev_busy_ms) {
            nv->read_mib_s=(rd-nv->prev_read_sectors)/2048.0/dt;
            nv->write_mib_s=(wr-nv->prev_write_sectors)/2048.0/dt;
            nv->busy_pct=fmin(100.0,(busy-nv->prev_busy_ms)/(dt*10.0));
            nv->peak_read_mib_s=fmax(nv->peak_read_mib_s,nv->read_mib_s);
            nv->peak_write_mib_s=fmax(nv->peak_write_mib_s,nv->write_mib_s);
            nv->valid=true;
        } else if(dt<0.5) return;
        else {nv->valid=false;nv->peak_read_mib_s=nv->peak_write_mib_s=0;}
    }
    nv->prev_read_sectors=rd;nv->prev_write_sectors=wr;nv->prev_busy_ms=busy;
    nv->prev_time=tm;nv->have_prev=true;
}
