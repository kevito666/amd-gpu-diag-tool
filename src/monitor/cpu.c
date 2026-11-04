/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "cpu.h"
#include "../core/util.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* /proc/PID/stat contains '(comm)' which can include spaces and brackets.
 * Work from the LAST ')' and count Linux fields 14/15 and 22 exactly. */
bool parse_proc_cpu_stat(const char *line, uint64_t *ticks, uint64_t *start_ticks) {
    if (!line || !ticks || !start_ticks) return false;
    const char *pos = strrchr(line, ')');
    if (!pos) return false;
    pos++;
    uint64_t user = 0, kernel = 0, started = 0;
    bool got_user = false, got_kernel = false, got_start = false;
    for (int field = 3; field <= 22; ++field) {
        while (*pos == ' ' || *pos == '\t') pos++;
        if (!*pos) return false;
        const char *end = pos;
        while (*end && *end != ' ' && *end != '\t' && *end != '\n') end++;
        if (field == 14 || field == 15 || field == 22) {
            errno = 0;
            char *number_end = NULL;
            unsigned long long value = strtoull(pos, &number_end, 10);
            if (errno || number_end != end) return false;
            if (field == 14) {user = value; got_user = true;}
            if (field == 15) {kernel = value; got_kernel = true;}
            if (field == 22) {started = value; got_start = true;}
        }
        pos = end;
    }
    if (!got_user || !got_kernel || !got_start || UINT64_MAX - user < kernel) return false;
    *ticks = user + kernel;
    *start_ticks = started;
    return true;
}

/* This is a real interval-based CPU ranking, rather than the previous lifetime
 * CPU-time ranking. Percent is per logical CPU (a multithreaded process can
 * exceed 100%). One /proc sweep every two seconds keeps the GUI lightweight. */
#define TOP_PROC_HISTORY_CAP 512
typedef struct {
    pid_t pid;
    uint64_t start_ticks;
    uint64_t cpu_ticks;
} TopProcSample;
void cpu_sample_top_processes(TopProcess out[3], int *out_count) {
    static TopProcSample previous[TOP_PROC_HISTORY_CAP];
    static int previous_count = 0;
    static struct timespec last = {0};
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long long elapsed_ns = diff_ns(now, last);
    if (last.tv_sec && elapsed_ns < 2000000000LL) return;

    DIR *dir = opendir("/proc");
    if (!dir) return;
    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) { closedir(dir); return; }
    double elapsed = last.tv_sec ? elapsed_ns / 1e9 : 0;
    TopProcSample current[TOP_PROC_HISTORY_CAP];
    int count = 0;
    TopProcess top[3] = {{0}};
    int top_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) && count < TOP_PROC_HISTORY_CAP) {
        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        char *pid_end = NULL;
        long raw_pid = strtol(entry->d_name, &pid_end, 10);
        if (!pid_end || *pid_end || raw_pid <= 0 || raw_pid > INT_MAX) continue;
        pid_t pid = (pid_t)raw_pid;
        char path[96], line[1024];
        int n = snprintf(path, sizeof(path), "/proc/%ld/stat", raw_pid);
        if (n <= 0 || (size_t)n >= sizeof(path) ||
            read_path_text(path, line, sizeof(line)) < 0) continue;
        uint64_t ticks, started;
        if (!parse_proc_cpu_stat(line, &ticks, &started)) continue;
        current[count++] = (TopProcSample){pid, started, ticks};
        if (elapsed <= 0) continue;
        double pct = 0;
        for (int i = 0; i < previous_count; ++i) {
            if (previous[i].pid == pid && previous[i].start_ticks == started &&
                ticks >= previous[i].cpu_ticks) {
                pct = 100.0 * (double)(ticks - previous[i].cpu_ticks) / (hz * elapsed);
                break;
            }
        }
        if (pct <= 0 || (top_count == 3 && pct <= top[2].cpu_pct)) continue;
        char comm[32] = "";
        n = snprintf(path, sizeof(path), "/proc/%ld/comm", raw_pid);
        if (n > 0 && (size_t)n < sizeof(path))
            (void)read_path_text(path, comm, sizeof(comm));
        TopProcess item = {.pid = pid, .cpu_pct = pct};
        text_copy(item.name, sizeof(item.name), comm);
        int at = top_count < 3 ? top_count++ : 2;
        while (at > 0 && pct > top[at-1].cpu_pct) {
            top[at] = top[at-1];
            at--;
        }
        top[at] = item;
    }
    closedir(dir);
    memcpy(previous, current, (size_t)count * sizeof(*previous));
    previous_count = count;
    last = now;
    *out_count = top_count;
    memcpy(out, top, (size_t)top_count * sizeof(top[0]));
}

/* Sample CPU per-core load from /proc/stat */
void cpu_sample_core_load(CpuCoreLoad *cores) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return;

    int core = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != 'c' || line[1] != 'p' || line[2] != 'u') break;
        if (line[3] == ' ') continue;  /* Skip the aggregate 'cpu' line */

        unsigned long long user=0, nice=0, system=0, idle=0, iowait=0, irq=0, softirq=0, steal=0;
        int n = sscanf(line, "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu",
                       &core, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
        if (n < 4) break;

        if (core < 0 || core >= MAX_CPU_CORES) break;
        int idx = core;  /* /proc/stat numbering starts at cpu0 */

        unsigned long long idle_all = idle + iowait;
        unsigned long long total = user + nice + system + idle_all + irq + softirq + steal;

        if (cores->have_sample && total >= cores->prev_core_total[idx] &&
            idle_all >= cores->prev_core_idle[idx]) {
            unsigned long long dt = total - cores->prev_core_total[idx];
            unsigned long long di = idle_all - cores->prev_core_idle[idx];
            cores->load_pct[idx] = (dt && di <= dt) ? (double)(dt - di) * 100.0 / (double)dt : 0.0;
        }

        cores->prev_core_total[idx] = total;
        cores->prev_core_idle[idx] = idle_all;
        if (idx + 1 > cores->core_count) {
            cores->core_count = idx + 1;
        }
    }
    fclose(f);

    if (!cores->have_sample) {
        cores->have_sample = true;
    }
}
