/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Kevin Kolasa
 *
 * AMD GPU Diagnostic Tool
 * Lightweight X11/XLibre AMDGPU diagnostics and power-policy control.
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/time.h>

#define VERSION "0.5.2"
#define MAX_PATH_LEN 512
#define MAX_TEXT 4096
#define MAX_LEVELS 32
#define UI_REFRESH_NS 500000000LL
#define DEFAULT_INTERVAL_US 5000
#define DEFAULT_DPM_INTERVAL_US 250000
#define MIN_DPM_INTERVAL_US 50000
#define MAX_DPM_INTERVAL_US 2000000
#define MIN_INTERVAL_US 500
#define UNSAFE_MIN_INTERVAL_US 10
#define MAX_INTERVAL_US 1000000
#define ADAPTIVE_BURST_INTERVAL_US 500
#define ADAPTIVE_BURST_NS 250000000LL

#define CLIENT_SAMPLE_NS 500000000LL
#define POWER_SAMPLE_NS 100000000LL
#define SESSION_SAMPLE_NS 5000000000LL
#define SYSTEM_SAMPLE_NS 500000000LL
#define NOMINAL_BOARD_VOLTAGE 12.0
#define VOLTAGE_BAR_MAX_V 1.20
#define CURRENT_BAR_MAX_A 50.0
#define POWER_BAR_MAX_W 600.0
#define MAX_GPU_CLIENTS 128
#define MAX_DIAG_EVENTS 96
#define MAX_TRACE_LINE 512
#define MAX_CONNECTORS 12

typedef struct {
    int card_num;
    char card_name[32];
    char base[MAX_PATH_LEN];
    char pci_addr[64];
    char device_id[32];
    char full_name[160];
    unsigned long long vram_bytes;
} Gpu;

typedef struct {
    int index;
    long mhz;
    bool active;
    bool sleep_level;
    char raw[128];
} DpmLevel;

typedef struct {
    int fd_policy_r;
    int fd_policy_w;
    int fd_sclk_r;
    int fd_sclk_w;
    int fd_mclk_r;
    int fd_mclk_w;
    int fd_busy;
    int fd_mem_busy;
    int fd_temp;
    int fd_power;
    int fd_freq1;
    int fd_freq2;
    int fd_fan_rpm;
    int fd_pwm;
    int fd_pwm_max;
    int fd_voltage;
    int fd_current;
    int fd_vram_used;
    int fd_power_profile;
    int fd_pcie_bw;
    char policy_path[MAX_PATH_LEN];
    char sclk_path[MAX_PATH_LEN];
    char mclk_path[MAX_PATH_LEN];
    char busy_path[MAX_PATH_LEN];
    char mem_busy_path[MAX_PATH_LEN];
    char temp_path[MAX_PATH_LEN];
    char power_path[MAX_PATH_LEN];
    char freq1_path[MAX_PATH_LEN];
    char freq2_path[MAX_PATH_LEN];
    char freq1_label[32];
    char freq2_label[32];
    char fan_rpm_path[MAX_PATH_LEN];
    char pwm_path[MAX_PATH_LEN];
    char pwm_max_path[MAX_PATH_LEN];
    char voltage_path[MAX_PATH_LEN];
    char voltage_label[32];
    char current_path[MAX_PATH_LEN];
    char current_label[32];
    char vram_used_path[MAX_PATH_LEN];
    char power_profile_path[MAX_PATH_LEN];
    char pcie_bw_path[MAX_PATH_LEN];
    char gpu_metrics_path[MAX_PATH_LEN];
    char debugfs_dir[MAX_PATH_LEN];
    char gfxoff_path[MAX_PATH_LEN];
    char gfxoff_status_path[MAX_PATH_LEN];
    char drm_state_path[MAX_PATH_LEN];
} Sysfs;

typedef struct {
    char policy[64];
    bool enabled;
    bool lock_sclk;
    bool lock_mclk;
    int sclk_level;
    int mclk_level;
} Target;

typedef struct {
    uint64_t checks;
    uint64_t dpm_checks;
    uint64_t corrections;
    uint64_t policy_mismatches;
    uint64_t sclk_mismatches;
    uint64_t mclk_mismatches;
    uint64_t verify_failures;
    uint64_t read_errors;
    uint64_t write_errors;
    uint64_t observed_sclk_changes;
    uint64_t observed_mclk_changes;
    uint64_t deadline_misses;
    uint64_t burst_entries;
    int last_active_sclk;
    int last_active_mclk;
    bool have_last_sclk;
    bool have_last_mclk;
    char sclk_display[256];
    char mclk_display[256];
    double loop_cpu_pct;
    double loop_hz;
} Stats;

typedef enum {
    VIEW_DASHBOARD = 0,
    VIEW_CLIENTS,
    VIEW_POWER,
    VIEW_DESKTOP,
    VIEW_EVENTS,
    VIEW_TRACE,
    VIEW_HELP,
    VIEW_ABOUT
} View;

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

typedef struct {
    int gpu_busy;
    int mem_busy;
    double temp_c;
    double power_w;
    double voltage_v;
    double current_a;
    double equivalent_input_ohm;
    bool current_is_estimate;
    char current_label[32];
    int fan_rpm;
    double fan_pct;
    long long gfx_hz;
    long long mem_hz;
    char freq1_label[32];
    char freq2_label[32];
    unsigned long long vram_used;
    char power_profile[192];
    char pcie_bw[192];
    bool gpu_metrics_present;
    int gpu_metrics_format;
    int gpu_metrics_content;
    int gfxoff_enabled;
    int gfxoff_status;
    uint64_t gfxoff_status_changes;
    int last_gfxoff_status;
    bool have_gfxoff_status;
    unsigned slow_tick;
} PowerTelemetry;

typedef struct {
    double cpu_pct;
    unsigned cpu_threads;
    unsigned long long ram_total_bytes;
    unsigned long long ram_used_bytes;
    double ram_pct;
    unsigned long long prev_cpu_total;
    unsigned long long prev_cpu_idle;
    bool have_cpu_sample;
} SystemTelemetry;

typedef struct {
    struct timespec when;
    char kind[32];
    char detail[192];
    char actor[96];
    double actor_activity;
    char trace_actor[96];
} DiagEvent;

typedef struct {
    DiagEvent ring[MAX_DIAG_EVENTS];
    int head;
    int count;
    uint64_t total;
    FILE *logf;
    bool logging;
    char log_path[MAX_PATH_LEN];
} EventLog;

typedef struct {
    bool available;
    bool active;
    char root[MAX_PATH_LEN];
    char instance[MAX_PATH_LEN];
    int pipe_fd;
    int amdgpu_events;
    int drm_sched_events;
    int drm_events;
    int drm_display_events_enabled;
    uint64_t lines;
    uint64_t amdgpu_lines;
    uint64_t sched_lines;
    uint64_t drm_lines;
    char last_line[MAX_TRACE_LINE];
    char last_actor[96];
    struct timespec last_seen;
} TraceState;

static volatile sig_atomic_t g_stop = 0;

static const char *POLICIES[] = {
    "auto",
    "low",
    "high",
    "profile_standard",
    "profile_min_sclk",
    "profile_min_mclk",
    "profile_peak",
    "manual"
};
static const size_t POLICY_COUNT = sizeof(POLICIES) / sizeof(POLICIES[0]);

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static long long ts_ns(struct timespec t) {
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static struct timespec ns_ts(long long ns) {
    struct timespec t;
    if (ns < 0) ns = 0;
    t.tv_sec = ns / 1000000000LL;
    t.tv_nsec = ns % 1000000000LL;
    return t;
}

static void add_ns(struct timespec *t, long long ns) {
    long long x = ts_ns(*t) + ns;
    *t = ns_ts(x);
}

static long long diff_ns(struct timespec a, struct timespec b) {
    return ts_ns(a) - ts_ns(b);
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

static void text_copy(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) src = "";
    size_t n = strnlen(src, cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool path_join(char *out, size_t cap, const char *base, const char *leaf) {
    if (!out || cap == 0 || !base || !leaf) { errno = EINVAL; return false; }
    size_t a = strlen(base), b = strlen(leaf);
    bool slash = a > 0 && base[a - 1] != '/' && b > 0 && leaf[0] != '/';
    bool trim = a > 0 && base[a - 1] == '/' && b > 0 && leaf[0] == '/';
    size_t skip = trim ? 1u : 0u;
    if (a + (slash ? 1u : 0u) + b - skip + 1u > cap) { errno = ENAMETOOLONG; out[0] = '\0'; return false; }
    memcpy(out, base, a);
    size_t pos = a;
    if (slash) out[pos++] = '/';
    memcpy(out + pos, leaf + skip, b - skip);
    out[pos + b - skip] = '\0';
    return true;
}

static int read_fd_text(int fd, char *buf, size_t cap) {
    if (fd < 0 || cap < 2) { errno = EINVAL; return -1; }
    ssize_t n;
    do { n = pread(fd, buf, cap - 1, 0); } while (n < 0 && errno == EINTR);
    if (n < 0) return -1;
    buf[n] = '\0';
    trim(buf);
    return (int)n;
}

static int read_path_text(const char *path, char *buf, size_t cap) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    int rc = read_fd_text(fd, buf, cap);
    close(fd);
    return rc;
}

static int write_fd_text(int fd, const char *text) {
    if (fd < 0 || !text) { errno = EINVAL; return -1; }
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "%s\n", text);
    if (len <= 0 || (size_t)len >= sizeof(buf)) { errno = EOVERFLOW; return -1; }
    (void)lseek(fd, 0, SEEK_SET);
    ssize_t n;
    do { n = write(fd, buf, (size_t)len); } while (n < 0 && errno == EINTR);
    if (n != len) { if (n >= 0) errno = EIO; return -1; }
    return 0;
}

static int open_rw_pair(const char *path, int *rfd, int *wfd) {
    *rfd = -1; *wfd = -1;
    int r = open(path, O_RDONLY | O_CLOEXEC);
    if (r < 0) return -1;
    int w = open(path, O_WRONLY | O_CLOEXEC);
    if (w < 0) { int saved = errno; close(r); errno = saved; return -1; }
    *rfd = r; *wfd = w;
    return 0;
}

static bool path_exists(const char *p) {
    return access(p, F_OK) == 0;
}

typedef struct {
    unsigned device;
    const char *name;
} AmdGpuName;

/* Marketing names for common discrete cards. This is intentionally only a
   small exact-name table; unknown devices fall back to the system pci.ids
   database so the program remains useful on newer hardware. */
static const AmdGpuName AMD_GPU_NAMES[] = {
    {0x744c, "AMD Radeon RX 7900 XTX"},
    {0x7448, "AMD Radeon RX 7900 XTX"},
    {0x744e, "AMD Radeon RX 7900 XT"},
    {0x747e, "AMD Radeon RX 7800 XT"},
    {0x7480, "AMD Radeon RX 7700 XT"},
    {0x7483, "AMD Radeon RX 7600 XT"},
    {0x73bf, "AMD Radeon RX 6800 XT / RX 6800"},
    {0x73df, "AMD Radeon RX 6700 XT"},
    {0x73ff, "AMD Radeon RX 6900 XT"},
    {0x73ef, "AMD Radeon RX 6650 XT"},
    {0x1638, "AMD Radeon RX 6600 XT"},
    {0x731f, "AMD Radeon RX 5700 XT / RX 5700"},
    {0x687f, "AMD Radeon RX Vega 56 / Vega 64"},
    {0x66af, "AMD Radeon VII"},
};

static bool lookup_pci_ids_name(unsigned device, char *out, size_t cap) {
    static const char *paths[] = {
        "/usr/share/hwdata/pci.ids",
        "/usr/share/misc/pci.ids",
        "/usr/share/pci.ids"
    };
    FILE *f = NULL;
    for (size_t i = 0; i < sizeof(paths)/sizeof(paths[0]); i++) {
        f = fopen(paths[i], "r");
        if (f) break;
    }
    if (!f) return false;

    char line[512];
    bool in_amd = false;
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        if (line[0] != '\t' && !isspace((unsigned char)line[0])) {
            unsigned vendor = 0;
            if (sscanf(line, "%4x", &vendor) == 1) in_amd = (vendor == 0x1002);
            else in_amd = false;
            continue;
        }
        if (!in_amd || line[0] != '\t' || line[1] == '\t') continue;

        unsigned dev = 0;
        char name[400] = {0};
        if (sscanf(line + 1, "%4x %399[^\n]", &dev, name) == 2 && dev == device) {
            trim(name);
            snprintf(out, cap, "AMD %s", name);
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

static void resolve_gpu_name(Gpu *g) {
    char p[MAX_PATH_LEN], txt[256];
    unsigned device = (unsigned)strtoul(g->device_id, NULL, 0);

    /* Prefer our exact marketing-name table for known cards. A generic board
       product string such as "Navi 31" is less useful in the UI. */
    for (size_t i = 0; i < sizeof(AMD_GPU_NAMES)/sizeof(AMD_GPU_NAMES[0]); i++) {
        if (AMD_GPU_NAMES[i].device == device) {
            text_copy(g->full_name, sizeof(g->full_name), AMD_GPU_NAMES[i].name);
            return;
        }
    }

    /* Some boards/drivers expose a useful product string. */
    if (!path_join(p, sizeof(p), g->base, "product_name")) return;
    if (read_path_text(p, txt, sizeof(txt)) >= 0 && txt[0]) {
        text_copy(g->full_name, sizeof(g->full_name), txt);
        return;
    }

    if (lookup_pci_ids_name(device, g->full_name, sizeof(g->full_name))) return;
    snprintf(g->full_name, sizeof(g->full_name), "AMD Radeon GPU");
}

static int detect_amd_gpus(Gpu *out, int max) {
    int count = 0;
    for (int n = 0; n < 64 && count < max; n++) {
        char dev[MAX_PATH_LEN], vendor[MAX_PATH_LEN], txt[128];
        snprintf(dev, sizeof(dev), "/sys/class/drm/card%d/device", n);
        if (!path_join(vendor, sizeof(vendor), dev, "vendor")) continue;
        if (read_path_text(vendor, txt, sizeof(txt)) < 0) continue;
        if (strcmp(txt, "0x1002") != 0) continue;

        Gpu *g = &out[count++];
        memset(g, 0, sizeof(*g));
        g->card_num = n;
        snprintf(g->card_name, sizeof(g->card_name), "card%d", n);
        text_copy(g->base, sizeof(g->base), dev);

        char p[MAX_PATH_LEN];
        if (!path_join(p, sizeof(p), dev, "device")) continue;
        if (read_path_text(p, txt, sizeof(txt)) >= 0)
            text_copy(g->device_id, sizeof(g->device_id), txt);
        else
            snprintf(g->device_id, sizeof(g->device_id), "unknown");

        char real[MAX_PATH_LEN];
        if (realpath(dev, real)) {
            const char *last = strrchr(real, '/');
            text_copy(g->pci_addr, sizeof(g->pci_addr), last ? last + 1 : real);
        } else {
            snprintf(g->pci_addr, sizeof(g->pci_addr), "unknown");
        }

        if (!path_join(p, sizeof(p), dev, "mem_info_vram_total")) continue;
        if (read_path_text(p, txt, sizeof(txt)) >= 0)
            g->vram_bytes = strtoull(txt, NULL, 10);
        resolve_gpu_name(g);
    }
    return count;
}

static int choose_gpu(Gpu *gpus, int count, int requested_card) {
    if (count <= 0) return -1;
    if (requested_card >= 0) {
        for (int i = 0; i < count; i++) if (gpus[i].card_num == requested_card) return i;
        return -1;
    }
    if (count == 1) return 0;

    int best = 0;
    for (int i = 1; i < count; i++) {
        if (gpus[i].vram_bytes > gpus[best].vram_bytes) best = i;
    }
    return best;
}

static void sysfs_close(Sysfs *s) {
    int *fds[] = {&s->fd_policy_r,&s->fd_policy_w,&s->fd_sclk_r,&s->fd_sclk_w,
                  &s->fd_mclk_r,&s->fd_mclk_w,&s->fd_busy,&s->fd_mem_busy,&s->fd_temp,&s->fd_power,
                  &s->fd_freq1,&s->fd_freq2,&s->fd_fan_rpm,&s->fd_pwm,&s->fd_pwm_max,&s->fd_voltage,&s->fd_current,
                  &s->fd_vram_used,&s->fd_power_profile,&s->fd_pcie_bw};
    for (size_t i = 0; i < sizeof(fds)/sizeof(fds[0]); i++) {
        if (*fds[i] >= 0) close(*fds[i]);
        *fds[i] = -1;
    }
}

static void find_hwmon(const Gpu *g, Sysfs *s) {
    char dirp[MAX_PATH_LEN];
    if (!path_join(dirp, sizeof(dirp), g->base, "hwmon")) return;
    DIR *d = opendir(dirp);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0) continue;
        char base[MAX_PATH_LEN];
        if (!path_join(base, sizeof(base), dirp, de->d_name)) continue;
        char p[MAX_PATH_LEN], txt[128];

        if (!path_join(p, sizeof(p), base, "temp1_input")) continue;
        if (path_exists(p)) {
            snprintf(s->temp_path, sizeof(s->temp_path), "%s", p);
            s->fd_temp = open(p, O_RDONLY | O_CLOEXEC);
        }
        if (!path_join(p, sizeof(p), base, "power1_average")) continue;
        if (path_exists(p)) {
            snprintf(s->power_path, sizeof(s->power_path), "%s", p);
            s->fd_power = open(p, O_RDONLY | O_CLOEXEC);
        }
        if (!path_join(p, sizeof(p), base, "freq1_input")) continue;
        if (path_exists(p)) {
            snprintf(s->freq1_path, sizeof(s->freq1_path), "%s", p);
            s->fd_freq1 = open(p, O_RDONLY | O_CLOEXEC);
            if (!path_join(p, sizeof(p), base, "freq1_label")) continue;
            if (read_path_text(p, txt, sizeof(txt)) >= 0) text_copy(s->freq1_label, sizeof(s->freq1_label), txt);
            else snprintf(s->freq1_label, sizeof(s->freq1_label), "freq1");
        }
        if (!path_join(p, sizeof(p), base, "freq2_input")) continue;
        if (path_exists(p)) {
            snprintf(s->freq2_path, sizeof(s->freq2_path), "%s", p);
            s->fd_freq2 = open(p, O_RDONLY | O_CLOEXEC);
            if (!path_join(p, sizeof(p), base, "freq2_label")) continue;
            if (read_path_text(p, txt, sizeof(txt)) >= 0) text_copy(s->freq2_label, sizeof(s->freq2_label), txt);
            else snprintf(s->freq2_label, sizeof(s->freq2_label), "freq2");
        }

        if (!path_join(p, sizeof(p), base, "fan1_input")) continue;
        if (path_exists(p)) {
            snprintf(s->fan_rpm_path, sizeof(s->fan_rpm_path), "%s", p);
            s->fd_fan_rpm = open(p, O_RDONLY | O_CLOEXEC);
        }
        if (!path_join(p, sizeof(p), base, "pwm1")) continue;
        if (path_exists(p)) {
            snprintf(s->pwm_path, sizeof(s->pwm_path), "%s", p);
            s->fd_pwm = open(p, O_RDONLY | O_CLOEXEC);
        }
        if (!path_join(p, sizeof(p), base, "pwm1_max")) continue;
        if (path_exists(p)) {
            snprintf(s->pwm_max_path, sizeof(s->pwm_max_path), "%s", p);
            s->fd_pwm_max = open(p, O_RDONLY | O_CLOEXEC);
        }

        /* AMD hwmon normally exposes the core rail as an in*_input voltage.
           Prefer a labelled VDD/GFX rail when present, otherwise use in0_input. */
        char fallback_voltage[MAX_PATH_LEN] = "";
        char fallback_label[32] = "voltage";
        for (int vi = 0; vi < 16; vi++) {
            char inp[MAX_PATH_LEN], lab[MAX_PATH_LEN], lbl[64] = "";
            char leaf[32]; snprintf(leaf, sizeof(leaf), "in%d_input", vi); if (!path_join(inp, sizeof(inp), base, leaf)) continue;
            if (!path_exists(inp)) continue;
            snprintf(leaf, sizeof(leaf), "in%d_label", vi); if (!path_join(lab, sizeof(lab), base, leaf)) continue;
            read_path_text(lab, lbl, sizeof(lbl));
            if (!fallback_voltage[0]) {
                snprintf(fallback_voltage, sizeof(fallback_voltage), "%s", inp);
                if (lbl[0]) snprintf(fallback_label, sizeof(fallback_label), "%.31s", lbl);
            }
            char lower[64];
            size_t ll = strlen(lbl);
            if (ll >= sizeof(lower)) ll = sizeof(lower)-1;
            for (size_t li=0; li<ll; li++) lower[li] = (char)tolower((unsigned char)lbl[li]);
            lower[ll] = 0;
            if (strstr(lower,"vddgfx") || strstr(lower,"gfx") || strstr(lower,"vdd")) {
                snprintf(s->voltage_path, sizeof(s->voltage_path), "%s", inp);
                text_copy(s->voltage_label, sizeof(s->voltage_label), lbl[0] ? lbl : "VDDGFX");
                s->fd_voltage = open(inp, O_RDONLY | O_CLOEXEC);
                break;
            }
        }
        if (s->fd_voltage < 0 && fallback_voltage[0]) {
            snprintf(s->voltage_path, sizeof(s->voltage_path), "%s", fallback_voltage);
            snprintf(s->voltage_label, sizeof(s->voltage_label), "%s", fallback_label);
            s->fd_voltage = open(fallback_voltage, O_RDONLY | O_CLOEXEC);
        }

        /* hwmon current channels, when present, are reported in milliamperes.
           Prefer a labelled GPU/board rail. If none exists, current is estimated
           from board power and the nominal 12 V board input. */
        char fallback_current[MAX_PATH_LEN] = "";
        char fallback_current_label[32] = "current";
        for (int ci = 1; ci <= 16; ci++) {
            char inp[MAX_PATH_LEN], lab[MAX_PATH_LEN], lbl[64] = "";
            char leaf[32];
            snprintf(leaf, sizeof(leaf), "curr%d_input", ci);
            if (!path_join(inp, sizeof(inp), base, leaf) || !path_exists(inp)) continue;
            snprintf(leaf, sizeof(leaf), "curr%d_label", ci);
            if (path_join(lab, sizeof(lab), base, leaf)) (void)read_path_text(lab, lbl, sizeof(lbl));
            if (!fallback_current[0]) {
                snprintf(fallback_current, sizeof(fallback_current), "%s", inp);
                if (lbl[0]) snprintf(fallback_current_label, sizeof(fallback_current_label), "%.31s", lbl);
            }
            char lower[64];
            size_t ll = strlen(lbl); if (ll >= sizeof(lower)) ll = sizeof(lower) - 1;
            for (size_t li = 0; li < ll; li++) lower[li] = (char)tolower((unsigned char)lbl[li]);
            lower[ll] = 0;
            if (strstr(lower, "gpu") || strstr(lower, "gfx") || strstr(lower, "board") || strstr(lower, "total") || strstr(lower, "vdd")) {
                snprintf(s->current_path, sizeof(s->current_path), "%s", inp);
                text_copy(s->current_label, sizeof(s->current_label), lbl[0] ? lbl : "GPU current");
                s->fd_current = open(inp, O_RDONLY | O_CLOEXEC);
                break;
            }
        }
        if (s->fd_current < 0 && fallback_current[0]) {
            snprintf(s->current_path, sizeof(s->current_path), "%s", fallback_current);
            snprintf(s->current_label, sizeof(s->current_label), "%s", fallback_current_label);
            s->fd_current = open(fallback_current, O_RDONLY | O_CLOEXEC);
        }
        break;
    }
    closedir(d);
}

static void find_debugfs(const Gpu *g, Sysfs *s) {
    const char *bases[] = {"/sys/kernel/debug/dri", "/sys/kernel/tracing"};
    (void)bases;
    char dri_root[MAX_PATH_LEN] = "/sys/kernel/debug/dri";
    DIR *d = opendir(dri_root);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!isdigit((unsigned char)de->d_name[0])) continue;
        char dir[MAX_PATH_LEN], namep[MAX_PATH_LEN], txt[256];
        if (!path_join(dir, sizeof(dir), dri_root, de->d_name)) continue;
        if (!path_join(namep, sizeof(namep), dir, "name")) continue;
        if (read_path_text(namep, txt, sizeof(txt)) < 0) continue;
        if (!strstr(txt, "amdgpu")) continue;
        if (g->pci_addr[0] && strcmp(g->pci_addr, "unknown") != 0 && !strstr(txt, g->pci_addr)) {
            /* Some kernels omit the domain. Accept a matching bus:slot.func suffix. */
            const char *shortpci = strchr(g->pci_addr, ':');
            if (!shortpci || !strstr(txt, shortpci + 1)) continue;
        }
        text_copy(s->debugfs_dir, sizeof(s->debugfs_dir), dir);
        (void)path_join(s->gfxoff_path, sizeof(s->gfxoff_path), dir, "amdgpu_gfxoff");
        (void)path_join(s->gfxoff_status_path, sizeof(s->gfxoff_status_path), dir, "amdgpu_gfxoff_status");
        (void)path_join(s->drm_state_path, sizeof(s->drm_state_path), dir, "state");
        break;
    }
    closedir(d);
}

static int sysfs_open(const Gpu *g, Sysfs *s) {
    memset(s, 0, sizeof(*s));
    s->fd_policy_r = s->fd_policy_w = s->fd_sclk_r = s->fd_sclk_w = -1;
    s->fd_mclk_r = s->fd_mclk_w = s->fd_busy = s->fd_mem_busy = -1;
    s->fd_temp = s->fd_power = s->fd_freq1 = s->fd_freq2 = -1;
    s->fd_fan_rpm = s->fd_pwm = s->fd_pwm_max = s->fd_voltage = s->fd_current = -1;
    s->fd_vram_used = s->fd_power_profile = s->fd_pcie_bw = -1;

    if (!path_join(s->policy_path, sizeof(s->policy_path), g->base, "power_dpm_force_performance_level")) return -1;
    if (!path_join(s->sclk_path, sizeof(s->sclk_path), g->base, "pp_dpm_sclk")) return -1;
    if (!path_join(s->mclk_path, sizeof(s->mclk_path), g->base, "pp_dpm_mclk")) return -1;
    if (!path_join(s->busy_path, sizeof(s->busy_path), g->base, "gpu_busy_percent")) return -1;
    if (!path_join(s->mem_busy_path, sizeof(s->mem_busy_path), g->base, "mem_busy_percent")) return -1;
    if (!path_join(s->vram_used_path, sizeof(s->vram_used_path), g->base, "mem_info_vram_used")) return -1;
    if (!path_join(s->power_profile_path, sizeof(s->power_profile_path), g->base, "pp_power_profile_mode")) return -1;
    if (!path_join(s->pcie_bw_path, sizeof(s->pcie_bw_path), g->base, "pcie_bw")) return -1;
    if (!path_join(s->gpu_metrics_path, sizeof(s->gpu_metrics_path), g->base, "gpu_metrics")) return -1;

    if (open_rw_pair(s->policy_path, &s->fd_policy_r, &s->fd_policy_w) < 0) {
        sysfs_close(s);
        return -1;
    }
    if (path_exists(s->sclk_path)) open_rw_pair(s->sclk_path, &s->fd_sclk_r, &s->fd_sclk_w);
    if (path_exists(s->mclk_path)) open_rw_pair(s->mclk_path, &s->fd_mclk_r, &s->fd_mclk_w);
    if (path_exists(s->busy_path)) s->fd_busy = open(s->busy_path, O_RDONLY | O_CLOEXEC);
    if (path_exists(s->mem_busy_path)) s->fd_mem_busy = open(s->mem_busy_path, O_RDONLY | O_CLOEXEC);
    if (path_exists(s->vram_used_path)) s->fd_vram_used = open(s->vram_used_path, O_RDONLY | O_CLOEXEC);
    if (path_exists(s->power_profile_path)) s->fd_power_profile = open(s->power_profile_path, O_RDONLY | O_CLOEXEC);
    if (path_exists(s->pcie_bw_path)) s->fd_pcie_bw = open(s->pcie_bw_path, O_RDONLY | O_CLOEXEC);
    find_hwmon(g, s);
    find_debugfs(g, s);
    return 0;
}

static int parse_dpm_levels(const char *text, DpmLevel *levels, int max) {
    int count = 0;
    const char *p = text;
    while (*p && count < max) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len >= sizeof(levels[count].raw)) len = sizeof(levels[count].raw) - 1;
        char line[128];
        memcpy(line, p, len);
        line[len] = '\0';
        trim(line);

        DpmLevel l;
        memset(&l, 0, sizeof(l));
        l.index = -1;
        l.mhz = -1;
        l.active = strchr(line, '*') != NULL;
        l.sleep_level = false;
        snprintf(l.raw, sizeof(l.raw), "%s", line);

        if (strncmp(line, "S:", 2) == 0) {
            l.sleep_level = true;
            l.index = -2;
            char *mhz = strstr(line, "Mhz");
            if (mhz) {
                char *q = mhz;
                while (q > line && isdigit((unsigned char)q[-1])) q--;
                l.mhz = strtol(q, NULL, 10);
            }
            levels[count++] = l;
        } else {
            char *end = NULL;
            long idx = strtol(line, &end, 10);
            if (end && end != line && *end == ':') {
                l.index = (int)idx;
                const char *mhz = strstr(end, "Mhz");
                if (mhz) {
                    const char *q = mhz;
                    while (q > end && isdigit((unsigned char)q[-1])) q--;
                    l.mhz = strtol(q, NULL, 10);
                }
                levels[count++] = l;
            }
        }
        if (!e) break;
        p = e + 1;
    }
    return count;
}

static int active_level_from_text(const char *text) {
    DpmLevel levels[MAX_LEVELS];
    int n = parse_dpm_levels(text, levels, MAX_LEVELS);
    for (int i = 0; i < n; i++) if (levels[i].active) return levels[i].index;
    return INT_MIN;
}

static bool valid_policy(const char *p) {
    for (size_t i = 0; i < POLICY_COUNT; i++) if (strcmp(p, POLICIES[i]) == 0) return true;
    return false;
}

static int apply_policy(Sysfs *s, const char *policy) {
    return write_fd_text(s->fd_policy_w, policy);
}

static int apply_level(int fd, int level) {
    char b[32];
    snprintf(b, sizeof(b), "%d", level);
    return write_fd_text(fd, b);
}

static bool selected_level_enabled(const char *text, int level) {
    DpmLevel levels[MAX_LEVELS];
    int n = parse_dpm_levels(text, levels, MAX_LEVELS);
    /* With one manually enabled level it should normally be the active (*) level.
       If firmware does not expose an asterisk reliably, presence + active is the strongest
       generic verification available through pp_dpm_* without ASIC-specific ioctls. */
    for (int i = 0; i < n; i++) {
        if (levels[i].index == level) return levels[i].active;
    }
    return false;
}

static void clock_line_from_text(const char *text, char *out, size_t cap) {
    DpmLevel levels[MAX_LEVELS];
    int n = parse_dpm_levels(text, levels, MAX_LEVELS);
    bool first = true;
    out[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (!levels[i].active) continue;
        char item[64];
        if (levels[i].index == -2) snprintf(item, sizeof(item), "S:%ld MHz", levels[i].mhz);
        else if (levels[i].mhz >= 0) snprintf(item, sizeof(item), "L%d %ld MHz", levels[i].index, levels[i].mhz);
        else snprintf(item, sizeof(item), "L%d", levels[i].index);
        size_t used = strlen(out);
        if (!first && used + 2 < cap) { strncat(out, ", ", cap - used - 1); used = strlen(out); }
        if (used + 1 < cap) strncat(out, item, cap - used - 1);
        first = false;
    }
    if (first) snprintf(out, cap, "no active marker");
}

static int reapply_manual_locks(Sysfs *s, const Target *t, Stats *st) {
    if (strcmp(t->policy, "manual") != 0) return 0;
    if (t->lock_sclk) {
        if (apply_level(s->fd_sclk_w, t->sclk_level) == 0) st->corrections++;
        else st->write_errors++;
    }
    if (t->lock_mclk) {
        if (apply_level(s->fd_mclk_w, t->mclk_level) == 0) st->corrections++;
        else st->write_errors++;
    }
    return 0;
}

/* Fast path: only touch power_dpm_force_performance_level.
   pp_dpm_* reads are intentionally kept out of this loop because they are
   dramatically more expensive on some RDNA cards. */
static int enforce_policy_once(Sysfs *s, const Target *t, Stats *st) {
    char policy[128], verify[128];

    if (read_fd_text(s->fd_policy_r, policy, sizeof(policy)) < 0) {
        st->read_errors++;
        return -1;
    }
    st->checks++;

    if (strcmp(policy, t->policy) == 0) return 0;

    st->policy_mismatches++;
    if (apply_policy(s, t->policy) < 0) {
        st->write_errors++;
        return -1;
    }
    st->corrections++;

    /* A policy reset can also discard manual DPM masks. Restore them
       immediately instead of waiting for the slow telemetry pass. */
    reapply_manual_locks(s, t, st);

    /* Immediate readback. Sysfs writes are synchronous enough that this is
       a useful "did it stick?" check without adding delay to the normal path. */
    if (read_fd_text(s->fd_policy_r, verify, sizeof(verify)) < 0) {
        st->read_errors++;
        return 1;
    }
    if (strcmp(verify, t->policy) != 0) {
        st->verify_failures++;
        return 1;
    }
    return 1;
}

/* Slow path: inspect the expensive pp_dpm_* attributes and repair manual
   clock locks. This runs independently of the fast policy watchdog rate. */
static void monitor_dpm_once(Sysfs *s, const Target *t, Stats *st, bool enforce) {
    char policy[128], sclk[MAX_TEXT], mclk[MAX_TEXT];
    bool manual_ready = false;
    st->dpm_checks++;

    if (s->fd_sclk_r >= 0) {
        if (read_fd_text(s->fd_sclk_r, sclk, sizeof(sclk)) >= 0) {
            clock_line_from_text(sclk, st->sclk_display, sizeof(st->sclk_display));
            int a = active_level_from_text(sclk);
            if (st->have_last_sclk && a != INT_MIN && a != st->last_active_sclk)
                st->observed_sclk_changes++;
            if (a != INT_MIN) {
                st->last_active_sclk = a;
                st->have_last_sclk = true;
            }

            if (enforce && t->lock_sclk && !selected_level_enabled(sclk, t->sclk_level)) {
                st->sclk_mismatches++;
                if (read_fd_text(s->fd_policy_r, policy, sizeof(policy)) >= 0) {
                    manual_ready = strcmp(policy, "manual") == 0;
                } else {
                    st->read_errors++;
                }
                if (!manual_ready) {
                    if (apply_policy(s, "manual") == 0) {
                        st->corrections++;
                        manual_ready = true;
                    } else {
                        st->write_errors++;
                    }
                }
                if (manual_ready) {
                    if (apply_level(s->fd_sclk_w, t->sclk_level) == 0) st->corrections++;
                    else st->write_errors++;
                }
            }
        } else st->read_errors++;
    }

    if (s->fd_mclk_r >= 0) {
        if (read_fd_text(s->fd_mclk_r, mclk, sizeof(mclk)) >= 0) {
            clock_line_from_text(mclk, st->mclk_display, sizeof(st->mclk_display));
            int a = active_level_from_text(mclk);
            if (st->have_last_mclk && a != INT_MIN && a != st->last_active_mclk)
                st->observed_mclk_changes++;
            if (a != INT_MIN) {
                st->last_active_mclk = a;
                st->have_last_mclk = true;
            }

            if (enforce && t->lock_mclk && !selected_level_enabled(mclk, t->mclk_level)) {
                st->mclk_mismatches++;
                if (!manual_ready) {
                    if (read_fd_text(s->fd_policy_r, policy, sizeof(policy)) >= 0)
                        manual_ready = strcmp(policy, "manual") == 0;
                    else
                        st->read_errors++;
                }
                if (!manual_ready) {
                    if (apply_policy(s, "manual") == 0) {
                        st->corrections++;
                        manual_ready = true;
                    } else {
                        st->write_errors++;
                    }
                }
                if (manual_ready) {
                    if (apply_level(s->fd_mclk_w, t->mclk_level) == 0) st->corrections++;
                    else st->write_errors++;
                }
            }
        } else st->read_errors++;
    }
}


static int read_u32_binary_path(const char *path, uint32_t *out) {
    if (!path || !*path) return -1;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    uint32_t v = 0;
    ssize_t n = pread(fd, &v, sizeof(v), 0);
    close(fd);
    if (n != (ssize_t)sizeof(v)) return -1;
    *out = v;
    return 0;
}

static void parse_active_power_profile(int fd, char *out, size_t cap) {
    char buf[MAX_TEXT];
    if (fd < 0 || read_fd_text(fd, buf, sizeof(buf)) < 0) {
        snprintf(out, cap, "unavailable");
        return;
    }
    const char *p = buf;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e-p) : strlen(p);
        if (len >= 256) len = 255;
        char line[256];
        memcpy(line, p, len); line[len] = 0; trim(line);
        if (strchr(line, '*')) {
            text_copy(out, cap, line);
            return;
        }
        if (!e) break;
        p = e + 1;
    }
    const char *nl = strchr(buf, '\n');
    size_t len = nl ? (size_t)(nl-buf) : strlen(buf);
    if (len >= cap) len = cap-1;
    memcpy(out, buf, len); out[len] = 0;
}

static long read_long_fd(int fd, long fallback) {
    char buf[128];
    if (fd < 0 || read_fd_text(fd, buf, sizeof(buf)) < 0) return fallback;
    errno = 0;
    char *end = NULL;
    long value = strtol(buf, &end, 10);
    if (errno || end == buf) return fallback;
    return value;
}

static void sample_power(Sysfs *s, PowerTelemetry *p) {
    p->gpu_busy = (int)read_long_fd(s->fd_busy, -1);
    p->mem_busy = (int)read_long_fd(s->fd_mem_busy, -1);
    long tm = read_long_fd(s->fd_temp, -1);
    long pw = read_long_fd(s->fd_power, -1);
    p->temp_c = tm >= 0 ? tm / 1000.0 : -1.0;
    p->power_w = pw >= 0 ? pw / 1000000.0 : -1.0;
    long mv = read_long_fd(s->fd_voltage, -1);
    p->voltage_v = mv >= 0 ? mv / 1000.0 : -1.0;
    long ma = read_long_fd(s->fd_current, -1);
    if (ma >= 0) {
        p->current_a = ma / 1000.0;
        p->current_is_estimate = false;
        snprintf(p->current_label, sizeof(p->current_label), "%s", s->current_label[0] ? s->current_label : "GPU current");
    } else if (p->power_w >= 0.0) {
        p->current_a = p->power_w / NOMINAL_BOARD_VOLTAGE;
        p->current_is_estimate = true;
        snprintf(p->current_label, sizeof(p->current_label), "board est. P/12V");
    } else {
        p->current_a = -1.0;
        p->current_is_estimate = true;
        snprintf(p->current_label, sizeof(p->current_label), "unavailable");
    }
    p->equivalent_input_ohm = p->power_w > 0.0 ?
        (NOMINAL_BOARD_VOLTAGE * NOMINAL_BOARD_VOLTAGE) / p->power_w : -1.0;
    p->fan_rpm = (int)read_long_fd(s->fd_fan_rpm, -1);
    long pwm = read_long_fd(s->fd_pwm, -1);
    long pwm_max = read_long_fd(s->fd_pwm_max, -1);
    if (pwm >= 0) {
        if (pwm_max <= 0) pwm_max = 255;
        p->fan_pct = pwm_max > 0 ? (double)pwm * 100.0 / (double)pwm_max : -1.0;
        if (p->fan_pct < 0.0) p->fan_pct = 0.0;
        if (p->fan_pct > 100.0) p->fan_pct = 100.0;
    } else p->fan_pct = -1.0;
    p->gfx_hz = read_long_fd(s->fd_freq1, -1);
    p->mem_hz = read_long_fd(s->fd_freq2, -1);
    snprintf(p->freq1_label, sizeof(p->freq1_label), "%s", s->freq1_label[0] ? s->freq1_label : "freq1");
    snprintf(p->freq2_label, sizeof(p->freq2_label), "%s", s->freq2_label[0] ? s->freq2_label : "freq2");
    long vu = read_long_fd(s->fd_vram_used, -1);
    if (vu >= 0) p->vram_used = (unsigned long long)vu;
    /* These are useful for diagnosis but may involve more driver/SMU work.
       Sample them at ~1 Hz instead of putting them on the 10 Hz fast telemetry path. */
    if ((p->slow_tick++ % 10U) == 0U || !p->power_profile[0]) {
        parse_active_power_profile(s->fd_power_profile, p->power_profile, sizeof(p->power_profile));
        if (s->fd_pcie_bw >= 0) {
            if (read_fd_text(s->fd_pcie_bw, p->pcie_bw, sizeof(p->pcie_bw)) < 0)
                snprintf(p->pcie_bw, sizeof(p->pcie_bw), "unavailable");
        } else snprintf(p->pcie_bw, sizeof(p->pcie_bw), "unavailable");

        p->gpu_metrics_present = path_exists(s->gpu_metrics_path);
        p->gpu_metrics_format = p->gpu_metrics_content = -1;
        if (p->gpu_metrics_present) {
            int fd = open(s->gpu_metrics_path, O_RDONLY | O_CLOEXEC);
            if (fd >= 0) {
                unsigned char hdr[4] = {0};
                if (read(fd, hdr, sizeof(hdr)) == 4) {
                    /* metrics_table_header: uint16 structure_size, uint8 format_revision, uint8 content_revision */
                    p->gpu_metrics_format = hdr[2];
                    p->gpu_metrics_content = hdr[3];
                }
                close(fd);
            }
        }
    }

    uint32_t v;
    p->gfxoff_enabled = read_u32_binary_path(s->gfxoff_path, &v) == 0 ? (int)v : -1;
    if (read_u32_binary_path(s->gfxoff_status_path, &v) == 0) {
        p->gfxoff_status = (int)v;
        if (p->have_gfxoff_status && p->gfxoff_status != p->last_gfxoff_status)
            p->gfxoff_status_changes++;
        p->last_gfxoff_status = p->gfxoff_status;
        p->have_gfxoff_status = true;
    } else p->gfxoff_status = -1;
}

static void sample_system(SystemTelemetry *s) {
    FILE *f = fopen("/proc/stat", "r");
    if (f) {
        char line[512];
        if (fgets(line, sizeof(line), f)) {
            unsigned long long user=0,nice=0,system=0,idle=0,iowait=0,irq=0,softirq=0,steal=0;
            int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                           &user,&nice,&system,&idle,&iowait,&irq,&softirq,&steal);
            if (n >= 4) {
                unsigned long long idle_all = idle + (n >= 5 ? iowait : 0);
                unsigned long long total = user + nice + system + idle_all + irq + softirq + steal;
                if (s->have_cpu_sample && total >= s->prev_cpu_total && idle_all >= s->prev_cpu_idle) {
                    unsigned long long dt = total - s->prev_cpu_total;
                    unsigned long long di = idle_all - s->prev_cpu_idle;
                    s->cpu_pct = (dt && di <= dt) ? (double)(dt - di) * 100.0 / (double)dt : 0.0;
                }
                s->prev_cpu_total = total;
                s->prev_cpu_idle = idle_all;
                s->have_cpu_sample = true;
            }
        }
        fclose(f);
    }

    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        unsigned long long total_kb = 0, avail_kb = 0;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "MemTotal: %llu kB", &total_kb) == 1) continue;
            if (sscanf(line, "MemAvailable: %llu kB", &avail_kb) == 1) continue;
        }
        fclose(f);
        if (total_kb > 0) {
            if (avail_kb > total_kb) avail_kb = total_kb;
            s->ram_total_bytes = total_kb * 1024ULL;
            s->ram_used_bytes = (total_kb - avail_kb) * 1024ULL;
            s->ram_pct = (double)s->ram_used_bytes * 100.0 / (double)s->ram_total_bytes;
        }
    }
    long threads = sysconf(_SC_NPROCESSORS_ONLN);
    s->cpu_threads = threads > 0 ? (unsigned)threads : 0U;
}

static const char *gfxoff_text(int status) {
    switch (status) {
        case 0: return "GFXOFF";
        case 1: return "exit transition";
        case 2: return "awake";
        case 3: return "entry transition";
        default: return "unavailable";
    }
}

static int numeric_name(const char *s) {
    if (!s || !*s) return 0;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

static int read_proc_comm(pid_t pid, char *out, size_t cap) {
    char p[64];
    snprintf(p, sizeof(p), "/proc/%d/comm", pid);
    return read_path_text(p, out, cap);
}

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

static bool compositor_process(const char *comm, char *label, size_t cap) {
    if (!comm || !*comm) return false;
    if (strstr(comm, "kwin_wayland")) snprintf(label, cap, "KWin Wayland");
    else if (strstr(comm, "kwin_x11")) snprintf(label, cap, "KWin X11");
    else if (strcasecmp(comm, "picom") == 0) snprintf(label, cap, "picom");
    else if (strcasecmp(comm, "compton") == 0) snprintf(label, cap, "Compton");
    else if (strcasecmp(comm, "xcompmgr") == 0) snprintf(label, cap, "xcompmgr");
    else if (strcasecmp(comm, "gnome-shell") == 0) snprintf(label, cap, "GNOME Shell");
    else if (strcasecmp(comm, "mutter") == 0) snprintf(label, cap, "Mutter");
    else if (strcasecmp(comm, "cinnamon") == 0) snprintf(label, cap, "Cinnamon");
    else if (strcasecmp(comm, "muffin") == 0) snprintf(label, cap, "Muffin");
    else if (strcasecmp(comm, "xfwm4") == 0) snprintf(label, cap, "Xfwm4");
    else if (strcasecmp(comm, "marco") == 0) snprintf(label, cap, "Marco");
    else if (strcasecmp(comm, "compiz") == 0) snprintf(label, cap, "Compiz");
    else if (strcasecmp(comm, "sway") == 0) snprintf(label, cap, "Sway");
    else if (strstr(comm, "Hyprland")) snprintf(label, cap, "Hyprland");
    else if (strcasecmp(comm, "weston") == 0) snprintf(label, cap, "Weston");
    else if (strstr(comm, "gamescope")) snprintf(label, cap, "gamescope");
    else return false;
    return true;
}

static void client_role(const char *comm, const char *cmd, char *out, size_t cap) {
    if ((strstr(comm, "brave") || strstr(comm, "chrome") || strstr(comm, "chromium")) &&
        (strstr(cmd, "--type=gpu") || strstr(cmd, "--type=gpu-process"))) {
        snprintf(out, cap, "%s GPU", strstr(comm, "brave") ? "Brave" : "Chromium");
    } else if (compositor_process(comm, out, cap)) {
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

static int parse_fdinfo_client(const char *path, const char *wanted_pci, GpuClient *out) {
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

static void sample_clients(const Gpu *g, ClientMonitor *m) {
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
        if (!numeric_name(pe->d_name)) continue;
        pid_t pid=(pid_t)atoi(pe->d_name);
        char fddir[64];
        snprintf(fddir,sizeof(fddir),"/proc/%d/fd",pid);
        DIR *fd=opendir(fddir);
        if (!fd) continue;
        struct dirent *fe;
        while ((fe=readdir(fd)) && ncur < MAX_GPU_CLIENTS) {
            if (!numeric_name(fe->d_name)) continue;
            char linkp[384],linkto[256];
            snprintf(linkp,sizeof(linkp),"/proc/%d/fd/%s",pid,fe->d_name);
            ssize_t ln=readlink(linkp,linkto,sizeof(linkto)-1);
            if (ln <= 0) continue;
            linkto[ln]=0;
            if (!strstr(linkto,"/dev/dri/")) continue;

            char infop[384];
            snprintf(infop,sizeof(infop),"/proc/%d/fdinfo/%s",pid,fe->d_name);
            GpuClient one;
            int rc=parse_fdinfo_client(infop,g->pci_addr,&one);
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
                read_proc_comm(pid,cur[ci].name,sizeof(cur[ci].name));
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

static const GpuClient *top_client(const ClientMonitor *m) {
    return m->count > 0 ? &m->items[0] : NULL;
}

static void read_env_value(pid_t pid, const char *key, char *out, size_t cap) {
    char p[64];
    snprintf(p,sizeof(p),"/proc/%d/environ",pid);
    int fd=open(p,O_RDONLY|O_CLOEXEC);
    if(fd<0)return;
    char buf[16384];
    ssize_t n=read(fd,buf,sizeof(buf)-1);
    close(fd);
    if(n<=0)return;
    buf[n]=0;
    size_t klen=strlen(key);
    for(ssize_t i=0;i<n;) {
        char *e=&buf[i];
        size_t l=strlen(e);
        if(strncmp(e,key,klen)==0 && e[klen]=='=') {
            snprintf(out,cap,"%s",e+klen+1);
            return;
        }
        i += (ssize_t)l+1;
    }
}


static void sample_kms_state(const Sysfs *s, SessionInfo *si) {
    if(!s->drm_state_path[0]) return;
    FILE *f=fopen(s->drm_state_path,"r");
    if(!f)return;
    char line[768];
    bool in_plane=false, plane_active=false;
    bool first_mode=true, first_alloc=true;
    while(fgets(line,sizeof(line),f)) {
        trim(line);
        if(strncmp(line,"plane[",6)==0) {
            in_plane=true; plane_active=false;
            continue;
        }
        if(strncmp(line,"crtc[",5)==0 || strncmp(line,"connector[",10)==0) {
            in_plane=false;
        }
        if(in_plane && strncmp(line,"crtc=",5)==0) {
            plane_active = strcmp(line+5,"(null)")!=0;
            if(plane_active) si->active_planes++;
        } else if(in_plane && plane_active && strncmp(line,"allocated by = ",15)==0) {
            const char *who=line+15;
            char who_short[64];
            snprintf(who_short,sizeof(who_short),"%.63s",who);
            if(!strstr(si->plane_allocators,who_short)) {
                if(!first_alloc) strncat(si->plane_allocators,", ",sizeof(si->plane_allocators)-strlen(si->plane_allocators)-1);
                strncat(si->plane_allocators,who_short,sizeof(si->plane_allocators)-strlen(si->plane_allocators)-1);
                first_alloc=false;
            }
        } else if(strncmp(line,"mode: \"",7)==0 && !strstr(line,"mode: \"\":")) {
            const char *q=line+7;
            const char *end=strchr(q,'"');
            if(end) {
                char name[80]={0};
                size_t n=(size_t)(end-q); if(n>=sizeof(name))n=sizeof(name)-1;
                memcpy(name,q,n);
                const char *colon=strchr(end,':');
                long refresh=-1;
                if(colon) refresh=strtol(colon+1,NULL,10);
                char one[96];
                if(refresh>0)snprintf(one,sizeof(one),"%.70s@%ld",name,refresh);
                else snprintf(one,sizeof(one),"%s",name);
                if(!strstr(si->kms_modes,one)) {
                    if(!first_mode)strncat(si->kms_modes,", ",sizeof(si->kms_modes)-strlen(si->kms_modes)-1);
                    strncat(si->kms_modes,one,sizeof(si->kms_modes)-strlen(si->kms_modes)-1);
                    first_mode=false;
                }
            }
        }
    }
    fclose(f);
    if(!si->kms_modes[0])snprintf(si->kms_modes,sizeof(si->kms_modes),"current KMS modes unavailable");
    if(!si->plane_allocators[0])snprintf(si->plane_allocators,sizeof(si->plane_allocators),"not reported");
}

static bool directory_has_prefix(const char *dirpath, const char *prefix) {
    DIR *d = opendir(dirpath);
    if (!d) return false;
    bool found = false;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, prefix, strlen(prefix)) == 0) { found = true; break; }
    }
    closedir(d);
    return found;
}

static bool xlibre_installed(void) {
    /* Arch-family package database is the cleanest zero-dependency signal on
       systems where XLibre replaces xorg-server. Keep generic file fallbacks
       for other packaging layouts. */
    if (directory_has_prefix("/var/lib/pacman/local", "xlibre-xserver-")) return true;
    if (directory_has_prefix("/var/lib/pacman/local", "xlibre-meta-")) return true;
    if (path_exists("/usr/lib/pkgconfig/xlibre-server.pc")) return true;
    if (path_exists("/usr/share/pkgconfig/xlibre-server.pc")) return true;
    return false;
}

static bool session_is_x11(const SessionInfo *si) {
    return strcasestr(si->session_type, "x11") != NULL;
}

static bool session_is_wayland(const SessionInfo *si) {
    return strcasestr(si->session_type, "wayland") != NULL;
}

static void set_compositor_from_pid(SessionInfo *si, pid_t pid, const char *comm, const char *label) {
    snprintf(si->compositor,sizeof(si->compositor),"%s",label && *label ? label : comm);
    si->compositor_pid=pid;
    if(!si->session_type[0]) read_env_value(pid,"XDG_SESSION_TYPE",si->session_type,sizeof(si->session_type));
    if(!si->desktop[0]) read_env_value(pid,"XDG_CURRENT_DESKTOP",si->desktop,sizeof(si->desktop));
    if(!si->display[0]) read_env_value(pid,"DISPLAY",si->display,sizeof(si->display));
    if(!si->wayland_display[0]) read_env_value(pid,"WAYLAND_DISPLAY",si->wayland_display,sizeof(si->wayland_display));
}

static void sample_session(const Gpu *g, const Sysfs *s, SessionInfo *si, const ClientMonitor *cm) {
    memset(si,0,sizeof(*si));
    const char *e;
    if ((e=getenv("XDG_SESSION_TYPE"))) snprintf(si->session_type,sizeof(si->session_type),"%s",e);
    if ((e=getenv("XDG_CURRENT_DESKTOP"))) snprintf(si->desktop,sizeof(si->desktop),"%s",e);
    if ((e=getenv("DISPLAY"))) snprintf(si->display,sizeof(si->display),"%s",e);
    if ((e=getenv("WAYLAND_DISPLAY"))) snprintf(si->wayland_display,sizeof(si->wayland_display),"%s",e);

    uid_t wanted=(uid_t)-1;
    const char *su=getenv("SUDO_UID");
    if(su && *su) wanted=(uid_t)strtoul(su,NULL,10);

    /* First reuse the DRM client inventory. If picom/KWin/etc already owns a
       DRM client, that is stronger evidence than an unrelated /proc name scan. */
    if (cm) {
        for (int i=0; i<cm->count && !si->compositor[0]; i++) {
            char label[64];
            if (compositor_process(cm->items[i].name,label,sizeof(label)))
                set_compositor_from_pid(si,cm->items[i].pid,cm->items[i].name,label);
        }
    }

    DIR *pd=opendir("/proc");
    if(pd) {
        struct dirent *pe;
        while((pe=readdir(pd))) {
            if(!numeric_name(pe->d_name)) continue;
            pid_t pid=(pid_t)atoi(pe->d_name);
            char pp[64]; snprintf(pp,sizeof(pp),"/proc/%d",pid);
            struct stat st;
            if(stat(pp,&st)<0) continue;
            if(wanted!=(uid_t)-1 && st.st_uid!=wanted) continue;
            char comm[128];
            if(read_proc_comm(pid,comm,sizeof(comm))<0) continue;
            char label[64];
            if(!si->compositor[0] && compositor_process(comm,label,sizeof(label))) {
                set_compositor_from_pid(si,pid,comm,label);
            }
        }
        closedir(pd);
    }
    if(!si->session_type[0]) {
        if(si->wayland_display[0] || strstr(si->compositor,"wayland") || strstr(si->compositor,"Hypr")) snprintf(si->session_type,sizeof(si->session_type),"wayland");
        else if(si->display[0] || strstr(si->compositor,"x11")) snprintf(si->session_type,sizeof(si->session_type),"x11");
        else snprintf(si->session_type,sizeof(si->session_type),"unknown");
    }
    if (strcasecmp(si->session_type,"x11") == 0 && xlibre_installed())
        snprintf(si->session_type,sizeof(si->session_type),"X11Libre");
    else if (strcasecmp(si->session_type,"x11") == 0)
        snprintf(si->session_type,sizeof(si->session_type),"X11");
    else if (strcasecmp(si->session_type,"wayland") == 0)
        snprintf(si->session_type,sizeof(si->session_type),"Wayland");
    if(!si->desktop[0]) snprintf(si->desktop,sizeof(si->desktop),"unknown");
    if(!si->compositor[0]) snprintf(si->compositor,sizeof(si->compositor),"unknown (no matching process)");
    si->x11_present_possible = session_is_x11(si);
    si->wayland_present_possible = session_is_wayland(si);

    DIR *dd=opendir("/sys/class/drm");
    if(dd) {
        struct dirent *de;
        char prefix[64]; snprintf(prefix,sizeof(prefix),"%s-",g->card_name);
        bool first=true;
        while((de=readdir(dd)) && si->connected_outputs<MAX_CONNECTORS) {
            if(strncmp(de->d_name,prefix,strlen(prefix))!=0) continue;
            char p[512],status[64];
            snprintf(p,sizeof(p),"/sys/class/drm/%s/status",de->d_name);
            if(read_path_text(p,status,sizeof(status))<0 || strcmp(status,"connected")!=0) continue;
            si->connected_outputs++;
            snprintf(p,sizeof(p),"/sys/class/drm/%s/modes",de->d_name);
            char modes[256]="";
            read_path_text(p,modes,sizeof(modes));
            char *nl=strchr(modes,'\n'); if(nl)*nl=0;
            const char *conn=de->d_name+strlen(prefix);
            char one[96];
            snprintf(one,sizeof(one),"%.31s%s%.55s",conn,modes[0]?" ":"",modes);
            if(!first) strncat(si->connector_summary,", ",sizeof(si->connector_summary)-strlen(si->connector_summary)-1);
            strncat(si->connector_summary,one,sizeof(si->connector_summary)-strlen(si->connector_summary)-1);
            first=false;
        }
        closedir(dd);
    }
    if(!si->connector_summary[0]) snprintf(si->connector_summary,sizeof(si->connector_summary),"no connected outputs detected");
    sample_kms_state(s,si);
}

static void event_log_add(EventLog *el, const char *kind, const char *detail,
                          const ClientMonitor *cm, const TraceState *tr) {
    DiagEvent ev;
    memset(&ev,0,sizeof(ev));
    clock_gettime(CLOCK_MONOTONIC,&ev.when);
    snprintf(ev.kind,sizeof(ev.kind),"%s",kind);
    snprintf(ev.detail,sizeof(ev.detail),"%s",detail?detail:"");
    const GpuClient *tc=top_client(cm);
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

static const DiagEvent *event_last(const EventLog *el) {
    if(el->count<=0)return NULL;
    int i=el->head-1; if(i<0)i+=MAX_DIAG_EVENTS;
    return &el->ring[i];
}

static void toggle_event_logging(EventLog *el) {
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

static void trace_discover(TraceState *tr) {
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

static int trace_start(TraceState *tr) {
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

static void trace_stop(TraceState *tr) {
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

static void trace_pump(TraceState *tr) {
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


/* ------------------------------------------------------------------------- */
/* X11 / XLibre GUI                                                           */
/* ------------------------------------------------------------------------- */

typedef struct {
    Display *dpy;
    int screen;
    Window root;
    Window win;
    GC gc;
    XFontStruct *font;
    XFontStruct *font_bold;
    Atom wm_delete;
    int width;
    int height;
    unsigned long bg;
    unsigned long panel;
    unsigned long panel_alt;
    unsigned long fg;
    unsigned long dim;
    unsigned long accent;
    unsigned long good;
    unsigned long warn;
    unsigned long bad;
    bool dirty;
} Gui;

typedef struct {
    int x, y, w, h;
} Rect;

static unsigned long gui_color(Gui *ui, const char *name, unsigned long fallback) {
    XColor exact, screen;
    Colormap cmap = DefaultColormap(ui->dpy, ui->screen);
    if (XAllocNamedColor(ui->dpy, cmap, name, &screen, &exact)) return screen.pixel;
    return fallback;
}

static void gui_set_font(Gui *ui, bool bold) {
    XFontStruct *f = bold && ui->font_bold ? ui->font_bold : ui->font;
    if (f) XSetFont(ui->dpy, ui->gc, f->fid);
}

static void gui_text(Gui *ui, int x, int y, unsigned long color, bool bold,
                     const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    XSetForeground(ui->dpy, ui->gc, color);
    gui_set_font(ui, bold);
    XDrawString(ui->dpy, ui->win, ui->gc, x, y, buf, (int)strlen(buf));
}

static void gui_fill(Gui *ui, Rect r, unsigned long color) {
    if (r.w <= 0 || r.h <= 0) return;
    XSetForeground(ui->dpy, ui->gc, color);
    XFillRectangle(ui->dpy, ui->win, ui->gc, r.x, r.y, (unsigned)r.w, (unsigned)r.h);
}

static void gui_outline(Gui *ui, Rect r, unsigned long color) {
    if (r.w <= 0 || r.h <= 0) return;
    XSetForeground(ui->dpy, ui->gc, color);
    XDrawRectangle(ui->dpy, ui->win, ui->gc, r.x, r.y, (unsigned)(r.w - 1), (unsigned)(r.h - 1));
}

static void gui_bar(Gui *ui, Rect r, double pct, const char *label) {
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    gui_fill(ui, r, ui->panel_alt);
    Rect fill = r;
    fill.w = (int)((double)r.w * pct / 100.0);
    gui_fill(ui, fill, ui->accent);
    gui_outline(ui, r, ui->dim);
    if (label && *label) gui_text(ui, r.x + 6, r.y + r.h - 6, ui->fg, true, "%s", label);
}

static bool point_in(Rect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static int x11_window_pid(Display *dpy, Window w, pid_t *pid_out) {
    Atom pid_atom = XInternAtom(dpy, "_NET_WM_PID", True);
    if (pid_atom == None) return -1;
    Atom type = None;
    int format = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *data = NULL;
    int rc = XGetWindowProperty(dpy, w, pid_atom, 0, 1, False, XA_CARDINAL,
                                &type, &format, &nitems, &bytes_after, &data);
    if (rc != Success || !data || nitems < 1 || format != 32) {
        if (data) XFree(data);
        return -1;
    }
    unsigned long value = *(unsigned long *)data;
    XFree(data);
    *pid_out = (pid_t)value;
    return 0;
}

static int x11_window_name(Display *dpy, Window w, char *out, size_t cap) {
    Atom name_atom = XInternAtom(dpy, "_NET_WM_NAME", True);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", True);
    if (name_atom != None && utf8 != None) {
        Atom type = None;
        int format = 0;
        unsigned long nitems = 0, bytes_after = 0;
        unsigned char *data = NULL;
        if (XGetWindowProperty(dpy, w, name_atom, 0, 256, False, utf8,
                               &type, &format, &nitems, &bytes_after, &data) == Success &&
            data && nitems > 0) {
            size_t n = nitems < cap - 1 ? nitems : cap - 1;
            memcpy(out, data, n);
            out[n] = '\0';
            XFree(data);
            return 0;
        }
        if (data) XFree(data);
    }
    char *legacy = NULL;
    if (XFetchName(dpy, w, &legacy) && legacy) {
        snprintf(out, cap, "%s", legacy);
        XFree(legacy);
        return 0;
    }
    return -1;
}

static void x11_probe_session(Gui *ui, SessionInfo *si) {
    const char *vendor = ServerVendor(ui->dpy);
    snprintf(si->xserver_vendor, sizeof(si->xserver_vendor), "%s", vendor ? vendor : "unknown");
    if (vendor && strcasestr(vendor, "xlibre"))
        snprintf(si->session_type, sizeof(si->session_type), "X11Libre");
    else if (xlibre_installed())
        snprintf(si->session_type, sizeof(si->session_type), "X11Libre");
    else
        snprintf(si->session_type, sizeof(si->session_type), "X11");
    snprintf(si->display, sizeof(si->display), "%s", DisplayString(ui->dpy));
    si->x11_present_possible = 1;
    si->wayland_present_possible = 0;

    char atom_name[64];
    snprintf(atom_name, sizeof(atom_name), "_NET_WM_CM_S%d", ui->screen);
    Atom cm = XInternAtom(ui->dpy, atom_name, True);
    if (cm != None) {
        Window owner = XGetSelectionOwner(ui->dpy, cm);
        if (owner != None) {
            pid_t pid = 0;
            char comm[128] = "";
            char label[64] = "";
            if (x11_window_pid(ui->dpy, owner, &pid) == 0 && read_proc_comm(pid, comm, sizeof(comm)) == 0) {
                if (!compositor_process(comm, label, sizeof(label))) text_copy(label, sizeof(label), comm);
                snprintf(si->compositor, sizeof(si->compositor), "%s", label);
                si->compositor_pid = pid;
            } else {
                char name[128] = "";
                if (x11_window_name(ui->dpy, owner, name, sizeof(name)) == 0)
                    text_copy(si->compositor, sizeof(si->compositor), name);
            }
        }
    }

    Atom wmcheck = XInternAtom(ui->dpy, "_NET_SUPPORTING_WM_CHECK", True);
    if (wmcheck != None) {
        Atom type = None;
        int format = 0;
        unsigned long nitems = 0, bytes_after = 0;
        unsigned char *data = NULL;
        if (XGetWindowProperty(ui->dpy, ui->root, wmcheck, 0, 1, False, XA_WINDOW,
                               &type, &format, &nitems, &bytes_after, &data) == Success &&
            data && nitems > 0) {
            Window w = *(Window *)data;
            char name[128] = "";
            if (x11_window_name(ui->dpy, w, name, sizeof(name)) == 0)
                text_copy(si->window_manager, sizeof(si->window_manager), name);
        }
        if (data) XFree(data);
    }
    if (!si->window_manager[0]) snprintf(si->window_manager, sizeof(si->window_manager), "unknown");
}

static int gui_init(Gui *ui, SessionInfo *si) {
    memset(ui, 0, sizeof(*ui));
    ui->dpy = XOpenDisplay(NULL);
    if (!ui->dpy) {
        fprintf(stderr, "Cannot open X11 display. This GUI is X11/XLibre-only.\n");
        fprintf(stderr, "When launching through sudo, preserve DISPLAY/XAUTHORITY, e.g. sudo -E ./amd-gpu-diagnostic\n");
        return -1;
    }
    ui->screen = DefaultScreen(ui->dpy);
    ui->root = RootWindow(ui->dpy, ui->screen);
    ui->width = 1100;
    ui->height = 910;
    ui->bg = gui_color(ui, "#101214", BlackPixel(ui->dpy, ui->screen));
    ui->panel = gui_color(ui, "#1a1d21", ui->bg);
    ui->panel_alt = gui_color(ui, "#262a2f", ui->panel);
    ui->fg = gui_color(ui, "#e5e7eb", WhitePixel(ui->dpy, ui->screen));
    ui->dim = gui_color(ui, "#7f8792", ui->fg);
    ui->accent = gui_color(ui, "#4ea1ff", ui->fg);
    ui->good = gui_color(ui, "#68d391", ui->fg);
    ui->warn = gui_color(ui, "#f6c85f", ui->fg);
    ui->bad = gui_color(ui, "#ff6b6b", ui->fg);

    ui->win = XCreateSimpleWindow(ui->dpy, ui->root, 80, 80, (unsigned)ui->width, (unsigned)ui->height,
                                  0, ui->dim, ui->bg);
    XStoreName(ui->dpy, ui->win, "AMD GPU Diagnostic Tool - XLibre");
    XSelectInput(ui->dpy, ui->win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    ui->wm_delete = XInternAtom(ui->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ui->dpy, ui->win, &ui->wm_delete, 1);
    XSizeHints hints = {0};
    hints.flags = PMinSize;
    hints.min_width = 960;
    hints.min_height = 820;
    XSetWMNormalHints(ui->dpy, ui->win, &hints);

    ui->gc = XCreateGC(ui->dpy, ui->win, 0, NULL);
    ui->font = XLoadQueryFont(ui->dpy, "9x15");
    if (!ui->font) ui->font = XLoadQueryFont(ui->dpy, "fixed");
    ui->font_bold = XLoadQueryFont(ui->dpy, "9x15bold");
    if (!ui->font_bold) ui->font_bold = ui->font;
    gui_set_font(ui, false);
    XMapWindow(ui->dpy, ui->win);
    XFlush(ui->dpy);
    ui->dirty = true;
    x11_probe_session(ui, si);
    return 0;
}

static void gui_close(Gui *ui) {
    if (!ui->dpy) return;
    if (ui->font_bold && ui->font_bold != ui->font) XFreeFont(ui->dpy, ui->font_bold);
    if (ui->font) XFreeFont(ui->dpy, ui->font);
    if (ui->gc) XFreeGC(ui->dpy, ui->gc);
    if (ui->win) XDestroyWindow(ui->dpy, ui->win);
    XCloseDisplay(ui->dpy);
    memset(ui, 0, sizeof(*ui));
}

static const char *view_name(View v) {
    static const char *names[] = {"Home", "Clients", "Power", "Desktop", "Events", "Trace", "Help", "About"};
    return (v >= VIEW_DASHBOARD && v <= VIEW_ABOUT) ? names[v] : "Home";
}

static Rect tab_rect(const Gui *ui, int idx) {
    int count = 8, gap = 4, x0 = 12, y = 300, h = 30;
    int w = (ui->width - x0 * 2 - gap * (count - 1)) / count;
    return (Rect){x0 + idx * (w + gap), y, w, h};
}

static Rect control_rect(const Gui *ui, int idx) {
    int count = 5, gap = 4, x0 = 12, y = ui->height - 72, h = 26;
    int w = (ui->width - x0 * 2 - gap * (count - 1)) / count;
    return (Rect){x0 + idx * (w + gap), y, w, h};
}

static Rect mode_rect(const Gui *ui, int idx) {
    int count = 8, gap = 3, x0 = 12, y = 78, h = 29;
    int w = (ui->width - x0 * 2 - gap * (count - 1)) / count;
    return (Rect){x0 + idx * (w + gap), y, w, h};
}

static void draw_button(Gui *ui, Rect r, const char *text, bool active) {
    gui_fill(ui, r, active ? ui->accent : ui->panel_alt);
    gui_outline(ui, r, active ? ui->fg : ui->dim);
    int tx = r.x + 7;
    int ty = r.y + r.h - 8;
    gui_text(ui, tx, ty, active ? ui->bg : ui->fg, active, "%s", text);
}

static void draw_header(Gui *ui, const Gpu *g, Sysfs *s, const Target *t,
                        const PowerTelemetry *p, const SystemTelemetry *sys,
                        const Stats *st) {
    gui_text(ui, 16, 25, ui->fg, true, "%s", g->full_name);
    gui_text(ui, 16, 47, ui->dim, false,
             "%s  |  PCI %s  |  ID 1002:%s  |  VRAM %.1f GiB",
             g->card_name, g->pci_addr, g->device_id, g->vram_bytes / 1073741824.0);

    char actual[128] = "unknown";
    (void)read_fd_text(s->fd_policy_r, actual, sizeof(actual));
    unsigned long lock_color = strcmp(actual, t->policy) == 0 ? ui->good : ui->bad;
    gui_text(ui, 16, 69, lock_color, true,
             "POWER MODE   target %s   actual %s   %s   corrections %llu",
             t->policy, actual, t->enabled ? "LOCKED" : "PAUSED",
             (unsigned long long)st->corrections);

    const char *modes[] = {
        "1 Auto", "2 Low", "3 High", "4 Standard",
        "5 Min-GFX", "6 Min-MEM", "7 Peak", "8 Manual"
    };
    for (int i = 0; i < 8; i++)
        draw_button(ui, mode_rect(ui, i), modes[i], strcmp(t->policy, POLICIES[i]) == 0);

    char label[192];

    /*
     * Board power is intentionally the dominant live meter.  Power changes are
     * the primary signal this tool is intended to correlate with rendering and
     * AMDGPU policy activity.
     */
    Rect power_bar = {16, 119, ui->width - 32, 38};
    double power_pct = p->power_w >= 0.0 ? p->power_w * 100.0 / POWER_BAR_MAX_W : 0.0;
    if (p->power_w >= 0.0)
        snprintf(label, sizeof(label), "BOARD POWER   %.1f W   (%.0f W scale)",
                 p->power_w, POWER_BAR_MAX_W);
    else
        snprintf(label, sizeof(label), "BOARD POWER   unavailable");
    gui_bar(ui, power_bar, power_pct, label);

    const int gap = 8;
    const int margin = 16;
    int half = (ui->width - margin * 2 - gap) / 2;
    Rect left = {margin, 167, half, 22};
    Rect right = {margin + half + gap, 167, half, 22};

    snprintf(label, sizeof(label), "GPU load %d%%", p->gpu_busy >= 0 ? p->gpu_busy : 0);
    gui_bar(ui, left, p->gpu_busy >= 0 ? p->gpu_busy : 0.0, label);

    double vram_pct = g->vram_bytes ?
        (double)p->vram_used * 100.0 / (double)g->vram_bytes : 0.0;
    snprintf(label, sizeof(label), "VRAM %.2f / %.2f GiB (%.1f%%)",
             p->vram_used / 1073741824.0,
             g->vram_bytes / 1073741824.0,
             vram_pct);
    gui_bar(ui, right, vram_pct, label);

    left.y = right.y = 195;
    snprintf(label, sizeof(label), "CPU %.1f%%  (%u threads)",
             sys->cpu_pct, sys->cpu_threads);
    gui_bar(ui, left, sys->cpu_pct, label);

    snprintf(label, sizeof(label), "System RAM %.2f / %.2f GiB (%.1f%%)",
             sys->ram_used_bytes / 1073741824.0,
             sys->ram_total_bytes / 1073741824.0,
             sys->ram_pct);
    gui_bar(ui, right, sys->ram_pct, label);

    left.y = right.y = 223;
    double voltage_pct = p->voltage_v >= 0.0 ?
        p->voltage_v * 100.0 / VOLTAGE_BAR_MAX_V : 0.0;
    if (p->voltage_v >= 0.0)
        snprintf(label, sizeof(label), "%s %.3f V",
                 s->voltage_label[0] ? s->voltage_label : "GPU voltage",
                 p->voltage_v);
    else
        snprintf(label, sizeof(label), "GPU voltage unavailable");
    gui_bar(ui, left, voltage_pct, label);

    double current_pct = p->current_a >= 0.0 ?
        p->current_a * 100.0 / CURRENT_BAR_MAX_A : 0.0;
    if (p->current_a >= 0.0)
        snprintf(label, sizeof(label), "Current %s %.2f A%s",
                 p->current_label, p->current_a,
                 p->current_is_estimate ? " (est.)" : "");
    else
        snprintf(label, sizeof(label), "Current unavailable");
    gui_bar(ui, right, current_pct, label);

    char fan[80], gfx[80], mem[80];
    if (p->fan_rpm >= 0 && p->fan_pct >= 0)
        snprintf(fan, sizeof(fan), "%d RPM / %.0f%%", p->fan_rpm, p->fan_pct);
    else if (p->fan_rpm >= 0)
        snprintf(fan, sizeof(fan), "%d RPM", p->fan_rpm);
    else
        snprintf(fan, sizeof(fan), "n/a");

    if (p->gfx_hz >= 0)
        snprintf(gfx, sizeof(gfx), "%.0f MHz", p->gfx_hz / 1000000.0);
    else
        snprintf(gfx, sizeof(gfx), "n/a");

    if (p->mem_hz >= 0)
        snprintf(mem, sizeof(mem), "%.0f MHz", p->mem_hz / 1000000.0);
    else
        snprintf(mem, sizeof(mem), "n/a");

    gui_text(ui, 16, 272, ui->fg, false,
             "Temp %.1f C   Fan %s   Board %.1f W   GFX %s   MEM %s   watchdog %.0f Hz",
             p->temp_c, fan, p->power_w, gfx, mem, st->loop_hz);
}

static void draw_tabs(Gui *ui, View view) {
    for (int i = 0; i < 8; i++) draw_button(ui, tab_rect(ui, i), view_name((View)i), view == (View)i);
}

static void panel_title(Gui *ui, Rect r, const char *title) {
    gui_fill(ui, r, ui->panel);
    gui_outline(ui, r, ui->panel_alt);
    gui_text(ui, r.x + 10, r.y + 20, ui->accent, true, "%s", title);
}

static void draw_dashboard(Gui *ui, const Target *t, const Stats *st, int interval_us,
                           bool adaptive, bool monitor_clocks, const ClientMonitor *cm,
                           const PowerTelemetry *p, const SessionInfo *si, const EventLog *el,
                           const TraceState *tr) {
    int top = 342, gap = 10, margin = 12;
    int w = (ui->width - margin * 2 - gap) / 2;
    int bottom = ui->height - 82;
    int h = (bottom - top - gap * 2) / 3;
    Rect r[6] = {
        {margin, top, w, h}, {margin + w + gap, top, w, h},
        {margin, top + h + gap, w, h}, {margin + w + gap, top + h + gap, w, h},
        {margin, top + 2*(h+gap), w, h}, {margin + w + gap, top + 2*(h+gap), w, h}
    };
    panel_title(ui, r[0], "LOCK / WATCHDOG");
    gui_text(ui,r[0].x+10,r[0].y+45,ui->fg,false,"Target %s   %s",t->policy,t->enabled?"ENFORCING":"PAUSED");
    gui_text(ui,r[0].x+10,r[0].y+68,ui->dim,false,"Base %.1f Hz  Adaptive %s  resets %llu",1000000.0/interval_us,adaptive?"on":"off",(unsigned long long)st->policy_mismatches);

    panel_title(ui, r[1], "POWER / CLOCKS");
    gui_text(ui,r[1].x+10,r[1].y+45,ui->fg,false,"GPU %d%%  MEM %d%%  %.1f W  %.1f C",p->gpu_busy,p->mem_busy,p->power_w,p->temp_c);
    gui_text(ui,r[1].x+10,r[1].y+68,ui->dim,false,"GFXOFF %s  DPM monitor %s",gfxoff_text(p->gfxoff_status),monitor_clocks?"on":"off");

    panel_title(ui, r[2], "GPU CLIENTS");
    const GpuClient *c = top_client(cm);
    if (c) {
        gui_text(ui,r[2].x+10,r[2].y+45,ui->fg,false,"Top: %s",c->role[0]?c->role:c->name);
        gui_text(ui,r[2].x+10,r[2].y+68,ui->dim,false,"%.2f%% activity  PID %d",c->activity_pct,c->pid);
    } else gui_text(ui,r[2].x+10,r[2].y+48,ui->dim,false,"No DRM client activity yet");

    panel_title(ui, r[3], "X11 / DESKTOP");
    gui_text(ui,r[3].x+10,r[3].y+45,ui->fg,false,"%s / %s",si->session_type,si->desktop);
    gui_text(ui,r[3].x+10,r[3].y+68,ui->dim,false,"WM %s   compositor %s",si->window_manager,si->compositor);

    panel_title(ui, r[4], "LAST EVENT");
    const DiagEvent *ev = event_last(el);
    if (ev) {
        gui_text(ui,r[4].x+10,r[4].y+45,ui->fg,false,"%s: %.60s",ev->kind,ev->detail);
        gui_text(ui,r[4].x+10,r[4].y+68,ui->dim,false,"Actor %s (%.2f%%)",ev->actor,ev->actor_activity);
    } else gui_text(ui,r[4].x+10,r[4].y+48,ui->dim,false,"No diagnostic events yet");

    panel_title(ui, r[5], "TRACE / LOG");
    gui_text(ui,r[5].x+10,r[5].y+45,ui->fg,false,"Kernel trace %s   lines %llu",tr->active?"ON":"off",(unsigned long long)tr->lines);
    gui_text(ui,r[5].x+10,r[5].y+68,ui->dim,false,"CSV log %s   events %llu",el->logging?"ON":"off",(unsigned long long)el->total);
}

static void draw_clients_page(Gui *ui, const ClientMonitor *cm) {
    int y = 350;
    gui_text(ui,16,y,ui->accent,true,"Per-process DRM activity"); y += 25;
    gui_text(ui,16,y,ui->dim,false,"PID      PROCESS / ROLE                          TOTAL    GFX   COMP    DMA  MEDIA      VRAM"); y += 20;
    int max = cm->count < 14 ? cm->count : 14;
    for (int i=0;i<max;i++) {
        const GpuClient *c=&cm->items[i];
        gui_text(ui,16,y,ui->fg,false,"%-8d %-39.39s %7.2f %6.2f %6.2f %6.2f %6.2f %7.1fM",
                 c->pid,c->role[0]?c->role:c->name,c->activity_pct,c->gfx_pct,c->compute_pct,c->dma_pct,c->media_pct,c->vram_bytes/1048576.0);
        y += 22;
    }
    gui_text(ui,16,ui->height-92,ui->dim,false,"Scan CPU %.3f%%  scans %llu  fdinfo reads %llu  errors %llu",
             cm->scan_cpu_pct,(unsigned long long)cm->scans,(unsigned long long)cm->fdinfo_reads,(unsigned long long)cm->scan_errors);
}

static void draw_power_page(Gui *ui, const Target *t, const Stats *st, const PowerTelemetry *p,
                            bool monitor_clocks, int dpm_interval_us) {
    int x=18,y=350;
    gui_text(ui,x,y,ui->accent,true,"Power and DPM telemetry"); y+=30;
    gui_text(ui,x,y,ui->fg,false,"Policy target: %s    watchdog: %s",t->policy,t->enabled?"enforcing":"paused"); y+=24;
    gui_text(ui,x,y,ui->fg,false,"GPU busy %d%%   MEM busy %d%%   board power %.2f W",p->gpu_busy,p->mem_busy,p->power_w); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Temperature %.1f C   fan %d RPM / %.1f%%   voltage %.3f V",p->temp_c,p->fan_rpm,p->fan_pct,p->voltage_v); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Current %.2f A%s (%s)   12V equivalent load %.3f ohm",
             p->current_a,p->current_is_estimate?" estimated":"",p->current_label,
             p->equivalent_input_ohm); y+=24;
    gui_text(ui,x,y,ui->fg,false,"GFX %.0f MHz (%s)   MEM %.0f MHz (%s)",p->gfx_hz/1000000.0,p->freq1_label,p->mem_hz/1000000.0,p->freq2_label); y+=24;
    gui_text(ui,x,y,ui->fg,false,"GFXOFF %s   transitions %llu   enabled %d",gfxoff_text(p->gfxoff_status),(unsigned long long)p->gfxoff_status_changes,p->gfxoff_enabled); y+=24;
    gui_text(ui,x,y,ui->dim,false,"Power profile: %.140s",p->power_profile); y+=24;
    gui_text(ui,x,y,ui->dim,false,"PCIe: %.150s",p->pcie_bw); y+=28;
    gui_text(ui,x,y,ui->accent,true,"Watchdog / DPM counters"); y+=26;
    gui_text(ui,x,y,ui->fg,false,"checks %llu  corrections %llu  policy mismatches %llu  verify failures %llu",
             (unsigned long long)st->checks,(unsigned long long)st->corrections,(unsigned long long)st->policy_mismatches,(unsigned long long)st->verify_failures); y+=24;
    gui_text(ui,x,y,ui->fg,false,"SCLK changes %llu  MCLK changes %llu  DPM sampling %s @ %.1f Hz",
             (unsigned long long)st->observed_sclk_changes,(unsigned long long)st->observed_mclk_changes,
             monitor_clocks?"ON":"off",1000000.0/dpm_interval_us); y+=24;
    gui_text(ui,x,y,ui->dim,false,"SCLK: %.150s",st->sclk_display[0]?st->sclk_display:"not sampled"); y+=22;
    gui_text(ui,x,y,ui->dim,false,"MCLK: %.150s",st->mclk_display[0]?st->mclk_display:"not sampled"); y+=30;
    gui_text(ui,x,y,ui->warn,true,"Manual mode: S = cycle GFX DPM level, M = cycle memory DPM level, X = clear manual locks");
}

static void draw_desktop_page(Gui *ui, const SessionInfo *si) {
    int x=18,y=350;
    gui_text(ui,x,y,ui->accent,true,"X11 / XLibre rendering stack"); y+=30;
    gui_text(ui,x,y,ui->fg,false,"Session: %s   Display: %s",si->session_type,si->display); y+=24;
    gui_text(ui,x,y,ui->fg,false,"X server vendor: %.100s",si->xserver_vendor[0]?si->xserver_vendor:"unknown"); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Desktop: %s   Window manager: %s",si->desktop,si->window_manager); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Compositor: %s   PID %d",si->compositor,si->compositor_pid); y+=32;
    gui_text(ui,x,y,ui->accent,true,"Display / KMS"); y+=28;
    gui_text(ui,x,y,ui->fg,false,"Connected outputs: %d   %s",si->connected_outputs,si->connector_summary); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Current modes: %.150s",si->kms_modes); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Active planes: %d   allocators: %.120s",si->active_planes,si->plane_allocators); y+=30;
    gui_text(ui,x,y,ui->dim,false,"Compositor detection uses the X11 _NET_WM_CM_S0 selection owner first, then process heuristics.");
}

static void draw_events_page(Gui *ui, const EventLog *el) {
    int y=350;
    gui_text(ui,16,y,ui->accent,true,"Diagnostic flight recorder - newest first"); y+=26;
    int max=el->count<15?el->count:15;
    for(int n=0;n<max;n++) {
        int idx=el->head-1-n; while(idx<0) idx+=MAX_DIAG_EVENTS;
        const DiagEvent *ev=&el->ring[idx];
        gui_text(ui,16,y,ui->fg,false,"%3d  %-13s %-48.48s  actor %-24.24s %6.2f%%",
                 n+1,ev->kind,ev->detail,ev->actor,ev->actor_activity);
        y+=22;
    }
    gui_text(ui,16,ui->height-92,ui->dim,false,"Total %llu   CSV logging %s%s%s",
             (unsigned long long)el->total,el->logging?"ON":"off",el->logging?"  ":"",el->logging?el->log_path:"");
}

static void draw_trace_page(Gui *ui, const TraceState *tr) {
    int x=18,y=350;
    gui_text(ui,x,y,ui->accent,true,"Kernel tracefs diagnostics"); y+=30;
    gui_text(ui,x,y,ui->fg,false,"Available: %s   Active: %s   total lines %llu",tr->available?"yes":"no",tr->active?"YES":"no",(unsigned long long)tr->lines); y+=24;
    gui_text(ui,x,y,ui->fg,false,"AMDGPU events %d   scheduler events %d   DRM events %d",tr->amdgpu_events,tr->drm_sched_events,tr->drm_events); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Lines: amdgpu %llu   scheduler %llu   drm/display %llu",
             (unsigned long long)tr->amdgpu_lines,(unsigned long long)tr->sched_lines,(unsigned long long)tr->drm_lines); y+=28;
    gui_text(ui,x,y,ui->fg,false,"Last actor: %s",tr->last_actor[0]?tr->last_actor:"none"); y+=24;
    gui_text(ui,x,y,ui->dim,false,"Last trace: %.150s",tr->last_line[0]?tr->last_line:"none"); y+=36;
    gui_text(ui,x,y,ui->warn,true,"T toggles tracing. It is OFF by default because tracing is diagnostic, not normal monitoring.");
}

static void draw_help_page(Gui *ui) {
    int x=18,y=350;
    gui_text(ui,x,y,ui->accent,true,"Keyboard / mouse controls"); y+=30;
    const char *lines[] = {
        "1 Auto    2 Low    3 High    4 Standard    5 Min-GFX    6 Min-MEM    7 Peak    8 Manual",
        "0 Home    P Clients    G Power    W Desktop    E Events    R Trace    H Help    I About",
        "Space pause/resume watchdog    A adaptive burst    C DPM clock telemetry",
        "+ faster watchdog    - slower watchdog    D cycle DPM telemetry rate",
        "S cycle manual GFX level    M cycle manual memory level    X clear manual level locks",
        "T kernel trace on/off    L CSV event logging on/off    Q quit",
        "All tabs, power modes, and the main toggles are also clickable.",
        "The GUI uses plain Xlib: no GTK, Qt, ncurses, Electron, XCB, or libdrm dependency."
    };
    for(size_t i=0;i<sizeof(lines)/sizeof(lines[0]);i++) { gui_text(ui,x,y,ui->fg,false,"%s",lines[i]); y+=28; }
}

static void draw_about_page(Gui *ui) {
    int x = 18, y = 350;

    gui_text(ui, x, y, ui->accent, true, "AMD GPU Diagnostic Tool"); y += 32;
    gui_text(ui, x, y, ui->fg, true, "Version %s", VERSION); y += 26;
    gui_text(ui, x, y, ui->fg, false, "Developer: Kevin"); y += 24;
    gui_text(ui, x, y, ui->dim, false, "Copyright (C) 2026 Kevin Kolasa"); y += 34;

    gui_text(ui, x, y, ui->accent, true, "About the project"); y += 28;
    gui_text(ui, x, y, ui->fg, false,
             "Independent Linux AMDGPU diagnostic software developed in Akita City, Japan."); y += 22;
    gui_text(ui, x, y, ui->fg, false,
             "The project grew from investigating rapid AMDGPU power-state changes, desktop/game"); y += 22;
    gui_text(ui, x, y, ui->fg, false,
             "rendering interactions, and associated coil-whine behavior on real hardware."); y += 32;

    gui_text(ui, x, y, ui->accent, true, "Technology"); y += 28;
    gui_text(ui, x, y, ui->fg, false,
             "C / Xlib / sysfs / procfs / debugfs / tracefs / DRM fdinfo"); y += 22;
    gui_text(ui, x, y, ui->fg, false,
             "X11/XLibre-first; intentionally avoids GTK, Qt, Electron, ncurses, libdrm, and Python."); y += 32;

    gui_text(ui, x, y, ui->accent, true, "Development"); y += 28;
    gui_text(ui, x, y, ui->fg, false,
             "Project direction, hardware testing, and technical decisions remain under direct developer review."); y += 22;
    gui_text(ui, x, y, ui->fg, false,
             "AI-assisted development tools are used to accelerate implementation, testing, review,"); y += 22;
    gui_text(ui, x, y, ui->fg, false,
             "and documentation."); y += 32;

    gui_text(ui, x, y, ui->accent, true, "License"); y += 28;
    gui_text(ui, x, y, ui->fg, false,
             "GNU General Public License version 3 or later (GPL-3.0-or-later)."); y += 36;

    gui_text(ui, x, y, ui->warn, true,
             "Built because listening to a GPU scream is not a diagnostic method.");
}

static void draw_bottom_controls(Gui *ui, const Target *t, bool adaptive, bool monitor_clocks,
                                 const TraceState *tr, const EventLog *el) {
    char b0[64],b1[64],b2[64],b3[64],b4[64];
    snprintf(b0,sizeof(b0),"Space Watchdog %s",t->enabled?"ON":"PAUSED");
    snprintf(b1,sizeof(b1),"A Adaptive %s",adaptive?"ON":"off");
    snprintf(b2,sizeof(b2),"C DPM %s",monitor_clocks?"ON":"off");
    snprintf(b3,sizeof(b3),"T Trace %s",tr->active?"ON":"off");
    snprintf(b4,sizeof(b4),"L Log %s",el->logging?"ON":"off");
    const char *labels[]={b0,b1,b2,b3,b4};
    bool act[]={t->enabled,adaptive,monitor_clocks,tr->active,el->logging};
    for(int i=0;i<5;i++) draw_button(ui,control_rect(ui,i),labels[i],act[i]);

}

static void gui_draw(Gui *ui, const Gpu *g, Sysfs *s, const Target *t, const Stats *st,
                     int interval_us, int dpm_interval_us, bool monitor_clocks, bool adaptive,
                     const ClientMonitor *clients, const PowerTelemetry *power, const SystemTelemetry *system, const SessionInfo *session,
                     const EventLog *events, const TraceState *trace, View view, const char *message) {
    gui_fill(ui,(Rect){0,0,ui->width,ui->height},ui->bg);
    draw_header(ui,g,s,t,power,system,st);
    draw_tabs(ui,view);
    switch(view) {
        case VIEW_DASHBOARD: draw_dashboard(ui,t,st,interval_us,adaptive,monitor_clocks,clients,power,session,events,trace); break;
        case VIEW_CLIENTS: draw_clients_page(ui,clients); break;
        case VIEW_POWER: draw_power_page(ui,t,st,power,monitor_clocks,dpm_interval_us); break;
        case VIEW_DESKTOP: draw_desktop_page(ui,session); break;
        case VIEW_EVENTS: draw_events_page(ui,events); break;
        case VIEW_TRACE: draw_trace_page(ui,trace); break;
        case VIEW_HELP: draw_help_page(ui); break;
        case VIEW_ABOUT: draw_about_page(ui); break;
    }
    if(message && *message) gui_text(ui,16,ui->height-84,ui->warn,false,"%s",message);
    draw_bottom_controls(ui,t,adaptive,monitor_clocks,trace,events);
    XFlush(ui->dpy);
    ui->dirty=false;
}

static int cycle_manual_level(Sysfs *s, Target *t, bool sclk, char *message, size_t cap) {
    int rfd=sclk?s->fd_sclk_r:s->fd_mclk_r;
    int wfd=sclk?s->fd_sclk_w:s->fd_mclk_w;
    if(rfd<0||wfd<0) { snprintf(message,cap,"%s DPM levels unavailable",sclk?"GFX":"Memory"); return -1; }
    char buf[MAX_TEXT];
    if(read_fd_text(rfd,buf,sizeof(buf))<0) { snprintf(message,cap,"Cannot read %s DPM levels",sclk?"GFX":"memory"); return -1; }
    DpmLevel levels[MAX_LEVELS];
    int n=parse_dpm_levels(buf,levels,MAX_LEVELS);
    int valid[MAX_LEVELS], nv=0, active=INT_MIN;
    for(int i=0;i<n;i++) { if(levels[i].index>=0) valid[nv++]=levels[i].index; if(levels[i].active) active=levels[i].index; }
    if(nv==0) { snprintf(message,cap,"No selectable %s DPM levels",sclk?"GFX":"memory"); return -1; }
    int current=sclk?(t->lock_sclk?t->sclk_level:active):(t->lock_mclk?t->mclk_level:active);
    int pos=-1; for(int i=0;i<nv;i++) if(valid[i]==current){pos=i;break;}
    int wanted=valid[(pos+1+nv)%nv];
    snprintf(t->policy,sizeof(t->policy),"manual");
    if(apply_policy(s,"manual")<0 || apply_level(wfd,wanted)<0) { snprintf(message,cap,"Failed to apply manual %s level %d",sclk?"GFX":"memory",wanted); return -1; }
    if(sclk){t->lock_sclk=true;t->sclk_level=wanted;}else{t->lock_mclk=true;t->mclk_level=wanted;}
    snprintf(message,cap,"Manual %s level -> %d",sclk?"GFX":"memory",wanted);
    return 0;
}

static int next_interval(int current, bool faster) {
    static const int vals[] = {500,1000,2000,5000,10000,20000,50000,100000,250000,500000,1000000};
    int n=(int)(sizeof(vals)/sizeof(vals[0])), nearest=0;
    long best=labs((long)current-vals[0]);
    for(int i=1;i<n;i++){long d=labs((long)current-vals[i]);if(d<best){best=d;nearest=i;}}
    if(faster&&nearest>0)nearest--; else if(!faster&&nearest<n-1)nearest++;
    return vals[nearest];
}

static int next_dpm_interval(int current) {
    static const int vals[] = {50000,100000,250000,500000,1000000,2000000};
    int n=(int)(sizeof(vals)/sizeof(vals[0]));
    for(int i=0;i<n;i++) if(vals[i]==current) return vals[(i+1)%n];
    return DEFAULT_DPM_INTERVAL_US;
}

static int benchmark_reads(Sysfs *s, int duration_ms, int interval_us,
                           double *cpu_pct, double *actual_hz, double *avg_read_us) {
    struct timespec wall0,wall1,cpu0,cpu1,next;
    clock_gettime(CLOCK_MONOTONIC,&wall0); clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu0); next=wall0;
    uint64_t loops=0; long long read_cpu_ns=0; char buf[256];
    long long end_ns=ts_ns(wall0)+(long long)duration_ms*1000000LL;
    while(1){
        struct timespec r0,r1,now; clock_gettime(CLOCK_THREAD_CPUTIME_ID,&r0);
        if(read_fd_text(s->fd_policy_r,buf,sizeof(buf))<0)return -1;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID,&r1); read_cpu_ns+=diff_ns(r1,r0); loops++;
        add_ns(&next,(long long)interval_us*1000LL); clock_gettime(CLOCK_MONOTONIC,&now);
        if(ts_ns(now)>=end_ns)break;
        if(ts_ns(next)>ts_ns(now))clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&next,NULL);else next=now;
    }
    clock_gettime(CLOCK_MONOTONIC,&wall1); clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu1);
    double wall=diff_ns(wall1,wall0)/1e9,cpu=diff_ns(cpu1,cpu0)/1e9;
    *cpu_pct=wall>0?cpu/wall*100.0:0.0; *actual_hz=wall>0?loops/wall:0.0; *avg_read_us=loops?(read_cpu_ns/1000.0)/loops:0.0;
    return 0;
}

static void run_benchmark_cli(Sysfs *s) {
    static const int intervals[]={20000,5000,2000,1000,500};
    printf("Policy-only amdgpu sysfs benchmark\n\n");
    printf(" interval      target Hz     actual Hz     thread CPU     read CPU/check\n");
    printf(" ----------------------------------------------------------------------\n");
    for(size_t i=0;i<sizeof(intervals)/sizeof(intervals[0]);i++){
        double cpu=0,hz=0,rus=0;
        if(benchmark_reads(s,350,intervals[i],&cpu,&hz,&rus)<0){printf(" %7d us      ERROR\n",intervals[i]);break;}
        printf(" %7d us   %10.0f   %10.1f     %7.3f%%      %8.3f us\n",intervals[i],1000000.0/intervals[i],hz,cpu,rus);
    }
}

static int parse_int_arg(const char *s, int minv, int maxv, int *out) {
    if(!s||!*s)return -1;
    errno=0; char *end=NULL; long v=strtol(s,&end,10);
    if(errno||!end||*end||v<minv||v>maxv)return -1;
    *out=(int)v; return 0;
}

static void usage(const char *argv0) {
    printf("AMD GPU Diagnostic Tool %s - X11/XLibre GUI\n",VERSION);
    printf("Usage: %s [--card N] [--interval-us N] [--dpm-interval-us N] [--mode MODE]\n",argv0);
    printf("          [--monitor-dpm] [--fixed] [--unsafe-fast] [--normal-priority] [--benchmark]\n");
    printf("Modes: auto low high manual profile_standard profile_min_sclk profile_min_mclk profile_peak\n");
    printf("Launch GUI through sudo with X11 credentials preserved: sudo -E %s\n",argv0);
}

static void set_mode(Sysfs *s, Target *t, int idx, char *message, size_t cap) {
    if(idx<0||idx>=(int)POLICY_COUNT)return;
    snprintf(t->policy,sizeof(t->policy),"%s",POLICIES[idx]);
    if(strcmp(t->policy,"manual")!=0){t->lock_sclk=false;t->lock_mclk=false;}
    if(apply_policy(s,t->policy)==0) snprintf(message,cap,"Target changed to %s",t->policy);
    else snprintf(message,cap,"Failed to apply %s: %s",t->policy,strerror(errno));
}

static void handle_action(char key, Sysfs *s, Target *target, int *interval_us, int *dpm_interval_us,
                          bool *monitor_clocks, bool *adaptive, long long *burst_until_ns,
                          TraceState *trace, EventLog *events, View *view, char *message, size_t message_cap) {
    message[0]='\0';
    if(key>='1'&&key<='8') set_mode(s,target,key-'1',message,message_cap);
    else if(key=='0') *view=VIEW_DASHBOARD;
    else if(key=='p'||key=='P') *view=VIEW_CLIENTS;
    else if(key=='g'||key=='G') *view=VIEW_POWER;
    else if(key=='w'||key=='W') *view=VIEW_DESKTOP;
    else if(key=='e'||key=='E') *view=VIEW_EVENTS;
    else if(key=='r'||key=='R') *view=VIEW_TRACE;
    else if(key=='h'||key=='H'||key=='?') *view=VIEW_HELP;
    else if(key=='i'||key=='I') *view=VIEW_ABOUT;
    else if(key==' '){target->enabled=!target->enabled;snprintf(message,message_cap,"Watchdog %s",target->enabled?"enabled":"paused");}
    else if(key=='a'||key=='A'){*adaptive=!*adaptive;*burst_until_ns=0;snprintf(message,message_cap,"Adaptive correction burst %s",*adaptive?"enabled":"disabled");}
    else if(key=='c'||key=='C'){*monitor_clocks=!*monitor_clocks;snprintf(message,message_cap,"DPM clock monitoring %s",*monitor_clocks?"enabled":"disabled");}
    else if(key=='+'){*interval_us=next_interval(*interval_us,true);snprintf(message,message_cap,"Watchdog interval %d us",*interval_us);}
    else if(key=='-'){*interval_us=next_interval(*interval_us,false);snprintf(message,message_cap,"Watchdog interval %d us",*interval_us);}
    else if(key=='d'||key=='D'){*dpm_interval_us=next_dpm_interval(*dpm_interval_us);snprintf(message,message_cap,"DPM interval %d us",*dpm_interval_us);}
    else if(key=='s'||key=='S'){(void)cycle_manual_level(s,target,true,message,message_cap);*monitor_clocks=true;}
    else if(key=='m'||key=='M'){(void)cycle_manual_level(s,target,false,message,message_cap);*monitor_clocks=true;}
    else if(key=='x'||key=='X'){target->lock_sclk=target->lock_mclk=false;snprintf(message,message_cap,"Manual clock locks cleared");}
    else if(key=='l'||key=='L'){toggle_event_logging(events);snprintf(message,message_cap,"CSV logging %s",events->logging?"enabled":"disabled");}
    else if(key=='t'||key=='T'){
        if(trace->active){trace_stop(trace);snprintf(message,message_cap,"Kernel trace stopped");}
        else if(trace_start(trace)==0)snprintf(message,message_cap,"Kernel trace started");
        else snprintf(message,message_cap,"Kernel trace unavailable or failed to start");
    }
}

static char keysym_to_action(XKeyEvent *ke) {
    KeySym ks=NoSymbol; char buf[8]={0};
    int n=XLookupString(ke,buf,sizeof(buf),&ks,NULL);
    if(ks==XK_Escape)return 'q';
    if(ks==XK_space)return ' ';
    if(ks==XK_plus||ks==XK_equal)return '+';
    if(ks==XK_minus||ks==XK_underscore)return '-';
    if(n>0)return buf[0];
    return 0;
}

static void handle_click(Gui *ui, int x, int y, Sysfs *s, Target *target, int *interval_us,
                         int *dpm_interval_us, bool *monitor_clocks, bool *adaptive,
                         long long *burst_until_ns, TraceState *trace, EventLog *events,
                         View *view, char *message, size_t message_cap) {
    for(int i=0;i<8;i++) if(point_in(tab_rect(ui,i),x,y)){*view=(View)i;message[0]=0;return;}
    for(int i=0;i<8;i++) if(point_in(mode_rect(ui,i),x,y)){set_mode(s,target,i,message,message_cap);return;}
    for(int i=0;i<5;i++) if(point_in(control_rect(ui,i),x,y)){
        char key=i==0?' ':i==1?'A':i==2?'C':i==3?'T':'L';
        handle_action(key,s,target,interval_us,dpm_interval_us,monitor_clocks,adaptive,burst_until_ns,trace,events,view,message,message_cap);
        return;
    }
}

int main(int argc, char **argv) {
    int requested_card=-1, interval_us=DEFAULT_INTERVAL_US, dpm_interval_us=DEFAULT_DPM_INTERVAL_US;
    bool monitor_clocks=false, adaptive=true, unsafe_fast=false, lower_priority=true, bench_at_start=false;
    Target target={.policy="profile_standard",.enabled=true,.lock_sclk=false,.lock_mclk=false,.sclk_level=0,.mclk_level=0};

    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--help")==0||strcmp(argv[i],"-h")==0){usage(argv[0]);return 0;}
        if(strcmp(argv[i],"--version")==0){printf("%s\n",VERSION);return 0;}
        if(strcmp(argv[i],"--card")==0){if(++i>=argc||parse_int_arg(argv[i],0,255,&requested_card)<0){fprintf(stderr,"Invalid --card\n");return 2;}}
        else if(strcmp(argv[i],"--interval-us")==0){if(++i>=argc||parse_int_arg(argv[i],1,MAX_INTERVAL_US,&interval_us)<0){fprintf(stderr,"Invalid --interval-us\n");return 2;}}
        else if(strcmp(argv[i],"--dpm-interval-us")==0){if(++i>=argc||parse_int_arg(argv[i],1,MAX_DPM_INTERVAL_US,&dpm_interval_us)<0){fprintf(stderr,"Invalid --dpm-interval-us\n");return 2;}}
        else if(strcmp(argv[i],"--mode")==0){if(++i>=argc||!valid_policy(argv[i])){fprintf(stderr,"Invalid --mode\n");return 2;}snprintf(target.policy,sizeof(target.policy),"%s",argv[i]);}
        else if(strcmp(argv[i],"--monitor-dpm")==0)monitor_clocks=true;
        else if(strcmp(argv[i],"--policy-only")==0)monitor_clocks=false;
        else if(strcmp(argv[i],"--fixed")==0)adaptive=false;
        else if(strcmp(argv[i],"--unsafe-fast")==0)unsafe_fast=true;
        else if(strcmp(argv[i],"--normal-priority")==0)lower_priority=false;
        else if(strcmp(argv[i],"--benchmark")==0)bench_at_start=true;
        else if(argv[i][0]=='-'){fprintf(stderr,"Unknown option: %s\n",argv[i]);usage(argv[0]);return 2;}
    }
    int min_interval=unsafe_fast?UNSAFE_MIN_INTERVAL_US:MIN_INTERVAL_US;
    if(interval_us<min_interval)interval_us=min_interval;
    if(dpm_interval_us<MIN_DPM_INTERVAL_US)dpm_interval_us=MIN_DPM_INTERVAL_US;

    if(geteuid()!=0){
        fprintf(stderr,"This build must run as root to write AMDGPU power controls.\n");
        fprintf(stderr,"On X11/XLibre use: sudo -E %s\n",argv[0]);
        return 1;
    }
    if(lower_priority)(void)setpriority(PRIO_PROCESS,0,10);
    struct sigaction sa={0}; sa.sa_handler=on_signal; sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL); sigaction(SIGTERM,&sa,NULL); sigaction(SIGHUP,&sa,NULL);

    Gpu gpus[16]; int ngpu=detect_amd_gpus(gpus,16);
    if(ngpu<=0){fprintf(stderr,"No AMD DRM GPUs found.\n");return 1;}
    int gi=choose_gpu(gpus,ngpu,requested_card);
    if(gi<0){fprintf(stderr,"Requested AMD GPU not found.\n");return 1;}
    Gpu *g=&gpus[gi]; Sysfs s;
    if(sysfs_open(g,&s)<0){fprintf(stderr,"Cannot open AMDGPU controls: %s\n",strerror(errno));return 1;}
    if(apply_policy(&s,target.policy)<0){fprintf(stderr,"Cannot set initial policy %s: %s\n",target.policy,strerror(errno));sysfs_close(&s);return 1;}
    if(bench_at_start){run_benchmark_cli(&s);sysfs_close(&s);return 0;}

    Stats st={0}; st.last_active_sclk=st.last_active_mclk=INT_MIN;
    ClientMonitor clients={0};
    PowerTelemetry power={.gpu_busy=-1,.mem_busy=-1,.temp_c=-1,.power_w=-1,.voltage_v=-1,.current_a=-1,.equivalent_input_ohm=-1,.fan_rpm=-1,.fan_pct=-1,.gfx_hz=-1,.mem_hz=-1,.gfxoff_enabled=-1,.gfxoff_status=-1,.last_gfxoff_status=-1};
    SystemTelemetry system={0};
    SessionInfo session={0}; EventLog events={0}; TraceState trace; trace_discover(&trace);
    sample_power(&s,&power); sample_system(&system); sample_clients(g,&clients); sample_session(g,&s,&session,&clients);

    Gui ui;
    if(gui_init(&ui,&session)<0){sysfs_close(&s);return 1;}
    x11_probe_session(&ui,&session);
    View view=VIEW_DASHBOARD; char message[256]="";

    struct timespec next,next_dpm,next_clients,next_power,next_system,next_session,last_ui,stat_wall0,stat_cpu0;
    clock_gettime(CLOCK_MONOTONIC,&next); next_dpm=next; add_ns(&next_dpm,(long long)dpm_interval_us*1000LL);
    next_clients=next;add_ns(&next_clients,CLIENT_SAMPLE_NS); next_power=next;add_ns(&next_power,POWER_SAMPLE_NS);
    next_system=next;add_ns(&next_system,SYSTEM_SAMPLE_NS);
    next_session=next;add_ns(&next_session,SESSION_SAMPLE_NS); last_ui=next; stat_wall0=next; clock_gettime(CLOCK_THREAD_CPUTIME_ID,&stat_cpu0);
    uint64_t stat_checks0=0; long long burst_until_ns=0; int active_interval_us=interval_us;

    while(!g_stop){
        struct timespec loop_now; clock_gettime(CLOCK_MONOTONIC,&loop_now);
        int enforce_rc=target.enabled?enforce_policy_once(&s,&target,&st):0;
        clock_gettime(CLOCK_MONOTONIC,&loop_now);
        if(adaptive&&target.enabled&&enforce_rc>0){if(ts_ns(loop_now)>=burst_until_ns)st.burst_entries++;burst_until_ns=ts_ns(loop_now)+ADAPTIVE_BURST_NS;}
        if(enforce_rc>0){char d[160];snprintf(d,sizeof(d),"restored target %s",target.policy);event_log_add(&events,"POLICY RESET",d,&clients,&trace);ui.dirty=true;}
        bool burst_active=adaptive&&target.enabled&&ts_ns(loop_now)<burst_until_ns;
        active_interval_us=burst_active?ADAPTIVE_BURST_INTERVAL_US:interval_us;

        if(monitor_clocks&&diff_ns(loop_now,next_dpm)>=0){
            uint64_t os=st.observed_sclk_changes,om=st.observed_mclk_changes; monitor_dpm_once(&s,&target,&st,target.enabled);
            if(st.observed_sclk_changes!=os||st.observed_mclk_changes!=om){char d[180];snprintf(d,sizeof(d),"SCLK %.78s | MCLK %.78s",st.sclk_display,st.mclk_display);event_log_add(&events,"DPM CHANGE",d,&clients,&trace);}
            next_dpm=loop_now;add_ns(&next_dpm,(long long)dpm_interval_us*1000LL);
        }
        if(diff_ns(loop_now,next_clients)>=0){sample_clients(g,&clients);next_clients=loop_now;add_ns(&next_clients,CLIENT_SAMPLE_NS);ui.dirty=true;}
        if(diff_ns(loop_now,next_power)>=0){
            uint64_t oldgo=power.gfxoff_status_changes;double oldpw=power.power_w;sample_power(&s,&power);
            if(power.gfxoff_status_changes!=oldgo){char d[128];snprintf(d,sizeof(d),"status %s",gfxoff_text(power.gfxoff_status));event_log_add(&events,"GFXOFF",d,&clients,&trace);}
            if(oldpw>=0&&power.power_w>=0&&fabs(power.power_w-oldpw)>30.0){char d[128];snprintf(d,sizeof(d),"%.1f W -> %.1f W",oldpw,power.power_w);event_log_add(&events,"POWER STEP",d,&clients,&trace);}
            next_power=loop_now;add_ns(&next_power,POWER_SAMPLE_NS);ui.dirty=true;
        }
        if(diff_ns(loop_now,next_system)>=0){sample_system(&system);next_system=loop_now;add_ns(&next_system,SYSTEM_SAMPLE_NS);ui.dirty=true;}
        if(diff_ns(loop_now,next_session)>=0){sample_session(g,&s,&session,&clients);x11_probe_session(&ui,&session);next_session=loop_now;add_ns(&next_session,SESSION_SAMPLE_NS);ui.dirty=true;}
        trace_pump(&trace);

        while(XPending(ui.dpy)>0){
            XEvent ev;XNextEvent(ui.dpy,&ev);
            if(ev.type==Expose){ui.dirty=true;}
            else if(ev.type==ConfigureNotify){ui.width=ev.xconfigure.width;ui.height=ev.xconfigure.height;ui.dirty=true;}
            else if(ev.type==ClientMessage&&(Atom)ev.xclient.data.l[0]==ui.wm_delete){g_stop=1;break;}
            else if(ev.type==KeyPress){
                char key=keysym_to_action(&ev.xkey); if(key=='q'||key=='Q'){g_stop=1;break;}
                if(key){handle_action(key,&s,&target,&interval_us,&dpm_interval_us,&monitor_clocks,&adaptive,&burst_until_ns,&trace,&events,&view,message,sizeof(message));ui.dirty=true;}
            } else if(ev.type==ButtonPress){
                handle_click(&ui,ev.xbutton.x,ev.xbutton.y,&s,&target,&interval_us,&dpm_interval_us,&monitor_clocks,&adaptive,&burst_until_ns,&trace,&events,&view,message,sizeof(message));ui.dirty=true;
            }
        }

        clock_gettime(CLOCK_MONOTONIC,&loop_now);
        if(diff_ns(loop_now,stat_wall0)>=1000000000LL){
            struct timespec cpu1;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu1);double wall=diff_ns(loop_now,stat_wall0)/1e9,cpu=diff_ns(cpu1,stat_cpu0)/1e9;uint64_t dc=st.checks-stat_checks0;
            st.loop_cpu_pct=wall>0?cpu/wall*100.0:0.0;st.loop_hz=wall>0?dc/wall:0.0;stat_wall0=loop_now;stat_cpu0=cpu1;stat_checks0=st.checks;ui.dirty=true;
        }
        if(ui.dirty&&diff_ns(loop_now,last_ui)>=250000000LL){gui_draw(&ui,g,&s,&target,&st,interval_us,dpm_interval_us,monitor_clocks,adaptive,&clients,&power,&system,&session,&events,&trace,view,message);last_ui=loop_now;}

        add_ns(&next,(long long)active_interval_us*1000LL);struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
        if(diff_ns(next,now)>0)clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&next,NULL);else{st.deadline_misses++;next=now;}
    }

    trace_stop(&trace);if(events.logf)fclose(events.logf);gui_close(&ui);sysfs_close(&s);return 0;
}
