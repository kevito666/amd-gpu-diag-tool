/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only AMDGPU DRM fdinfo client sampling; no Xlib or GPU control writes.
 * The caller owns its ClientMonitor history and calls this on one thread.
 */
#define _GNU_SOURCE
#include "clients.h"
#include "desktop_probe.h"
#include "../core/util.h"
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int read_proc_cmdline(pid_t pid, char *out, size_t cap) {
    char p[64];
    snprintf(p, sizeof(p), "/proc/%d/cmdline", pid);
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = read(fd, out, cap-1);
    close(fd);
    if (n <= 0) { if (cap) out[0]=0; return -1; }
    out[n] = 0;
    for (ssize_t i=0;i<n-1;i++) if (out[i] == 0) out[i] = ' ';
    trim(out);
    return (int)n;
}

static void client_role(const char *comm, const char *cmd, char *out, size_t cap) {
    if ((strstr(comm, "brave") || strstr(comm, "chrome") || strstr(comm, "chromium")) &&
        (strstr(cmd, "--type=gpu") || strstr(cmd, "--type=gpu-process"))) {
        snprintf(out, cap, "%s GPU", strstr(comm, "brave") ? "Brave" : "Chromium");
    } else if (desktop_compositor_process(comm, out, cap)) {
        size_t used = strlen(out);
        if (used + 11 < cap) strncat(out, " compositor", cap-used-1);
    }
    else if (strstr(comm, "telegram") || strstr(comm, "Telegram")) snprintf(out, cap, "Telegram");
    else if (strstr(comm, "Xwayland")) snprintf(out, cap, "Xwayland");
    else if (strcmp(comm, "Xorg") == 0) snprintf(out, cap, "Xorg");
    else if (strstr(comm, "steam")) snprintf(out, cap, "Steam");
    else snprintf(out, cap, "%s", comm);
}

static uint64_t parse_scaled_bytes(const char *v) {
    while (*v && isspace((unsigned char)*v)) v++;
    unsigned long long x = strtoull(v, NULL, 10);
    if (strstr(v, "MiB")) x *= 1024ULL*1024ULL;
    else if (strstr(v, "KiB")) x *= 1024ULL;
    return x;
}

typedef struct {
    unsigned long long id;
    pid_t pid;
} SeenClient;

static bool seen_client(SeenClient *seen, int n, pid_t pid, unsigned long long id) {
    (void)pid;
    for (int i=0;i<n;i++) if (seen[i].id == id) return true;
    return false;
}

int client_monitor_parse_fdinfo(const char *path, const char *wanted_pci, GpuClient *out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    bool amdgpu = false, pci_ok = false;
    unsigned long long cid = ULLONG_MAX;
    uint64_t engine=0,gfx=0,compute=0,dma=0,media=0;
    uint64_t vram_mem=0,vram_res=0,vram_total=0;
    while (fgets(line, sizeof(line), f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = 0;
        char *key = line, *val = colon+1;
        trim(key); trim(val);
        if (strcmp(key, "drm-driver") == 0) amdgpu = strcmp(val, "amdgpu") == 0;
        else if (strcmp(key, "drm-pdev") == 0) {
            pci_ok = strcmp(val, wanted_pci) == 0;
            if (!pci_ok && strlen(wanted_pci) > 5) {
                const char *shortpci = strchr(wanted_pci, ':');
                if (shortpci && strstr(val, shortpci+1)) pci_ok = true;
            }
        } else if (strcmp(key, "drm-client-id") == 0) cid = strtoull(val,NULL,10);
        else if (strncmp(key, "drm-engine-", 11) == 0 && strncmp(key, "drm-engine-capacity-", 20) != 0) {
            uint64_t ns = strtoull(val, NULL, 10);
            engine += ns;
            const char *en = key + 11;
            if (strstr(en,"gfx") || strstr(en,"render")) gfx += ns;
            else if (strstr(en,"compute") || strstr(en,"comp")) compute += ns;
            else if (strstr(en,"dma") || strstr(en,"sdma") || strstr(en,"copy")) dma += ns;
            else if (strstr(en,"vcn") || strstr(en,"video") || strstr(en,"dec") || strstr(en,"enc")) media += ns;
        } else if (strcmp(key, "drm-memory-vram") == 0) vram_mem = parse_scaled_bytes(val);
        else if (strcmp(key, "drm-resident-vram") == 0) vram_res = parse_scaled_bytes(val);
        else if (strcmp(key, "drm-total-vram") == 0) vram_total = parse_scaled_bytes(val);
    }
    fclose(f);
    if (!amdgpu || !pci_ok || cid == ULLONG_MAX) return 0;
    memset(out,0,sizeof(*out));
    out->client_id = cid;
    out->engine_ns=engine; out->gfx_ns=gfx; out->compute_ns=compute; out->dma_ns=dma; out->media_ns=media;
    out->vram_bytes = vram_res ? vram_res : (vram_mem ? vram_mem : vram_total);
    return 1;
}

static int find_client_index(GpuClient *a, int n, pid_t pid) {
    for (int i=0;i<n;i++) if (a[i].pid == pid) return i;
    return -1;
}

static int cmp_clients(const void *aa, const void *bb) {
    const GpuClient *a=aa,*b=bb;
    if (a->activity_pct < b->activity_pct) return 1;
    if (a->activity_pct > b->activity_pct) return -1;
    return a->pid > b->pid ? 1 : -1;
}

void client_monitor_sample(const Gpu *g, ClientMonitor *m) {
    struct timespec wall0,wall1,cpu0,cpu1;
    clock_gettime(CLOCK_MONOTONIC,&wall0);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu0);

    GpuClient cur[MAX_GPU_CLIENTS];
    memset(cur,0,sizeof(cur));
    int ncur=0;
    SeenClient seen[MAX_GPU_CLIENTS*2];
    int nseen=0;

    DIR *pd = opendir("/proc");
    if (!pd) { m->scan_errors++; return; }
    struct dirent *pe;
    while ((pe=readdir(pd)) && ncur < MAX_GPU_CLIENTS) {
        if (!desktop_numeric_name(pe->d_name)) continue;
        pid_t pid=(pid_t)atoi(pe->d_name);
        char fddir[64];
        snprintf(fddir,sizeof(fddir),"/proc/%d/fd",pid);
        DIR *fd=opendir(fddir);
        if (!fd) continue;
        struct dirent *fe;
        while ((fe=readdir(fd)) && ncur < MAX_GPU_CLIENTS) {
            if (!desktop_numeric_name(fe->d_name)) continue;
            char linkp[384],linkto[256];
            snprintf(linkp,sizeof(linkp),"/proc/%d/fd/%s",pid,fe->d_name);
            ssize_t ln=readlink(linkp,linkto,sizeof(linkto)-1);
            if (ln <= 0) continue;
            linkto[ln]=0;
            if (!strstr(linkto,"/dev/dri/")) continue;

            char infop[384];
            snprintf(infop,sizeof(infop),"/proc/%d/fdinfo/%s",pid,fe->d_name);
            GpuClient one;
            int rc=client_monitor_parse_fdinfo(infop,g->pci_addr,&one);
            m->fdinfo_reads++;
            if (rc <= 0) continue;
            if (seen_client(seen,nseen,pid,one.client_id)) continue;
            if (nseen < (int)(sizeof(seen)/sizeof(seen[0]))) {
                seen[nseen].pid=pid; seen[nseen].id=one.client_id; nseen++;
            }
            int ci=find_client_index(cur,ncur,pid);
            if (ci < 0) {
                ci=ncur++;
                cur[ci].pid=pid;
                desktop_read_proc_comm(pid,cur[ci].name,sizeof(cur[ci].name));
                read_proc_cmdline(pid,cur[ci].cmdline,sizeof(cur[ci].cmdline));
                char comm_copy[64];
                snprintf(comm_copy,sizeof(comm_copy),"%s",cur[ci].name);
                client_role(comm_copy,cur[ci].cmdline,cur[ci].role,sizeof(cur[ci].role));
            }
            cur[ci].engine_ns += one.engine_ns;
            cur[ci].gfx_ns += one.gfx_ns;
            cur[ci].compute_ns += one.compute_ns;
            cur[ci].dma_ns += one.dma_ns;
            cur[ci].media_ns += one.media_ns;
            cur[ci].vram_bytes += one.vram_bytes;
        }
        closedir(fd);
    }
    closedir(pd);

    clock_gettime(CLOCK_MONOTONIC,&wall1);
    double dt = m->have_sample ? diff_ns(wall1,m->last_sample) : 0.0;
    if (dt > 0) {
        for (int i=0;i<ncur;i++) {
            int oi=find_client_index(m->items,m->count,cur[i].pid);
            if (oi < 0) continue;
#define DELTA_PCT(field) ((cur[i].field >= m->items[oi].field) ? ((cur[i].field-m->items[oi].field)*100.0/dt) : 0.0)
            cur[i].activity_pct=DELTA_PCT(engine_ns);
            cur[i].gfx_pct=DELTA_PCT(gfx_ns);
            cur[i].compute_pct=DELTA_PCT(compute_ns);
            cur[i].dma_pct=DELTA_PCT(dma_ns);
            cur[i].media_pct=DELTA_PCT(media_ns);
#undef DELTA_PCT
        }
    }
    qsort(cur,ncur,sizeof(cur[0]),cmp_clients);
    memcpy(m->items,cur,sizeof(GpuClient)*ncur);
    m->count=ncur;
    m->last_sample=wall1;
    m->have_sample=true;
    m->scans++;

    clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu1);
    double wall=diff_ns(wall1,wall0)/1e9, cpu=diff_ns(cpu1,cpu0)/1e9;
    m->scan_cpu_pct=wall>0?cpu/wall*100.0:0.0;
}
