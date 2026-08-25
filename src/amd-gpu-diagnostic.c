#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#define VERSION "0.4.1"
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
#define MAX_GPU_CLIENTS 128
#define MAX_DIAG_EVENTS 96
#define MAX_TRACE_LINE 512
#define MAX_CONNECTORS 12

#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_BLUE    "\033[34m"
#define C_MAGENTA "\033[35m"
#define C_CYAN    "\033[36m"
#define C_DIM     "\033[2m"
#define C_HOME    "\033[H"
#define C_CLEAR   "\033[2J"
#define C_HIDE    "\033[?25l"
#define C_SHOW    "\033[?25h"

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

typedef struct {
    struct termios oldt;
    bool active;
} Terminal;


typedef enum {
    VIEW_DASHBOARD = 0,
    VIEW_CLIENTS,
    VIEW_POWER,
    VIEW_DESKTOP,
    VIEW_EVENTS,
    VIEW_TRACE,
    VIEW_HELP
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
static Terminal g_term = {0};

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

static int read_fd_text(int fd, char *buf, size_t cap) {
    if (fd < 0 || cap < 2) return -1;
    ssize_t n = pread(fd, buf, cap - 1, 0);
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
    if (fd < 0) return -1;
    char buf[128];
    int m = snprintf(buf, sizeof(buf), "%s\n", text);
    if (m <= 0 || (size_t)m >= sizeof(buf)) return -1;
    if (lseek(fd, 0, SEEK_SET) < 0) {
        /* Some sysfs attrs ignore seek position; try the write anyway. */
    }
    ssize_t n = write(fd, buf, (size_t)m);
    return n == m ? 0 : -1;
}

static int open_rw_pair(const char *path, int *rfd, int *wfd) {
    *rfd = open(path, O_RDONLY | O_CLOEXEC);
    *wfd = open(path, O_WRONLY | O_CLOEXEC);
    return (*rfd >= 0 && *wfd >= 0) ? 0 : -1;
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
            snprintf(g->full_name, sizeof(g->full_name), "%s", AMD_GPU_NAMES[i].name);
            return;
        }
    }

    /* Some boards/drivers expose a useful product string. */
    snprintf(p, sizeof(p), "%s/product_name", g->base);
    if (read_path_text(p, txt, sizeof(txt)) >= 0 && txt[0]) {
        snprintf(g->full_name, sizeof(g->full_name), "%s", txt);
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
        snprintf(vendor, sizeof(vendor), "%s/vendor", dev);
        if (read_path_text(vendor, txt, sizeof(txt)) < 0) continue;
        if (strcmp(txt, "0x1002") != 0) continue;

        Gpu *g = &out[count++];
        memset(g, 0, sizeof(*g));
        g->card_num = n;
        snprintf(g->card_name, sizeof(g->card_name), "card%d", n);
        snprintf(g->base, sizeof(g->base), "%s", dev);

        char p[MAX_PATH_LEN];
        snprintf(p, sizeof(p), "%s/device", dev);
        if (read_path_text(p, txt, sizeof(txt)) >= 0)
            snprintf(g->device_id, sizeof(g->device_id), "%s", txt);
        else
            snprintf(g->device_id, sizeof(g->device_id), "unknown");

        char real[MAX_PATH_LEN];
        if (realpath(dev, real)) {
            const char *last = strrchr(real, '/');
            snprintf(g->pci_addr, sizeof(g->pci_addr), "%s", last ? last + 1 : real);
        } else {
            snprintf(g->pci_addr, sizeof(g->pci_addr), "unknown");
        }

        snprintf(p, sizeof(p), "%s/mem_info_vram_total", dev);
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
                  &s->fd_freq1,&s->fd_freq2,&s->fd_fan_rpm,&s->fd_pwm,&s->fd_pwm_max,&s->fd_voltage,
                  &s->fd_vram_used,&s->fd_power_profile,&s->fd_pcie_bw};
    for (size_t i = 0; i < sizeof(fds)/sizeof(fds[0]); i++) {
        if (*fds[i] >= 0) close(*fds[i]);
        *fds[i] = -1;
    }
}

static void find_hwmon(const Gpu *g, Sysfs *s) {
    char dirp[MAX_PATH_LEN];
    snprintf(dirp, sizeof(dirp), "%s/hwmon", g->base);
    DIR *d = opendir(dirp);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0) continue;
        char base[MAX_PATH_LEN];
        snprintf(base, sizeof(base), "%s/%s", dirp, de->d_name);
        char p[MAX_PATH_LEN], txt[128];

        snprintf(p, sizeof(p), "%s/temp1_input", base);
        if (path_exists(p)) {
            snprintf(s->temp_path, sizeof(s->temp_path), "%s", p);
            s->fd_temp = open(p, O_RDONLY | O_CLOEXEC);
        }
        snprintf(p, sizeof(p), "%s/power1_average", base);
        if (path_exists(p)) {
            snprintf(s->power_path, sizeof(s->power_path), "%s", p);
            s->fd_power = open(p, O_RDONLY | O_CLOEXEC);
        }
        snprintf(p, sizeof(p), "%s/freq1_input", base);
        if (path_exists(p)) {
            snprintf(s->freq1_path, sizeof(s->freq1_path), "%s", p);
            s->fd_freq1 = open(p, O_RDONLY | O_CLOEXEC);
            snprintf(p, sizeof(p), "%s/freq1_label", base);
            if (read_path_text(p, txt, sizeof(txt)) >= 0) snprintf(s->freq1_label, sizeof(s->freq1_label), "%s", txt);
            else snprintf(s->freq1_label, sizeof(s->freq1_label), "freq1");
        }
        snprintf(p, sizeof(p), "%s/freq2_input", base);
        if (path_exists(p)) {
            snprintf(s->freq2_path, sizeof(s->freq2_path), "%s", p);
            s->fd_freq2 = open(p, O_RDONLY | O_CLOEXEC);
            snprintf(p, sizeof(p), "%s/freq2_label", base);
            if (read_path_text(p, txt, sizeof(txt)) >= 0) snprintf(s->freq2_label, sizeof(s->freq2_label), "%s", txt);
            else snprintf(s->freq2_label, sizeof(s->freq2_label), "freq2");
        }

        snprintf(p, sizeof(p), "%s/fan1_input", base);
        if (path_exists(p)) {
            snprintf(s->fan_rpm_path, sizeof(s->fan_rpm_path), "%s", p);
            s->fd_fan_rpm = open(p, O_RDONLY | O_CLOEXEC);
        }
        snprintf(p, sizeof(p), "%s/pwm1", base);
        if (path_exists(p)) {
            snprintf(s->pwm_path, sizeof(s->pwm_path), "%s", p);
            s->fd_pwm = open(p, O_RDONLY | O_CLOEXEC);
        }
        snprintf(p, sizeof(p), "%s/pwm1_max", base);
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
            snprintf(inp, sizeof(inp), "%s/in%d_input", base, vi);
            if (!path_exists(inp)) continue;
            snprintf(lab, sizeof(lab), "%s/in%d_label", base, vi);
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
                snprintf(s->voltage_label, sizeof(s->voltage_label), "%s", lbl[0] ? lbl : "VDDGFX");
                s->fd_voltage = open(inp, O_RDONLY | O_CLOEXEC);
                break;
            }
        }
        if (s->fd_voltage < 0 && fallback_voltage[0]) {
            snprintf(s->voltage_path, sizeof(s->voltage_path), "%s", fallback_voltage);
            snprintf(s->voltage_label, sizeof(s->voltage_label), "%s", fallback_label);
            s->fd_voltage = open(fallback_voltage, O_RDONLY | O_CLOEXEC);
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
        snprintf(dir, sizeof(dir), "%s/%s", dri_root, de->d_name);
        snprintf(namep, sizeof(namep), "%s/name", dir);
        if (read_path_text(namep, txt, sizeof(txt)) < 0) continue;
        if (!strstr(txt, "amdgpu")) continue;
        if (g->pci_addr[0] && strcmp(g->pci_addr, "unknown") != 0 && !strstr(txt, g->pci_addr)) {
            /* Some kernels omit the domain. Accept a matching bus:slot.func suffix. */
            const char *shortpci = strchr(g->pci_addr, ':');
            if (!shortpci || !strstr(txt, shortpci + 1)) continue;
        }
        snprintf(s->debugfs_dir, sizeof(s->debugfs_dir), "%s", dir);
        snprintf(s->gfxoff_path, sizeof(s->gfxoff_path), "%s/amdgpu_gfxoff", dir);
        snprintf(s->gfxoff_status_path, sizeof(s->gfxoff_status_path), "%s/amdgpu_gfxoff_status", dir);
        snprintf(s->drm_state_path, sizeof(s->drm_state_path), "%s/state", dir);
        break;
    }
    closedir(d);
}

static int sysfs_open(const Gpu *g, Sysfs *s) {
    memset(s, 0, sizeof(*s));
    s->fd_policy_r = s->fd_policy_w = s->fd_sclk_r = s->fd_sclk_w = -1;
    s->fd_mclk_r = s->fd_mclk_w = s->fd_busy = s->fd_mem_busy = -1;
    s->fd_temp = s->fd_power = s->fd_freq1 = s->fd_freq2 = -1;
    s->fd_fan_rpm = s->fd_pwm = s->fd_pwm_max = s->fd_voltage = -1;
    s->fd_vram_used = s->fd_power_profile = s->fd_pcie_bw = -1;

    snprintf(s->policy_path, sizeof(s->policy_path), "%s/power_dpm_force_performance_level", g->base);
    snprintf(s->sclk_path, sizeof(s->sclk_path), "%s/pp_dpm_sclk", g->base);
    snprintf(s->mclk_path, sizeof(s->mclk_path), "%s/pp_dpm_mclk", g->base);
    snprintf(s->busy_path, sizeof(s->busy_path), "%s/gpu_busy_percent", g->base);
    snprintf(s->mem_busy_path, sizeof(s->mem_busy_path), "%s/mem_busy_percent", g->base);
    snprintf(s->vram_used_path, sizeof(s->vram_used_path), "%s/mem_info_vram_used", g->base);
    snprintf(s->power_profile_path, sizeof(s->power_profile_path), "%s/pp_power_profile_mode", g->base);
    snprintf(s->pcie_bw_path, sizeof(s->pcie_bw_path), "%s/pcie_bw", g->base);
    snprintf(s->gpu_metrics_path, sizeof(s->gpu_metrics_path), "%s/gpu_metrics", g->base);

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

static void clock_line_from_text(const char *text, char *out, size_t cap);

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
            snprintf(out, cap, "%s", line);
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

static long read_long_fd(int fd, long fallback);

static void sample_power(Sysfs *s, PowerTelemetry *p) {
    p->gpu_busy = (int)read_long_fd(s->fd_busy, -1);
    p->mem_busy = (int)read_long_fd(s->fd_mem_busy, -1);
    long tm = read_long_fd(s->fd_temp, -1);
    long pw = read_long_fd(s->fd_power, -1);
    p->temp_c = tm >= 0 ? tm / 1000.0 : -1.0;
    p->power_w = pw >= 0 ? pw / 1000000.0 : -1.0;
    long mv = read_long_fd(s->fd_voltage, -1);
    p->voltage_v = mv >= 0 ? mv / 1000.0 : -1.0;
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
                if(refresh>0)snprintf(one,sizeof(one),"%s@%ld",name,refresh);
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
            snprintf(one,sizeof(one),"%s%s%s",conn,modes[0]?" ":"",modes);
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
        snprintf(p,sizeof(p),"%s/%s/enable",dirp,de->d_name);
        if(path_exists(p) && write_path_literal(p,enable?"1":"0")==0)n++;
    }
    closedir(d);
    return n;
}

static int trace_start(TraceState *tr) {
    if(!tr->available || tr->active)return tr->active?0:-1;
    snprintf(tr->instance,sizeof(tr->instance),"%s/instances/amd-gpu-diagnostic-%d",tr->root,getpid());
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


static void terminal_restore(void) {
    if (g_term.active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_term.oldt);
        g_term.active = false;
    }
    printf(C_SHOW C_RESET "\n");
    fflush(stdout);
}

static int terminal_raw(void) {
    if (!isatty(STDIN_FILENO)) return -1;
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &g_term.oldt) < 0) return -1;
    t = g_term.oldt;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0) return -1;
    g_term.active = true;
    atexit(terminal_restore);
    printf(C_HIDE);
    return 0;
}

static int read_key(void) {
    struct pollfd p = {.fd = STDIN_FILENO, .events = POLLIN};
    int rc = poll(&p, 1, 0);
    if (rc > 0 && (p.revents & POLLIN)) {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) == 1) return c;
    }
    return -1;
}

static void clock_line_from_text(const char *text, char *out, size_t cap) {
    DpmLevel levels[MAX_LEVELS];
    int n = parse_dpm_levels(text, levels, MAX_LEVELS);
    bool first = true;
    out[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (!levels[i].active) continue;
        char x[64];
        if (levels[i].index == -2) snprintf(x, sizeof(x), "S:%ld MHz", levels[i].mhz);
        else if (levels[i].mhz >= 0) snprintf(x, sizeof(x), "L%d %ld MHz", levels[i].index, levels[i].mhz);
        else snprintf(x, sizeof(x), "L%d", levels[i].index);
        if (!first) strncat(out, ", ", cap - strlen(out) - 1);
        strncat(out, x, cap - strlen(out) - 1);
        first = false;
    }
    if (first) snprintf(out, cap, "no '*' marker");
}

static long read_long_fd(int fd, long fallback) {
    char b[128];
    if (fd < 0 || read_fd_text(fd, b, sizeof(b)) < 0) return fallback;
    return strtol(b, NULL, 10);
}

static void draw_header(const Gpu *g, View v) {
    const char *vn[]={"Dashboard","GPU Clients","Power / DPM","Desktop / Display","Event Flight Recorder","Kernel Trace","Help"};
    printf(C_HOME C_CLEAR);
    printf(C_CYAN "================================================================================\n" C_RESET);
    printf(C_BOLD C_YELLOW " AMD GPU Diagnostic Tool v%s" C_RESET "   [%s]\n",VERSION,vn[v]);
    printf(C_BOLD C_MAGENTA " %s" C_RESET "\n",g->full_name);
    printf(C_DIM " %s  PCI %s  ID 1002:%s  VRAM %.1f GiB" C_RESET "\n",
           g->card_name,g->pci_addr,strncmp(g->device_id,"0x",2)==0?g->device_id+2:g->device_id,
           (double)g->vram_bytes/(1024.0*1024.0*1024.0));
    printf(C_CYAN "================================================================================\n" C_RESET);
}

static void draw_nav(void) {
    printf("\n" C_BLUE "Modes" C_RESET " [1]Auto [2]Low [3]High [4]Standard [5]Min-GFX [6]Min-MEM [7]Peak [8]Manual\n");
    printf(C_BLUE "Views" C_RESET " [0]Home [P]Clients [G]Power [W]Desktop [E]Events [R]Trace [H]Help   [Q]Quit\n");
}

static void draw_vram_bar(const Gpu *g, const PowerTelemetry *pt) {
    const int width = 28;
    double used_gib = pt->vram_used / (1024.0*1024.0*1024.0);
    double total_gib = g->vram_bytes / (1024.0*1024.0*1024.0);
    double frac = g->vram_bytes ? (double)pt->vram_used / (double)g->vram_bytes : 0.0;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    int fill = (int)(frac * width + 0.5);
    const char *color = frac >= 0.90 ? C_RED : (frac >= 0.75 ? C_YELLOW : C_GREEN);
    printf(" VRAM  %s[", color);
    for (int i=0;i<width;i++) putchar(i<fill ? '#' : '.');
    printf("]" C_RESET "  %.2f / %.1f GiB  %5.1f%%\n",used_gib,total_gib,frac*100.0);
}

static void draw_hardware_summary(const Gpu *g, const Sysfs *s, const PowerTelemetry *pt) {
    printf(C_DIM " Hardware: " C_RESET);
    if (pt->temp_c >= 0) printf("Temp %.1f C",pt->temp_c); else printf("Temp n/a");
    if (pt->fan_rpm >= 0) {
        printf(" | Fan %d RPM",pt->fan_rpm);
        if (pt->fan_pct >= 0) printf(" (%.0f%%)",pt->fan_pct);
    } else printf(" | Fan n/a");
    if (pt->power_w >= 0) printf(" | Power %.1f W",pt->power_w); else printf(" | Power n/a");
    if (pt->voltage_v >= 0) printf(" | %s %.3f V",s->voltage_label[0]?s->voltage_label:"VDD",pt->voltage_v);
    else printf(" | Voltage n/a");
    printf("\n");

    printf(C_DIM " Clocks:   " C_RESET);
    if (pt->gfx_hz >= 0) printf("GFX %.0f MHz",pt->gfx_hz/1000000.0); else printf("GFX n/a");
    if (pt->mem_hz >= 0) printf(" | MEM %.0f MHz",pt->mem_hz/1000000.0); else printf(" | MEM n/a");
    if (pt->gpu_busy >= 0) printf(" | GPU %d%%",pt->gpu_busy);
    if (pt->mem_busy >= 0) printf(" | MEM busy %d%%",pt->mem_busy);
    printf("\n");
    draw_vram_bar(g,pt);
}

static void draw_dashboard(const Gpu *g, Sysfs *s, const Target *t, const Stats *st,
                    int interval_us, bool adaptive, bool burst_active, int active_interval_us,
                    const ClientMonitor *cm,const PowerTelemetry *pt,const SessionInfo *si,
                    const EventLog *el,const TraceState *tr,const char *message) {
    char policy[128]="?";
    read_fd_text(s->fd_policy_r,policy,sizeof(policy));
    bool ok=strcmp(policy,t->policy)==0;
    const GpuClient *a=cm->count>0?&cm->items[0]:NULL;
    const GpuClient *b=cm->count>1?&cm->items[1]:NULL;
    const DiagEvent *ev=event_last(el);

    draw_header(g,VIEW_DASHBOARD);
    draw_hardware_summary(g,s,pt);
    printf("\n");
    printf(C_BLUE "LOCK" C_RESET "    target " C_BOLD C_YELLOW "%s" C_RESET "   driver %s%s%s   %s\n",
           t->policy,ok?C_GREEN:C_RED,policy,C_RESET,ok?C_GREEN "LOCK OK" C_RESET:C_RED "MISMATCH" C_RESET);
    printf("        %s | %.0f checks/s | corrections %llu | resets %llu | steady %d us / active %d us%s\n",
           t->enabled?"enforcing":"PAUSED",st->loop_hz,(unsigned long long)st->corrections,
           (unsigned long long)st->policy_mismatches,interval_us,active_interval_us,
           adaptive&&burst_active?" BURST":"");

    printf(C_BLUE "POWER" C_RESET "   GPU %s",pt->gpu_busy>=0?"":"?");
    if(pt->gpu_busy>=0)printf("%d%%",pt->gpu_busy);
    if(pt->mem_busy>=0)printf("  VRAM %d%%",pt->mem_busy);
    if(pt->power_w>=0)printf("  %.1f W",pt->power_w);
    if(pt->temp_c>=0)printf("  %.1f C",pt->temp_c);
    printf(" | GFXOFF %s\n",gfxoff_text(pt->gfxoff_status));
    printf("        clocks %s",st->sclk_display[0]?st->sclk_display:"DPM sampler off");
    printf(" / %s | power profile %.80s\n",st->mclk_display[0]?st->mclk_display:"DPM sampler off",pt->power_profile);

    printf(C_BLUE "CLIENTS" C_RESET " ");
    if(a) printf("#1 %-24.24s %6.2f%%",a->role,a->activity_pct); else printf("no DRM fdinfo clients");
    if(b) printf(" | #2 %-20.20s %6.2f%%",b->role,b->activity_pct);
    printf("\n");
    printf("        scans %llu | fdinfo reads %llu | latest scan CPU %.2f%% during scan\n",
           (unsigned long long)cm->scans,(unsigned long long)cm->fdinfo_reads,cm->scan_cpu_pct);

    printf(C_BLUE "DESKTOP" C_RESET " %-10s | %-22.22s | compositor %-22.22s\n",
           si->session_type,si->desktop,si->compositor);
    printf("        outputs %d | %.58s | KMS %.48s | planes %d\n",si->connected_outputs,si->connector_summary,si->kms_modes,si->active_planes);

    printf(C_BLUE "EVENTS" C_RESET "  %llu captured", (unsigned long long)el->total);
    if(ev) printf(" | last %-14s actor %-22.22s %.1f%%",ev->kind,ev->actor,ev->actor_activity);
    else printf(" | no correlated event yet");
    printf("\n");
    printf("        event log %s",el->logging?el->log_path:"OFF");
    if(ev && ev->trace_actor[0]) printf(" | near trace actor %s",ev->trace_actor);
    printf("\n");

    printf(C_BLUE "TRACE" C_RESET "   %s | events amdgpu=%d sched=%d drm=%d | display hooks=%d | lines %llu\n",
           tr->active?C_GREEN "ACTIVE" C_RESET:(tr->available?"off":"unavailable"),
           tr->amdgpu_events,tr->drm_sched_events,tr->drm_events,tr->drm_display_events_enabled,(unsigned long long)tr->lines);
    printf("        last actor %s\n",tr->last_actor[0]?tr->last_actor:"none");

    draw_nav();
    printf(C_DIM "Control [Space] pause | [L] event log | [C] DPM telemetry | [B] benchmark" C_RESET "\n");
    if(message&&*message)printf("\n" C_YELLOW "%s" C_RESET "\n",message);
}

static void draw_clients(const Gpu *g,const ClientMonitor *cm,const EventLog *el,const char *message) {
    draw_header(g,VIEW_CLIENTS);
    printf("Per-process DRM fdinfo deltas. Activity is summed engine busy time over the sample window.\n\n");
    printf(" PID     PROCESS / ROLE                     TOTAL    GFX    COMP    DMA   MEDIA      VRAM\n");
    printf(" ---------------------------------------------------------------------------------------\n");
    int n=cm->count<18?cm->count:18;
    for(int i=0;i<n;i++) {
        const GpuClient *c=&cm->items[i];
        printf(" %-7d %-34.34s %7.2f %6.2f %6.2f %6.2f %7.2f %8.1fM\n",
               c->pid,c->role,c->activity_pct,c->gfx_pct,c->compute_pct,c->dma_pct,c->media_pct,
               c->vram_bytes/(1024.0*1024.0));
    }
    if(!n)printf(" No matching amdgpu DRM clients found yet.\n");
    const DiagEvent *ev=event_last(el);
    printf("\nSummary: ");
    if(n)printf("top=%s %.2f%%",cm->items[0].role,cm->items[0].activity_pct); else printf("no active client");
    if(ev)printf(" | last event actor=%s",ev->actor);
    printf("\nScans=%llu fdinfo=%llu errors=%llu scan CPU=%.2f%% (only while scanning)\n",
           (unsigned long long)cm->scans,(unsigned long long)cm->fdinfo_reads,
           (unsigned long long)cm->scan_errors,cm->scan_cpu_pct);
    draw_nav();
    if(message&&*message)printf("\n" C_YELLOW "%s" C_RESET "\n",message);
}

static void draw_power(const Gpu *g,const Sysfs *s,const Target *t,const Stats *st,const PowerTelemetry *p,
                       int interval_us,int dpm_interval_us,bool monitor_clocks,bool adaptive,const char *message) {
    draw_header(g,VIEW_POWER);
    printf(C_BLUE "Policy / watchdog" C_RESET "\n");
    printf(" target %-22s checks %.1f Hz  CPU %.3f%%  interval %d us  adaptive %s\n",
           t->policy,st->loop_hz,st->loop_cpu_pct,interval_us,adaptive?"on":"off");
    printf(" corrections=%llu policy resets=%llu verify failures=%llu deadline misses=%llu\n",
           (unsigned long long)st->corrections,(unsigned long long)st->policy_mismatches,
           (unsigned long long)st->verify_failures,(unsigned long long)st->deadline_misses);
    printf("\n" C_BLUE "Live telemetry" C_RESET "\n");
    printf(" GPU=%d%% MEM=%d%%  power=%.1f W  temp=%.1f C  VRAM used=%.2f GiB\n",
           p->gpu_busy,p->mem_busy,p->power_w,p->temp_c,p->vram_used/(1024.0*1024.0*1024.0));
    printf(" fan=");
    if(p->fan_rpm>=0) {
        printf("%d RPM",p->fan_rpm);
        if(p->fan_pct>=0)printf(" (%.1f%%)",p->fan_pct);
    } else printf("n/a");
    printf("  voltage=");
    if(p->voltage_v>=0)printf("%s %.3f V",s->voltage_label[0]?s->voltage_label:"VDD",p->voltage_v);
    else printf("n/a");
    printf("\n");
    if(p->gfx_hz>=0 || p->mem_hz>=0)
        printf(" hwmon clocks: %s=%.0f MHz  %s=%.0f MHz\n",p->freq1_label,p->gfx_hz/1000000.0,p->freq2_label,p->mem_hz/1000000.0);
    draw_vram_bar(g,p);
    printf(" DPM: SCLK %-28s MCLK %-28s\n",st->sclk_display[0]?st->sclk_display:"off",
           st->mclk_display[0]?st->mclk_display:"off");
    printf(" DPM scans=%llu interval=%d us (%s) observed changes sclk=%llu mclk=%llu\n",
           (unsigned long long)st->dpm_checks,dpm_interval_us,monitor_clocks?"ON":"OFF",
           (unsigned long long)st->observed_sclk_changes,(unsigned long long)st->observed_mclk_changes);
    printf("\n" C_BLUE "Power-management clues" C_RESET "\n");
    printf(" GFXOFF enabled=%s status=%s sampled transitions=%llu\n",
           p->gfxoff_enabled<0?"n/a":(p->gfxoff_enabled?"yes":"no"),gfxoff_text(p->gfxoff_status),
           (unsigned long long)p->gfxoff_status_changes);
    printf(" pp_power_profile_mode: %.150s\n",p->power_profile);
    printf(" PCIe accounting: %.160s\n",p->pcie_bw);
    printf(" gpu_metrics: %s",p->gpu_metrics_present?"present":"unavailable");
    if(p->gpu_metrics_format>=0)printf(" header v%d.%d",p->gpu_metrics_format,p->gpu_metrics_content);
    printf("\n debugfs: %s\n",s->debugfs_dir[0]?s->debugfs_dir:"not mounted / not found");
    draw_nav();
    printf(C_DIM "[C] DPM telemetry on/off [D] DPM rate [-/+] watchdog rate [A] adaptive [S/M] manual clocks" C_RESET "\n");
    if(message&&*message)printf("\n" C_YELLOW "%s" C_RESET "\n",message);
}

static void draw_desktop(const Gpu *g,const SessionInfo *si,const char *message) {
    draw_header(g,VIEW_DESKTOP);
    printf("Session type: %-16s Desktop: %s\n",si->session_type,si->desktop);
    printf("Compositor:   %-24s PID %d\n",si->compositor,si->compositor_pid);
    printf("DISPLAY:      %-24s WAYLAND_DISPLAY: %s\n",si->display[0]?si->display:"-",si->wayland_display[0]?si->wayland_display:"-");
    printf("\nConnected outputs: %d\n  %s\n",si->connected_outputs,si->connector_summary);
    printf("KMS current modes: %s\n",si->kms_modes);
    printf("Active hardware planes: %d   framebuffer allocators: %s\n",si->active_planes,si->plane_allocators);
    printf("\nRendering visibility:\n");
    if(session_is_x11(si)) {
        printf(" X11 session detected. Global window/presentation correlation can be added with optional XCB Present support.\n");
        printf(" Current build intentionally does not require libxcb; process DRM + compositor attribution is active now.\n");
    } else if(session_is_wayland(si)) {
        printf(" Wayland session detected. Other clients' surface presentation streams are intentionally not globally visible.\n");
        printf(" Correlation uses per-process DRM fdinfo, compositor activity, DRM tracepoints, and display state instead.\n");
    } else printf(" Session protocol could not be identified yet.\n");
    printf("\nSummary: %s / %s | %d outputs | %s | planes=%d | compositor %s\n",
           si->desktop,si->session_type,si->connected_outputs,si->kms_modes,si->active_planes,si->compositor);
    draw_nav();
    if(message&&*message)printf("\n" C_YELLOW "%s" C_RESET "\n",message);
}

static void draw_events(const Gpu *g,const EventLog *el,const char *message) {
    draw_header(g,VIEW_EVENTS);
    printf("Flight recorder: %llu total events, %d retained in RAM. CSV logging: %s\n\n",
           (unsigned long long)el->total,el->count,el->logging?el->log_path:"OFF");
    printf(" age(s)  EVENT                 DETAIL                                  LIKELY ACTOR\n");
    printf(" ---------------------------------------------------------------------------------------\n");
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    int show=el->count<20?el->count:20;
    for(int k=0;k<show;k++) {
        int i=el->head-1-k; while(i<0)i+=MAX_DIAG_EVENTS;
        const DiagEvent *e=&el->ring[i];
        printf(" %6.2f  %-20.20s %-38.38s %-24.24s",
               diff_ns(now,e->when)/1e9,e->kind,e->detail,e->actor);
        if(e->trace_actor[0])printf(" [trace:%s]",e->trace_actor);
        printf("\n");
    }
    if(!show)printf(" No events yet. Policy resets, DPM changes and sampled GFXOFF transitions will appear here.\n");
    draw_nav();
    printf(C_DIM "[L] toggle CSV event logging" C_RESET "\n");
    if(message&&*message)printf("\n" C_YELLOW "%s" C_RESET "\n",message);
}

static void draw_trace(const Gpu *g,const TraceState *tr,const char *message) {
    draw_header(g,VIEW_TRACE);
    printf("tracefs: %s\n",tr->root[0]?tr->root:"not found");
    printf("Available events: amdgpu=%d drm_sched=%d drm=%d\n",tr->amdgpu_events,tr->drm_sched_events,tr->drm_events);
    printf("Isolated trace instance: %s\n",tr->active?tr->instance:"OFF");
    printf("Display trace hooks enabled: %d (matching vblank/flip/atomic/commit events)\n",tr->drm_display_events_enabled);
    printf("Captured lines: %llu  amdgpu~%llu sched~%llu drm/display~%llu\n",
           (unsigned long long)tr->lines,(unsigned long long)tr->amdgpu_lines,
           (unsigned long long)tr->sched_lines,(unsigned long long)tr->drm_lines);
    printf("Last actor: %s\n",tr->last_actor[0]?tr->last_actor:"none");
    printf("Last line:\n  %.450s\n",tr->last_line[0]?tr->last_line:"none");
    printf("\nTrace mode uses its own tracefs instance and enables amdgpu + drm_sched events only.\n");
    printf("It is OFF by default because tracing can generate substantial data during a game.\n");
    draw_nav();
    printf(C_DIM "[T] start/stop kernel trace" C_RESET "\n");
    if(message&&*message)printf("\n" C_YELLOW "%s" C_RESET "\n",message);
}

static void draw_help(const Gpu *g,const char *message) {
    draw_header(g,VIEW_HELP);
    printf("Dashboard pages\n");
    printf("  [0] dashboard   [P] per-process DRM clients   [G] power/DPM\n");
    printf("  [W] desktop/display   [E] flight recorder   [R] kernel trace\n\n");
    printf("Power target\n");
    printf("  [1] auto [2] low [3] high [4] profile_standard [5] profile_min_sclk\n");
    printf("  [6] profile_min_mclk [7] profile_peak [8] manual\n\n");
    printf("Control\n");
    printf("  [Space] pause/resume enforcement   [S]/[M] select manual SCLK/MCLK   [X] clear manual locks\n");
    printf("  [-]/[+] steady watchdog rate       [A] adaptive burst               [C] DPM telemetry\n");
    printf("  [D] DPM sample rate                [L] CSV event logging             [B] safe policy benchmark\n");
    printf("  [T] kernel trace on/off (isolated tracefs instance)                 [Q] quit\n\n");
    printf("Sampling tiers: policy=%d us default; clients=%.1f Hz; power/GFXOFF=%.1f Hz; desktop=%.1f Hz.\n",
           DEFAULT_INTERVAL_US,1e9/CLIENT_SAMPLE_NS,1e9/POWER_SAMPLE_NS,1e9/SESSION_SAMPLE_NS);
    draw_nav();
    if(message&&*message)printf("\n" C_YELLOW "%s" C_RESET "\n",message);
}

static void draw_ui(const Gpu *g, Sysfs *s, const Target *t, const Stats *st,
                    int interval_us, int dpm_interval_us, bool monitor_clocks,
                    bool adaptive, bool burst_active, int active_interval_us,
                    const ClientMonitor *cm,const PowerTelemetry *pt,const SessionInfo *si,
                    const EventLog *el,const TraceState *tr,View view,const char *message) {
    switch(view) {
        case VIEW_CLIENTS: draw_clients(g,cm,el,message); break;
        case VIEW_POWER: draw_power(g,s,t,st,pt,interval_us,dpm_interval_us,monitor_clocks,adaptive,message); break;
        case VIEW_DESKTOP: draw_desktop(g,si,message); break;
        case VIEW_EVENTS: draw_events(g,el,message); break;
        case VIEW_TRACE: draw_trace(g,tr,message); break;
        case VIEW_HELP: draw_help(g,message); break;
        default: draw_dashboard(g,s,t,st,interval_us,adaptive,burst_active,active_interval_us,cm,pt,si,el,tr,message); break;
    }
    fflush(stdout);
}

static void show_levels_and_prompt(Sysfs *s, Target *t, bool sclk) {
    int fd = sclk ? s->fd_sclk_r : s->fd_mclk_r;
    if (fd < 0) return;
    char text[MAX_TEXT];
    if (read_fd_text(fd, text, sizeof(text)) < 0) return;
    DpmLevel levels[MAX_LEVELS];
    int n = parse_dpm_levels(text, levels, MAX_LEVELS);

    if (g_term.active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_term.oldt);
        g_term.active = false;
    }
    printf(C_SHOW C_CLEAR C_HOME);
    printf("%s DPM levels:\n\n", sclk ? "SCLK" : "MCLK");
    for (int i = 0; i < n; i++) {
        if (levels[i].index >= 0)
            printf("  %d: %ld MHz%s\n", levels[i].index, levels[i].mhz, levels[i].active ? "  *" : "");
    }
    printf("\nSelect level number, or blank to cancel: ");
    fflush(stdout);
    char b[64];
    if (fgets(b, sizeof(b), stdin)) {
        trim(b);
        if (*b) {
            int wanted = atoi(b);
            bool found = false;
            for (int i = 0; i < n; i++) if (levels[i].index == wanted) found = true;
            if (found) {
                snprintf(t->policy, sizeof(t->policy), "manual");
                if (sclk) { t->lock_sclk = true; t->sclk_level = wanted; }
                else { t->lock_mclk = true; t->mclk_level = wanted; }
                apply_policy(s, "manual");
                apply_level(sclk ? s->fd_sclk_w : s->fd_mclk_w, wanted);
            }
        }
    }
    terminal_raw();
}

static int benchmark_reads(Sysfs *s, bool full, int duration_ms, int interval_us,
                           double *cpu_pct, double *actual_hz, double *avg_read_us) {
    struct timespec wall0, wall1, cpu0, cpu1, next;
    clock_gettime(CLOCK_MONOTONIC, &wall0);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu0);
    next = wall0;
    uint64_t loops = 0;
    long long read_cpu_ns = 0;
    char b1[256], b2[MAX_TEXT], b3[MAX_TEXT];
    long long end_ns = ts_ns(wall0) + (long long)duration_ms * 1000000LL;
    const long long step = (long long)interval_us * 1000LL;

    while (1) {
        struct timespec rn0, rn1, now;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &rn0);
        if (read_fd_text(s->fd_policy_r, b1, sizeof(b1)) < 0) return -1;
        if (full && s->fd_sclk_r >= 0) read_fd_text(s->fd_sclk_r, b2, sizeof(b2));
        if (full && s->fd_mclk_r >= 0) read_fd_text(s->fd_mclk_r, b3, sizeof(b3));
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &rn1);
        read_cpu_ns += diff_ns(rn1, rn0);
        loops++;

        add_ns(&next, step);
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (ts_ns(now) >= end_ns) break;
        if (ts_ns(next) > ts_ns(now)) clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        else next = now;
    }

    clock_gettime(CLOCK_MONOTONIC, &wall1);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu1);
    double wall = diff_ns(wall1, wall0) / 1e9;
    double cpu = diff_ns(cpu1, cpu0) / 1e9;
    *cpu_pct = wall > 0 ? cpu / wall * 100.0 : 0.0;
    *actual_hz = wall > 0 ? loops / wall : 0.0;
    *avg_read_us = loops ? (read_cpu_ns / 1000.0) / loops : 0.0;
    return 0;
}

static void run_benchmark(Sysfs *s, bool full) {
    static const int intervals[] = {20000, 5000, 2000, 1000, 500};
    if (g_term.active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_term.oldt);
        g_term.active = false;
    }
    printf(C_SHOW C_CLEAR C_HOME);
    printf(C_BOLD "Audio-safe sysfs watchdog benchmark (%s)\n" C_RESET, full ? "policy + SCLK + MCLK" : "policy only");
    printf("This reads live amdgpu sysfs attributes and does not change the GPU.\n");
    printf("v0.3 intentionally does not benchmark below 500 us from the UI.\n\n");
    printf(" interval      target Hz     actual Hz     thread CPU     read CPU/check\n");
    printf(" ----------------------------------------------------------------------\n");
    for (size_t i = 0; i < sizeof(intervals)/sizeof(intervals[0]); i++) {
        double cpu=0,hz=0,rus=0;
        if (benchmark_reads(s, full, 350, intervals[i], &cpu, &hz, &rus) < 0) {
            printf(" %7d us      ERROR\n", intervals[i]);
            break;
        }
        printf(" %7d us   %10.0f   %10.1f     %7.3f%%      %8.3f us\n",
               intervals[i], 1000000.0 / intervals[i], hz, cpu, rus);
        fflush(stdout);
    }
    printf("\nPress Enter to return.\n");
    fflush(stdout);
    char b[8];
    fgets(b, sizeof(b), stdin);
    terminal_raw();
}

static int next_interval(int current, bool faster) {
    static const int vals[] = {500,1000,2000,5000,10000,20000,50000,100000,250000,500000,1000000};
    int n = (int)(sizeof(vals)/sizeof(vals[0]));
    int nearest = 0;
    long best = labs((long)current - vals[0]);
    for (int i = 1; i < n; i++) {
        long d = labs((long)current - vals[i]);
        if (d < best) { best = d; nearest = i; }
    }
    if (faster) {
        if (nearest > 0) nearest--;
    } else {
        if (nearest < n - 1) nearest++;
    }
    return vals[nearest];
}

static int next_dpm_interval(int current) {
    static const int vals[] = {50000,100000,250000,500000,1000000,2000000};
    int n = (int)(sizeof(vals)/sizeof(vals[0]));
    for (int i = 0; i < n; i++) {
        if (vals[i] == current) return vals[(i + 1) % n];
    }
    return DEFAULT_DPM_INTERVAL_US;
}

static void usage(const char *argv0) {
    printf("amd-gpu-diagnostic %s\n", VERSION);
    printf("Usage: %s [--card N] [--interval-us N] [--dpm-interval-us N] [--mode MODE]\n", argv0);
    printf("          [--monitor-dpm] [--fixed] [--unsafe-fast] [--normal-priority] [--benchmark]\n");
    printf("Modes: auto low high manual profile_standard profile_min_sclk profile_min_mclk profile_peak\n");
    printf("Defaults: profile_standard, 5 ms adaptive watchdog, DPM telemetry off.\n");
    printf("Diagnostics: per-process DRM clients, power/GFXOFF, desktop/display, flight recorder, optional tracefs.\n");
}

int main(int argc, char **argv) {
    int requested_card = -1;
    int interval_us = DEFAULT_INTERVAL_US;
    int dpm_interval_us = DEFAULT_DPM_INTERVAL_US;
    bool monitor_clocks = false;
    bool adaptive = true;
    bool unsafe_fast = false;
    bool lower_priority = true;
    bool bench_at_start = false;
    bool mode_given = false;
    Target target = {.policy="profile_standard", .enabled=true, .lock_sclk=false, .lock_mclk=false, .sclk_level=0, .mclk_level=0};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(argv[0]); return 0; }
        else if (strcmp(argv[i], "--card") == 0 && i + 1 < argc) requested_card = atoi(argv[++i]);
        else if (strcmp(argv[i], "--interval-us") == 0 && i + 1 < argc) interval_us = atoi(argv[++i]);
        else if (strcmp(argv[i], "--dpm-interval-us") == 0 && i + 1 < argc) dpm_interval_us = atoi(argv[++i]);
        else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) { snprintf(target.policy, sizeof(target.policy), "%s", argv[++i]); mode_given = true; }
        else if (strcmp(argv[i], "--policy-only") == 0) monitor_clocks = false; /* v0.2 compatibility */
        else if (strcmp(argv[i], "--monitor-dpm") == 0) monitor_clocks = true;
        else if (strcmp(argv[i], "--fixed") == 0) adaptive = false;
        else if (strcmp(argv[i], "--unsafe-fast") == 0) unsafe_fast = true;
        else if (strcmp(argv[i], "--normal-priority") == 0) lower_priority = false;
        else if (strcmp(argv[i], "--benchmark") == 0) bench_at_start = true;
        else { fprintf(stderr, "Unknown argument: %s\n", argv[i]); usage(argv[0]); return 2; }
    }

    if (mode_given && !valid_policy(target.policy)) {
        fprintf(stderr, "Invalid mode: %s\n", target.policy);
        return 2;
    }
    int min_interval_us = unsafe_fast ? UNSAFE_MIN_INTERVAL_US : MIN_INTERVAL_US;
    if (interval_us < min_interval_us) interval_us = min_interval_us;
    if (interval_us > MAX_INTERVAL_US) interval_us = MAX_INTERVAL_US;
    if (dpm_interval_us < MIN_DPM_INTERVAL_US) dpm_interval_us = MIN_DPM_INTERVAL_US;
    if (dpm_interval_us > MAX_DPM_INTERVAL_US) dpm_interval_us = MAX_DPM_INTERVAL_US;

    if (geteuid() != 0) {
        fprintf(stderr, "amd-gpu-diagnostic must run as root because amdgpu power sysfs writes require root.\n");
        fprintf(stderr, "Run: sudo %s\n", argv[0]);
        return 1;
    }

    /* This watchdog is latency-insensitive compared with PipeWire. Give audio
       and games scheduling preference unless the user explicitly opts out. */
    if (lower_priority) (void)setpriority(PRIO_PROCESS, 0, 10);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    Gpu gpus[16];
    int ngpu = detect_amd_gpus(gpus, 16);
    if (ngpu <= 0) {
        fprintf(stderr, "No AMD DRM cards found under /sys/class/drm/card*/device.\n");
        return 1;
    }
    int gi = choose_gpu(gpus, ngpu, requested_card);
    if (gi < 0) {
        fprintf(stderr, "Requested AMD card not found. Detected:");
        for (int i = 0; i < ngpu; i++) fprintf(stderr, " card%d", gpus[i].card_num);
        fprintf(stderr, "\n");
        return 1;
    }
    Gpu *g = &gpus[gi];

    Sysfs s;
    if (sysfs_open(g, &s) < 0) {
        fprintf(stderr, "Cannot open amdgpu power controls for %s: %s\n", g->card_name, strerror(errno));
        return 1;
    }

    if (apply_policy(&s, target.policy) < 0) {
        fprintf(stderr, "Cannot set initial policy '%s': %s\n", target.policy, strerror(errno));
        sysfs_close(&s);
        return 1;
    }

    if (terminal_raw() < 0) {
        fprintf(stderr, "Interactive TTY required.\n");
        sysfs_close(&s);
        return 1;
    }

    if (bench_at_start) run_benchmark(&s, false);

    Stats st = {0};
    st.last_active_sclk = st.last_active_mclk = INT_MIN;
    ClientMonitor clients = {0};
    PowerTelemetry power = {.gpu_busy=-1,.mem_busy=-1,.temp_c=-1,.power_w=-1,.voltage_v=-1,.fan_rpm=-1,.fan_pct=-1,
                            .gfx_hz=-1,.mem_hz=-1,
                            .gfxoff_enabled=-1,.gfxoff_status=-1,.last_gfxoff_status=-1};
    SessionInfo session = {0};
    EventLog events = {0};
    TraceState trace;
    trace_discover(&trace);
    View view = VIEW_DASHBOARD;
    sample_power(&s,&power);
    sample_clients(g,&clients);
    sample_session(g,&s,&session,&clients);
    struct timespec next, next_dpm, next_clients, next_power, next_session, last_ui, stat_wall0, stat_cpu0;
    clock_gettime(CLOCK_MONOTONIC, &next);
    next_dpm = next;
    add_ns(&next_dpm, (long long)dpm_interval_us * 1000LL);
    next_clients = next; add_ns(&next_clients, CLIENT_SAMPLE_NS);
    next_power = next; add_ns(&next_power, POWER_SAMPLE_NS);
    next_session = next; add_ns(&next_session, SESSION_SAMPLE_NS);
    last_ui = next;
    stat_wall0 = next;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stat_cpu0);
    uint64_t stat_checks0 = 0;
    char message[256] = "";
    long long burst_until_ns = 0;
    int active_interval_us = interval_us;

    while (!g_stop) {
        struct timespec loop_now;
        clock_gettime(CLOCK_MONOTONIC, &loop_now);

        int enforce_rc = 0;
        if (target.enabled) enforce_rc = enforce_policy_once(&s, &target, &st);

        /* A real mismatch is the only thing that activates the faster polling
           window. Stable operation stays at the low-overhead base interval. */
        clock_gettime(CLOCK_MONOTONIC, &loop_now);
        if (adaptive && target.enabled && enforce_rc > 0) {
            if (ts_ns(loop_now) >= burst_until_ns) st.burst_entries++;
            burst_until_ns = ts_ns(loop_now) + ADAPTIVE_BURST_NS;
        }
        if (enforce_rc > 0) {
            char d[160];
            snprintf(d,sizeof(d),"restored target %s",target.policy);
            event_log_add(&events,"POLICY RESET",d,&clients,&trace);
        }
        bool burst_active = adaptive && target.enabled && ts_ns(loop_now) < burst_until_ns;
        active_interval_us = burst_active ? ADAPTIVE_BURST_INTERVAL_US : interval_us;

        if (monitor_clocks && diff_ns(loop_now, next_dpm) >= 0) {
            uint64_t old_s=st.observed_sclk_changes, old_m=st.observed_mclk_changes;
            monitor_dpm_once(&s, &target, &st, target.enabled);
            if(st.observed_sclk_changes!=old_s || st.observed_mclk_changes!=old_m) {
                char d[180];
                snprintf(d,sizeof(d),"SCLK %s | MCLK %s",st.sclk_display,st.mclk_display);
                event_log_add(&events,"DPM CHANGE",d,&clients,&trace);
            }
            clock_gettime(CLOCK_MONOTONIC, &next_dpm);
            add_ns(&next_dpm, (long long)dpm_interval_us * 1000LL);
        }

        if(diff_ns(loop_now,next_clients)>=0) {
            sample_clients(g,&clients);
            next_clients=loop_now; add_ns(&next_clients,CLIENT_SAMPLE_NS);
        }
        if(diff_ns(loop_now,next_power)>=0) {
            uint64_t oldgo=power.gfxoff_status_changes;
            double oldpw=power.power_w;
            sample_power(&s,&power);
            if(power.gfxoff_status_changes!=oldgo) {
                char d[128]; snprintf(d,sizeof(d),"status %s",gfxoff_text(power.gfxoff_status));
                event_log_add(&events,"GFXOFF",d,&clients,&trace);
            }
            if(oldpw>=0 && power.power_w>=0 && (power.power_w-oldpw>30.0 || oldpw-power.power_w>30.0)) {
                char d[128]; snprintf(d,sizeof(d),"%.1f W -> %.1f W",oldpw,power.power_w);
                event_log_add(&events,"POWER STEP",d,&clients,&trace);
            }
            next_power=loop_now; add_ns(&next_power,POWER_SAMPLE_NS);
        }
        if(diff_ns(loop_now,next_session)>=0) {
            sample_session(g,&s,&session,&clients);
            next_session=loop_now; add_ns(&next_session,SESSION_SAMPLE_NS);
        }
        trace_pump(&trace);

        int key = read_key();
        if (key >= 0) {
            message[0] = '\0';
            if (key >= '1' && key <= '8') {
                int idx = key - '1';
                snprintf(target.policy, sizeof(target.policy), "%s", POLICIES[idx]);
                if (strcmp(target.policy, "manual") != 0) {
                    target.lock_sclk = false;
                    target.lock_mclk = false;
                }
                if (apply_policy(&s, target.policy) == 0)
                    snprintf(message, sizeof(message), "Target changed to %s", target.policy);
                else
                    snprintf(message, sizeof(message), "Failed to apply %s: %s", target.policy, strerror(errno));
            } else if (key == 's' || key == 'S') {
                show_levels_and_prompt(&s, &target, true);
                snprintf(message, sizeof(message), "SCLK manual lock updated");
            } else if (key == 'm' || key == 'M') {
                show_levels_and_prompt(&s, &target, false);
                snprintf(message, sizeof(message), "MCLK manual lock updated");
            } else if (key == 'x' || key == 'X') {
                target.lock_sclk = target.lock_mclk = false;
                snprintf(message, sizeof(message), "Manual SCLK/MCLK locks cleared; policy remains %s", target.policy);
            } else if (key == ' ') {
                target.enabled = !target.enabled;
                snprintf(message, sizeof(message), "Watchdog %s", target.enabled ? "enabled" : "paused");
            } else if (key == '+' || key == '=') {
                interval_us = next_interval(interval_us, true);
                snprintf(message, sizeof(message), "Steady watchdog interval %d us", interval_us);
                clock_gettime(CLOCK_MONOTONIC, &next);
            } else if (key == '-' || key == '_') {
                interval_us = next_interval(interval_us, false);
                snprintf(message, sizeof(message), "Steady watchdog interval %d us", interval_us);
                clock_gettime(CLOCK_MONOTONIC, &next);
            } else if (key == 'a' || key == 'A') {
                adaptive = !adaptive;
                burst_until_ns = 0;
                active_interval_us = interval_us;
                snprintf(message, sizeof(message), "Adaptive correction burst %s", adaptive ? "enabled" : "disabled");
            } else if (key == 'd' || key == 'D') {
                dpm_interval_us = next_dpm_interval(dpm_interval_us);
                clock_gettime(CLOCK_MONOTONIC, &next_dpm);
                add_ns(&next_dpm, (long long)dpm_interval_us * 1000LL);
                snprintf(message, sizeof(message), "DPM scan interval %d us (%.1f Hz)",
                         dpm_interval_us, 1000000.0 / dpm_interval_us);
            } else if (key == 'c' || key == 'C') {
                monitor_clocks = !monitor_clocks;
                snprintf(message, sizeof(message), "Clock-state monitoring %s", monitor_clocks ? "enabled" : "disabled");
            } else if (key == '0') {
                view=VIEW_DASHBOARD;
            } else if (key == 'p' || key == 'P') {
                view=VIEW_CLIENTS;
            } else if (key == 'g' || key == 'G') {
                view=VIEW_POWER;
            } else if (key == 'w' || key == 'W') {
                view=VIEW_DESKTOP;
            } else if (key == 'e' || key == 'E') {
                view=VIEW_EVENTS;
            } else if (key == 'r' || key == 'R') {
                view=VIEW_TRACE;
            } else if (key == 'h' || key == 'H' || key == '?') {
                view=VIEW_HELP;
            } else if (key == 'l' || key == 'L') {
                toggle_event_logging(&events);
                if(events.logging) snprintf(message,sizeof(message),"Event CSV logging enabled: %.180s",events.log_path);
                else snprintf(message,sizeof(message),"Event CSV logging disabled");
            } else if (key == 't' || key == 'T') {
                if(trace.active) {
                    trace_stop(&trace);
                    snprintf(message,sizeof(message),"Kernel trace stopped");
                } else if(trace_start(&trace)==0) {
                    snprintf(message,sizeof(message),"Kernel trace started in isolated tracefs instance");
                } else {
                    snprintf(message,sizeof(message),"Kernel trace unavailable or could not start");
                }
            } else if (key == 'b' || key == 'B') {
                run_benchmark(&s, false);
                clock_gettime(CLOCK_MONOTONIC, &next);
                next_dpm = next;
                add_ns(&next_dpm, (long long)dpm_interval_us * 1000LL);
                next_clients=next; add_ns(&next_clients,CLIENT_SAMPLE_NS);
                next_power=next; add_ns(&next_power,POWER_SAMPLE_NS);
                next_session=next; add_ns(&next_session,SESSION_SAMPLE_NS);
                last_ui = next;
                stat_wall0 = next;
                clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stat_cpu0);
                stat_checks0 = st.checks;
                snprintf(message, sizeof(message), "Benchmark complete");
            } else if (key == 'q' || key == 'Q') {
                break;
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &loop_now);
        if (diff_ns(loop_now, stat_wall0) >= 1000000000LL) {
            struct timespec cpu1;
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu1);
            double wall = diff_ns(loop_now, stat_wall0) / 1e9;
            double cpu = diff_ns(cpu1, stat_cpu0) / 1e9;
            uint64_t dc = st.checks - stat_checks0;
            st.loop_cpu_pct = wall > 0 ? cpu / wall * 100.0 : 0.0;
            st.loop_hz = wall > 0 ? dc / wall : 0.0;
            stat_wall0 = loop_now;
            stat_cpu0 = cpu1;
            stat_checks0 = st.checks;
        }

        if (diff_ns(loop_now, last_ui) >= UI_REFRESH_NS) {
            bool ui_burst_active = adaptive && target.enabled && ts_ns(loop_now) < burst_until_ns;
            draw_ui(g, &s, &target, &st, interval_us, dpm_interval_us, monitor_clocks,
                    adaptive, ui_burst_active, active_interval_us,
                    &clients,&power,&session,&events,&trace,view,message);
            last_ui = loop_now;
        }

        long long step = (long long)active_interval_us * 1000LL;
        add_ns(&next, step);
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (diff_ns(next, now) > 0) {
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        } else {
            st.deadline_misses++;
            next = now;
        }
    }

    trace_stop(&trace);
    if(events.logf) fclose(events.logf);
    terminal_restore();
    g_term.active = false;
    sysfs_close(&s);
    return 0;
}
