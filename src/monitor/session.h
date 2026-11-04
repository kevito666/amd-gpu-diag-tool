/* SPDX-License-Identifier: GPL-3.0-or-later
 * Desktop, compositor and connector snapshot. No Xlib dependency: the X11
 * enrichment remains in the UI/controller translation unit.
 */
#ifndef AMD_DIAG_MONITOR_SESSION_H
#define AMD_DIAG_MONITOR_SESSION_H
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <stddef.h>
#include "../gpu/readback.h"
#include "../config/settings.h"
#ifndef MAX_GPU_CLIENTS
#define MAX_GPU_CLIENTS 128
#endif
#ifndef MAX_CONNECTORS
#define MAX_CONNECTORS 12
#endif
typedef struct {
    pid_t pid;
    unsigned long long client_id;
    char name[64];
    char role[96];
    char cmdline[256];
    uint64_t engine_ns;
    uint64_t gfx_ns;
    uint64_t compute_ns;
    uint64_t dma_ns;
    uint64_t media_ns;
    uint64_t vram_bytes;
    double activity_pct;
    double gfx_pct;
    double compute_pct;
    double dma_pct;
    double media_pct;
} GpuClient;

typedef struct {
    GpuClient items[MAX_GPU_CLIENTS];
    int count;
    struct timespec last_sample;
    bool have_sample;
    uint64_t scans;
    uint64_t fdinfo_reads;
    uint64_t scan_errors;
    double scan_cpu_pct;
} ClientMonitor;

typedef struct {
    char session_type[32];
    char session_version[96];
    char session_built[32];
    char desktop_version[96];
    char desktop_built[32];
    char xserver_version[96];
    char xserver_built[32];
    char wm_version[96];
    char wm_built[32];
    char compositor_version[96];
    char compositor_built[32];
    char refresh[96];
    char vrr_state[80];
    char vsync_state[80];
    char buffer_state[80];
    char desktop[96];
    char compositor[64];
    pid_t compositor_pid;
    char window_manager[64];
    char xserver_vendor[128];
    char display[64];
    char wayland_display[64];
    char connector_summary[256];
    char kms_modes[256];
    char plane_allocators[192];
    int connected_outputs;
    int active_planes;
    int x11_present_possible;
    int wayland_present_possible;
} SessionInfo;

bool desktop_numeric_name(const char *str);
int desktop_read_proc_comm(pid_t pid, char *out, size_t cap);
bool desktop_compositor_process(const char *comm, char *label, size_t cap);
/* Values are reset to a fresh snapshot on each sample. */
void desktop_sample_session(const Gpu *gpu, const Sysfs *sys, SessionInfo *session,
                            const ClientMonitor *clients, CompositorSource source);
/* Arch package metadata only where available; otherwise leaves dates empty. */
void desktop_session_package_info(SessionInfo *session, int vendor_release);
/* Parse a fixture or driver state file. Other /sys files are not needed. */
void desktop_parse_kms_state(const char *path, SessionInfo *session);
#endif
