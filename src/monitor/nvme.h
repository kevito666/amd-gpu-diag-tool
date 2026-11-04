/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only Linux NVMe namespace statistics. I/O is device-wide, not AI-only.
 */
#ifndef AMD_GPU_DIAG_MONITOR_NVME_H
#define AMD_GPU_DIAG_MONITOR_NVME_H
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    char name[32];
    char model[96];
} NvmeDevice;
typedef struct {
    NvmeDevice devices[24];
    int device_count;
    char selected[32];
    bool have_prev, valid;
    uint64_t prev_read_sectors, prev_write_sectors, prev_busy_ms;
    struct timespec prev_time;
    double read_mib_s, write_mib_s, peak_read_mib_s, peak_write_mib_s, busy_pct;
    time_t last_discover;
} NvmeMonitor;

bool nvme_namespace_name(const char *name);
bool nvme_parse_stat(const char *line, uint64_t *rd, uint64_t *wr, uint64_t *busy);
void nvme_discover(NvmeMonitor *nv);
void nvme_select(NvmeMonitor *nv, const char *name);
void nvme_sample(NvmeMonitor *nv);
#endif
