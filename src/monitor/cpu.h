/* SPDX-License-Identifier: GPL-3.0-or-later
 * Bounded per-core and process CPU sampling from Linux /proc.
 */
#ifndef AMD_GPU_DIAG_MONITOR_CPU_H
#define AMD_GPU_DIAG_MONITOR_CPU_H
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
/* CPU per-core load */
#define MAX_CPU_CORES 128

typedef struct {
    double load_pct[MAX_CPU_CORES];
    int core_count;
    unsigned long long prev_core_total[MAX_CPU_CORES];
    unsigned long long prev_core_idle[MAX_CPU_CORES];
    bool have_sample;
} CpuCoreLoad;

typedef struct {
    pid_t pid;
    char name[16];
    double cpu_pct;
} TopProcess;



bool parse_proc_cpu_stat(const char *line, uint64_t *ticks, uint64_t *start_ticks);
void cpu_sample_top_processes(TopProcess out[3], int *out_count);
void cpu_sample_core_load(CpuCoreLoad *cores);
#endif
