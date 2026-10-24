/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Kevin Kolasa
 *
 * AMD GPU Performance and Diagnostic Tool
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <pwd.h>
#include <grp.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/time.h>

#include <curl/curl.h>

#include "gpu_specs.h"

#define VERSION "0.7.0b"
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

#define CLIENT_SAMPLE_NS 1000000000LL
#define POWER_SAMPLE_NS 100000000LL
#define SESSION_SAMPLE_NS 10000000000LL
#define SYSTEM_SAMPLE_NS 500000000LL
#define NOMINAL_BOARD_VOLTAGE 12.0
#define VOLTAGE_BAR_MAX_V 1.20
#define CURRENT_BAR_MAX_A 50.0
#define POWER_BAR_MAX_W 800.0
#define MAX_GPU_CLIENTS 128
#define MAX_DIAG_EVENTS 512
#define MAX_TRACE_LINE 512
#define MAX_CONNECTORS 12
#define MAX_GPU_TEMPS 3

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
    int fd_temp[MAX_GPU_TEMPS];
    int temp_count;
    long temp_crit_mc[MAX_GPU_TEMPS];
    long temp_emergency_mc[MAX_GPU_TEMPS];
    int fd_power;
    int fd_freq1;
    int fd_freq2;
    int fd_fan_rpm;
    int fd_pwm;
    int fd_pwm_w;
    int fd_pwm_max;
    int fd_voltage;
    int fd_current;
    int fd_vram_used;
    int fd_power_profile;
    int fd_pcie_bw;
    int fd_power_cap_r;
    int fd_power_cap_w;
    int fd_power_cap_min;
    int fd_power_cap_max;
    int fd_od_r;
    int fd_od_w;
    char policy_path[MAX_PATH_LEN];
    char sclk_path[MAX_PATH_LEN];
    char mclk_path[MAX_PATH_LEN];
    char busy_path[MAX_PATH_LEN];
    char mem_busy_path[MAX_PATH_LEN];
    char temp_path[MAX_GPU_TEMPS][MAX_PATH_LEN];
    char temp_label[MAX_GPU_TEMPS][32];
    char power_path[MAX_PATH_LEN];
    char power_cap_path[MAX_PATH_LEN];
    char power_cap_min_path[MAX_PATH_LEN];
    char power_cap_max_path[MAX_PATH_LEN];
    char od_path[MAX_PATH_LEN];
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
    char gpu_base[MAX_PATH_LEN];
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
    VIEW_TUNING,
    VIEW_DESKTOP,
    VIEW_EVENTS,
    VIEW_TRACE,
    VIEW_SETTINGS,
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

typedef struct {
    int gpu_busy;
    int mem_busy;
    double temp_c;
    double temps_c[MAX_GPU_TEMPS];
    int temp_count;
    double power_w;
    double power_cap_w;
    double power_cap_min_w;
    double power_cap_max_w;
    double voltage_v;
    double current_a;
    double equivalent_input_ohm;
    bool current_is_estimate;
    char current_label[32];
    int fan_rpm;
    double fan_pct;
    long long gfx_hz;
    long long mem_hz;
    long long gfx_hz_max;
    long long mem_hz_max;
    char freq1_label[32];
    char freq2_label[32];
    unsigned long long vram_used;
    bool vram_usage_valid;
    char power_profile[192];
    char pcie_bw[192];
    /* Hardware PCIe link state, read from the selected GPU PCI sysfs device. */
    char pcie_current_speed[48];
    char pcie_max_speed[48];
    int pcie_current_width;
    int pcie_max_width;
    bool pcie_link_valid;
    bool bar0_valid;
    uint64_t bar0_bytes;
    bool rebar_supported;  /* presence of resource0_resize: capability, not active size */
    int pcie_link_loaded_low;  /* -1 unknown, 0 OK/idle, 1 low under load */
    int amdgpu_dpm; /* -2 unknown, -1 firmware auto, 0 disabled, 1 enabled */
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

typedef enum {
    WORKFLOW_AI = 0,
    WORKFLOW_GAMING
} WorkflowMode;

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

typedef enum {
    TOKEN_SOURCE_HEURISTIC = 0,
    TOKEN_SOURCE_LLAMA,
    TOKEN_SOURCE_OLLAMA,
    TOKEN_SOURCE_VLLM,
    TOKEN_SOURCE_LMSTUDIO,
    TOKEN_SOURCE_MAYA, /* engine log, no API polling */
    TOKEN_SOURCE_COUNT
} TokenSource;

typedef struct {
    double tokens_per_sec;
    double prompt_tokens_per_sec;
    int64_t total_tokens;
    int64_t prompt_tokens;
    int64_t eval_tokens;
    bool has_data;
    bool is_processing;
    /* Prevent counters from one backend being reused by another. */
    bool source_initialized;
    TokenSource source_in_use;
    char source_label[32];
    /* Heuristic state */
    double power_history[100];
    int power_history_idx;
    double recent_peak_w;
    bool in_decode_phase;
    double decode_power_w;
    /* API state */
    struct timespec last_api_poll;
    int64_t prev_decoded;
    int64_t n_prompt_tokens;
    int64_t n_prompt_tokens_processed;
    int64_t n_prompt_tokens_cache;
} TokenRate;

/* Host-RAM prompt-state cache from llama.cpp/Bionic's --cache-ram.
 * These are NOT GPU VRAM, GPU KV-cache or system-RSS measurements.
 * Usage is reported only when the backend logs an actual cache-state summary. */
typedef struct {
    bool usage_valid;
    bool limit_valid;
    double used_mib;
    double limit_mib;   /* 0 = disabled; -1 = unlimited */
    int entries;
    int save_attempts;
    int matches;       /* f_keep matches; not proof that a load succeeded */
    int evictions;
    int rejected;
    int load_failures;
    int forced_reprocess;
    double evicted_mib;
    double last_rejected_mib;
} AiPromptCache;

typedef struct {
    double cpu_pct;
    unsigned cpu_threads;
    unsigned long long ram_total_bytes;
    unsigned long long ram_used_bytes;
    double ram_pct;
    unsigned long long prev_cpu_total;
    unsigned long long prev_cpu_idle;
    bool have_cpu_sample;
    int fd_cpu_temp;
    int fd_cpu_tctl;
    char cpu_tctl_path[MAX_PATH_LEN];
    char cpu_tctl_label[48];
    double cpu_tctl_c;
    char cpu_temp_path[MAX_PATH_LEN];
    char cpu_temp_label[32];
    double cpu_temp_c;
    long cpu_temp_crit_mc;
    bool cpu_temp_discovered;

    /* CPU per-CCX temps (k10temp) */
    int fd_cpu_ccx_temp[4];
    char cpu_ccx_temp_paths[4][MAX_PATH_LEN];
    char cpu_ccx_temp_labels[4][32];
    double cpu_ccx_temp_c[4];
    int cpu_ccx_temp_count;
    bool cpu_ccx_temp_discovered;

    /* CPU fan */
    int fd_cpu_fan_rpm;
    char cpu_fan_rpm_path[MAX_PATH_LEN];
    int cpu_fan_rpm;
    int fd_cpu_fan_pct;
    char cpu_fan_pct_path[MAX_PATH_LEN];
    double cpu_fan_pct;
    bool cpu_fan_discovered;

    /* CPU power */
    int fd_cpu_power;
    char cpu_power_path[MAX_PATH_LEN];
    double cpu_power_w;
    bool cpu_power_discovered;

    /* Motherboard temperatures */
    int mb_temp_count;
    int mb_temp_fds[4];
    char mb_temp_paths[4][MAX_PATH_LEN];
    char mb_temp_labels[4][32];
    double mb_temp_c[4];
    bool mb_temp_discovered;

    /* Top CPU processes */
    TopProcess top_procs[3];
    int top_proc_count;

    /* Token rate estimation */
    TokenRate tokens;
    TokenSource token_source;
    bool llama_available;
    bool ollama_available;
    bool vllm_available;
    bool lmstudio_available;

    /* Gaming mode metrics */
    CpuCoreLoad cpu_cores;
    double cpu_clock_min_mhz, cpu_clock_avg_mhz, cpu_clock_max_mhz;
    double cpu_clock_cap_mhz;
    int cpu_clock_count;
    struct timespec last_cpu_clock_scan;
    int cpu_boost; /* -1 unknown, 0 disabled, 1 enabled */
    char cpu_governor[32];
    int cpu_smt; /* -1 unknown, otherwise actual online SMT state */

    /* AI log events */
    char ai_log_path[MAX_PATH_LEN];
    char ai_log_source[32];
    int ai_log_event_count;
    time_t ai_log_last_update;
    time_t ai_log_last_detect;

    /* AI model info */
    char ai_model_name[128];
    char ai_model_type[32];
    int ai_ctx_size;
    char ai_cache_type_k[16];
    char ai_cache_type_v[16];
    bool ai_flash_attn;
    bool ai_mtp;
    AiPromptCache ai_prompt_cache;
    /* Prefer the currently running server's setting over old log lines. */
    bool ai_cache_process_limit_valid;
    double ai_cache_process_limit_mib;
    time_t ai_cache_process_last_check;
    bool ai_model_info_valid;
    char ai_model_activity[32];  /* "Idle", "Reading", "Thinking", "Outputting" */
} SystemTelemetry;

typedef struct {
    time_t timestamp;
    char time_str[16];
    double prompt_eval_ms;
    double prompt_tokens;
    double prompt_ms_per_token;
    double prompt_tps;
    double eval_ms;
    double eval_tokens;
    double eval_ms_per_token;
    double eval_tps;
    double total_ms;
    double total_tokens;
    int slot_id;
    int task_id;
    bool has_prompt;
    bool has_eval;
    bool has_total;
} AiLogEvent;

#define AI_LOG_MAX_EVENTS 12
static AiLogEvent g_ai_log_events[AI_LOG_MAX_EVENTS];

typedef enum {
    TUNE_ZONE_SAFE = 0,
    TUNE_ZONE_WARNING,
    TUNE_ZONE_DANGER,
    TUNE_ZONE_UNAVAILABLE
} TuneZone;

typedef struct {
    bool available;
    bool armed;
    bool has_sclk;
    bool sclk_max_only;
    bool has_sclk_offset;
    bool sclk_offset_legacy;
    bool has_mclk;
    bool mclk_max_only;
    bool has_vdd_offset;
    bool has_power_cap;
    long sclk_min_mhz;
    long sclk_max_mhz;
    long sclk_limit_min_mhz;
    long sclk_limit_max_mhz;
    long sclk_offset_mhz;
    long sclk_offset_min_mhz;
    long sclk_offset_max_mhz;
    long mclk_min_mhz;
    long mclk_max_mhz;
    long mclk_limit_min_mhz;
    long mclk_limit_max_mhz;
    long vdd_offset_mv;
    long vdd_offset_min_mv;
    long vdd_offset_max_mv;
    double power_cap_w;
    double power_cap_min_w;
    double power_cap_max_w;

    long default_sclk_min_mhz;
    long default_sclk_max_mhz;
    long default_sclk_offset_mhz;
    long default_mclk_min_mhz;
    long default_mclk_max_mhz;
    long default_vdd_offset_mv;
    double default_power_cap_w;

    long edit_sclk_min_mhz;
    long edit_sclk_max_mhz;
    long edit_sclk_offset_mhz;
    long edit_mclk_max_mhz;
    long edit_vdd_offset_mv;
    double edit_power_cap_w;

    bool has_fan;
    int fan_rpm;
    long fan_pwm;
    long fan_pwm_max;
    long edit_fan_pwm;

    bool initialized;
    const GpuSpec *spec;
} TuningState;

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
static WorkflowMode g_workflow = WORKFLOW_AI;
static TokenSource g_token_source = TOKEN_SOURCE_LLAMA;
static char g_ai_log_path[MAX_PATH_LEN] = {0};
/* Settings are persisted for the *invoking desktop user*, never /root with sudo. */
typedef enum { LOG_AUTO, LOG_LLAMA, LOG_BIONIC, LOG_OLLAMA, LOG_VLLM, LOG_LMSTUDIO, LOG_OFF, LOG_MAYA, LOG_COUNT } LogParser;
typedef enum { COMP_AUTO, COMP_X11, COMP_DRM, COMP_PROCESS, COMP_OFF, COMP_COUNT } CompositorSource;
typedef struct {
    TokenSource token_source;
    LogParser log_parser;
    CompositorSource compositor_source;
    bool ai_log_manual;
    char ai_log_file[MAX_PATH_LEN];
    int preferred_card;
    char ai_nvme_device[32]; /* selected NVMe namespace, e.g. nvme0n1 */
} AppSettings;
static AppSettings g_settings = {.token_source=TOKEN_SOURCE_LLAMA,.log_parser=LOG_AUTO,
                                 .compositor_source=COMP_AUTO,.preferred_card=-1};
static const char *const g_parser_names[]={"Auto (llama compatible)","llama.cpp","Bionic / llama.cpp",
    "Ollama (experimental)","vLLM (experimental)","LM Studio (experimental)","Disabled",
    "Project Maya (engine log)"};
/* Read-only aggregate block-device throughput. Linux /sys/block/DEVICE/stat counts
 * 512-byte sectors regardless of the drive's advertised logical sector size.
 * It includes ALL host I/O, not exclusively inference traffic. */
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
static NvmeMonitor g_nvme;

/* Project Maya writes maya-<model>.log beside its model's .json config.
 * Engine lines vary by version and backend: raw provenance is retained.
 * Speeds are populated only from explicit known 'tokens in milliseconds'
 * prefill entries, never inferred from unrelated statistic lines. */
typedef struct {
    char recent[3][160];
    int count;
    unsigned long recognized;
    bool prefill_valid;
    double prefill_tps;
    long prompt_tokens;
    bool decode_valid;
    double decode_tps, decode_ms_per_tok;
    bool vram_hit_valid, disk_valid, ram_fetch_valid;
    double vram_hit_pct, disk_reads_per_token, disk_wait_ms_per_token;
    double ram_fetches_per_token;
} MayaLogMonitor;
static MayaLogMonitor g_maya;

static const char *const g_compositor_names[]={"Auto", "X11 selection", "DRM client", "Process scan", "Disabled"};
static int g_requested_gpu=-1;
static bool g_gpu_switch_requested=false;
static Gpu g_gpu_inventory[16];
static int g_gpu_inventory_count=0;
static bool g_edit_ai_path=false;
static char g_edit_path_buffer[MAX_PATH_LEN]={0};


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

/* HTTP GET using libcurl */
/* Bound local API responses: a runaway /metrics endpoint must not grow this
 * root-owned GUI indefinitely. CURL_WRITEFUNC_ERROR is signalled by 0 bytes. */
#define HTTP_MAX_RESPONSE (1024u * 1024u)
/* Do not download 1 MiB just to reject it for a 4 KiB caller buffer.
 * The bounded writer fails the transfer immediately at the usable size. */
static size_t http_response_limit(size_t caller_capacity) {
    if (caller_capacity < 2) return 0;
    return caller_capacity - 1 < HTTP_MAX_RESPONSE ? caller_capacity - 1 : HTTP_MAX_RESPONSE;
}
struct http_buf {
    char *data;
    size_t size;
    size_t capacity;
    size_t limit;
};

static size_t http_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct http_buf *buf = userdata;
    if (!buf || (size && nmemb > SIZE_MAX / size)) return 0;
    size_t total = size * nmemb;
    if (!total) return 0;
    if (buf->size > buf->limit || total > buf->limit - buf->size) return 0;
    size_t required = buf->size + total + 1;
    if (required > buf->capacity) {
        size_t next_cap = buf->capacity ? buf->capacity : 1024;
        while (next_cap < required) {
            if (next_cap > (buf->limit + 1) / 2) { next_cap = buf->limit + 1; break; }
            next_cap *= 2;
        }
        char *next = realloc(buf->data, next_cap);
        if (!next) return 0;  /* Never lose ownership of the original allocation. */
        buf->data = next;
        buf->capacity = next_cap;
    }
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = 0;
    return total;
}

/* Configuration is intentionally simple, version tolerant and uses no shell execution. */
static const char *settings_user_home(void) {
#ifdef AMD_GPU_DIAG_TEST
    const char *override=getenv("AMD_GPU_DIAG_TEST_HOME");
    if (override && *override) return override;
#endif
    const char *su=getenv("SUDO_UID");
    if (geteuid()==0 && su && *su) {
        char *end=NULL; errno=0;
        unsigned long n=strtoul(su,&end,10);
        if (!errno && end && !*end && n<=UINT32_MAX) {
            struct passwd *pw=getpwuid((uid_t)n);
            if (pw && pw->pw_dir) return pw->pw_dir;
        }
    }
    struct passwd *pw=getpwuid(getuid());
    return pw && pw->pw_dir ? pw->pw_dir : getenv("HOME");
}
static bool settings_paths(char *folder, size_t fc, char *file, size_t pc) {
    const char *home=settings_user_home();
    if (!home || !*home || snprintf(folder,fc,"%s/.config/amd-gpu-diagnostic",home)>=(int)fc) return false;
    return snprintf(file,pc,"%s/settings.ini",folder)<(int)pc;
}
/* Reject malformed, negative, overflowed and fractional enum config values.
 * atoi() silently turns a corrupted config line into option zero. */
static bool settings_int_in_range(const char *value, int min, int max, int *out) {
    if (!value || !*value || isspace((unsigned char)*value)) return false;
    errno = 0;
    char *end = NULL;
    long v = strtol(value, &end, 10);
    if (errno || end == value || *end || v < min || v > max) return false;
    *out = (int)v;
    return true;
}

static void settings_load(void) {
    char folder[MAX_PATH_LEN],file[MAX_PATH_LEN];
    if (!settings_paths(folder,sizeof(folder),file,sizeof(file))) return;
    FILE *f=fopen(file,"r");
    if (!f) return;
    char line[1024];
    while(fgets(line,sizeof(line),f)) {
        trim(line);
        char *eq=strchr(line,'='); if(!eq) continue;
        *eq++=0;
        if(!strcmp(line,"token_source")) {
            int n; if(settings_int_in_range(eq,0,TOKEN_SOURCE_COUNT-1,&n)) g_settings.token_source=(TokenSource)n;
        } else if(!strcmp(line,"log_parser")) {
            int n; if(settings_int_in_range(eq,0,LOG_COUNT-1,&n)) g_settings.log_parser=(LogParser)n;
        } else if(!strcmp(line,"compositor_source")) {
            int n; if(settings_int_in_range(eq,0,COMP_COUNT-1,&n)) g_settings.compositor_source=(CompositorSource)n;
        } else if(!strcmp(line,"ai_log_manual")) g_settings.ai_log_manual=!strcmp(eq,"1");
        else if(!strcmp(line,"ai_log_file") && eq[0]=='/' && !strchr(eq,'\n') &&
                strlen(eq)<sizeof(g_settings.ai_log_file)) text_copy(g_settings.ai_log_file,sizeof(g_settings.ai_log_file),eq);
        else if(!strcmp(line,"ai_nvme_device") && strlen(eq)<sizeof(g_settings.ai_nvme_device)) {
            /* Store only a kernel-provided device basename, never an arbitrary path. */
            bool safe = !strncmp(eq,"nvme",4);
            for(const unsigned char *q=(const unsigned char*)eq; *q; ++q)
                if(!isalnum(*q)) safe=false;
            if(safe) text_copy(g_settings.ai_nvme_device,sizeof(g_settings.ai_nvme_device),eq);
        }
        else if(!strcmp(line,"preferred_card")) {
            int n; if(settings_int_in_range(eq,-1,255,&n)) g_settings.preferred_card=n;
        }
    }
    fclose(f);
    g_token_source=g_settings.token_source;
}
static bool settings_save(void) {
    char folder[MAX_PATH_LEN],file[MAX_PATH_LEN];
    if (!settings_paths(folder,sizeof(folder),file,sizeof(file))) return false;
    /* A root-run diagnostic must never leave root-owned configuration files in HOME.
     * Fork, drop privileges first, then create/write atomically as the desktop user. */
    pid_t pid=fork();
    if(pid<0) return false;
    if(pid==0) {
        const char *su=getenv("SUDO_UID"),*sg=getenv("SUDO_GID");
        if(geteuid()==0 && su && sg) {
            char *e1=NULL,*e2=NULL;
            unsigned long uid=strtoul(su,&e1,10),gid=strtoul(sg,&e2,10);
            if(!e1||*e1||!e2||*e2||uid>UINT32_MAX||gid>UINT32_MAX ||
               setgroups(0,NULL)!=0 || setgid((gid_t)gid)!=0 || setuid((uid_t)uid)!=0) _exit(2);
        }
        char config[MAX_PATH_LEN];
        const char *home=settings_user_home();
        /* After privilege drop, resolve the user's own HOME again. */
        if(!home || snprintf(config,sizeof(config),"%s/.config",home)>=(int)sizeof(config)) _exit(2);
        if(mkdir(config,0700)!=0 && errno!=EEXIST) _exit(2);
        if(mkdir(folder,0700)!=0 && errno!=EEXIST) _exit(2);
        char tmp[MAX_PATH_LEN];
        if(snprintf(tmp,sizeof(tmp),"%s/.settings-XXXXXX",folder)>=(int)sizeof(tmp)) _exit(2);
        int fd=mkstemp(tmp);
        if(fd<0) _exit(2);
        (void)fchmod(fd,0600);
        FILE *f=fdopen(fd,"w");
        if(!f) {close(fd);unlink(tmp);_exit(2);}
        fprintf(f,"# amd-gpu-diagnostic v0.7 configuration\n");
        fprintf(f,"token_source=%d\nlog_parser=%d\ncompositor_source=%d\n",(int)g_settings.token_source,
                (int)g_settings.log_parser,(int)g_settings.compositor_source);
        fprintf(f,"ai_log_manual=%d\nai_log_file=%s\npreferred_card=%d\nai_nvme_device=%s\n",g_settings.ai_log_manual?1:0,
                g_settings.ai_log_file,g_settings.preferred_card,g_settings.ai_nvme_device);
        int ok=ferror(f)==0 && fflush(f)==0 && fsync(fd)==0;
        if(fclose(f)!=0) ok=0;
        if(ok) ok=rename(tmp,file)==0;
        if(!ok) unlink(tmp);
        _exit(ok?0:2);
    }
    int status=0;
    return waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0;
}

/* Reusable CURL handle to avoid init/cleanup overhead on every poll */
static CURL *g_curl = NULL;

static CURL *get_curl_handle(void) {
    if (!g_curl) {
        g_curl = curl_easy_init();
    }
    return g_curl;
}

static void cleanup_curl_handle(void) {
    if (g_curl) {
        curl_easy_cleanup(g_curl);
        g_curl = NULL;
    }
}

/* The shared CURL handle must never retain a header-list pointer between calls.
 * Treat a truncated response as a failure rather than parsing half a JSON value. */
static int http_get_auth(const char *url, char *out, size_t cap, int timeout_ms, const char *bearer_token) {
    if (!url || !out || cap < 2) return -1;
    out[0] = '\0';
    CURL *curl = get_curl_handle();
    if (!curl) return -1;
    struct http_buf buf = { .data = malloc(1024), .size = 0,
                            .capacity = 1024, .limit = http_response_limit(cap) };
    if (!buf.data) return -1;
    struct curl_slist *headers = NULL;
    if (bearer_token && bearer_token[0]) {
        char auth_header[512];
        int n = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", bearer_token);
        if (n < 0 || (size_t)n >= sizeof(auth_header) ||
            !(headers = curl_slist_append(NULL, auth_header))) { free(buf.data); return -1; }
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*"); /* loopback APIs, never proxy */
    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    int ok = rc == CURLE_OK && http_code == 200 && buf.size > 0 && buf.size < cap;
    if (ok) memcpy(out, buf.data, buf.size + 1);
    /* Clear dangling pointers before freeing the per-request header list. */
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_slist_free_all(headers);
    free(buf.data);
    return ok ? 0 : -1;
}

static int http_get(const char *url, char *out, size_t cap, int timeout_ms) {
    return http_get_auth(url, out, cap, timeout_ms, NULL);
}

/* Check if a TCP port is listening */
static int check_port(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port) };
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
    return rc == 0;
}

/* Discover llama-server once per 15 seconds from the kernel process table.
 * This replaces four fork/ps/grep/awk subprocesses per second in AI mode.
 * argv is NUL-delimited, so filenames with whitespace remain intact.
 * Never log argv: it can contain an API key or private model pathname. */
typedef struct {
    bool found;
    int port;
    int context_size;
    char api_key[256];
    char model_name[128];
    char cache_k[16];
    char cache_v[16];
    bool flash_attn;
    bool mtp;
} LlamaProcessInfo;

static LlamaProcessInfo g_llama_process;
static time_t g_llama_last_scan = 0;

static bool llama_server_executable(const char *argv0) {
    const char *name = strrchr(argv0, '/');
    name = name ? name + 1 : argv0;
    return strstr(name, "llama-server") != NULL || strstr(name, "llama_server") != NULL;
}

/* Testable, bounded parser for a single /proc/PID/cmdline buffer. */
static bool llama_parse_cmdline(const char *argv, size_t used, LlamaProcessInfo *out) {
    if (!argv || !used || !out) return false;
    size_t exe_len = strnlen(argv, used);
    if (exe_len == used || !llama_server_executable(argv)) return false;
    LlamaProcessInfo info = { .found = true, .port = 8080 };
    for (size_t pos = exe_len + 1; pos < used;) {
        const char *arg = argv + pos;
        size_t len = strnlen(arg, used - pos);
        if (len == used - pos) break;
        pos += len + 1;
        if (!len) continue;
        const char *value = NULL;
        char option[64];
        const char *eq = strchr(arg, '=');
        if (eq && (size_t)(eq - arg) < sizeof(option)) {
            size_t n = (size_t)(eq - arg);
            memcpy(option, arg, n);
            option[n] = '\0';
            value = eq + 1;
        } else {
            if (len >= sizeof(option)) continue;
            memcpy(option, arg, len + 1);
            if (pos < used) {
                size_t next_len = strnlen(argv + pos, used - pos);
                if (next_len < used - pos && next_len && argv[pos] != '-') {
                    value = argv + pos;
                    pos += next_len + 1;
                }
            }
        }
        if (!value) continue;
        if (!strcmp(option, "--port")) {
            int port;
            if (settings_int_in_range(value, 1, 65535, &port)) info.port = port;
        } else if (!strcmp(option, "--api-key")) {
            if (strlen(value) < sizeof(info.api_key)) text_copy(info.api_key, sizeof(info.api_key), value);
        } else if (!strcmp(option, "--model") || !strcmp(option, "-m")) {
            const char *base = strrchr(value, '/');
            text_copy(info.model_name, sizeof(info.model_name), base ? base+1 : value);
        } else if (!strcmp(option, "--ctx-size") || !strcmp(option, "-c")) {
            int ctx;
            if (settings_int_in_range(value, 1, INT_MAX, &ctx)) info.context_size = ctx;
        } else if (!strcmp(option, "--cache-type-k")) {
            text_copy(info.cache_k, sizeof(info.cache_k), value);
        } else if (!strcmp(option, "--cache-type-v")) {
            text_copy(info.cache_v, sizeof(info.cache_v), value);
        } else if (!strcmp(option, "--flash-attn")) {
            info.flash_attn = !strcmp(value, "on") || !strcmp(value, "1");
        } else if (!strcmp(option, "--spec-type")) {
            info.mtp = strstr(value, "mtp") != NULL;
        }
    }
    *out = info;
    return true;
}

static void refresh_llama_process(void) {
    time_t now = time(NULL);
    if (g_llama_last_scan && now >= g_llama_last_scan && now - g_llama_last_scan < 15) return;
    g_llama_last_scan = now;
    memset(&g_llama_process, 0, sizeof(g_llama_process));
    DIR *dir = opendir("/proc");
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        char path[96];
        int n = snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        char args[8192];
        size_t used = fread(args, 1, sizeof(args) - 1, f);
        fclose(f);
        args[used] = '\0';
        if (llama_parse_cmdline(args, used, &g_llama_process)) break;
    }
    closedir(dir);
}

static int find_llama_port(void) {
    refresh_llama_process();
    return g_llama_process.found ? g_llama_process.port : 8080;
}

static void find_llama_api_key(char *out, size_t cap) {
    refresh_llama_process();
    text_copy(out, cap, g_llama_process.api_key);
}

/* Detect available AI servers */
static void detect_ai_servers(SystemTelemetry *sys) {
    sys->llama_available = check_port(find_llama_port());
    sys->ollama_available = check_port(11434);
    sys->vllm_available = check_port(8000);
    /* Check for LM Studio/Bionic via lms CLI or HTTP */
    sys->lmstudio_available = check_port(1234);
}

/* Heuristic token rate estimation from GPU power */
static void estimate_tokens_heuristic(SystemTelemetry *sys, double gpu_power_w) {
    TokenRate *t = &sys->tokens;

    /* Store power in history */
    t->power_history[t->power_history_idx] = gpu_power_w;
    t->power_history_idx = (t->power_history_idx + 1) % 100;

    /* Calculate recent peak (last 2 seconds = 20 samples at 10Hz) */
    double peak = 0;
    for (int i = 0; i < 20; i++) {
        int idx = (t->power_history_idx - i + 100) % 100;
        if (t->power_history[idx] > peak) peak = t->power_history[idx];
    }
    t->recent_peak_w = peak;

    /* Detect decode phase: power is 40-80% of recent peak and steady */
    double ratio = (peak > 0) ? gpu_power_w / peak : 0;
    if (ratio >= 0.4 && ratio <= 0.8 && peak > 50) {
        t->in_decode_phase = true;
        t->decode_power_w = gpu_power_w;
    } else if (ratio > 0.85) {
        t->in_decode_phase = false;
    }

    if (t->in_decode_phase) {
        /* Estimate tokens/sec based on power
         * Typical decode: ~30-50W per token/sec for modern GPUs
         * This is a rough heuristic */
        double decode_w = gpu_power_w;
        double base_w = peak * 0.3;  /* Base power when idle */
        double compute_w = decode_w - base_w;
        t->tokens_per_sec = compute_w / 40.0;  /* ~40W per tok/s */
        if (t->tokens_per_sec < 1) t->tokens_per_sec = 1;
        if (t->tokens_per_sec > 200) t->tokens_per_sec = 200;
        t->has_data = true;
    } else {
        t->tokens_per_sec = 0;
        t->has_data = false;
    }
}

/* Poll llama.cpp API for token stats */
static void poll_llama_tokens(SystemTelemetry *sys) {
    TokenRate *t = &sys->tokens;
    int port = find_llama_port();
    char api_key[256];
    find_llama_api_key(api_key, sizeof(api_key));
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/slots", port);
    char resp[4096];
    if (http_get_auth(url, resp, sizeof(resp), 500, api_key) != 0) {
        t->has_data = false;
        t->is_processing = false;
        t->tokens_per_sec = 0;
        t->prev_decoded = 0;
        t->last_api_poll = (struct timespec){0};
        text_copy(sys->ai_model_activity, sizeof(sys->ai_model_activity), "API unavailable");
        return;
    }
    /* Parse JSON - look for n_decoded in next_token */
    /* Response format: [{"id":0,...,"is_processing":true/false,...,"next_token":[{"n_decoded":N,...}]}] */
    char *p = strstr(resp, "\"n_decoded\"");
    int64_t n_decoded = 0;
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            n_decoded = strtoll(p, NULL, 10);
        }
    }
    if (!p || !strstr(resp, "\"n_decoded\"")) {
        t->has_data = false;
        t->is_processing = false;
        t->tokens_per_sec = 0;
        return;
    }
    /* Parse is_processing */
    p = strstr(resp, "\"is_processing\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            t->is_processing = (*p == 't');
        }
    }

    /* Parse n_prompt_tokens */
    p = strstr(resp, "\"n_prompt_tokens\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            t->n_prompt_tokens = strtoll(p, NULL, 10);
        }
    }

    /* Parse n_prompt_tokens_processed */
    p = strstr(resp, "\"n_prompt_tokens_processed\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            t->n_prompt_tokens_processed = strtoll(p, NULL, 10);
        }
    }

    /* Parse n_prompt_tokens_cache */
    p = strstr(resp, "\"n_prompt_tokens_cache\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            t->n_prompt_tokens_cache = strtoll(p, NULL, 10);
        }
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (t->last_api_poll.tv_sec > 0 || t->last_api_poll.tv_nsec > 0) {
        double dt = (double)(now.tv_sec - t->last_api_poll.tv_sec) + (double)(now.tv_nsec - t->last_api_poll.tv_nsec) / 1e9;
        double delta = (double)(n_decoded - t->prev_decoded);
        if (dt > 0.0) {
            t->tokens_per_sec = delta / dt;
            if (t->tokens_per_sec < 0) t->tokens_per_sec = 0;
            t->has_data = true;
        }
    }

    /* Determine activity state from /slots data */
    if (!t->is_processing) {
        snprintf(sys->ai_model_activity, sizeof(sys->ai_model_activity), "Idle");
    } else if (t->n_prompt_tokens_processed < t->n_prompt_tokens) {
        snprintf(sys->ai_model_activity, sizeof(sys->ai_model_activity), "Reading");
    } else if (n_decoded != t->prev_decoded) {
        snprintf(sys->ai_model_activity, sizeof(sys->ai_model_activity), "Outputting");
    } else {
        snprintf(sys->ai_model_activity, sizeof(sys->ai_model_activity), "Thinking");
    }

    t->prev_decoded = n_decoded;
    t->last_api_poll = now;
    t->total_tokens = n_decoded;
}

/* Poll Ollama API for token stats */
static void poll_ollama_tokens(SystemTelemetry *sys) {
    TokenRate *t = &sys->tokens;
    char resp[4096];
    if (http_get("http://127.0.0.1:11434/api/ps", resp, sizeof(resp), 500) != 0) {
        t->has_data = false;
        t->tokens_per_sec = 0;
        return;
    }
    /* Ollama /api/ps returns loaded models but not token rates
     * We need to use /api/generate or track during generation
     * For now, just mark as unavailable for real-time stats */
    t->has_data = false;
}

/* Poll vLLM API for token stats */
static void poll_vllm_tokens(SystemTelemetry *sys) {
    TokenRate *t = &sys->tokens;
    char resp[8192];
    if (http_get("http://127.0.0.1:8000/metrics", resp, sizeof(resp), 500) != 0) {
        t->has_data = false;
        t->tokens_per_sec = 0;
        return;
    }
    /* Parse Prometheus metrics for vllm:generate_tokens_total or similar */
    /* Look for vllm:num_requests_running or throughput metrics */
    char *p = strstr(resp, "vllm:e2e_request_latency_seconds");
    if (!p) {
        /* Try alternative metric names */
        p = strstr(resp, "vllm:time_to_first_token_seconds");
    }
    if (p) {
        /* Found some vLLM metrics - estimate from request count */
        t->has_data = false;
        t->tokens_per_sec = 0; /* Request metrics alone cannot establish token rate. */
    } else {
        t->has_data = false;
    }
}

/* Poll LM Studio/Bionic for token stats using lms CLI or HTTP API */
static void poll_lmstudio_tokens(SystemTelemetry *sys) {
    TokenRate *t = &sys->tokens;

    /* Throttle lms CLI polling to every 2 seconds to avoid fork/exec overhead */
    static time_t last_lms_poll = 0;
    time_t now = time(NULL);
    if (now - last_lms_poll < 2) {
        /* Return cached state */
        return;
    }
    last_lms_poll = now;

    /* Try lms CLI first (Bionic) */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "lms ps --json 2>/dev/null | head -c 2048");
    FILE *f = popen(cmd, "r");
    if (f) {
        char buf[4096];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = 0;
        pclose(f);
        if (strstr(buf, "model") || strstr(buf, "models")) {
            t->has_data = false;
            t->tokens_per_sec = 0; /* No real-time token rate in process listing. */
            return;
        }
    }

    /* Fall back to HTTP API (classic LM Studio) */
    char resp[4096];
    if (http_get("http://127.0.0.1:1234/api/v0/models", resp, sizeof(resp), 500) != 0) {
        t->has_data = false;
        t->tokens_per_sec = 0;
        return;
    }
    if (strstr(resp, "[")) {
        t->has_data = false; /* Model availability is not token throughput. */
        t->tokens_per_sec = 0;
    } else {
        t->has_data = false;
    }
}

/* Backend counters and cached rates have no meaning after a source change.
 * This includes a previous /slots decode counter and heuristic power history. */
static void token_rate_prepare_source(SystemTelemetry *sys) {
    TokenRate *t = &sys->tokens;
    if (!t->source_initialized || t->source_in_use != sys->token_source) {
        memset(t, 0, sizeof(*t));
        t->source_initialized = true;
        t->source_in_use = sys->token_source;
        sys->ai_model_activity[0] = '\0';
    }
}

/* Main token rate sampling function */
static void sample_tokens(SystemTelemetry *sys, double gpu_power_w) {
    token_rate_prepare_source(sys);
    switch (sys->token_source) {
        case TOKEN_SOURCE_HEURISTIC:
            estimate_tokens_heuristic(sys, gpu_power_w);
            snprintf(sys->tokens.source_label, sizeof(sys->tokens.source_label), "Heuristic");
            break;
        case TOKEN_SOURCE_LLAMA:
            poll_llama_tokens(sys);
            snprintf(sys->tokens.source_label, sizeof(sys->tokens.source_label), "llama.cpp");
            break;
        case TOKEN_SOURCE_OLLAMA:
            poll_ollama_tokens(sys);
            snprintf(sys->tokens.source_label, sizeof(sys->tokens.source_label), "Ollama");
            break;
        case TOKEN_SOURCE_VLLM:
            poll_vllm_tokens(sys);
            snprintf(sys->tokens.source_label, sizeof(sys->tokens.source_label), "vLLM");
            break;
        case TOKEN_SOURCE_LMSTUDIO:
            poll_lmstudio_tokens(sys);
            snprintf(sys->tokens.source_label, sizeof(sys->tokens.source_label), "LM Studio");
            break;
        case TOKEN_SOURCE_MAYA:
            sys->tokens.has_data=false;sys->tokens.is_processing=false;
            snprintf(sys->tokens.source_label,sizeof(sys->tokens.source_label),"Project Maya log");
            break;
        default:
            estimate_tokens_heuristic(sys, gpu_power_w);
            break;
    }
}

/* Read model metadata from the same cached /proc command line used for API
 * discovery. Unknown information is left unavailable, never guessed. */
static void detect_ai_model_info(SystemTelemetry *sys) {
    refresh_llama_process();
    sys->ai_model_info_valid = g_llama_process.found;
    if (!g_llama_process.found) {
        sys->ai_model_name[0] = '\0';
        sys->ai_ctx_size = 0;
        sys->ai_cache_type_k[0] = '\0';
        sys->ai_cache_type_v[0] = '\0';
        sys->ai_flash_attn = false;
        sys->ai_mtp = false;
        return;
    }
    text_copy(sys->ai_model_name, sizeof(sys->ai_model_name), g_llama_process.model_name);
    if (strlen(sys->ai_model_name) > 45)
        memcpy(sys->ai_model_name+42, "...", 4);
    sys->ai_ctx_size = g_llama_process.context_size;
    text_copy(sys->ai_cache_type_k, sizeof(sys->ai_cache_type_k), g_llama_process.cache_k);
    text_copy(sys->ai_cache_type_v, sizeof(sys->ai_cache_type_v), g_llama_process.cache_v);
    sys->ai_flash_attn = g_llama_process.flash_attn;
    sys->ai_mtp = g_llama_process.mtp;
}

/* Select by modification time, not alphabetical filename, and ignore directories.
 * Bionic uses nested month directories; the latest file can be in any month. */
static void ai_log_consider(char *best, size_t cap, time_t *best_mtime, const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode) &&
        (!best[0] || st.st_mtime > *best_mtime ||
         (st.st_mtime == *best_mtime && strcmp(path, best) > 0))) {
        text_copy(best, cap, path);
        *best_mtime = st.st_mtime;
    }
}

/* NVMe namespaces only (nvme0n1 etc.); avoid partition counters and
 * double-counting by never summing namespace and partition traffic. */
static bool nvme_namespace_name(const char *name) {
    if(strncmp(name,"nvme",4)) return false;
    const char *p=name+4;
    if(!isdigit((unsigned char)*p)) return false;
    while(isdigit((unsigned char)*p)) ++p;
    if(*p++!='n' || !isdigit((unsigned char)*p)) return false;
    while(isdigit((unsigned char)*p)) ++p;
    return !*p;
}
static bool nvme_parse_stat(const char *line, uint64_t *rd, uint64_t *wr, uint64_t *busy) {
    unsigned long long a[11]={0};
    if(sscanf(line,"%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
              &a[0],&a[1],&a[2],&a[3],&a[4],&a[5],&a[6],&a[7],&a[8],&a[9],&a[10])!=11) return false;
    *rd=a[2]; *wr=a[6]; *busy=a[9];
    return true;
}
static void nvme_discover(NvmeMonitor *nv) {
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
static void nvme_select(NvmeMonitor *nv,const char *name) {
    if(strcmp(nv->selected,name)) {
        text_copy(nv->selected,sizeof(nv->selected),name);
        nv->have_prev=false; nv->valid=false;
        nv->read_mib_s=nv->write_mib_s=nv->busy_pct=0;
        nv->peak_read_mib_s=nv->peak_write_mib_s=0;
    }
}
static void nvme_sample(NvmeMonitor *nv) {
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

/* Dedicated Maya parser: no attempt to pass its records through the task/slot
 * timing grammar used by llama.cpp. Each log scan is a bounded tail. */
static void maya_parse_lines(const char *log, MayaLogMonitor *out) {
    memset(out,0,sizeof(*out));
    if(!log) return;
    for(const char *p=log;*p;) {
        const char *end=strchr(p,'\n');
        size_t len=end?(size_t)(end-p):strlen(p);
        if(len>0 && len<1024) {
            char line[1024];memcpy(line,p,len);line[len]=0;
            char *mark=NULL;
            const char *prefixes[]={"glm prefill:","glm fast:","glm stat:","glm split", "glm spec:",
                                    "ds4 prefill:","ds4 fast:","ds4 stat:","ds4 split"};
            for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);++i) {
                mark=strstr(line,prefixes[i]); if(mark) break;
            }
            if(mark) {
                out->recognized++;
                if(out->count<3) out->count++;
                for(int j=out->count-1;j>0;--j) text_copy(out->recent[j],sizeof(out->recent[j]),out->recent[j-1]);
                text_copy(out->recent[0],sizeof(out->recent[0]),mark);
                if(strstr(mark,"glm stat:") || strstr(mark,"ds4 stat:")) {
                    const char *dec=strstr(mark,"decode ");
                    double mspt=0,tps=0;
                    if(dec && sscanf(dec,"decode %lf ms/tok (%lf tok/s)",&mspt,&tps)==2 &&
                       isfinite(mspt) && isfinite(tps) && mspt>0 && tps>0) {
                        out->decode_valid=true;out->decode_ms_per_tok=mspt;out->decode_tps=tps;
                    }
                    const char *hit=strstr(mark,"vram hit ");
                    if(hit && sscanf(hit,"vram hit %lf%%",&out->vram_hit_pct)==1 &&
                       isfinite(out->vram_hit_pct) && out->vram_hit_pct>=0 && out->vram_hit_pct<=100)
                        out->vram_hit_valid=true;
                    const char *ram=strstr(mark,"ram fetches ");
                    if(ram && sscanf(ram,"ram fetches %lf/tok",&out->ram_fetches_per_token)==1 &&
                       isfinite(out->ram_fetches_per_token) && out->ram_fetches_per_token>=0)
                        out->ram_fetch_valid=true;
                    const char *disk=strstr(mark,"disk ");
                    double reads=0,waits=0,ms=0;
                    if(disk && sscanf(disk,"disk %lf reads/tok in %lf waits/tok, %lf ms/tok",
                                     &reads,&waits,&ms)==3 &&
                       isfinite(reads) && isfinite(ms) && reads>=0 && ms>=0) {
                        out->disk_valid=true;out->disk_reads_per_token=reads;out->disk_wait_ms_per_token=ms;
                    }
                }
                if(strstr(mark,"prefill:")) {
                    long tokens=0;double ms=0;
                    const char *v=strchr(mark,':');
                    if(v && sscanf(v+1," %ld tokens in %lf ms",&tokens,&ms)==2 && tokens>0 && ms>0) {
                        out->prefill_valid=true;out->prefill_tps=tokens*1000.0/ms;out->prompt_tokens=tokens;
                    }
                }
                /* Decoder speed records are not guaranteed to share a format
                 * across Maya models; display the original line unaltered. */
            }
        }
        if(!end) break;
        p=end+1;
    }
}

static void detect_ai_log(SystemTelemetry *sys) {
    if (g_settings.ai_log_manual) {
        text_copy(sys->ai_log_path,sizeof(sys->ai_log_path),g_settings.ai_log_file);
        text_copy(sys->ai_log_source,sizeof(sys->ai_log_source),"Manual");
        return;
    }
    const char *home = getenv("HOME");
    const char *sudo_user = getenv("SUDO_USER");
    if (sudo_user) {
        struct passwd *pw = getpwnam(sudo_user);
        if (pw && pw->pw_dir) home = pw->pw_dir;
    }
    sys->ai_log_path[0] = '\0';
    sys->ai_log_source[0] = '\0';
    if (!home) return;
    char root[MAX_PATH_LEN], month[MAX_PATH_LEN], candidate[MAX_PATH_LEN];
    char best[MAX_PATH_LEN] = "";
    time_t best_mtime = 0;
    if (snprintf(root, sizeof(root), "%s/.lmstudio/apps/bionic/server-logs", home) < (int)sizeof(root)) {
        DIR *months = opendir(root);
        if (months) {
            struct dirent *m;
            while ((m = readdir(months))) {
                if (m->d_name[0] == '.' || !path_join(month, sizeof(month), root, m->d_name)) continue;
                DIR *logs = opendir(month);
                if (!logs) continue;
                struct dirent *f;
                while ((f = readdir(logs))) {
                    if (f->d_name[0] == '.' || !path_join(candidate, sizeof(candidate), month, f->d_name)) continue;
                    ai_log_consider(best, sizeof(best), &best_mtime, candidate);
                }
                closedir(logs);
            }
            closedir(months);
        }
    }
    if (g_settings.log_parser == LOG_MAYA || (g_settings.log_parser==LOG_AUTO && !best[0])) {
        const char *roots[]={"src/project-maya","project-maya","Downloads/project-maya"};
        char maya_best[MAX_PATH_LEN]=""; time_t maya_mtime=0;
        for(size_t i=0;i<sizeof(roots)/sizeof(roots[0]);++i) {
            if(snprintf(root,sizeof(root),"%s/%s",home,roots[i])>=(int)sizeof(root)) continue;
            DIR *d=opendir(root);
            if(!d) continue;
            struct dirent *e;
            while((e=readdir(d))) {
                if(strncmp(e->d_name,"maya-",5)) continue;
                size_t l=strlen(e->d_name);
                if(l<9 || strcmp(e->d_name+l-4,".log")) continue;
                if(path_join(candidate,sizeof(candidate),root,e->d_name))
                    ai_log_consider(maya_best,sizeof(maya_best),&maya_mtime,candidate);
            }
            closedir(d);
        }
        if(maya_best[0]) {
            text_copy(sys->ai_log_path,sizeof(sys->ai_log_path),maya_best);
            text_copy(sys->ai_log_source,sizeof(sys->ai_log_source),"Project Maya");
            return;
        }
        if(g_settings.log_parser==LOG_MAYA) return;
    }
    if (best[0]) {
        text_copy(sys->ai_log_path, sizeof(sys->ai_log_path), best);
        text_copy(sys->ai_log_source, sizeof(sys->ai_log_source), "Bionic");
        return;
    }
    if (snprintf(candidate, sizeof(candidate), "%s/.ollama/logs/server.log", home) < (int)sizeof(candidate) &&
        access(candidate, R_OK) == 0) {
        text_copy(sys->ai_log_path, sizeof(sys->ai_log_path), candidate);
        text_copy(sys->ai_log_source, sizeof(sys->ai_log_source), "Ollama");
    } else if (snprintf(candidate, sizeof(candidate), "%s/.cache/vllm/log.txt", home) < (int)sizeof(candidate) &&
               access(candidate, R_OK) == 0) {
        text_copy(sys->ai_log_path, sizeof(sys->ai_log_path), candidate);
        text_copy(sys->ai_log_source, sizeof(sys->ai_log_source), "vLLM");
    }
}

static void ai_log_timing_values(const char *p, double *ms, double *tokens,
                                 double *mspt, double *tps) {
    *ms = *tokens = *mspt = *tps = 0.0;
    const char *eq = strchr(p, '=');
    if (!eq) return;
    char *after;
    *ms = strtod(eq + 1, &after);
    const char *slash = strchr(after, '/');
    if (slash) *tokens = strtod(slash + 1, &after);
    const char *paren = strchr(after, '(');
    if (paren) {
        *mspt = strtod(paren + 1, &after);
        const char *comma = strchr(after, ',');
        if (comma) *tps = strtod(comma + 1, NULL);
    }
    if (*ms > 0 && *tokens > 0) {
        if (*mspt <= 0) *mspt = *ms / *tokens;
        if (*tps <= 0) *tps = 1000.0 * *tokens / *ms;
    }
}

/* Parse ALL timings in a bounded tail and merge prompt/eval/total lines by
 * (slot, task). Keep the most recently updated tasks, never the first N lines. */
static int ai_log_parse_lines(char *buf, AiLogEvent *out, int max_events) {
    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strstr(line, "print_timing")) continue;
        const char *tp = strstr(line, "task ");
        const char *sp = strstr(line, "id ");
        if (!tp || !sp) continue;
        int task = atoi(tp + 5), slot = atoi(sp + 3);
        if (task < 0 || slot < 0) continue;
        const char *metric = NULL;
        int kind = 0;
        if ((metric = strstr(line, "prompt eval time"))) kind = 1;
        else if ((metric = strstr(line, "eval time"))) kind = 2;
        else if ((metric = strstr(line, "total time"))) kind = 3;
        if (!kind) continue;
        int idx = -1;
        for (int i = 0; i < count; i++)
            if (out[i].task_id == task && out[i].slot_id == slot) { idx = i; break; }
        AiLogEvent ev = {0};
        if (idx >= 0) {
            ev = out[idx];
            memmove(&out[idx], &out[idx+1], (size_t)(count - idx - 1) * sizeof(*out));
            count--;
        } else { ev.task_id = task; ev.slot_id = slot; }
        int yy=0,mm=0,dd=0,hh=0,mi=0,ss=0;
        if (sscanf(line, "[%d-%d-%d %d:%d:%d]", &yy, &mm, &dd, &hh, &mi, &ss) == 6)
            snprintf(ev.time_str, sizeof(ev.time_str), "%02d:%02d:%02d", hh, mi, ss);
        else if (!ev.time_str[0]) text_copy(ev.time_str, sizeof(ev.time_str), "--:--:--");
        double ms=0, tok=0, mspt=0, tps=0;
        ai_log_timing_values(metric, &ms, &tok, &mspt, &tps);
        if (kind == 1) {
            ev.prompt_eval_ms = ms; ev.prompt_tokens = tok;
            ev.prompt_ms_per_token = mspt; ev.prompt_tps = tps; ev.has_prompt = true;
        } else if (kind == 2) {
            ev.eval_ms = ms; ev.eval_tokens = tok;
            ev.eval_ms_per_token = mspt; ev.eval_tps = tps; ev.has_eval = true;
        } else {
            ev.total_ms = ms; ev.total_tokens = tok; ev.has_total = true;
        }
        if (!ev.has_total) {
            ev.total_ms = ev.prompt_eval_ms + ev.eval_ms;
            ev.total_tokens = ev.prompt_tokens + ev.eval_tokens;
        }
        if (count == max_events) {
            memmove(&out[0], &out[1], (size_t)(count - 1) * sizeof(*out));
            count--;
        }
        out[count++] = ev;
    }
    return count;
}

/* Return only a setting that is an actual number (in MiB), not a guessed
 * proportion of process RSS or GPU VRAM. -1 means unlimited, 0 disabled. */
static bool ai_cache_parse_limit(const char *value, double *limit_mib) {
    if (!value || !*value) return false;
    errno = 0;
    char *end = NULL;
    long number = strtol(value, &end, 10);
    if (errno || end == value || (number < -1) || number > 1048576L) return false;
    if (*end && !isspace((unsigned char)*end)) return false;
    *limit_mib = (double)number;
    return true;
}

/* Read a running llama-server's configured --cache-ram/LLAMA_ARG_CACHE_RAM.
 * Bionic launches llama-server separately, so inspecting our own environment
 * would normally be wrong. Sample infrequently (30 s); never modify /proc. */
static bool ai_cache_process_limit(double *limit_mib) {
    DIR *dir = opendir("/proc");
    if (!dir) return false;
    struct dirent *entry;
    bool found = false;
    while ((entry = readdir(dir)) != NULL && !found) {
        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        char path[96], argv[8192];
        int n = snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        size_t used = fread(argv, 1, sizeof(argv)-1, f);
        fclose(f);
        if (!used) continue;
        argv[used] = '\0';
        const char *exe = strrchr(argv, '/');
        exe = exe ? exe+1 : argv;
        if (!strstr(exe, "llama-server") && !strstr(exe, "llama_server")) continue;
        /* Command-line arguments are NUL-delimited. Check both syntaxes. */
        for (size_t i = strlen(argv)+1; i < used && !found;) {
            const char *a = argv+i;
            size_t remain = used-i;
            size_t len = strnlen(a, remain);
            if (len == remain) break;
            if (!strcmp(a, "--cache-ram") || !strcmp(a, "-cram")) {
                size_t j = i+len+1;
                if (j < used) found = ai_cache_parse_limit(argv+j, limit_mib);
            } else if (!strncmp(a, "--cache-ram=", 12)) {
                found = ai_cache_parse_limit(a+12, limit_mib);
            }
            i += len+1;
        }
        if (found) break;
        n = snprintf(path, sizeof(path), "/proc/%s/environ", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        f = fopen(path, "rb");
        if (!f) continue;
        char env[65536];
        used = fread(env, 1, sizeof(env)-1, f);
        fclose(f);
        env[used] = '\0';
        for (size_t i=0; i < used && !found;) {
            const char *a = env+i;
            size_t len = strnlen(a, used-i);
            if (len == used-i) break;
            if (!strncmp(a, "LLAMA_ARG_CACHE_RAM=", 20))
                found = ai_cache_parse_limit(a+20, limit_mib);
            i += len+1;
        }
    }
    closedir(dir);
    return found;
}

/* Scan the same bounded log tail as the timing parser. Unlike estimated RAM
 * figures, 'cache state' reports real cache occupancy and number of entries.
 * Counters reflect the *visible log tail*, not lifetime cumulative totals.
 * Keep this parser non-destructive; the timing parser reuses the same buffer. */
static void ai_cache_parse_lines(const char *buf, AiPromptCache *out) {
    memset(out, 0, sizeof(*out));
    if (!buf) return;
    for (const char *p=buf; *p;) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end-p) : strlen(p);
        if (len < 1024) {
            char line[1024];
            memcpy(line, p, len);
            line[len] = '\0';
            const char *mark;
            int entries;
            double used, limit;
            if ((mark = strstr(line, "cache state:")) &&
                sscanf(mark, "cache state: %d prompts, %lf MiB (limits: %lf MiB", &entries, &used, &limit) == 3 &&
                entries >= 0 && used >= 0 && limit >= -1) {
                out->usage_valid = true;
                out->limit_valid = true;
                out->entries = entries;
                out->used_mib = used;
                out->limit_mib = limit;
            } else if ((mark = strstr(line, "prompt cache is enabled, size limit:"))) {
                mark += strlen("prompt cache is enabled, size limit:");
                while (*mark == ' ') mark++;
                if (strstr(mark, "no limit")) {
                    out->limit_valid = true;
                    out->limit_mib = -1;
                } else if (sscanf(mark, "%lf MiB", &limit)==1 && limit >= 0) {
                    out->limit_valid = true;
                    out->limit_mib = limit;
                }
            } else if (strstr(line, "prompt cache is disabled")) {
                out->limit_valid = true;
                out->limit_mib = 0;
            }
            if (strstr(line, "saving prompt with length") && strstr(line, "total state size"))
                out->save_attempts++;
            if (strstr(line, "found better prompt with f_keep")) out->matches++;
            if ((mark = strstr(line, "making room for prompt cache entry, removing oldest entry"))) {
                out->evictions++;
                if ((mark = strstr(mark, "size = ")) && sscanf(mark, "size = %lf MiB", &used)==1 && used>=0)
                    out->evicted_mib += used;
            }
            if ((mark = strstr(line, "prompt state size")) && strstr(mark, "exceeds cache size limit")) {
                out->rejected++;
                if (sscanf(mark, "prompt state size %lf MiB exceeds cache size limit %lf MiB", &used, &limit)==2) {
                    out->last_rejected_mib = used;
                    if (limit>=0) { out->limit_valid = true; out->limit_mib = limit; }
                }
            }
            if (strstr(line, "failed to load prompt from cache")) out->load_failures++;
            if (strstr(line, "forcing full prompt re-processing due to lack of cache data"))
                out->forced_reprocess++;
        }
        if (!end) break;
        p=end+1;
    }
}

/* Keep parser configuration coherent without waiting for the 30-second log
 * discovery timer or leaving historical values visible when logging is off. */
static void parse_ai_log(SystemTelemetry *sys) {
    static LogParser last_parser = LOG_COUNT;
    static bool last_manual = false;
    static char last_filename[MAX_PATH_LEN] = "";
    if (last_parser != g_settings.log_parser ||
        last_manual != g_settings.ai_log_manual ||
        strcmp(last_filename, g_settings.ai_log_file) != 0) {
        last_parser = g_settings.log_parser;
        last_manual = g_settings.ai_log_manual;
        text_copy(last_filename, sizeof(last_filename), g_settings.ai_log_file);
        sys->ai_log_last_detect = 0;
        sys->ai_log_last_update = 0;
        sys->ai_log_path[0] = '\0';
        sys->ai_log_event_count = 0;
        memset(&sys->ai_prompt_cache, 0, sizeof(sys->ai_prompt_cache));
        memset(g_ai_log_events, 0, sizeof(g_ai_log_events));
        memset(&g_maya,0,sizeof(g_maya));
    }
    if (g_settings.log_parser == LOG_OFF) {
        sys->ai_log_event_count = 0;
        sys->ai_log_path[0] = '\0';
        g_ai_log_path[0] = '\0';
        memset(&sys->ai_prompt_cache, 0, sizeof(sys->ai_prompt_cache));
        memset(g_ai_log_events, 0, sizeof(g_ai_log_events));
        memset(&g_maya,0,sizeof(g_maya));
        return;
    }
    time_t now = time(NULL);
    if (!sys->ai_log_path[0] || now - sys->ai_log_last_detect >= 30) {
        detect_ai_log(sys);
        sys->ai_log_last_detect = now;
    }
    text_copy(g_ai_log_path, sizeof(g_ai_log_path), sys->ai_log_path);
    if (now - sys->ai_log_last_update < 2) return;
    sys->ai_log_last_update = now;
    sys->ai_log_event_count = 0;
    memset(&sys->ai_prompt_cache, 0, sizeof(sys->ai_prompt_cache));
    if (now - sys->ai_cache_process_last_check >= 30 || !sys->ai_cache_process_last_check) {
        sys->ai_cache_process_limit_valid = ai_cache_process_limit(&sys->ai_cache_process_limit_mib);
        sys->ai_cache_process_last_check = now;
    }
    if (sys->ai_cache_process_limit_valid) {
        sys->ai_prompt_cache.limit_valid = true;
        sys->ai_prompt_cache.limit_mib = sys->ai_cache_process_limit_mib;
    }
    if (!sys->ai_log_path[0]) {memset(&g_maya,0,sizeof(g_maya));return;}
    FILE *f = fopen(sys->ai_log_path, "r");
    if (!f) {memset(&g_maya,0,sizeof(g_maya));return;}
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || st.st_size <= 0) { fclose(f);memset(&g_maya,0,sizeof(g_maya));return; }
    enum { AI_TAIL_BYTES = 512 * 1024 };
    long offset = st.st_size > AI_TAIL_BYTES ? (long)(st.st_size - AI_TAIL_BYTES) : 0;
    if (fseek(f, offset, SEEK_SET) != 0) { fclose(f);memset(&g_maya,0,sizeof(g_maya));return; }
    char *buffer = malloc(AI_TAIL_BYTES + 1);
    if (!buffer) { fclose(f);memset(&g_maya,0,sizeof(g_maya));return; }
    size_t len = fread(buffer, 1, AI_TAIL_BYTES, f);
    fclose(f);
    buffer[len] = 0;
    char *start = buffer;
    if (offset && (start = strchr(buffer, '\n')) != NULL) start++;
    if (start) {
        memset(&g_maya,0,sizeof(g_maya));
        bool maya_mode = g_settings.log_parser==LOG_MAYA ||
                         (g_settings.log_parser==LOG_AUTO && !strcmp(sys->ai_log_source,"Project Maya"));
        if (maya_mode) {
            maya_parse_lines(start,&g_maya);
            if(g_settings.token_source==TOKEN_SOURCE_MAYA) {
                sys->tokens.has_data=false;sys->tokens.is_processing=false;
                sys->tokens.tokens_per_sec=0;
                sys->tokens.prompt_tokens_per_sec=g_maya.prefill_valid?g_maya.prefill_tps:0;
                if(now>=st.st_mtime && now-st.st_mtime<=8 && g_maya.count)
                    text_copy(sys->ai_model_activity,sizeof(sys->ai_model_activity),"Recent Maya log");
                else text_copy(sys->ai_model_activity,sizeof(sys->ai_model_activity),"Idle / no recent log");
            }
        }
        bool compatible = g_settings.log_parser==LOG_AUTO || g_settings.log_parser==LOG_LLAMA ||
                          g_settings.log_parser==LOG_BIONIC;
        if (compatible) ai_cache_parse_lines(start, &sys->ai_prompt_cache);
        AiLogEvent ordered[AI_LOG_MAX_EVENTS] = {{0}};
        int count = compatible ? ai_log_parse_lines(start, ordered, AI_LOG_MAX_EVENTS) : 0;
        sys->ai_log_event_count = count;
        for (int i = 0; i < count; i++) g_ai_log_events[i] = ordered[count - 1 - i];
        /* A finished timing line is history, not proof of a live inference.
         * Prefer live /slots processing state; otherwise classify fresh logs as recent. */
        if (!sys->tokens.is_processing) {
            const char *activity = "Idle";
            if (now >= st.st_mtime && now - st.st_mtime <= 8 && count > 0) {
                activity = g_ai_log_events[0].has_eval ? "Recent decoding" :
                           g_ai_log_events[0].has_prompt ? "Recent prompt" : "Recent task";
            }
            snprintf(sys->ai_model_activity, sizeof(sys->ai_model_activity), "%s", activity);
        }
    }
    free(buffer);
    if (sys->ai_cache_process_limit_valid) {
        /* A different limit in the log suggests old startup records or a
         * previous backend. Never combine those stale usages with the new
         * server's current limit. */
        if (sys->ai_prompt_cache.limit_valid &&
            fabs(sys->ai_prompt_cache.limit_mib - sys->ai_cache_process_limit_mib) > 0.1)
            memset(&sys->ai_prompt_cache, 0, sizeof(sys->ai_prompt_cache));
        sys->ai_prompt_cache.limit_valid = true;
        sys->ai_prompt_cache.limit_mib = sys->ai_cache_process_limit_mib;
    }
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
            snprintf(out, cap, "AMD %.150s", name);
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
                  &s->fd_mclk_r,&s->fd_mclk_w,&s->fd_busy,&s->fd_mem_busy,&s->fd_power,
                  &s->fd_freq1,&s->fd_freq2,&s->fd_fan_rpm,&s->fd_pwm,&s->fd_pwm_w,&s->fd_pwm_max,&s->fd_voltage,&s->fd_current,
                  &s->fd_vram_used,&s->fd_power_profile,&s->fd_pcie_bw,
                  &s->fd_power_cap_r,&s->fd_power_cap_w,&s->fd_power_cap_min,&s->fd_power_cap_max,
                  &s->fd_od_r,&s->fd_od_w};
    for (size_t i = 0; i < sizeof(fds)/sizeof(fds[0]); i++) {
        if (*fds[i] >= 0) close(*fds[i]);
        *fds[i] = -1;
    }
    for (int i = 0; i < MAX_GPU_TEMPS; i++) {
        if (s->fd_temp[i] >= 0) close(s->fd_temp[i]);
        s->fd_temp[i] = -1;
    }
}

static long read_optional_long_path(const char *path, long fallback) {
    char b[64];
    if (!path || !*path || read_path_text(path, b, sizeof(b)) < 0) return fallback;
    char *end = NULL;
    errno = 0;
    long v = strtol(b, &end, 10);
    if (errno || end == b) return fallback;
    return v;
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

        s->temp_count = 0;
        for (int ti = 1; ti <= MAX_GPU_TEMPS; ti++) {
            char leaf[32];
            snprintf(leaf, sizeof(leaf), "temp%d_input", ti);
            if (!path_join(p, sizeof(p), base, leaf) || !path_exists(p)) continue;
            int slot = ti - 1;
            text_copy(s->temp_path[slot], sizeof(s->temp_path[slot]), p);
            s->fd_temp[slot] = open(p, O_RDONLY | O_CLOEXEC);
            if (s->fd_temp[slot] < 0) continue;
            s->temp_count = ti;

            snprintf(leaf, sizeof(leaf), "temp%d_label", ti);
            if (path_join(p, sizeof(p), base, leaf) && read_path_text(p, txt, sizeof(txt)) >= 0 && txt[0])
                text_copy(s->temp_label[slot], sizeof(s->temp_label[slot]), txt);
            else
                snprintf(s->temp_label[slot], sizeof(s->temp_label[slot]), "GPU temp %d", ti);

            snprintf(leaf, sizeof(leaf), "temp%d_crit", ti);
            if (path_join(p, sizeof(p), base, leaf)) s->temp_crit_mc[slot] = read_optional_long_path(p, -1);
            snprintf(leaf, sizeof(leaf), "temp%d_emergency", ti);
            if (path_join(p, sizeof(p), base, leaf)) s->temp_emergency_mc[slot] = read_optional_long_path(p, -1);
        }

        if (path_join(p, sizeof(p), base, "power1_average") && path_exists(p)) {
            text_copy(s->power_path, sizeof(s->power_path), p);
            s->fd_power = open(p, O_RDONLY | O_CLOEXEC);
        } else if (path_join(p, sizeof(p), base, "power1_input") && path_exists(p)) {
            text_copy(s->power_path, sizeof(s->power_path), p);
            s->fd_power = open(p, O_RDONLY | O_CLOEXEC);
        }

        if (path_join(p, sizeof(p), base, "power1_cap") && path_exists(p)) {
            text_copy(s->power_cap_path, sizeof(s->power_cap_path), p);
            (void)open_rw_pair(p, &s->fd_power_cap_r, &s->fd_power_cap_w);
        }
        if (path_join(p, sizeof(p), base, "power1_cap_min") && path_exists(p)) {
            text_copy(s->power_cap_min_path, sizeof(s->power_cap_min_path), p);
            s->fd_power_cap_min = open(p, O_RDONLY | O_CLOEXEC);
        }
        if (path_join(p, sizeof(p), base, "power1_cap_max") && path_exists(p)) {
            text_copy(s->power_cap_max_path, sizeof(s->power_cap_max_path), p);
            s->fd_power_cap_max = open(p, O_RDONLY | O_CLOEXEC);
        }

        if (path_join(p, sizeof(p), base, "freq1_input") && path_exists(p)) {
            text_copy(s->freq1_path, sizeof(s->freq1_path), p);
            s->fd_freq1 = open(p, O_RDONLY | O_CLOEXEC);
            if (path_join(p, sizeof(p), base, "freq1_label") && read_path_text(p, txt, sizeof(txt)) >= 0)
                text_copy(s->freq1_label, sizeof(s->freq1_label), txt);
            else snprintf(s->freq1_label, sizeof(s->freq1_label), "freq1");
        }
        if (path_join(p, sizeof(p), base, "freq2_input") && path_exists(p)) {
            text_copy(s->freq2_path, sizeof(s->freq2_path), p);
            s->fd_freq2 = open(p, O_RDONLY | O_CLOEXEC);
            if (path_join(p, sizeof(p), base, "freq2_label") && read_path_text(p, txt, sizeof(txt)) >= 0)
                text_copy(s->freq2_label, sizeof(s->freq2_label), txt);
            else snprintf(s->freq2_label, sizeof(s->freq2_label), "freq2");
        }

        if (path_join(p, sizeof(p), base, "fan1_input") && path_exists(p)) {
            text_copy(s->fan_rpm_path, sizeof(s->fan_rpm_path), p);
            s->fd_fan_rpm = open(p, O_RDONLY | O_CLOEXEC);
        }
        if (path_join(p, sizeof(p), base, "pwm1") && path_exists(p)) {
            text_copy(s->pwm_path, sizeof(s->pwm_path), p);
            s->fd_pwm = open(p, O_RDONLY | O_CLOEXEC);
            s->fd_pwm_w = open(p, O_WRONLY | O_CLOEXEC);
        }
        if (path_join(p, sizeof(p), base, "pwm1_max") && path_exists(p)) {
            text_copy(s->pwm_max_path, sizeof(s->pwm_max_path), p);
            s->fd_pwm_max = open(p, O_RDONLY | O_CLOEXEC);
        }

        /* Prefer a labelled GPU core rail when one is exposed. */
        char fallback_voltage[MAX_PATH_LEN] = "";
        char fallback_label[32] = "voltage";
        for (int vi = 0; vi < 16; vi++) {
            char inp[MAX_PATH_LEN], lab[MAX_PATH_LEN], lbl[64] = "", leaf[32];
            snprintf(leaf, sizeof(leaf), "in%d_input", vi);
            if (!path_join(inp, sizeof(inp), base, leaf) || !path_exists(inp)) continue;
            snprintf(leaf, sizeof(leaf), "in%d_label", vi);
            if (path_join(lab, sizeof(lab), base, leaf)) (void)read_path_text(lab, lbl, sizeof(lbl));
            if (!fallback_voltage[0]) {
                text_copy(fallback_voltage, sizeof(fallback_voltage), inp);
                if (lbl[0]) text_copy(fallback_label, sizeof(fallback_label), lbl);
            }
            char lower[64];
            size_t ll = strlen(lbl); if (ll >= sizeof(lower)) ll = sizeof(lower) - 1;
            for (size_t li = 0; li < ll; li++) lower[li] = (char)tolower((unsigned char)lbl[li]);
            lower[ll] = 0;
            if (strstr(lower, "vddgfx") || strstr(lower, "gfx") || strstr(lower, "vdd")) {
                text_copy(s->voltage_path, sizeof(s->voltage_path), inp);
                text_copy(s->voltage_label, sizeof(s->voltage_label), lbl[0] ? lbl : "VDDGFX");
                s->fd_voltage = open(inp, O_RDONLY | O_CLOEXEC);
                break;
            }
        }
        if (s->fd_voltage < 0 && fallback_voltage[0]) {
            text_copy(s->voltage_path, sizeof(s->voltage_path), fallback_voltage);
            text_copy(s->voltage_label, sizeof(s->voltage_label), fallback_label);
            s->fd_voltage = open(fallback_voltage, O_RDONLY | O_CLOEXEC);
        }

        /* Use real current when exposed; otherwise telemetry estimates board-side current. */
        char fallback_current[MAX_PATH_LEN] = "";
        char fallback_current_label[32] = "current";
        for (int ci = 1; ci <= 16; ci++) {
            char inp[MAX_PATH_LEN], lab[MAX_PATH_LEN], lbl[64] = "", leaf[32];
            snprintf(leaf, sizeof(leaf), "curr%d_input", ci);
            if (!path_join(inp, sizeof(inp), base, leaf) || !path_exists(inp)) continue;
            snprintf(leaf, sizeof(leaf), "curr%d_label", ci);
            if (path_join(lab, sizeof(lab), base, leaf)) (void)read_path_text(lab, lbl, sizeof(lbl));
            if (!fallback_current[0]) {
                text_copy(fallback_current, sizeof(fallback_current), inp);
                if (lbl[0]) text_copy(fallback_current_label, sizeof(fallback_current_label), lbl);
            }
            char lower[64];
            size_t ll = strlen(lbl); if (ll >= sizeof(lower)) ll = sizeof(lower) - 1;
            for (size_t li = 0; li < ll; li++) lower[li] = (char)tolower((unsigned char)lbl[li]);
            lower[ll] = 0;
            if (strstr(lower, "gpu") || strstr(lower, "gfx") || strstr(lower, "board") || strstr(lower, "total") || strstr(lower, "vdd")) {
                text_copy(s->current_path, sizeof(s->current_path), inp);
                text_copy(s->current_label, sizeof(s->current_label), lbl[0] ? lbl : "GPU current");
                s->fd_current = open(inp, O_RDONLY | O_CLOEXEC);
                break;
            }
        }
        if (s->fd_current < 0 && fallback_current[0]) {
            text_copy(s->current_path, sizeof(s->current_path), fallback_current);
            text_copy(s->current_label, sizeof(s->current_label), fallback_current_label);
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
    s->fd_power = s->fd_freq1 = s->fd_freq2 = -1;
    s->fd_fan_rpm = s->fd_pwm = s->fd_pwm_w = s->fd_pwm_max = s->fd_voltage = s->fd_current = -1;
    s->fd_vram_used = s->fd_power_profile = s->fd_pcie_bw = -1;
    s->fd_power_cap_r = s->fd_power_cap_w = s->fd_power_cap_min = s->fd_power_cap_max = -1;
    s->fd_od_r = s->fd_od_w = -1;
    for (int i = 0; i < MAX_GPU_TEMPS; i++) {
        s->fd_temp[i] = -1;
        s->temp_crit_mc[i] = -1;
        s->temp_emergency_mc[i] = -1;
    }

    text_copy(s->gpu_base, sizeof(s->gpu_base), g->base);
    if (!path_join(s->policy_path, sizeof(s->policy_path), g->base, "power_dpm_force_performance_level")) return -1;
    if (!path_join(s->sclk_path, sizeof(s->sclk_path), g->base, "pp_dpm_sclk")) return -1;
    if (!path_join(s->mclk_path, sizeof(s->mclk_path), g->base, "pp_dpm_mclk")) return -1;
    if (!path_join(s->busy_path, sizeof(s->busy_path), g->base, "gpu_busy_percent")) return -1;
    if (!path_join(s->mem_busy_path, sizeof(s->mem_busy_path), g->base, "mem_busy_percent")) return -1;
    if (!path_join(s->vram_used_path, sizeof(s->vram_used_path), g->base, "mem_info_vram_used")) return -1;
    if (!path_join(s->power_profile_path, sizeof(s->power_profile_path), g->base, "pp_power_profile_mode")) return -1;
    if (!path_join(s->pcie_bw_path, sizeof(s->pcie_bw_path), g->base, "pcie_bw")) return -1;
    if (!path_join(s->gpu_metrics_path, sizeof(s->gpu_metrics_path), g->base, "gpu_metrics")) return -1;
    if (!path_join(s->od_path, sizeof(s->od_path), g->base, "pp_od_clk_voltage")) return -1;

    if (open_rw_pair(s->policy_path, &s->fd_policy_r, &s->fd_policy_w) < 0) {
        sysfs_close(s);
        return -1;
    }
    if (path_exists(s->sclk_path)) (void)open_rw_pair(s->sclk_path, &s->fd_sclk_r, &s->fd_sclk_w);
    if (path_exists(s->mclk_path)) (void)open_rw_pair(s->mclk_path, &s->fd_mclk_r, &s->fd_mclk_w);
    if (path_exists(s->busy_path)) s->fd_busy = open(s->busy_path, O_RDONLY | O_CLOEXEC);
    if (path_exists(s->mem_busy_path)) s->fd_mem_busy = open(s->mem_busy_path, O_RDONLY | O_CLOEXEC);
    if (path_exists(s->vram_used_path)) s->fd_vram_used = open(s->vram_used_path, O_RDONLY | O_CLOEXEC);
    if (path_exists(s->power_profile_path)) s->fd_power_profile = open(s->power_profile_path, O_RDONLY | O_CLOEXEC);
    if (path_exists(s->pcie_bw_path)) s->fd_pcie_bw = open(s->pcie_bw_path, O_RDONLY | O_CLOEXEC);
    if (path_exists(s->od_path)) (void)open_rw_pair(s->od_path, &s->fd_od_r, &s->fd_od_w);
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

/* Parse max MHz from pp_dpm_sclk / pp_dpm_mclk files.
   Format: "s:  700Mhz *900Mhz 1100Mhz 1300Mhz" — highest number wins. */
static long parse_dpm_max_mhz(const char *path) {
    char buf[512];
    if (read_path_text(path, buf, sizeof(buf)) < 0) return -1;
    long max_mhz = -1;
    char *tok = strtok(buf, " \t\n");
    while (tok) {
        /* Skip tokens without digits (e.g. "s:", "*") */
        char *end = NULL;
        long val = strtol(tok, &end, 10);
        if (end != tok && *end == 0) {
            if (max_mhz < 0 || val > max_mhz) max_mhz = val;
        }
        tok = strtok(NULL, " \t\n");
    }
    return max_mhz;
}

/* PCIe negotiated link, GPU BAR0 aperture, and BAR resize capability.
 * resource0_resize advertises possible sizes, NOT the currently active BAR size.
 * The active aperture comes only from the first line of PCI sysfs `resource`. */
static void sample_pcie_link(const Sysfs *s, PowerTelemetry *p) {
    char path[MAX_PATH_LEN], value[128];
    p->pcie_link_valid = false;
    p->pcie_current_width = p->pcie_max_width = -1;
    p->pcie_current_speed[0] = p->pcie_max_speed[0] = 0;
    if (path_join(path, sizeof(path), s->gpu_base, "current_link_speed") &&
        read_path_text(path, value, sizeof(value)) >= 0)
        text_copy(p->pcie_current_speed, sizeof(p->pcie_current_speed), value);
    if (path_join(path, sizeof(path), s->gpu_base, "max_link_speed") &&
        read_path_text(path, value, sizeof(value)) >= 0)
        text_copy(p->pcie_max_speed, sizeof(p->pcie_max_speed), value);
    if (path_join(path, sizeof(path), s->gpu_base, "current_link_width"))
        p->pcie_current_width = (int)read_optional_long_path(path, -1);
    if (path_join(path, sizeof(path), s->gpu_base, "max_link_width"))
        p->pcie_max_width = (int)read_optional_long_path(path, -1);
    p->pcie_link_valid = p->pcie_current_width > 0 && p->pcie_current_speed[0] != 0;
    p->bar0_valid = false;
    p->bar0_bytes = 0;
    if (path_join(path, sizeof(path), s->gpu_base, "resource")) {
        FILE *f = fopen(path, "r");
        if (f) {
            unsigned long long begin=0, end=0, flags=0;
            if (fscanf(f, "%llx %llx %llx", &begin, &end, &flags) == 3 &&
                end >= begin && begin != 0 && (flags & 0x200ULL)) {
                p->bar0_bytes = end - begin + 1ULL;
                p->bar0_valid = true;
            }
            fclose(f);
        }
    }
    p->rebar_supported = path_join(path, sizeof(path), s->gpu_base, "resource0_resize") && path_exists(path);
    /* -1 means kernel auto: never show that as positively enabled. */
    long dpm=read_optional_long_path("/sys/module/amdgpu/parameters/dpm", -2);
    p->amdgpu_dpm = dpm >= -1 && dpm <= 1 ? (int)dpm : -2;
}

/* The PCIe link can downshift when idle. Only flag it as a possible bottleneck
 * if the GPU is actually under load, rather than treating power savings as bad. */
static int pcie_downshift_under_load(const PowerTelemetry *p) {
    if (!p->pcie_link_valid || !p->pcie_max_speed[0] || p->pcie_max_width <= 0 || p->gpu_busy < 0)
        return -1;
    if (p->gpu_busy < 40) return 0;
    double cur = strtod(p->pcie_current_speed, NULL);
    double max = strtod(p->pcie_max_speed, NULL);
    if (cur <= 0.0 || max <= 0.0) return -1;
    return (p->pcie_current_width < p->pcie_max_width || cur + 0.5 < max) ? 1 : 0;
}

static void sample_power(Sysfs *s, PowerTelemetry *p) {
    p->gpu_busy = (int)read_long_fd(s->fd_busy, -1);
    p->mem_busy = (int)read_long_fd(s->fd_mem_busy, -1);
    p->temp_count = s->temp_count;
    p->temp_c = -1.0;
    for (int i = 0; i < MAX_GPU_TEMPS; i++) {
        long tm = read_long_fd(s->fd_temp[i], -1);
        p->temps_c[i] = tm >= 0 ? tm / 1000.0 : -1.0;
        if (i == 0) p->temp_c = p->temps_c[i];
    }
    long pw = read_long_fd(s->fd_power, -1);
    p->power_w = pw >= 0 ? pw / 1000000.0 : -1.0;
    long cap = read_long_fd(s->fd_power_cap_r, -1);
    long cap_min = read_long_fd(s->fd_power_cap_min, -1);
    long cap_max = read_long_fd(s->fd_power_cap_max, -1);
    p->power_cap_w = cap >= 0 ? cap / 1000000.0 : -1.0;
    p->power_cap_min_w = cap_min >= 0 ? cap_min / 1000000.0 : -1.0;
    p->power_cap_max_w = cap_max >= 0 ? cap_max / 1000000.0 : -1.0;
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
    p->vram_usage_valid = vu >= 0;
    if (vu >= 0) p->vram_used = (unsigned long long)vu;
    /* These are useful for diagnosis but may involve more driver/SMU work.
       Sample them at ~1 Hz instead of putting them on the 10 Hz fast telemetry path. */
    if ((p->slow_tick++ % 10U) == 0U || !p->power_profile[0]) {
        parse_active_power_profile(s->fd_power_profile, p->power_profile, sizeof(p->power_profile));
        sample_pcie_link(s, p);
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

        /* Max clocks change only on DPM reconfiguration; sample slowly. */
        p->gfx_hz_max = parse_dpm_max_mhz(s->sclk_path);
        p->mem_hz_max = parse_dpm_max_mhz(s->mclk_path);
    }

    p->pcie_link_loaded_low = pcie_downshift_under_load(p);
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

static void discover_cpu_temp(SystemTelemetry *s) {
    if (s->cpu_temp_discovered) return;
    s->cpu_temp_discovered = true;
    s->fd_cpu_temp = -1;
    s->fd_cpu_tctl = -1;
    s->cpu_temp_crit_mc = -1;

    DIR *d = opendir("/sys/class/hwmon");
    if (!d) return;
    char best_path[MAX_PATH_LEN] = "";
    char best_label[32] = "";
    char best_crit[MAX_PATH_LEN] = "";
    char tctl_path[MAX_PATH_LEN] = "";
    char tctl_driver[32] = "";
    int tctl_score = -1;
    int best_score = -1;
    struct dirent *de;

    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0) continue;
        char base[MAX_PATH_LEN], namep[MAX_PATH_LEN], name[64] = "";
        if (!path_join(base, sizeof(base), "/sys/class/hwmon", de->d_name)) continue;
        if (!path_join(namep, sizeof(namep), base, "name")) continue;
        if (read_path_text(namep, name, sizeof(name)) < 0) continue;
        int chip_score = strcasestr(name, "k10temp") ? 100 : 0;
        if (!chip_score && !strcasestr(name, "zenpower")) continue;

        for (int ti = 1; ti <= 16; ti++) {
            char inp[MAX_PATH_LEN], labp[MAX_PATH_LEN], critp[MAX_PATH_LEN], label[64] = "", leaf[32];
            snprintf(leaf, sizeof(leaf), "temp%d_input", ti);
            if (!path_join(inp, sizeof(inp), base, leaf) || !path_exists(inp)) continue;
            snprintf(leaf, sizeof(leaf), "temp%d_label", ti);
            if (path_join(labp, sizeof(labp), base, leaf))
                (void)read_path_text(labp, label, sizeof(label));

            if (strcasestr(label, "tctl") && chip_score >= tctl_score) {
                text_copy(tctl_path, sizeof(tctl_path), inp);
                text_copy(tctl_driver, sizeof(tctl_driver), name);
                tctl_score = chip_score;
            }
            int score = chip_score;
            if (strcasestr(label, "tdie")) score += 40;
            else if (strcasestr(label, "tctl")) score += 30;
            else if (strcasestr(label, "package")) score += 20;
            else score += 5;

            if (score > best_score) {
                best_score = score;
                text_copy(best_path, sizeof(best_path), inp);
                text_copy(best_label, sizeof(best_label), label[0] ? label : name);
                snprintf(leaf, sizeof(leaf), "temp%d_crit", ti);
                if (path_join(critp, sizeof(critp), base, leaf) && path_exists(critp))
                    text_copy(best_crit, sizeof(best_crit), critp);
                else
                    best_crit[0] = '\0';
            }
        }
    }
    closedir(d);

    if (tctl_path[0]) {
        text_copy(s->cpu_tctl_path, sizeof(s->cpu_tctl_path), tctl_path);
        snprintf(s->cpu_tctl_label, sizeof(s->cpu_tctl_label), "CPU Tctl (%s)",
                 tctl_driver[0] ? tctl_driver : "hwmon");
        s->fd_cpu_tctl = open(tctl_path, O_RDONLY | O_CLOEXEC);
    }
    if (best_path[0]) {
        text_copy(s->cpu_temp_path, sizeof(s->cpu_temp_path), best_path);
        text_copy(s->cpu_temp_label, sizeof(s->cpu_temp_label), best_label[0] ? best_label : "CPU");
        s->fd_cpu_temp = open(best_path, O_RDONLY | O_CLOEXEC);
        if (best_crit[0]) s->cpu_temp_crit_mc = read_optional_long_path(best_crit, -1);
    }
}

static void discover_cpu_ccx_temps(SystemTelemetry *s) {
    if (s->cpu_ccx_temp_discovered) return;
    s->cpu_ccx_temp_discovered = true;
    s->cpu_ccx_temp_count = 0;

    DIR *d = opendir("/sys/class/hwmon");
    if (!d) return;
    struct dirent *de;

    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0) continue;
        char base[MAX_PATH_LEN], namep[MAX_PATH_LEN], name[64] = "";
        if (!path_join(base, sizeof(base), "/sys/class/hwmon", de->d_name)) continue;
        if (!path_join(namep, sizeof(namep), base, "name")) continue;
        if (read_path_text(namep, name, sizeof(name)) < 0) continue;
        if (!strcasestr(name, "k10temp")) continue;

        for (int ti = 1; ti <= 8 && s->cpu_ccx_temp_count < 4; ti++) {
            char inp[MAX_PATH_LEN], labp[MAX_PATH_LEN], label[64] = "", leaf[32];
            snprintf(leaf, sizeof(leaf), "temp%d_input", ti);
            if (!path_join(inp, sizeof(inp), base, leaf) || !path_exists(inp)) continue;
            snprintf(leaf, sizeof(leaf), "temp%d_label", ti);
            if (path_join(labp, sizeof(labp), base, leaf))
                (void)read_path_text(labp, label, sizeof(label));

            int idx = s->cpu_ccx_temp_count;
            s->fd_cpu_ccx_temp[idx] = open(inp, O_RDONLY | O_CLOEXEC);
            if (s->fd_cpu_ccx_temp[idx] < 0) continue;
            text_copy(s->cpu_ccx_temp_paths[idx], sizeof(s->cpu_ccx_temp_paths[idx]), inp);
            if (label[0])
                text_copy(s->cpu_ccx_temp_labels[idx], sizeof(s->cpu_ccx_temp_labels[idx]), label);
            else
                snprintf(s->cpu_ccx_temp_labels[idx], sizeof(s->cpu_ccx_temp_labels[idx]), "CCX%d", ti - 1);
            s->cpu_ccx_temp_count++;
        }
        if (s->cpu_ccx_temp_count > 0) break;
    }
    closedir(d);
}

static void discover_cpu_fan(SystemTelemetry *s) {
    if (s->cpu_fan_discovered) return;
    s->cpu_fan_discovered = true;
    s->fd_cpu_fan_rpm = -1;
    s->fd_cpu_fan_pct = -1;

    DIR *d = opendir("/sys/class/hwmon");
    if (!d) return;
    struct dirent *de;

    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0) continue;
        char base[MAX_PATH_LEN], namep[MAX_PATH_LEN], name[64] = "";
        if (!path_join(base, sizeof(base), "/sys/class/hwmon", de->d_name)) continue;
        if (!path_join(namep, sizeof(namep), base, "name")) continue;
        if (read_path_text(namep, name, sizeof(name)) < 0) continue;

        /* Look for fan1_input (RPM) */
        if (s->fd_cpu_fan_rpm < 0) {
            char fanp[MAX_PATH_LEN];
            if (path_join(fanp, sizeof(fanp), base, "fan1_input") && path_exists(fanp)) {
                s->fd_cpu_fan_rpm = open(fanp, O_RDONLY | O_CLOEXEC);
                if (s->fd_cpu_fan_rpm >= 0)
                    text_copy(s->cpu_fan_rpm_path, sizeof(s->cpu_fan_rpm_path), fanp);
            }
        }
        /* Look for pwm1 or fan1 for percentage */
        if (s->fd_cpu_fan_pct < 0) {
            char pwm[MAX_PATH_LEN];
            if (path_join(pwm, sizeof(pwm), base, "pwm1") && path_exists(pwm)) {
                s->fd_cpu_fan_pct = open(pwm, O_RDONLY | O_CLOEXEC);
                if (s->fd_cpu_fan_pct >= 0)
                    text_copy(s->cpu_fan_pct_path, sizeof(s->cpu_fan_pct_path), pwm);
            }
        }
        if (s->fd_cpu_fan_rpm >= 0 && s->fd_cpu_fan_pct >= 0) break;
    }
    closedir(d);
}

static void discover_cpu_power(SystemTelemetry *s) {
    if (s->cpu_power_discovered) return;
    s->cpu_power_discovered = true;
    s->fd_cpu_power = -1;

    DIR *d = opendir("/sys/class/hwmon");
    if (!d) return;
    struct dirent *de;

    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0) continue;
        char base[MAX_PATH_LEN], namep[MAX_PATH_LEN], name[64] = "";
        if (!path_join(base, sizeof(base), "/sys/class/hwmon", de->d_name)) continue;
        if (!path_join(namep, sizeof(namep), base, "name")) continue;
        if (read_path_text(namep, name, sizeof(name)) < 0) continue;

        /* Look for k10temp or zenpower power sensor */
        if (strcasestr(name, "k10temp") || strcasestr(name, "zenpower")) {
            char powp[MAX_PATH_LEN];
            if (path_join(powp, sizeof(powp), base, "power1_input") && path_exists(powp)) {
                s->fd_cpu_power = open(powp, O_RDONLY | O_CLOEXEC);
                if (s->fd_cpu_power >= 0)
                    text_copy(s->cpu_power_path, sizeof(s->cpu_power_path), powp);
                break;
            }
        }
    }
    closedir(d);
}

static void discover_motherboard_temps(SystemTelemetry *s) {
    if (s->mb_temp_discovered) return;
    s->mb_temp_discovered = true;
    s->mb_temp_count = 0;

    DIR *d = opendir("/sys/class/hwmon");
    if (!d) return;
    struct dirent *de;

    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0) continue;
        if (s->mb_temp_count >= 4) break;
        char base[MAX_PATH_LEN], namep[MAX_PATH_LEN], name[64] = "";
        if (!path_join(base, sizeof(base), "/sys/class/hwmon", de->d_name)) continue;
        if (!path_join(namep, sizeof(namep), base, "name")) continue;
        if (read_path_text(namep, name, sizeof(name)) < 0) continue;

        /* Skip CPU, GPU, NVMe, battery devices - only keep RAM/motherboard/VRM temps */
        if (strcasestr(name, "k10temp") || strcasestr(name, "zenpower") ||
            strcasestr(name, "amdgpu") || strcasestr(name, "nvme") ||
            strcasestr(name, "battery") || strcasestr(name, "hidpp") ||
            strcasestr(name, "asusec")) continue;

        /* Look for temp sensors that might be RAM or PCIe */
        for (int ti = 1; ti <= 8 && s->mb_temp_count < 4; ti++) {
            char inp[MAX_PATH_LEN], labp[MAX_PATH_LEN], label[64] = "", leaf[32];
            snprintf(leaf, sizeof(leaf), "temp%d_input", ti);
            if (!path_join(inp, sizeof(inp), base, leaf) || !path_exists(inp)) continue;
            if (path_join(labp, sizeof(labp), base, leaf)) {
                snprintf(leaf, sizeof(leaf), "temp%d_label", ti);
                if (path_join(labp, sizeof(labp), base, leaf))
                    (void)read_path_text(labp, label, sizeof(label));
            }

            /* Skip temps with "cpu" in the label */
            if (label[0] && strcasestr(label, "cpu")) continue;

            int idx = s->mb_temp_count;
            s->mb_temp_fds[idx] = open(inp, O_RDONLY | O_CLOEXEC);
            if (s->mb_temp_fds[idx] < 0) continue;
            text_copy(s->mb_temp_paths[idx], sizeof(s->mb_temp_paths[idx]), inp);
            if (label[0])
                text_copy(s->mb_temp_labels[idx], sizeof(s->mb_temp_labels[idx]), label);
            else
                snprintf(s->mb_temp_labels[idx], sizeof(s->mb_temp_labels[idx]), "%.18s T%u", name, (unsigned)ti);
            s->mb_temp_count++;
        }
    }
    closedir(d);
}

/* /proc/PID/stat contains '(comm)' which can include spaces and brackets.
 * Work from the LAST ')' and count Linux fields 14/15 and 22 exactly. */
static bool parse_proc_cpu_stat(const char *line, uint64_t *ticks, uint64_t *start_ticks) {
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
static void sample_top_processes(SystemTelemetry *s) {
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
    s->top_proc_count = top_count;
    memcpy(s->top_procs, top, (size_t)top_count * sizeof(top[0]));
}

/* Sample CPU per-core load from /proc/stat */
static void sample_cpu_cores(SystemTelemetry *s) {
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

        if (s->cpu_cores.have_sample && total >= s->cpu_cores.prev_core_total[idx] &&
            idle_all >= s->cpu_cores.prev_core_idle[idx]) {
            unsigned long long dt = total - s->cpu_cores.prev_core_total[idx];
            unsigned long long di = idle_all - s->cpu_cores.prev_core_idle[idx];
            s->cpu_cores.load_pct[idx] = (dt && di <= dt) ? (double)(dt - di) * 100.0 / (double)dt : 0.0;
        }

        s->cpu_cores.prev_core_total[idx] = total;
        s->cpu_cores.prev_core_idle[idx] = idle_all;
        if (idx + 1 > s->cpu_cores.core_count) {
            s->cpu_cores.core_count = idx + 1;
        }
    }
    fclose(f);

    if (!s->cpu_cores.have_sample) {
        s->cpu_cores.have_sample = true;
    }
}

static void sample_cpu_clocks(SystemTelemetry *s) {
    double min_mhz=0.0, max_mhz=0.0, total_mhz=0.0, cap_mhz=0.0;
    int count=0;
    char path[MAX_PATH_LEN], base[128], label[64];
    for (int core=0; core<MAX_CPU_CORES; core++) {
        snprintf(base, sizeof(base), "/sys/devices/system/cpu/cpu%d/cpufreq", core);
        if (!path_join(path, sizeof(path), base, "scaling_cur_freq")) continue;
        long khz=read_optional_long_path(path, -1);
        if (khz <= 0) {
            if (!path_join(path, sizeof(path), base, "cpuinfo_cur_freq")) continue;
            khz=read_optional_long_path(path, -1);
        }
        if (khz <= 0) continue;
        double mhz=khz / 1000.0;
        if (!count || mhz < min_mhz) min_mhz=mhz;
        if (mhz > max_mhz) max_mhz=mhz;
        total_mhz+=mhz;
        count++;
        if (core == 0) {
            if (path_join(path, sizeof(path), base, "scaling_governor") &&
                read_path_text(path, label, sizeof(label)) >= 0)
                text_copy(s->cpu_governor, sizeof(s->cpu_governor), label);
        }
        if (path_join(path, sizeof(path), base, "cpuinfo_max_freq")) {
            long maxkhz=read_optional_long_path(path, -1);
            if (maxkhz > 0 && maxkhz / 1000.0 > cap_mhz) cap_mhz=maxkhz / 1000.0;
        }
    }
    s->cpu_clock_count=count;
    s->cpu_clock_min_mhz=count ? min_mhz : -1.0;
    s->cpu_clock_avg_mhz=count ? total_mhz/count : -1.0;
    s->cpu_clock_max_mhz=count ? max_mhz : -1.0;
    s->cpu_clock_cap_mhz=cap_mhz;
    /* cpufreq boost flag may be absent on modern amd-pstate systems. */
    long boost=read_optional_long_path("/sys/devices/system/cpu/cpufreq/boost", -1);
    s->cpu_boost=boost < 0 ? -1 : !!boost;
    long smt=read_optional_long_path("/sys/devices/system/cpu/smt/active", -1);
    s->cpu_smt=smt < 0 ? -1 : !!smt;
}

/* Resolve a sensor sibling without changing the saved pwm path.  Reading via
 * read_optional_long_path closes its descriptor before returning. */
static long read_cpu_fan_pwm_max(const char *pwm_path) {
    if (!pwm_path || !*pwm_path) return 255;
    char dir[MAX_PATH_LEN], path[MAX_PATH_LEN];
    text_copy(dir, sizeof(dir), pwm_path);
    char *slash = strrchr(dir, '/');
    if (!slash) return 255;
    slash[1] = '\0';
    if (!path_join(path, sizeof(path), dir, "pwm1_max")) return 255;
    long value = read_optional_long_path(path, 255);
    return value > 0 ? value : 255;
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

    discover_cpu_temp(s);
    long cpu_mc = read_long_fd(s->fd_cpu_temp, -1);
    s->cpu_temp_c = cpu_mc >= 0 ? cpu_mc / 1000.0 : -1.0;
    long tctl_mc = read_long_fd(s->fd_cpu_tctl, -1);
    s->cpu_tctl_c = tctl_mc >= 0 ? tctl_mc / 1000.0 : -1.0;

    /* CPU CCX temps */
    discover_cpu_ccx_temps(s);
    for (int i = 0; i < s->cpu_ccx_temp_count; i++) {
        long mc = read_long_fd(s->fd_cpu_ccx_temp[i], -1);
        s->cpu_ccx_temp_c[i] = mc >= 0 ? mc / 1000.0 : -1.0;
    }

    /* CPU fan */
    discover_cpu_fan(s);
    if (s->fd_cpu_fan_rpm >= 0) {
        long rpm = read_long_fd(s->fd_cpu_fan_rpm, -1);
        s->cpu_fan_rpm = rpm >= 0 ? (int)rpm : 0;
    }
    if (s->fd_cpu_fan_pct >= 0) {
        long pwm = read_long_fd(s->fd_cpu_fan_pct, -1);
        long pwm_max = read_cpu_fan_pwm_max(s->cpu_fan_pct_path);
        s->cpu_fan_pct = pwm >= 0 && pwm_max > 0 ? (double)pwm * 100.0 / (double)pwm_max : 0.0;
    }

    /* CPU power: never show zero watts when the sensor does not exist. */
    s->cpu_power_w=-1.0;
    discover_cpu_power(s);
    if (s->fd_cpu_power >= 0) {
        long uw = read_long_fd(s->fd_cpu_power, -1);
        s->cpu_power_w = uw >= 0 ? uw / 1000000.0 : -1.0;
    }

    /* Motherboard temps */
    discover_motherboard_temps(s);
    for (int i = 0; i < s->mb_temp_count; i++) {
        long mc = read_long_fd(s->mb_temp_fds[i], -1);
        s->mb_temp_c[i] = mc >= 0 ? mc / 1000.0 : -1.0;
    }

    /* Top processes */
    sample_top_processes(s);

    /* Per-core load is inexpensive and remains at 500 ms. CPU frequency
     * collection probes many sysfs files; keep it to a 2-second cadence. */
    sample_cpu_cores(s);
    struct timespec clock_now;
    clock_gettime(CLOCK_MONOTONIC, &clock_now);
    if ((!s->last_cpu_clock_scan.tv_sec && !s->last_cpu_clock_scan.tv_nsec) ||
        diff_ns(clock_now, s->last_cpu_clock_scan) >= 2000000000LL) {
        sample_cpu_clocks(s);
        s->last_cpu_clock_scan = clock_now;
    }
}


static int extract_longs(const char *text, long *values, int max_values) {
    int n = 0;
    const char *p = text;
    while (p && *p && n < max_values) {
        while (*p && !isdigit((unsigned char)*p) && *p != '-' && *p != '+') p++;
        if (!*p) break;
        errno = 0;
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p) { p++; continue; }
        if (!errno) values[n++] = v;
        p = end;
    }
    return n;
}

static long clamp_long_value(long v, long lo, long hi) {
    if (lo <= hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
    }
    return v;
}

static double clamp_double_value(double v, double lo, double hi) {
    if (lo <= hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
    }
    return v;
}

static void tuning_refresh(Sysfs *s, TuningState *t, bool capture_defaults) {
    bool was_initialized = t->initialized;
    const GpuSpec *spec = t->spec;
    bool armed = t->armed;

    long old_def_smin = t->default_sclk_min_mhz;
    long old_def_smax = t->default_sclk_max_mhz;
    long old_def_soff = t->default_sclk_offset_mhz;
    long old_def_mmax = t->default_mclk_max_mhz;
    long old_def_voff = t->default_vdd_offset_mv;
    double old_def_pcap = t->default_power_cap_w;

    memset(t, 0, sizeof(*t));
    t->spec = spec;
    t->armed = armed;
    t->sclk_min_mhz = t->sclk_max_mhz = -1;
    t->sclk_limit_min_mhz = t->sclk_limit_max_mhz = -1;
    t->sclk_offset_mhz = t->sclk_offset_min_mhz = t->sclk_offset_max_mhz = 0;
    t->mclk_min_mhz = t->mclk_max_mhz = -1;
    t->mclk_limit_min_mhz = t->mclk_limit_max_mhz = -1;
    t->vdd_offset_mv = t->vdd_offset_min_mv = t->vdd_offset_max_mv = 0;
    t->power_cap_w = t->power_cap_min_w = t->power_cap_max_w = -1.0;

    int sclk_count = 0, mclk_count = 0;
    long sclk_values[16] = {0}, mclk_values[16] = {0};
    bool in_sclk = false, in_mclk = false, in_soff = false, in_voff = false, in_range = false;

    if (s->fd_od_r >= 0) {
        char buf[MAX_TEXT];
        if (read_fd_text(s->fd_od_r, buf, sizeof(buf)) >= 0) {
            char *save = NULL;
            for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
                trim(line);
                if (!*line) continue;
                if (strcmp(line, "OD_SCLK:") == 0) {
                    in_sclk = true; in_mclk = in_soff = in_voff = in_range = false; continue;
                }
                if (strcmp(line, "OD_MCLK:") == 0) {
                    in_mclk = true; in_sclk = in_soff = in_voff = in_range = false; continue;
                }
                if (strcmp(line, "OD_SCLK_OFFSET:") == 0) {
                    in_soff = true; in_sclk = in_mclk = in_voff = in_range = false; continue;
                }
                if (strcmp(line, "OD_VDDGFX_OFFSET:") == 0) {
                    in_voff = true; in_sclk = in_mclk = in_soff = in_range = false; continue;
                }
                if (strcmp(line, "OD_RANGE:") == 0) {
                    in_range = true; in_sclk = in_mclk = in_soff = in_voff = false; continue;
                }
                if (strncmp(line, "OD_", 3) == 0) {
                    in_sclk = in_mclk = in_soff = in_voff = in_range = false; continue;
                }

                long v[8] = {0};
                int n = extract_longs(line, v, 8);
                if (in_range) {
                    if (strncasecmp(line, "SCLK_OFFSET", 11) == 0 && n >= 2) {
                        t->sclk_offset_min_mhz = v[0];
                        t->sclk_offset_max_mhz = v[1];
                        t->has_sclk_offset = true;
                    } else if (strncasecmp(line, "SCLK", 4) == 0 && n >= 2) {
                        t->sclk_limit_min_mhz = v[0];
                        t->sclk_limit_max_mhz = v[1];
                    } else if (strncasecmp(line, "MCLK", 4) == 0 && n >= 2) {
                        t->mclk_limit_min_mhz = v[0];
                        t->mclk_limit_max_mhz = v[1];
                    } else if ((strncasecmp(line, "VDDGFX_OFFSET", 13) == 0 ||
                                strncasecmp(line, "VDDC_OFFSET", 11) == 0) && n >= 2) {
                        t->vdd_offset_min_mv = v[0];
                        t->vdd_offset_max_mv = v[1];
                        t->has_vdd_offset = true;
                    }
                } else if (in_sclk && n >= 2) {
                    if (sclk_count < (int)(sizeof(sclk_values)/sizeof(sclk_values[0])))
                        sclk_values[sclk_count++] = v[1];
                } else if (in_mclk && n >= 2) {
                    if (mclk_count < (int)(sizeof(mclk_values)/sizeof(mclk_values[0])))
                        mclk_values[mclk_count++] = v[1];
                } else if (in_soff && n >= 1) {
                    t->sclk_offset_mhz = v[n - 1];
                    t->has_sclk_offset = true;
                    if (strchr(line, ':')) t->sclk_offset_legacy = true;
                } else if (in_voff && n >= 1) {
                    t->vdd_offset_mv = v[n - 1];
                    t->has_vdd_offset = true;
                }
            }
        }
    }

    /* Vega20+ / RDNA min-max form has exactly two editable clock endpoints.
       Older multi-state voltage tables are deliberately left read-only here. */
    if (sclk_count == 2 && t->sclk_limit_min_mhz >= 0 && t->sclk_limit_max_mhz >= 0) {
        t->has_sclk = true;
        t->sclk_min_mhz = sclk_values[0];
        t->sclk_max_mhz = sclk_values[1];
    } else if (sclk_count == 1 && t->sclk_limit_min_mhz >= 0 && t->sclk_limit_max_mhz >= 0) {
        /* Navi/RDNA1 commonly exposes only editable state 1 (maximum). */
        t->has_sclk = true;
        t->sclk_max_only = true;
        t->sclk_min_mhz = -1;
        t->sclk_max_mhz = sclk_values[0];
    }
    if (mclk_count == 2 && t->mclk_limit_min_mhz >= 0 && t->mclk_limit_max_mhz >= 0) {
        t->has_mclk = true;
        t->mclk_min_mhz = mclk_values[0];
        t->mclk_max_mhz = mclk_values[1];
    } else if (mclk_count == 1 && t->mclk_limit_min_mhz >= 0 && t->mclk_limit_max_mhz >= 0) {
        t->has_mclk = true;
        t->mclk_max_only = true;
        t->mclk_min_mhz = -1;
        t->mclk_max_mhz = mclk_values[0];
    }

    long pcap = read_long_fd(s->fd_power_cap_r, -1);
    long pmin = read_long_fd(s->fd_power_cap_min, -1);
    long pmax = read_long_fd(s->fd_power_cap_max, -1);
    if (pcap >= 0) {
        t->has_power_cap = true;
        t->power_cap_w = pcap / 1000000.0;
        t->power_cap_min_w = pmin >= 0 ? pmin / 1000000.0 : t->power_cap_w;
        t->power_cap_max_w = pmax >= 0 ? pmax / 1000000.0 : t->power_cap_w;
    }

    t->fan_rpm = (int)read_long_fd(s->fd_fan_rpm, -1);
    t->fan_pwm = read_long_fd(s->fd_pwm, -1);
    t->fan_pwm_max = read_long_fd(s->fd_pwm_max, -1);
    if (t->fan_pwm_max <= 0) t->fan_pwm_max = 255;
    t->has_fan = t->fan_pwm >= 0;

    t->available = t->has_sclk || t->has_sclk_offset || t->has_mclk ||
                   t->has_vdd_offset || t->has_power_cap || t->has_fan;

    if (!was_initialized || capture_defaults) {
        t->default_sclk_min_mhz = t->sclk_min_mhz;
        t->default_sclk_max_mhz = t->sclk_max_mhz;
        t->default_sclk_offset_mhz = t->sclk_offset_mhz;
        t->default_mclk_min_mhz = t->mclk_min_mhz;
        t->default_mclk_max_mhz = t->mclk_max_mhz;
        t->default_vdd_offset_mv = t->vdd_offset_mv;
        t->default_power_cap_w = t->power_cap_w;

        t->edit_sclk_min_mhz = t->sclk_min_mhz;
        t->edit_sclk_max_mhz = t->sclk_max_mhz;
        t->edit_sclk_offset_mhz = t->sclk_offset_mhz;
        t->edit_mclk_max_mhz = t->mclk_max_mhz;
        t->edit_vdd_offset_mv = t->vdd_offset_mv;
        t->edit_power_cap_w = t->power_cap_w;
        t->edit_fan_pwm = t->fan_pwm;
    } else {
        t->default_sclk_min_mhz = old_def_smin;
        t->default_sclk_max_mhz = old_def_smax;
        t->default_sclk_offset_mhz = old_def_soff;
        t->default_mclk_max_mhz = old_def_mmax;
        t->default_vdd_offset_mv = old_def_voff;
        t->default_power_cap_w = old_def_pcap;

        /* After a successful apply, edits follow the now-current values. */
        t->edit_sclk_min_mhz = t->sclk_min_mhz;
        t->edit_sclk_max_mhz = t->sclk_max_mhz;
        t->edit_sclk_offset_mhz = t->sclk_offset_mhz;
        t->edit_mclk_max_mhz = t->mclk_max_mhz;
        t->edit_vdd_offset_mv = t->vdd_offset_mv;
        t->edit_power_cap_w = t->power_cap_w;
        t->edit_fan_pwm = t->fan_pwm;
    }
    t->initialized = true;
}

static TuneZone tuning_zone_long(long value, long baseline, long limit_max) {
    if (value < 0 || baseline < 0) return TUNE_ZONE_UNAVAILABLE;
    if (value <= baseline || limit_max <= baseline) return TUNE_ZONE_SAFE;
    double f = (double)(value - baseline) / (double)(limit_max - baseline);
    return f >= 0.75 ? TUNE_ZONE_DANGER : TUNE_ZONE_WARNING;
}

static TuneZone tuning_zone_double(double value, double baseline, double limit_max) {
    if (value < 0.0 || baseline < 0.0) return TUNE_ZONE_UNAVAILABLE;
    if (value <= baseline || limit_max <= baseline) return TUNE_ZONE_SAFE;
    double f = (value - baseline) / (limit_max - baseline);
    return f >= 0.75 ? TUNE_ZONE_DANGER : TUNE_ZONE_WARNING;
}

static TuneZone tuning_zone_offset(long value, long max_positive) {
    if (max_positive <= 0 || value <= 0) return TUNE_ZONE_SAFE;
    return value >= (long)llround((double)max_positive * 0.75) ?
        TUNE_ZONE_DANGER : TUNE_ZONE_WARNING;
}

static int write_power_cap(Sysfs *s, double watts) {
    if (s->fd_power_cap_w < 0) { errno = ENOTSUP; return -1; }
    long long uw = llround(watts * 1000000.0);
    char b[64];
    snprintf(b, sizeof(b), "%lld", uw);
    return write_fd_text(s->fd_power_cap_w, b);
}

static int tuning_apply(Sysfs *s, Target *target, TuningState *t,
                        char *message, size_t cap) {
    if (!t->armed) {
        snprintf(message, cap, "Tuning writes are disarmed. Click ARM first.");
        return -1;
    }
    if (!t->available) {
        snprintf(message, cap, "No firmware-exposed tuning controls are available.");
        return -1;
    }

    bool od_changed = false;
    char cmd[96];
    if (apply_policy(s, "manual") < 0) {
        snprintf(message, cap, "Cannot enter manual power mode: %s", strerror(errno));
        return -1;
    }
    text_copy(target->policy, sizeof(target->policy), "manual");

    if (s->fd_od_w >= 0) {
        if (t->has_sclk) {
            t->edit_sclk_max_mhz = clamp_long_value(t->edit_sclk_max_mhz,
                                                     t->sclk_limit_min_mhz,
                                                     t->sclk_limit_max_mhz);
            if (!t->sclk_max_only) {
                t->edit_sclk_min_mhz = clamp_long_value(t->edit_sclk_min_mhz,
                                                         t->sclk_limit_min_mhz,
                                                         t->sclk_limit_max_mhz);
                if (t->edit_sclk_min_mhz > t->edit_sclk_max_mhz)
                    t->edit_sclk_min_mhz = t->edit_sclk_max_mhz;
                snprintf(cmd, sizeof(cmd), "s 0 %ld", t->edit_sclk_min_mhz);
                if (write_fd_text(s->fd_od_w, cmd) < 0) goto od_fail;
            }
            snprintf(cmd, sizeof(cmd), "s 1 %ld", t->edit_sclk_max_mhz);
            if (write_fd_text(s->fd_od_w, cmd) < 0) goto od_fail;
            od_changed = true;
        } else if (t->has_sclk_offset) {
            t->edit_sclk_offset_mhz = clamp_long_value(t->edit_sclk_offset_mhz,
                                                        t->sclk_offset_min_mhz,
                                                        t->sclk_offset_max_mhz);
            if (t->sclk_offset_legacy)
                snprintf(cmd, sizeof(cmd), "s 1 %ld", t->edit_sclk_offset_mhz);
            else
                snprintf(cmd, sizeof(cmd), "s %ld", t->edit_sclk_offset_mhz);
            if (write_fd_text(s->fd_od_w, cmd) < 0) goto od_fail;
            od_changed = true;
        }

        if (t->has_mclk) {
            t->edit_mclk_max_mhz = clamp_long_value(t->edit_mclk_max_mhz,
                                                     t->mclk_limit_min_mhz,
                                                     t->mclk_limit_max_mhz);
            snprintf(cmd, sizeof(cmd), "m 1 %ld", t->edit_mclk_max_mhz);
            if (write_fd_text(s->fd_od_w, cmd) < 0) goto od_fail;
            od_changed = true;
        }

        if (t->has_vdd_offset) {
            t->edit_vdd_offset_mv = clamp_long_value(t->edit_vdd_offset_mv,
                                                      t->vdd_offset_min_mv,
                                                      t->vdd_offset_max_mv);
            snprintf(cmd, sizeof(cmd), "vo %ld", t->edit_vdd_offset_mv);
            if (write_fd_text(s->fd_od_w, cmd) < 0) goto od_fail;
            od_changed = true;
        }

        if (od_changed && write_fd_text(s->fd_od_w, "c") < 0) goto od_fail;
    }

    if (t->has_power_cap) {
        t->edit_power_cap_w = clamp_double_value(t->edit_power_cap_w,
                                                  t->power_cap_min_w,
                                                  t->power_cap_max_w);
        if (write_power_cap(s, t->edit_power_cap_w) < 0) {
            snprintf(message, cap, "Clock settings applied, but power-cap write failed: %s", strerror(errno));
            t->armed = false;
            tuning_refresh(s, t, false);
            return -1;
        }
    }

    if (t->has_fan && s->fd_pwm_w >= 0) {
        t->edit_fan_pwm = clamp_long_value(t->edit_fan_pwm, 0, t->fan_pwm_max);
        char pwm_buf[32];
        snprintf(pwm_buf, sizeof(pwm_buf), "%ld", t->edit_fan_pwm);
        if (write_fd_text(s->fd_pwm_w, pwm_buf) < 0) {
            snprintf(message, cap, "Clock settings applied, but fan PWM write failed: %s", strerror(errno));
            t->armed = false;
            tuning_refresh(s, t, false);
            return -1;
        }
    }

    t->armed = false;
    tuning_refresh(s, t, false);
    snprintf(message, cap, "Tuning applied within driver/firmware limits. Writes disarmed.");
    return 0;

od_fail:
    {
        int saved = errno;
        (void)write_fd_text(s->fd_od_w, "r");
        errno = saved;
        snprintf(message, cap, "Overdrive write rejected by driver: %s", strerror(errno));
        t->armed = false;
        tuning_refresh(s, t, false);
        return -1;
    }
}

static int tuning_reset(Sysfs *s, Target *target, TuningState *t,
                        char *message, size_t cap) {
    int rc = 0;
    if (s->fd_od_w >= 0) {
        if (write_fd_text(s->fd_od_w, "r") < 0) rc = -1;
    }
    if (t->has_power_cap && t->default_power_cap_w > 0.0) {
        if (write_power_cap(s, t->default_power_cap_w) < 0) rc = -1;
    }
    /* Preserve the watchdog target on driver rejection. Reset may already
     * have partially succeeded, so retain the partial-failure message. */
    if (apply_policy(s, "profile_standard") == 0) {
        text_copy(target->policy, sizeof(target->policy), "profile_standard");
        target->lock_sclk = false;
        target->lock_mclk = false;
    } else rc = -1;
    t->armed = false;
    tuning_refresh(s, t, true);
    snprintf(message, cap, rc == 0 ?
             "Tuning reset to driver defaults / captured stock power cap." :
             "Reset attempted; one or more driver writes were rejected.");
    return rc;
}

static void tuning_adjust(TuningState *t, int row, int direction) {
    long sign = direction < 0 ? -1L : 1L;
    switch (row) {
        case 0:
            if (t->has_sclk)
                t->edit_sclk_max_mhz = clamp_long_value(t->edit_sclk_max_mhz + sign * 25L,
                                                         t->sclk_limit_min_mhz, t->sclk_limit_max_mhz);
            else if (t->has_sclk_offset)
                t->edit_sclk_offset_mhz = clamp_long_value(t->edit_sclk_offset_mhz + sign * 10L,
                                                            t->sclk_offset_min_mhz, t->sclk_offset_max_mhz);
            break;
        case 1:
            if (t->has_mclk)
                t->edit_mclk_max_mhz = clamp_long_value(t->edit_mclk_max_mhz + sign * 25L,
                                                         t->mclk_limit_min_mhz, t->mclk_limit_max_mhz);
            break;
        case 2:
            if (t->has_vdd_offset)
                t->edit_vdd_offset_mv = clamp_long_value(t->edit_vdd_offset_mv + sign * 10L,
                                                          t->vdd_offset_min_mv, t->vdd_offset_max_mv);
            break;
        case 3:
            if (t->has_power_cap)
                t->edit_power_cap_w = clamp_double_value(t->edit_power_cap_w + direction * 5.0,
                                                          t->power_cap_min_w, t->power_cap_max_w);
            break;
        case 4:
            if (t->has_fan)
                t->edit_fan_pwm = clamp_long_value(t->edit_fan_pwm + sign * 10L, 0, t->fan_pwm_max);
            break;
        default:
            break;
    }
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
        } else if(strncmp(line,"vrr_enabled=",12)==0 || strncmp(line,"vrr_enabled:",12)==0) {
            const char *v=line+12;
            while(*v==' ' || *v=='\t')v++;
            snprintf(si->vrr_state,sizeof(si->vrr_state),"%s (DRM CRTC)",(*v=='1')?"ENABLED":(*v=='0')?"DISABLED":"UNKNOWN");
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
                if(refresh>1000) refresh=(refresh+500)/1000; /* DRM reports millihertz on some kernels */
                if(refresh>0) {
                    snprintf(one,sizeof(one),"%.70s@%ldHz",name,refresh);
                    if(!si->refresh[0]) snprintf(si->refresh,sizeof(si->refresh),"%ld Hz (DRM CRTC)",refresh);
                }
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
    if (cm && (g_settings.compositor_source==COMP_AUTO || g_settings.compositor_source==COMP_DRM)) {
        for (int i=0; i<cm->count && !si->compositor[0]; i++) {
            char label[64];
            if (compositor_process(cm->items[i].name,label,sizeof(label)))
                set_compositor_from_pid(si,cm->items[i].pid,cm->items[i].name,label);
        }
    }

    DIR *pd=(g_settings.compositor_source==COMP_AUTO || g_settings.compositor_source==COMP_PROCESS) ? opendir("/proc") : NULL;
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
    if(g_settings.compositor_source==COMP_OFF) snprintf(si->compositor,sizeof(si->compositor),"Detection disabled");
    else if(!si->compositor[0]) snprintf(si->compositor,sizeof(si->compositor),"unavailable via selected source");
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
    unsigned long text_dark;
    bool dirty;
    /* Scroll only the content viewport. The header and footer never scroll. */
    int scroll_y;
    int draw_offset_y;
    int text_right;
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

/* Compact desktop layout: all view bodies use UI_PAGE_Y as their logical origin. */
enum { UI_MARGIN = 12, UI_GAP = 7, UI_PAGE_Y = 407,
       UI_CONTENT_TOP = 275, UI_FOOTER_SPACE = 62 };

static int gui_content_bottom(const Gui *ui) { return ui->height - UI_FOOTER_SPACE; }
static int gui_content_height(const Gui *ui) { return gui_content_bottom(ui) - UI_CONTENT_TOP; }

static void gui_text(Gui *ui, int x, int y, unsigned long color, bool bold,
                     const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    /* Truncate instead of allowing long diagnostic text to spill into other tiles. */
    XFontStruct *font = bold && ui->font_bold ? ui->font_bold : ui->font;
    int right = ui->text_right ? ui->text_right : ui->width - UI_MARGIN;
    int room = right - x;
    if (room <= 4 || !font) return;
    int n = (int)strlen(buf);
    if (XTextWidth(font, buf, n) > room) {
        const char *dots = "..";
        int dots_w = XTextWidth(font, dots, 2);
        while (n > 0 && XTextWidth(font, buf, n) + dots_w > room) n--;
        if (n <= 0) return;
        buf[n] = '\0';
        if (n + 2 < (int)sizeof(buf)) strcat(buf, dots);
    }
    XSetForeground(ui->dpy, ui->gc, color);
    gui_set_font(ui, bold);
    XDrawString(ui->dpy, ui->win, ui->gc, x, y + ui->draw_offset_y, buf, (int)strlen(buf));
}

static void gui_fill(Gui *ui, Rect r, unsigned long color) {
    if (r.w <= 0 || r.h <= 0) return;
    XSetForeground(ui->dpy, ui->gc, color);
    XFillRectangle(ui->dpy, ui->win, ui->gc, r.x, r.y + ui->draw_offset_y, (unsigned)r.w, (unsigned)r.h);
}

static void gui_outline(Gui *ui, Rect r, unsigned long color) {
    if (r.w <= 0 || r.h <= 0) return;
    XSetForeground(ui->dpy, ui->gc, color);
    XDrawRectangle(ui->dpy, ui->win, ui->gc, r.x, r.y + ui->draw_offset_y, (unsigned)(r.w - 1), (unsigned)(r.h - 1));
}

/* Outlined light labels remain legible on both filled and unfilled bar sections. */
static void gui_bar_label(Gui *ui, Rect r, const char *label) {
    if (!label || !*label) return;
    int old_right = ui->text_right;
    ui->text_right = r.x + r.w - 3;
    XFontStruct *font = ui->font_bold ? ui->font_bold : ui->font;
    int asc = font ? font->ascent : 10, desc = font ? font->descent : 3;
    int y = r.y + (r.h + asc - desc) / 2;
    int x = r.x + 5;
    gui_text(ui, x+1, y+1, ui->bg, true, "%s", label);
    gui_text(ui, x, y, ui->fg, true, "%s", label);
    ui->text_right = old_right;
}

static void gui_bar(Gui *ui, Rect r, double pct, const char *label) {
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    gui_fill(ui, r, ui->panel_alt);
    Rect fill = r;
    fill.w = (int)((double)r.w * pct / 100.0);
    gui_fill(ui, fill, ui->accent);
    gui_outline(ui, r, ui->dim);
    gui_bar_label(ui, r, label);
}

static void gui_bar_color(Gui *ui, Rect r, double pct, const char *label,
                          unsigned long fill_color) {
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    gui_fill(ui, r, ui->panel_alt);
    Rect fill = r;
    fill.w = (int)((double)r.w * pct / 100.0);
    gui_fill(ui, fill, fill_color);
    gui_outline(ui, r, ui->dim);
    gui_bar_label(ui, r, label);
}

static void gui_bar_color_text(Gui *ui, Rect r, double pct, const char *label,
                               unsigned long fill_color, unsigned long text_color) {
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    gui_fill(ui, r, ui->panel_alt);
    Rect fill = r;
    fill.w = (int)((double)r.w * pct / 100.0);
    gui_fill(ui, fill, fill_color);
    gui_outline(ui, r, ui->dim);
    (void)text_color;
    gui_bar_label(ui, r, label);
}

static unsigned long tune_zone_color(Gui *ui, TuneZone zone) {
    switch (zone) {
        case TUNE_ZONE_SAFE: return ui->good;
        case TUNE_ZONE_WARNING: return ui->warn;
        case TUNE_ZONE_DANGER: return ui->bad;
        case TUNE_ZONE_UNAVAILABLE: default: return ui->dim;
    }
}

static TuneZone temperature_zone(double c, long crit_mc, long emergency_mc) {
    if (c < 0.0) return TUNE_ZONE_UNAVAILABLE;
    double crit = crit_mc > 0 ? crit_mc / 1000.0 : 90.0;
    double emergency = emergency_mc > 0 ? emergency_mc / 1000.0 : fmax(crit + 10.0, 105.0);
    if (c >= emergency || c >= 100.0) return TUNE_ZONE_DANGER;
    if (c >= crit || c >= 85.0) return TUNE_ZONE_WARNING;
    return TUNE_ZONE_SAFE;
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

/* Local Arch package metadata: versions and *package* build dates, never guessed
 * from process names. Non-Arch hosts legitimately show 'unavailable'. */
static bool pacman_package_info(const char *pkg, char *version, size_t vc, char *built, size_t bc) {
    DIR *d=opendir("/var/lib/pacman/local");
    if(!d) return false;
    char best[MAX_PATH_LEN]="";
    struct dirent *de;
    size_t n=strlen(pkg);
    while((de=readdir(d))) {
        if(strncmp(de->d_name,pkg,n)!=0 || de->d_name[n]!='-') continue;
        if(strcmp(de->d_name,best)>0) snprintf(best,sizeof(best),"%s",de->d_name);
    }
    closedir(d);
    if(!best[0]) return false;
    char path[MAX_PATH_LEN];
    if(snprintf(path,sizeof(path),"/var/lib/pacman/local/%s/desc",best)>=(int)sizeof(path)) return false;
    FILE *f=fopen(path,"r");
    if(!f) return false;
    char line[256];
    enum {NONE,VERSION_FIELD,BUILD_FIELD} mode=NONE;
    while(fgets(line,sizeof(line),f)) {
        trim(line);
        if(line[0]=='%') {
            mode=!strcmp(line,"%VERSION%")?VERSION_FIELD:!strcmp(line,"%BUILDDATE%")?BUILD_FIELD:NONE;
            continue;
        }
        if(!line[0]) continue;
        if(mode==VERSION_FIELD) text_copy(version,vc,line);
        else if(mode==BUILD_FIELD) {
            char *end=NULL;errno=0;
            long long timestamp=strtoll(line,&end,10);
            if(!errno && end && !*end && timestamp>0) {
                time_t ts=(time_t)timestamp;
                struct tm tm_value;
                if(gmtime_r(&ts,&tm_value)) strftime(built,bc,"%Y-%m-%d UTC",&tm_value);
            }
        }
        mode=NONE;
    }
    fclose(f);
    return version[0]!=0;
}
static void session_package_info(SessionInfo *si, int vendor_release) {
    const char *xname=xlibre_installed()?"xlibre-xserver":"xorg-server";
    if(!pacman_package_info(xname,si->xserver_version,sizeof(si->xserver_version),si->xserver_built,sizeof(si->xserver_built)))
        snprintf(si->xserver_version,sizeof(si->xserver_version),"vendor release %d",vendor_release);
    const char *sess=strcasestr(si->desktop,"xfce")?"xfce4-session":
                     strcasestr(si->desktop,"kde")?"plasma-workspace":
                     strcasestr(si->desktop,"gnome")?"gnome-session":"unknown";
    if(strcmp(sess,"unknown")) {
        (void)pacman_package_info(sess,si->session_version,sizeof(si->session_version),si->session_built,sizeof(si->session_built));
        const char *desktop=strcasestr(si->desktop,"xfce")?"xfdesktop":
                             strcasestr(si->desktop,"kde")?"plasma-desktop":"gnome-shell";
        (void)pacman_package_info(desktop,si->desktop_version,sizeof(si->desktop_version),si->desktop_built,sizeof(si->desktop_built));
    }
    const char *wm=strcasestr(si->window_manager,"xfwm4")?"xfwm4":
                   strcasestr(si->window_manager,"openbox")?"openbox":
                   strcasestr(si->window_manager,"i3")?"i3-wm":
                   strcasestr(si->window_manager,"kwin")?"kwin-x11":"";
    if(*wm) (void)pacman_package_info(wm,si->wm_version,sizeof(si->wm_version),si->wm_built,sizeof(si->wm_built));
    const char *comp=strcasestr(si->compositor,"picom")?"picom":
                     strcasestr(si->compositor,"xfwm4")?"xfwm4":
                     strcasestr(si->compositor,"kwin")?"kwin-x11":"";
    if(*comp) (void)pacman_package_info(comp,si->compositor_version,sizeof(si->compositor_version),si->compositor_built,sizeof(si->compositor_built));
    if(!si->refresh[0]) snprintf(si->refresh,sizeof(si->refresh),"KMS mode not reported");
    /* Neither compositing nor Present support proves vsync/buffering is enabled. */
    snprintf(si->vsync_state,sizeof(si->vsync_state),"unverified (compositor / app-specific)");
    snprintf(si->buffer_state,sizeof(si->buffer_state),"unverified (driver / application-specific)");
    if(!si->vrr_state[0]) snprintf(si->vrr_state,sizeof(si->vrr_state),"unreported by DRM state");
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
    if (cm != None && (g_settings.compositor_source==COMP_AUTO || g_settings.compositor_source==COMP_X11)) {
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
    session_package_info(si,VendorRelease(ui->dpy));
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
    ui->width = 840;
    ui->height = 660;
    ui->bg = gui_color(ui, "#101214", BlackPixel(ui->dpy, ui->screen));
    ui->panel = gui_color(ui, "#1a1d21", ui->bg);
    ui->panel_alt = gui_color(ui, "#262a2f", ui->panel);
    ui->fg = gui_color(ui, "#e5e7eb", WhitePixel(ui->dpy, ui->screen));
    ui->dim = gui_color(ui, "#7f8792", ui->fg);
    ui->accent = gui_color(ui, "#4ea1ff", ui->fg);
    ui->good = gui_color(ui, "#68d391", ui->fg);
    ui->warn = gui_color(ui, "#f6c85f", ui->fg);
    ui->bad = gui_color(ui, "#ff6b6b", ui->fg);
    ui->text_dark = gui_color(ui, "#1a1d21", ui->bg);

    ui->win = XCreateSimpleWindow(ui->dpy, ui->root, 80, 80, (unsigned)ui->width, (unsigned)ui->height,
                                  0, ui->dim, ui->bg);
    char title[128];
    snprintf(title, sizeof(title), "AMD GPU Performance and Diagnostic Tool v%s", VERSION);
    XStoreName(ui->dpy, ui->win, title);
    XSelectInput(ui->dpy, ui->win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    ui->wm_delete = XInternAtom(ui->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ui->dpy, ui->win, &ui->wm_delete, 1);
    XSizeHints hints = {0};
    hints.flags = PMinSize;
    hints.min_width = 780;
    hints.min_height = 610;
    XSetWMNormalHints(ui->dpy, ui->win, &hints);

    ui->gc = XCreateGC(ui->dpy, ui->win, 0, NULL);
    ui->font = XLoadQueryFont(ui->dpy, "8x13");
    if (!ui->font) ui->font = XLoadQueryFont(ui->dpy, "9x15");
    if (!ui->font) ui->font = XLoadQueryFont(ui->dpy, "fixed");
    ui->font_bold = XLoadQueryFont(ui->dpy, "8x13bold");
    if (!ui->font_bold) ui->font_bold = XLoadQueryFont(ui->dpy, "9x15bold");
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
    static const char *names[] = {"Home", "Clients", "Power", "Tuning", "Desktop", "Events", "Trace", "Settings", "About", "?"};
    return (v >= VIEW_DASHBOARD && v <= VIEW_ABOUT) ? names[v] : "Home";
}

static Rect compact_row_rect(const Gui *ui, int idx, int count,
                             int y, int h, int gap, int preferred_w) {
    int full = ui->width - UI_MARGIN * 2;
    int w = (full - (count - 1) * gap) / count;
    if (w > preferred_w) w = preferred_w;
    int group = count * w + (count - 1) * gap;
    int x = (ui->width - group) / 2 + idx * (w + gap);
    return (Rect){x, y, w, h};
}

static Rect tab_rect(const Gui *ui, int idx) {
    int total = ui->width - 2*UI_MARGIN;
    int help_w=26, gap=4;
    int w=(total-help_w-9*gap)/9;
    return (Rect){UI_MARGIN+idx*(w+gap),248,w,23};
}
static Rect help_rect(const Gui *ui) {
    int total=ui->width-2*UI_MARGIN, gap=4, help_w=26;
    int w=(total-help_w-9*gap)/9;
    return (Rect){UI_MARGIN+9*(w+gap),248,help_w,23};
}

static Rect control_rect(const Gui *ui, int idx) {
    return compact_row_rect(ui, idx, 5, ui->height - 33, 23, 6, 135);
}

static Rect mode_rect(const Gui *ui, int idx) {
    return compact_row_rect(ui, idx, 8, 56, 23, 3, 103);
}

static Rect workflow_rect(const Gui *ui, int idx) {
    int w = 102, h = 22, gap = 5;
    return (Rect){ui->width - UI_MARGIN - (2 - idx) * (w + gap) + gap, 8, w, h};
}

static Rect tuning_tile_rect(const Gui *ui, int row) {
    int gap = UI_GAP, w = (ui->width - UI_MARGIN * 2 - 2 * gap) / 3;
    return (Rect){UI_MARGIN + (row % 3) * (w + gap), 533 + (row / 3) * 100, w, 91};
}

static Rect tuning_minus_rect(const Gui *ui, int row) {
    Rect r = tuning_tile_rect(ui, row);
    return (Rect){r.x + r.w - 53, r.y + 30, 20, 20};
}

static Rect tuning_plus_rect(const Gui *ui, int row) {
    Rect r = tuning_tile_rect(ui, row);
    return (Rect){r.x + r.w - 28, r.y + 30, 20, 20};
}

static Rect tuning_action_rect(const Gui *ui, int idx) {
    Rect safe=tuning_tile_rect(ui,5);
    int w=(safe.w-24)/2;
    return (Rect){safe.x+8+(idx%2)*(w+8),safe.y+29+(idx/2)*28,w,22};
}

static void draw_button(Gui *ui, Rect r, const char *text, bool active) {
    gui_fill(ui, r, active ? ui->accent : ui->panel_alt);
    gui_outline(ui, r, active ? ui->fg : ui->dim);
    XFontStruct *font = active && ui->font_bold ? ui->font_bold : ui->font;
    int len = (int)strlen(text);
    int text_w = font ? XTextWidth(font, text, len) : len * 8;
    int ascent = font ? font->ascent : 10;
    int descent = font ? font->descent : 3;
    int tx = r.x + (r.w - text_w) / 2;
    int ty = r.y + (r.h + ascent - descent) / 2;
    if (tx < r.x + 3) tx = r.x + 3;
    int old_right = ui->text_right;
    ui->text_right = r.x + r.w - 3;
    gui_text(ui, tx, ty, active ? ui->bg : ui->fg, active, "%s", text);
    ui->text_right = old_right;
}


static void draw_header(Gui *ui, const Gpu *g, Sysfs *s, const Target *t,
                        const PowerTelemetry *p, const SystemTelemetry *sys,
                        const Stats *st) {
    ui->text_right = workflow_rect(ui,0).x - 8;
    gui_text(ui, UI_MARGIN, 19, ui->fg, true, "%s", g->full_name);
    ui->text_right = 0;

    /* Workflow mode buttons */
    Rect ai_btn = workflow_rect(ui, 0);
    Rect game_btn = workflow_rect(ui, 1);
    bool ai_active = (g_workflow == WORKFLOW_AI);
    bool game_active = (g_workflow == WORKFLOW_GAMING);
    draw_button(ui, ai_btn, "AI", ai_active);
    draw_button(ui, game_btn, "GAMING", game_active);

    /* Data sources are configured in Settings (not the header). */
    gui_text(ui, UI_MARGIN, 35, ui->dim, false,
             "%s  |  PCI %s  |  ID 1002:%s  |  VRAM %.1f GiB",
             g->card_name, g->pci_addr, g->device_id, g->vram_bytes / 1073741824.0);

    char actual[128] = "unknown";
    (void)read_fd_text(s->fd_policy_r, actual, sizeof(actual));
    unsigned long lock_color = strcmp(actual, t->policy) == 0 ? ui->good : ui->bad;
    gui_text(ui, UI_MARGIN, 50, lock_color, true,
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
    double power_scale = POWER_BAR_MAX_W;
    if (p->power_cap_max_w > 1.0 && p->power_cap_max_w < power_scale)
        power_scale = p->power_cap_max_w;
    if (power_scale < 400.0) power_scale = 400.0;
    Rect power_bar = {UI_MARGIN, 86, ui->width - UI_MARGIN * 2, 26};
    double power_pct = p->power_w >= 0.0 ? p->power_w * 100.0 / power_scale : 0.0;
    if (p->power_w >= 0.0 && p->power_cap_w > 0.0)
        snprintf(label, sizeof(label), "BOARD POWER   %.1f W    POWER CAP %.0f W    firmware scale %.0f W",
                 p->power_w, p->power_cap_w, power_scale);
    else if (p->power_w >= 0.0)
        snprintf(label, sizeof(label), "BOARD POWER   %.1f W    scale %.0f W", p->power_w, power_scale);
    else
        snprintf(label, sizeof(label), "BOARD POWER   unavailable");
    if (power_pct >= 95.0)
        gui_bar_color(ui, power_bar, power_pct, label, ui->bad);
    else if (power_pct >= 80.0)
        gui_bar_color_text(ui, power_bar, power_pct, label, ui->warn, ui->text_dark);
    else
        gui_bar_color(ui, power_bar, power_pct, label, ui->accent);

    /* Label the physical source explicitly; temp2 is junction, not another edge.
     * Tctl is read independently of the preferred CPU die sensor (Tdie). */
    double temp_values[MAX_GPU_TEMPS + 1];
    long temp_crit[MAX_GPU_TEMPS + 1];
    long temp_emergency[MAX_GPU_TEMPS + 1];
    const char *temp_names[MAX_GPU_TEMPS + 1];
    const char *const gpu_names[] = {"GPU Edge (t1)", "GPU Junction (t2)", "GPU Mem (t3)"};
    int nt = 0;
    for (int i = 0; i < MAX_GPU_TEMPS; i++) {
        if (p->temps_c[i] < 0.0) continue;
        temp_values[nt] = p->temps_c[i];
        temp_crit[nt] = s->temp_crit_mc[i];
        temp_emergency[nt] = s->temp_emergency_mc[i];
        /* The kernel's actual label wins when it differs from the standard mapping. */
        const char *sensor_label = s->temp_label[i];
        if (sensor_label[0] && strcasecmp(sensor_label, "edge") && strcasecmp(sensor_label, "junction") &&
            strcasecmp(sensor_label, "hotspot") && strcasecmp(sensor_label, "mem") &&
            strcasecmp(sensor_label, "memory") && strcasecmp(sensor_label, "GPU Edge") &&
            strcasecmp(sensor_label, "GPU Junction") && strcasecmp(sensor_label, "GPU Memory"))
            temp_names[nt] = sensor_label;
        else temp_names[nt] = gpu_names[i];
        nt++;
    }
    double cpu_label_temp = sys->cpu_tctl_c >= 0.0 ? sys->cpu_tctl_c : sys->cpu_temp_c;
    if (cpu_label_temp >= 0.0 && nt < MAX_GPU_TEMPS + 1) {
        temp_values[nt] = cpu_label_temp;
        temp_crit[nt] = sys->cpu_temp_crit_mc;
        temp_emergency[nt] = -1;
        temp_names[nt] = sys->cpu_tctl_c >= 0.0 ?
                         (sys->cpu_tctl_label[0] ? sys->cpu_tctl_label : "CPU Tctl (hwmon)") :
                         strcasestr(sys->cpu_temp_label, "tdie") ? "CPU Tdie (hwmon)" : "CPU temp (hwmon)";
        nt++;
    }

    int margin = UI_MARGIN, gap = UI_GAP;
    /* Bars start below the board power bar */
    int bar_top = 118;
    if (nt > 0) {
        int tw = (ui->width - margin * 2 - gap * (nt - 1)) / nt;
        for (int i = 0; i < nt; i++) {
            double max_c = temp_emergency[i] > 0 ? temp_emergency[i] / 1000.0 :
                           temp_crit[i] > 0 ? temp_crit[i] / 1000.0 + 10.0 : 110.0;
            if (max_c < 80.0) max_c = 110.0;
            snprintf(label, sizeof(label), "%s   %.1f C", temp_names[i], temp_values[i]);
            TuneZone z = temperature_zone(temp_values[i], temp_crit[i], temp_emergency[i]);
            Rect temp_bar = {margin + i * (tw + gap), bar_top, tw, 21};
            unsigned long fill = tune_zone_color(ui, z);
            if (z == TUNE_ZONE_SAFE || z == TUNE_ZONE_WARNING)
                gui_bar_color_text(ui, temp_bar, temp_values[i] * 100.0 / max_c, label, fill, ui->text_dark);
            else
                gui_bar_color(ui, temp_bar, temp_values[i] * 100.0 / max_c, label, fill);
        }
    } else {
        gui_bar_color(ui, (Rect){margin, bar_top, ui->width - margin * 2, 21}, 0.0,
                      "Temperatures unavailable", ui->dim);
    }

    const int half_gap = 8;
    int half = (ui->width - margin * 2 - half_gap) / 2;
    Rect left = {margin, bar_top + 26, half, 19};
    Rect right = {margin + half + half_gap, bar_top + 26, half, 19};

    snprintf(label, sizeof(label), "GPU load %d%%", p->gpu_busy >= 0 ? p->gpu_busy : 0);
    gui_bar(ui, left, p->gpu_busy >= 0 ? p->gpu_busy : 0.0, label);
    double vram_pct = g->vram_bytes ? (double)p->vram_used * 100.0 / (double)g->vram_bytes : 0.0;
    snprintf(label, sizeof(label), "VRAM %.2f / %.2f GiB (%.1f%%)",
             p->vram_used / 1073741824.0, g->vram_bytes / 1073741824.0, vram_pct);
    gui_bar(ui, right, vram_pct, label);

    left.y = right.y = bar_top + 49;
    snprintf(label, sizeof(label), "CPU %.1f%%  (%u threads)", sys->cpu_pct, sys->cpu_threads);
    gui_bar(ui, left, sys->cpu_pct, label);
    snprintf(label, sizeof(label), "System RAM %.2f / %.2f GiB (%.1f%%)",
             sys->ram_used_bytes / 1073741824.0, sys->ram_total_bytes / 1073741824.0, sys->ram_pct);
    gui_bar(ui, right, sys->ram_pct, label);

    left.y = right.y = bar_top + 72;
    double voltage_pct = p->voltage_v >= 0.0 ? p->voltage_v * 100.0 / VOLTAGE_BAR_MAX_V : 0.0;
    if (p->voltage_v >= 0.0)
        snprintf(label, sizeof(label), "%s %.3f V",
                 s->voltage_label[0] ? s->voltage_label : "GPU voltage", p->voltage_v);
    else
        snprintf(label, sizeof(label), "GPU voltage unavailable");
    gui_bar(ui, left, voltage_pct, label);

    double current_pct = p->current_a >= 0.0 ? p->current_a * 100.0 / CURRENT_BAR_MAX_A : 0.0;
    if (p->current_a >= 0.0)
        snprintf(label, sizeof(label), "Current %s %.2f A%s", p->current_label, p->current_a,
                 p->current_is_estimate ? " (est.)" : "");
    else
        snprintf(label, sizeof(label), "Current unavailable");
    gui_bar(ui, right, current_pct, label);

    char fan[80], gfx[80], mem[80];
    if (p->fan_rpm >= 0 && p->fan_pct >= 0) snprintf(fan, sizeof(fan), "%d RPM / %.0f%%", p->fan_rpm, p->fan_pct);
    else if (p->fan_rpm >= 0) snprintf(fan, sizeof(fan), "%d RPM", p->fan_rpm);
    else snprintf(fan, sizeof(fan), "n/a");
    if (p->gfx_hz >= 0) snprintf(gfx, sizeof(gfx), "%.0f MHz", p->gfx_hz / 1000000.0); else snprintf(gfx, sizeof(gfx), "n/a");
    if (p->mem_hz >= 0) snprintf(mem, sizeof(mem), "%.0f MHz", p->mem_hz / 1000000.0); else snprintf(mem, sizeof(mem), "n/a");
    gui_text(ui, UI_MARGIN, 236, ui->fg, false,
             "Fan %s   GFX %s   MEM %s   watchdog %.0f Hz", fan, gfx, mem, st->loop_hz);
}

static void draw_tabs(Gui *ui, View view) {
    for (int i=0;i<9;i++) draw_button(ui,tab_rect(ui,i),view_name((View)i),view==(View)i);
    draw_button(ui,help_rect(ui),"?",view==VIEW_HELP);
}

static void panel_title(Gui *ui, Rect r, const char *title) {
    gui_fill(ui, r, ui->panel);
    gui_outline(ui, r, ui->panel_alt);
    ui->text_right = r.x + r.w - 8;
    gui_text(ui, r.x + 8, r.y + 17, ui->accent, true, "%s", title);
}

/* Read-only gaming telemetry. No FPS or hardware feature statuses are inferred
 * from the UI's own redraw rate or unsupported kernel interfaces. */
static void draw_gaming_dashboard(Gui *ui, const Gpu *g, const Sysfs *s,
                                  const PowerTelemetry *p, const SystemTelemetry *sys) {
    int margin=UI_MARGIN, gap=UI_GAP, tile_h=96;
    int w=(ui->width-2*margin-2*gap)/3;
    int top=UI_PAGE_Y;
    Rect r[3]={{margin, top, w, tile_h},
               {margin+w+gap, top, w, tile_h},
               {margin+2*(w+gap), top, w, tile_h}};
    char label[128];
    double watts_scale=p->power_cap_w>0.0 ? p->power_cap_w : 400.0;
    if (watts_scale<200) watts_scale=200;

    /* Row 1: both measured powers, individual GPU temperatures, free memory. */
    panel_title(ui,r[0],"POWER CONSUMPTION");
    if (p->power_w>=0.0) {
        if (p->power_cap_w > 0.0)
            snprintf(label,sizeof(label),"GPU %.0f / %.0f W",p->power_w,p->power_cap_w);
        else snprintf(label,sizeof(label),"GPU %.0f W (board)",p->power_w);
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+31,w-18,21},p->power_w*100.0/watts_scale,label,ui->accent);
    } else gui_text(ui,r[0].x+9,r[0].y+47,ui->dim,false,"GPU power unavailable");
    if (sys->cpu_power_w>=0.0) {
        snprintf(label,sizeof(label),"CPU %.0f W (sensor)",sys->cpu_power_w);
        /* CPU is an independent 200 W reference scale, not the GPU's power cap. */
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+61,w-18,21},sys->cpu_power_w/2.0,label,ui->accent);
    } else gui_text(ui,r[0].x+9,r[0].y+76,ui->dim,false,"CPU power: sensor unavailable");

    panel_title(ui,r[1],"GPU TEMPERATURE");
    static const char *const temp_names[3]={"EDGE [temp1]","JUNCTION [temp2]","MEM [temp3]"};
    int shown=0;
    for (int i=0;i<MAX_GPU_TEMPS;i++) {
        if (p->temps_c[i]<0.0) continue;
        TuneZone zone=temperature_zone(p->temps_c[i],s->temp_crit_mc[i],s->temp_emergency_mc[i]);
        double limit=s->temp_emergency_mc[i]>0 ? s->temp_emergency_mc[i]/1000.0 :
                     s->temp_crit_mc[i]>0 ? s->temp_crit_mc[i]/1000.0+10 : 110;
        if (limit<80) limit=110;
        snprintf(label,sizeof(label),"%s %.0f C",temp_names[i],p->temps_c[i]);
        gui_bar_color(ui,(Rect){r[1].x+9,r[1].y+29+i*22,w-18,18},
                      p->temps_c[i]*100.0/limit,label,tune_zone_color(ui,zone));
        shown++;
    }
    if (!shown) gui_text(ui,r[1].x+9,r[1].y+45,ui->dim,false,"GPU sensors unavailable");

    panel_title(ui,r[2],"MEMORY");
    if (g->vram_bytes>0 && p->vram_usage_valid && p->vram_used<=g->vram_bytes) {
        uint64_t free=g->vram_bytes-p->vram_used;
        snprintf(label,sizeof(label),"GPU free %.1f / %.1f GiB",free/1073741824.0,g->vram_bytes/1073741824.0);
        gui_bar_color(ui,(Rect){r[2].x+9,r[2].y+31,w-18,21},
                      free*100.0/g->vram_bytes,label,free*100.0/g->vram_bytes<10?ui->warn:ui->accent);
    } else gui_text(ui,r[2].x+9,r[2].y+47,ui->dim,false,"GPU headroom unavailable");
    if (sys->ram_total_bytes && sys->ram_used_bytes<=sys->ram_total_bytes) {
        uint64_t free=sys->ram_total_bytes-sys->ram_used_bytes;
        snprintf(label,sizeof(label),"RAM free %.1f / %.1f GiB",free/1073741824.0,sys->ram_total_bytes/1073741824.0);
        gui_bar_color(ui,(Rect){r[2].x+9,r[2].y+61,w-18,21},
                      free*100.0/sys->ram_total_bytes,label,ui->accent);
    } else gui_text(ui,r[2].x+9,r[2].y+76,ui->dim,false,"System RAM headroom n/a");

    top+=tile_h+gap;
    for(int i=0;i<3;i++) r[i].y=top;
    panel_title(ui,r[0],"GPU ENGINE UTILIZATION");
    if (p->gpu_busy>=0) {
        snprintf(label,sizeof(label),"Graphics/compute %d%%",p->gpu_busy);
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+31,w-18,19},p->gpu_busy,label,ui->accent);
    } else gui_text(ui,r[0].x+9,r[0].y+45,ui->dim,false,"GPU busy unavailable");
    if (p->mem_busy>=0) {
        snprintf(label,sizeof(label),"Memory controller %d%%",p->mem_busy);
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+59,w-18,19},p->mem_busy,label,ui->accent);
    } else gui_text(ui,r[0].x+9,r[0].y+75,ui->dim,false,"Memory busy unavailable");

    panel_title(ui,r[1],"CPU LOAD PER CORE");
    int max_cores=sys->cpu_cores.core_count;
    if (max_cores>8) max_cores=8;
    if (max_cores<=0) gui_text(ui,r[1].x+9,r[1].y+45,ui->dim,false,"CPU load unavailable");
    else {
        int bw=(w-20-3*5)/4;
        for (int i=0;i<max_cores;i++) {
            int x=r[1].x+10+(i%4)*(bw+5), y=r[1].y+32+(i/4)*24;
            snprintf(label,sizeof(label),"C%d %.0f%%",i,sys->cpu_cores.load_pct[i]);
            gui_bar_color(ui,(Rect){x,y,bw,19},sys->cpu_cores.load_pct[i],label,ui->accent);
        }
    }
    panel_title(ui,r[2],"CPU CLOCKS");
    if (sys->cpu_clock_count>0) {
        double scale=sys->cpu_clock_cap_mhz>0?sys->cpu_clock_cap_mhz:sys->cpu_clock_max_mhz;
        const double f[]={sys->cpu_clock_min_mhz,sys->cpu_clock_avg_mhz,sys->cpu_clock_max_mhz};
        static const char *const names[]={"Min","Avg","Max"};
        for(int i=0;i<3;i++) {
            snprintf(label,sizeof(label),"%s %.0f MHz",names[i],f[i]);
            gui_bar_color(ui,(Rect){r[2].x+9,r[2].y+29+i*22,w-18,18},f[i]*100.0/scale,label,ui->accent);
        }
    } else gui_text(ui,r[2].x+9,r[2].y+45,ui->dim,false,"CPU cpufreq unavailable");

    top+=tile_h+gap;
    for(int i=0;i<3;i++) r[i].y=top;
    panel_title(ui,r[0],"CPU TEMPERATURE");
    if (sys->cpu_tctl_c>=0.0) {
        snprintf(label,sizeof(label),"Tctl [%s] %.1f C",
                 strcasestr(sys->cpu_tctl_label,"zenpower")?"zenpower":"k10temp",sys->cpu_tctl_c);
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+31,w-18,20},sys->cpu_tctl_c,label,ui->accent);
    } else gui_text(ui,r[0].x+9,r[0].y+45,ui->dim,false,"Tctl unavailable");
    if (sys->cpu_temp_c>=0.0) {
        snprintf(label,sizeof(label),"%s %.1f C",sys->cpu_temp_label[0]?sys->cpu_temp_label:"CPU",sys->cpu_temp_c);
        gui_bar_color(ui,(Rect){r[0].x+9,r[0].y+61,w-18,19},sys->cpu_temp_c,label,ui->accent);
    }
    panel_title(ui,r[1],"MOTHERBOARD TEMP");
    if (sys->mb_temp_count>0) {
        int show=sys->mb_temp_count>3?3:sys->mb_temp_count;
        for(int i=0;i<show;i++) {
            if (sys->mb_temp_c[i]<0.0) continue;
            snprintf(label,sizeof(label),"%s %.1f C",sys->mb_temp_labels[i],sys->mb_temp_c[i]);
            gui_bar_color(ui,(Rect){r[1].x+9,r[1].y+29+i*22,w-18,18},sys->mb_temp_c[i],label,ui->accent);
        }
    } else gui_text(ui,r[1].x+9,r[1].y+45,ui->dim,false,"No motherboard sensors");
    panel_title(ui,r[2],"GPU CLOCKS / FAN");
    if (p->gfx_hz>=0) gui_text(ui,r[2].x+9,r[2].y+36,ui->fg,false,"GFX %.0f MHz",p->gfx_hz/1e6);
    else gui_text(ui,r[2].x+9,r[2].y+36,ui->dim,false,"GFX clock unavailable");
    if (p->mem_hz>=0) gui_text(ui,r[2].x+9,r[2].y+57,ui->fg,false,"VRAM %.0f MHz",p->mem_hz/1e6);
    else gui_text(ui,r[2].x+9,r[2].y+57,ui->dim,false,"VRAM clock unavailable");
    if (p->fan_rpm>=0) gui_text(ui,r[2].x+9,r[2].y+77,ui->fg,false,"Fan %d RPM",p->fan_rpm);
    else gui_text(ui,r[2].x+9,r[2].y+77,ui->dim,false,"Fan unavailable");

    top+=tile_h+gap;
    for(int i=0;i<3;i++) r[i].y=top;
    panel_title(ui,r[0],"PCIe LINK");
    if (p->pcie_link_valid) {
        gui_text(ui,r[0].x+9,r[0].y+38,ui->fg,false,"NOW %s x%d",p->pcie_current_speed,p->pcie_current_width);
        if (p->pcie_max_width>0 && p->pcie_max_speed[0])
            gui_text(ui,r[0].x+9,r[0].y+58,ui->dim,false,"MAX %s x%d",p->pcie_max_speed,p->pcie_max_width);
        gui_text(ui,r[0].x+9,r[0].y+80,p->pcie_link_loaded_low==1?ui->warn:ui->dim,false,
                 "Under load: %s",p->pcie_link_loaded_low<0?"UNKNOWN":p->pcie_link_loaded_low?"LINK DOWN-SHIFT":"OK / IDLE");
    } else gui_text(ui,r[0].x+9,r[0].y+44,ui->dim,false,"Link negotiation n/a");

    panel_title(ui,r[1],"RESIZABLE / LARGE BAR");
    if (p->bar0_valid) {
        bool large=p->bar0_bytes>=1073741824ULL;
        gui_text(ui,r[1].x+9,r[1].y+36,large?ui->good:ui->warn,false,
                 "Large BAR: %s",large?"ENABLED":"DISABLED");
        gui_text(ui,r[1].x+9,r[1].y+56,ui->fg,false,"BAR0 aperture %.1f GiB",p->bar0_bytes/1073741824.0);
    } else {
        gui_text(ui,r[1].x+9,r[1].y+38,ui->dim,false,"Large BAR: UNKNOWN");
        gui_text(ui,r[1].x+9,r[1].y+58,ui->dim,false,"BAR0 aperture unavailable");
    }
    gui_text(ui,r[1].x+9,r[1].y+79,ui->dim,false,
             "Resize sysfs: %s",p->rebar_supported?"EXPOSED":"NOT EXPOSED");

    panel_title(ui,r[2],"PERFORMANCE FLAGS");
    gui_text(ui,r[2].x+9,r[2].y+34,sys->cpu_boost<0?ui->dim:sys->cpu_boost?ui->good:ui->warn,
             false,"CPU Boost: %s",sys->cpu_boost<0?"UNKNOWN":sys->cpu_boost?"ENABLED":"DISABLED");
    gui_text(ui,r[2].x+9,r[2].y+53,ui->fg,false,"CPU governor: %s",
             sys->cpu_governor[0]?sys->cpu_governor:"unknown");
    gui_text(ui,r[2].x+9,r[2].y+72,p->amdgpu_dpm==0?ui->warn:ui->dim,false,"GPU DPM: %s",
             p->amdgpu_dpm==-2?"UNKNOWN":p->amdgpu_dpm==-1?"AUTO":p->amdgpu_dpm?"ENABLED":"DISABLED");
    gui_text(ui,r[2].x+9,r[2].y+89,ui->dim,false,"CPU SMT: %s",
             sys->cpu_smt<0?"UNKNOWN":sys->cpu_smt?"ENABLED":"DISABLED");
}

/* Unified, dense tile grid shared in size/spacing with the gaming dashboard. */
static Rect ai_log_button_rect(const Gui *ui) {
    int margin = UI_MARGIN, gap = UI_GAP;
    int w = (ui->width - margin * 2 - gap) / 2;
    return (Rect){margin + w + gap + w - 78, UI_PAGE_Y + 149, 70, 21};
}

static void draw_ai_dashboard(Gui *ui, const Gpu *g, const Target *t, const Stats *st, int interval_us,
                              bool adaptive, bool monitor_clocks, const ClientMonitor *cm,
                              const PowerTelemetry *p, const SessionInfo *si, const EventLog *el,
                              const TraceState *tr, const SystemTelemetry *sys) {
    (void)monitor_clocks;
    (void)si;
    (void)tr;
    const int margin = UI_MARGIN, gap = UI_GAP, top = UI_PAGE_Y;
    const int half = (ui->width - margin * 2 - gap) / 2;
    const int lead_h = 180, tile_h = 96;
    const int third = (ui->width - margin * 2 - gap * 2) / 3;
    const int tile_top = top + lead_h + gap;
    Rect perf = {margin, top, half, lead_h};
    Rect log = {margin + half + gap, top, half, lead_h};
    Rect r[6];
    for (int i = 0; i < 6; i++)
        r[i] = (Rect){margin + (i % 3) * (third + gap), tile_top + (i / 3) * (tile_h + gap), third, tile_h};

    panel_title(ui, perf, "AI PERFORMANCE");
    if ((g_settings.log_parser==LOG_MAYA ||
         (g_settings.log_parser==LOG_AUTO && !strcmp(sys->ai_log_source,"Project Maya"))) &&
        (!sys->tokens.has_data || g_settings.token_source==TOKEN_SOURCE_MAYA) &&
        (g_maya.prefill_valid || g_maya.decode_valid)) {
        if(g_maya.decode_valid) gui_text(ui,perf.x+9,top+36,ui->accent,true,
            "Maya decode %.1f tok/s (completed log)",g_maya.decode_tps);
        else gui_text(ui,perf.x+9,top+36,ui->dim,true,"Maya decode: no completed stat");
        if(g_maya.prefill_valid) gui_text(ui,perf.x+9,top+53,ui->fg,false,
            "Prefill %.0f tok/s | %ld tokens",g_maya.prefill_tps,g_maya.prompt_tokens);
        else gui_text(ui,perf.x+9,top+53,ui->dim,false,"Prefill: no measured timing in log");
    } else if (sys->tokens.has_data && sys->tokens.tokens_per_sec > 0.0) {
        gui_text(ui, perf.x+9, top+36, ui->accent, true, "%s: %.1f tok/s (%s)",
                 sys->token_source == TOKEN_SOURCE_HEURISTIC ? "Est." : "Live",
                 sys->tokens.tokens_per_sec, sys->tokens.source_label);
        if (p->power_w > 0)
            gui_text(ui, perf.x+9, top+53, ui->fg, false, "%.2f tok/s/W | %.1f W",
                     sys->tokens.tokens_per_sec / p->power_w, p->power_w);
    } else if (sys->ai_log_event_count > 0 && g_ai_log_events[0].has_eval) {
        gui_text(ui,perf.x+9,top+36,ui->accent,true,"Last decode: %.1f tok/s (log)",g_ai_log_events[0].eval_tps);
        gui_text(ui,perf.x+9,top+53,ui->dim,false,"Completed timing - not live throughput");
    } else {
        gui_text(ui,perf.x+9,top+36,ui->dim,false,"No live token rate available");
        gui_text(ui,perf.x+9,top+53,ui->fg,false,"GPU %d%% | %.1f W",p->gpu_busy,p->power_w);
    }
    /* Host RAM prompt-state cache; only the cache-state log provides usage.
     * Never pass the full GPU VRAM reading as 'prompt cache used'. */
    const AiPromptCache *cache = &sys->ai_prompt_cache;
    bool maya_active=g_settings.log_parser==LOG_MAYA ||
        (g_settings.log_parser==LOG_AUTO && !strcmp(sys->ai_log_source,"Project Maya"));
    if (maya_active) {
        if(g_maya.vram_hit_valid)
            gui_text(ui,perf.x+9,top+82,ui->fg,false,"Maya VRAM expert hit %.1f%%",g_maya.vram_hit_pct);
        else gui_text(ui,perf.x+9,top+82,ui->dim,false,"Maya VRAM hit: not reported");
        if(g_maya.disk_valid)
            gui_text(ui,perf.x+9,top+101,ui->fg,false,"Disk %.1f reads/tok | %.1f ms/tok",
                     g_maya.disk_reads_per_token,g_maya.disk_wait_ms_per_token);
        else gui_text(ui,perf.x+9,top+101,ui->dim,false,"Disk reads/tok: not reported");
        if(g_maya.ram_fetch_valid)
            gui_text(ui,perf.x+9,top+119,ui->dim,false,"RAM fetches %.1f/tok | engine statistics",
                     g_maya.ram_fetches_per_token);
        else gui_text(ui,perf.x+9,top+119,ui->dim,false,"Maya stats: available when logged");
    } else if (cache->limit_valid && cache->limit_mib == 0) {
        gui_text(ui,perf.x+9,top+82,ui->dim,false,"Host prompt cache: disabled");
    } else if (cache->usage_valid && cache->limit_valid && cache->limit_mib > 0) {
        char cache_label[120];
        double pct = 100.0 * cache->used_mib / cache->limit_mib;
        snprintf(cache_label,sizeof(cache_label),"Prompt RAM (log) %.2f/%.2f GiB (%.0f%%)",
                 cache->used_mib/1024.0, cache->limit_mib/1024.0, pct);
        gui_bar_color(ui,(Rect){perf.x+9,top+71,perf.w-18,19},pct,cache_label,
                      pct >= 95 ? ui->bad : pct >= 85 ? ui->warn : ui->accent);
    } else if (cache->usage_valid) {
        gui_text(ui,perf.x+9,top+82,ui->fg,false,"Prompt RAM %.2f GiB | limit %s",
                 cache->used_mib/1024.0,cache->limit_valid?"unlimited":"unknown");
    } else if (cache->limit_valid && cache->limit_mib > 0) {
        gui_text(ui,perf.x+9,top+82,ui->dim,false,"Prompt RAM usage: -- / %.2f GiB",
                 cache->limit_mib/1024.0);
    } else {
        gui_text(ui,perf.x+9,top+82,ui->dim,false,"Prompt RAM cache: usage unavailable");
    }
    if (maya_active) {
        /* Project Maya has its own expert tiers; llama.cpp host prompt-cache
           counters are not meaningful and must not be shown here. */
    } else if (cache->usage_valid || cache->save_attempts || cache->matches ||
        cache->evictions || cache->rejected || cache->load_failures || cache->forced_reprocess) {
        if (cache->usage_valid)
            gui_text(ui,perf.x+9,top+101,ui->fg,false,"States %d | matches %d | evicts %d (%.1f GiB)",
                     cache->entries,cache->matches,cache->evictions,cache->evicted_mib/1024.0);
        else
            gui_text(ui,perf.x+9,top+101,ui->fg,false,"States -- | matches %d | evicts %d (%.1f GiB)",
                     cache->matches,cache->evictions,cache->evicted_mib/1024.0);
        gui_text(ui,perf.x+9,top+119,ui->dim,false,"Save tries %d | skip %d | fail %d | redo %d",
                 cache->save_attempts,cache->rejected,cache->load_failures,cache->forced_reprocess);
    } else {
        gui_text(ui,perf.x+9,top+101,ui->dim,false,"Cache events: unavailable from current log");
        gui_text(ui,perf.x+9,top+119,ui->dim,false,"Enable Bionic cache-state logging for stats");
    }
    const char *activity = sys->ai_model_activity[0] ? sys->ai_model_activity : "Idle";
    unsigned long activity_color = strcmp(activity,"Idle") == 0 ? ui->dim : ui->good;
    gui_text(ui,perf.x+9,top+139,activity_color,true,"Activity: %s",activity);
    if (sys->tokens.is_processing) {
        gui_text(ui,perf.x+9,top+158,ui->fg,false,"Prompt processed %lld / %lld tokens",
                 (long long)sys->tokens.n_prompt_tokens_processed,(long long)sys->tokens.n_prompt_tokens);
    } else if (sys->ai_log_event_count) {
        const AiLogEvent *ev = &g_ai_log_events[0];
        gui_text(ui,perf.x+9,top+158,ui->dim,false,"Task #%d | %s | P %.0f / D %.0f tok",
                 ev->task_id,ev->time_str,ev->prompt_tokens,ev->eval_tokens);
    } else {
        gui_text(ui,perf.x+9,top+158,ui->dim,false,"Counters: recent log tail, not lifetime");
    }

    panel_title(ui, log, "AI PERFORMANCE LOG");
    bool maya_log = g_settings.log_parser==LOG_MAYA ||
                    (g_settings.log_parser==LOG_AUTO && !strcmp(sys->ai_log_source,"Project Maya"));
    if(maya_log && g_maya.count) {
        for(int i=0;i<g_maya.count;i++) {
            gui_text(ui,log.x+9,top+36+i*42,ui->dim,false,"%.56s",g_maya.recent[i]);
        }
    } else if (sys->ai_log_event_count) {
        int n = sys->ai_log_event_count < 3 ? sys->ai_log_event_count : 3;
        for (int i=0; i<n; ++i) {
            const AiLogEvent *ev=&g_ai_log_events[i];
            int y=top+34+i*37;
            gui_text(ui,log.x+9,y,i==0?ui->fg:ui->dim,false,
                     "%s  task %d / slot %d  %s",ev->time_str,ev->task_id,ev->slot_id,
                     ev->has_total?"done":ev->has_eval?"decode":"prompt");
            gui_text(ui,log.x+9,y+16,ui->dim,false,
                     "Prompt %.0f @ %.1f/s | Decode %.0f @ %.1f/s",
                     ev->prompt_tokens,ev->prompt_tps,ev->eval_tokens,ev->eval_tps);
        }
    } else gui_text(ui,log.x+9,top+37,ui->dim,false,
                    maya_log?"No Maya engine lines in log tail":"No recent timing entries in log");
    if (sys->ai_log_path[0]) draw_button(ui, ai_log_button_rect(ui), "View Log", false);

    panel_title(ui, r[0], "LOCK / WATCHDOG");
    gui_text(ui,r[0].x+9,r[0].y+36,ui->fg,false,"%s",t->enabled?"ENFORCING":"PAUSED");
    gui_text(ui,r[0].x+9,r[0].y+54,ui->dim,false,"Target %s",t->policy);
    gui_text(ui,r[0].x+9,r[0].y+72,ui->dim,false,"%.0f Hz | adaptive %s | resets %llu",
             1000000.0/interval_us,adaptive?"on":"off",(unsigned long long)st->policy_mismatches);

    panel_title(ui, r[1], "MODEL INFO");
    if (maya_active) {
        gui_text(ui,r[1].x+9,r[1].y+35,ui->fg,false,"Project Maya engine log");
        gui_text(ui,r[1].x+9,r[1].y+52,ui->dim,false,"Engine stats: %lu entries in tail",g_maya.recognized);
        const char *base=strrchr(sys->ai_log_path,'/');
        gui_text(ui,r[1].x+9,r[1].y+71,ui->dim,false,"%.25s",base?base+1:
                 sys->ai_log_path[0]?sys->ai_log_path:"No log discovered");
    } else if (sys->ai_model_info_valid) {
        gui_text(ui,r[1].x+9,r[1].y+35,ui->fg,false,"Context %d",sys->ai_ctx_size);
        gui_text(ui,r[1].x+9,r[1].y+52,ui->fg,false,"KV %s / %s",sys->ai_cache_type_k,sys->ai_cache_type_v);
        gui_text(ui,r[1].x+9,r[1].y+69,ui->dim,false,"Flash %s | MTP %s",sys->ai_flash_attn?"on":"off",sys->ai_mtp?"on":"off");
    } else gui_text(ui,r[1].x+9,r[1].y+42,ui->dim,false,"No model data");

    panel_title(ui, r[2], "GPU CLIENTS / VRAM");
    const GpuClient *c = top_client(cm);
    if (c) {
        gui_text(ui,r[2].x+9,r[2].y+36,ui->fg,false,"%s",c->role[0]?c->role:c->name);
        gui_text(ui,r[2].x+9,r[2].y+54,ui->dim,false,"PID %d | activity %.1f%%",c->pid,c->activity_pct);
    } else gui_text(ui,r[2].x+9,r[2].y+36,ui->dim,false,"No active DRM clients");
    if (g->vram_bytes)
        gui_text(ui,r[2].x+9,r[2].y+74,ui->fg,false,"VRAM %.1f / %.1f GiB",p->vram_used/1073741824.0,g->vram_bytes/1073741824.0);

    panel_title(ui, r[3], "TOP CPU PROCESSES / 2s");
    if (sys->top_proc_count > 0) {
        for (int i=0; i<sys->top_proc_count && i<3; ++i)
            gui_text(ui,r[3].x+9,r[3].y+35+i*18,ui->fg,false,"#%d %5.1f%% %.17s",
                     i+1,sys->top_procs[i].cpu_pct,sys->top_procs[i].name);
    } else gui_text(ui,r[3].x+9,r[3].y+40,ui->dim,false,"No CPU activity in sample yet");

    panel_title(ui, r[4], "LAST EVENT");
    const DiagEvent *ev = event_last(el);
    if (ev) {
        gui_text(ui,r[4].x+9,r[4].y+36,ui->fg,false,"%s",ev->kind);
        gui_text(ui,r[4].x+9,r[4].y+54,ui->fg,false,"%s",ev->detail);
        gui_text(ui,r[4].x+9,r[4].y+73,ui->dim,false,"Actor %s (%.1f%%)",ev->actor,ev->actor_activity);
    } else gui_text(ui,r[4].x+9,r[4].y+40,ui->dim,false,"No diagnostic events");

    panel_title(ui,r[5],"AI NVME CACHE I/O");
    gui_text(ui,r[5].x+9,r[5].y+35,ui->fg,false,"Drive: %s",g_nvme.selected[0]?g_nvme.selected:"None selected");
    if(g_nvme.valid) {
        gui_text(ui,r[5].x+9,r[5].y+53,ui->accent,false,"Read %.1f | Write %.1f MiB/s",g_nvme.read_mib_s,g_nvme.write_mib_s);
        gui_text(ui,r[5].x+9,r[5].y+72,ui->dim,false,"Peak R %.0f W %.0f | Busy %.0f%%",
                 g_nvme.peak_read_mib_s,g_nvme.peak_write_mib_s,g_nvme.busy_pct);
    } else gui_text(ui,r[5].x+9,r[5].y+57,ui->dim,false,"Waiting for selected NVMe counters");
    gui_text(ui,r[5].x+9,r[5].y+89,ui->dim,false,"Whole drive traffic, not AI-only");
}

/* Main dashboard dispatcher */
static void draw_dashboard(Gui *ui, const Gpu *g, const Sysfs *s, const Target *t, const Stats *st,
                           int interval_us, bool adaptive, bool monitor_clocks, const ClientMonitor *cm,
                           const PowerTelemetry *p, const SessionInfo *si, const EventLog *el,
                           const TraceState *tr, const SystemTelemetry *sys) {
    if (g_workflow == WORKFLOW_GAMING) {
        draw_gaming_dashboard(ui, g, s, p, sys);
    } else {
        draw_ai_dashboard(ui, g, t, st, interval_us, adaptive, monitor_clocks, cm, p, si, el, tr, sys);
    }
}

static void draw_clients_page(Gui *ui, const ClientMonitor *cm) {
    int y = UI_PAGE_Y+16;
    gui_text(ui,16,y,ui->accent,true,"Per-process DRM activity"); y += 23;
    gui_text(ui,16,y,ui->dim,false,"PID    PROCESS / ROLE                  TOTAL   GFX  COMP   DMA MEDIA   VRAM"); y += 18;
    int max = cm->count < 14 ? cm->count : 14;
    for (int i=0;i<max;i++) {
        const GpuClient *c=&cm->items[i];
        gui_text(ui,16,y,ui->fg,false,"%-6d %-29.29s %6.1f %5.1f %5.1f %5.1f %5.1f %6.0fM",
                 c->pid,c->role[0]?c->role:c->name,c->activity_pct,c->gfx_pct,c->compute_pct,c->dma_pct,c->media_pct,c->vram_bytes/1048576.0);
        y += 18;
    }
    gui_text(ui,16,y+18,ui->dim,false,"Scan CPU %.3f%%  scans %llu  fdinfo reads %llu  errors %llu",
             cm->scan_cpu_pct,(unsigned long long)cm->scans,(unsigned long long)cm->fdinfo_reads,(unsigned long long)cm->scan_errors);
}

static void draw_power_page(Gui *ui, const Target *t, const Stats *st, const PowerTelemetry *p,
                            const Sysfs *s, const SystemTelemetry *sys,
                            bool monitor_clocks, int dpm_interval_us) {
    (void)st; (void)monitor_clocks; (void)dpm_interval_us;
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"ELECTRICAL / POWER telemetry (read-only)"); y+=30;
    gui_text(ui,x,y,ui->fg,false,"GPU board power: %s",p->power_w>=0?"available":"not reported"); y+=21;
    if(p->power_w>=0) {gui_text(ui,x,y,ui->fg,true,"Measured board power  %.2f W",p->power_w); y+=22;}
    if(sys->cpu_power_w>=0) {gui_text(ui,x,y,ui->fg,false,"CPU package power    %.2f W (hwmon)",sys->cpu_power_w); y+=22;}
    else {gui_text(ui,x,y,ui->dim,false,"CPU package power    unavailable"); y+=22;}
    gui_text(ui,x,y,ui->fg,false,"GPU power cap         %.1f W",p->power_cap_w); y+=22;
    gui_text(ui,x,y,ui->dim,false,"Firmware cap range     %.1f - %.1f W",p->power_cap_min_w,p->power_cap_max_w); y+=30;
    gui_text(ui,x,y,ui->accent,true,"Voltage / current"); y+=26;
    if(p->voltage_v>=0) gui_text(ui,x,y,ui->fg,false,"%s: %.4f V",s->voltage_label[0]?s->voltage_label:"GPU voltage",p->voltage_v);
    else {gui_text(ui,x,y,ui->dim,false,"GPU voltage: not exposed");} y+=22;
    if(p->current_a>=0) gui_text(ui,x,y,ui->fg,false,"%s current: %.2f A%s",p->current_label,p->current_a,
                                p->current_is_estimate?" (derived estimate)":" (sensor)");
    else {gui_text(ui,x,y,ui->dim,false,"GPU current: unavailable");} y+=22;
    if(p->equivalent_input_ohm>=0) {
        gui_text(ui,x,y,ui->dim,false,"Equivalent 12 V load: %.3f ohm (estimated)",p->equivalent_input_ohm); y+=22;
    }
    gui_text(ui,x,y,ui->accent,true,"Electrical control state"); y+=26;
    gui_text(ui,x,y,ui->fg,false,"Power profile: %.160s",p->power_profile[0]?p->power_profile:"unavailable"); y+=22;
    gui_text(ui,x,y,ui->fg,false,"Power policy: %s   watchdog: %s",t->policy,t->enabled?"enforcing":"paused"); y+=23;
    gui_text(ui,x,y,ui->dim,false,"Thermals, clocks, link and AI metrics have their own dedicated pages.");
}

static void draw_tuning_row(Gui *ui, int row, const char *name, const char *value,
                            const char *range, TuneZone zone, bool available) {
    Rect panel = tuning_tile_rect(ui, row);
    int y = panel.y;
    gui_fill(ui, panel, ui->panel);
    gui_outline(ui, panel, available ? tune_zone_color(ui, zone) : ui->dim);
    gui_text(ui, panel.x+9, y + 18, available ? ui->fg : ui->dim, true, "%s", name);
    gui_text(ui, panel.x+9, y + 40, available ? tune_zone_color(ui, zone) : ui->dim, true, "%s", value);
    ui->text_right = panel.x + panel.w - 9;
    gui_text(ui, panel.x+9, y + 62, ui->dim, false, "%s", range);
    ui->text_right = 0;
    if (available) {
        draw_button(ui, tuning_minus_rect(ui,row), "-", false);
        draw_button(ui, tuning_plus_rect(ui,row), "+", false);
    } else {
        Rect mr=tuning_minus_rect(ui,row), pr=tuning_plus_rect(ui,row);
        gui_fill(ui,mr,ui->panel_alt); gui_outline(ui,mr,ui->dim);
        gui_fill(ui,pr,ui->panel_alt); gui_outline(ui,pr,ui->dim);
        gui_text(ui,mr.x+11,mr.y+17,ui->dim,true,"-");
        gui_text(ui,pr.x+11,pr.y+17,ui->dim,true,"+");
    }
}

static void draw_tuning_page(Gui *ui, const TuningState *t) {
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"Software tuning - firmware bounded"); y+=28;
    gui_text(ui,x,y,ui->warn,true,
             "Green/amber/red = aggressiveness relative to captured stock, NOT a hardware-safety guarantee."); y+=26;

    if (t->spec) {
        gui_text(ui,x,y,ui->fg,true,"%s   %s / %s",t->spec->name,t->spec->series,t->spec->architecture); y+=23;
        gui_text(ui,x,y,ui->dim,false,
                 "AMD reference: game %d MHz   boost %d MHz   TBP %d W   %d MB %s @ %.1f Gbps / %d-bit",
                 t->spec->game_mhz,t->spec->boost_mhz,t->spec->tbp_w,t->spec->vram_mb,t->spec->memory_type,
                 t->spec->memory_mbps/1000.0,t->spec->memory_bus_bits); y+=23;
    } else {
        gui_text(ui,x,y,ui->dim,false,"No exact reference-card match; driver/firmware ranges remain fully authoritative."); y+=23;
    }
    gui_text(ui,x,UI_PAGE_Y+99,ui->dim,false,"Built-in reference database: %zu desktop Radeon models.",gpu_spec_count());

    char value[96], range[160];
    TuneZone z;

    if (t->has_sclk) {
        snprintf(value,sizeof(value),"%ld MHz",t->edit_sclk_max_mhz);
        snprintf(range,sizeof(range),"stock %ld | firmware %ld..%ld MHz",
                 t->default_sclk_max_mhz,t->sclk_limit_min_mhz,t->sclk_limit_max_mhz);
        z=tuning_zone_long(t->edit_sclk_max_mhz,t->default_sclk_max_mhz,t->sclk_limit_max_mhz);
        draw_tuning_row(ui,0,"GFX MAX CLOCK",value,range,z,true);
    } else if (t->has_sclk_offset) {
        snprintf(value,sizeof(value),"%+ld MHz",t->edit_sclk_offset_mhz);
        snprintf(range,sizeof(range),"stock %+ld | firmware %+ld..%+ld MHz",
                 t->default_sclk_offset_mhz,t->sclk_offset_min_mhz,t->sclk_offset_max_mhz);
        z=tuning_zone_offset(t->edit_sclk_offset_mhz,t->sclk_offset_max_mhz);
        draw_tuning_row(ui,0,"GFX CLOCK OFFSET",value,range,z,true);
    } else {
        draw_tuning_row(ui,0,"GFX CLOCK","unavailable","driver exposes no supported modern OD clock control",TUNE_ZONE_UNAVAILABLE,false);
    }

    if (t->has_mclk) {
        snprintf(value,sizeof(value),"%ld MHz controller",t->edit_mclk_max_mhz);
        snprintf(range,sizeof(range),"stock %ld | firmware %ld..%ld MHz",
                 t->default_mclk_max_mhz,t->mclk_limit_min_mhz,t->mclk_limit_max_mhz);
        z=tuning_zone_long(t->edit_mclk_max_mhz,t->default_mclk_max_mhz,t->mclk_limit_max_mhz);
        draw_tuning_row(ui,1,"MEMORY MAX CLOCK",value,range,z,true);
    } else {
        draw_tuning_row(ui,1,"MEMORY CLOCK","unavailable","driver exposes no supported modern MCLK control",TUNE_ZONE_UNAVAILABLE,false);
    }

    if (t->has_vdd_offset) {
        snprintf(value,sizeof(value),"%+ld mV",t->edit_vdd_offset_mv);
        snprintf(range,sizeof(range),"stock %+ld | firmware %+ld..%+ld mV",
                 t->default_vdd_offset_mv,t->vdd_offset_min_mv,t->vdd_offset_max_mv);
        z=tuning_zone_offset(t->edit_vdd_offset_mv,t->vdd_offset_max_mv);
        draw_tuning_row(ui,2,"VDDGFX OFFSET",value,range,z,true);
    } else {
        draw_tuning_row(ui,2,"VDDGFX OFFSET","unavailable","not exposed by this ASIC/driver",TUNE_ZONE_UNAVAILABLE,false);
    }

    if (t->has_power_cap) {
        snprintf(value,sizeof(value),"%.0f W",t->edit_power_cap_w);
        snprintf(range,sizeof(range),"stock %.0f | firmware %.0f..%.0f W%s%d%s",
                 t->default_power_cap_w,t->power_cap_min_w,t->power_cap_max_w,
                 t->spec?" | AMD ref TBP ":"",t->spec?t->spec->tbp_w:0,t->spec?" W":"");
        z=tuning_zone_double(t->edit_power_cap_w,t->default_power_cap_w,t->power_cap_max_w);
        draw_tuning_row(ui,3,"POWER CAP",value,range,z,true);
    } else {
        draw_tuning_row(ui,3,"POWER CAP","unavailable","power1_cap is not exposed by this driver/board",TUNE_ZONE_UNAVAILABLE,false);
    }

    if (t->has_fan) {
        snprintf(value,sizeof(value),"%ld%% (%d RPM)",
                 t->fan_pwm_max > 0 ? (t->edit_fan_pwm * 100 / t->fan_pwm_max) : 0,
                 t->fan_rpm);
        snprintf(range,sizeof(range),"PWM %ld..%ld | current %ld",
                 (long)0,t->fan_pwm_max,t->fan_pwm);
        draw_tuning_row(ui,4,"FAN SPEED",value,range,TUNE_ZONE_SAFE,true);
    } else {
        draw_tuning_row(ui,4,"FAN SPEED","unavailable","fan PWM not exposed by this driver/board",TUNE_ZONE_UNAVAILABLE,false);
    }

    char arm[64];
    snprintf(arm,sizeof(arm),"ARM: %s",t->armed?"ON":"OFF");
    Rect safe=tuning_tile_rect(ui,5);
    panel_title(ui,safe,"SAFETY / WORKFLOW");
    draw_button(ui,tuning_action_rect(ui,0),arm,t->armed);
    draw_button(ui,tuning_action_rect(ui,1),"APPLY",false);
    draw_button(ui,tuning_action_rect(ui,2),"RESET STOCK",false);
    draw_button(ui,tuning_action_rect(ui,3),"REFRESH",false);
    gui_text(ui,UI_MARGIN,747,ui->warn,false,
             "ARM then APPLY required. RESET STOCK changes device settings; firmware limits apply.");
    gui_text(ui,UI_MARGIN,769,ui->dim,false,
             "Clock/voltage: pp_od_clk_voltage; power cap: hwmon. Unsupported settings are read-only.");
}

static void draw_desktop_page(Gui *ui, const SessionInfo *si) {
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"Desktop / X server software & display state"); y+=28;
    gui_text(ui,x,y,ui->fg,false,"Session: %s  |  X display: %s",si->session_type,si->display); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Session manager: %s   built %s",si->session_version[0]?si->session_version:"unknown",
             si->session_built[0]?si->session_built:"unknown");y+=24;
    gui_text(ui,x,y,ui->fg,false,"X server: %s   built %s",si->xserver_version,si->xserver_built[0]?si->xserver_built:"unknown");y+=24;
    gui_text(ui,x,y,ui->dim,false,"X vendor: %.110s",si->xserver_vendor);y+=24;
    gui_text(ui,x,y,ui->fg,false,"Desktop %s: %s   built %s",si->desktop,
             si->desktop_version[0]?si->desktop_version:"unknown",si->desktop_built[0]?si->desktop_built:"unknown");y+=24;
    gui_text(ui,x,y,ui->fg,false,"WM %s: %s   built %s",si->window_manager,
             si->wm_version[0]?si->wm_version:"unknown",si->wm_built[0]?si->wm_built:"unknown");y+=24;
    gui_text(ui,x,y,ui->fg,false,"Compositor %s: %s   built %s",si->compositor,
             si->compositor_version[0]?si->compositor_version:"unknown",si->compositor_built[0]?si->compositor_built:"unknown");y+=22;
    gui_text(ui,x,y,ui->dim,false,"Compositor PID %d  |  detector: %s",si->compositor_pid,
             g_compositor_names[g_settings.compositor_source]); y+=34;
    gui_text(ui,x,y,ui->accent,true,"Display timing / presentation"); y+=27;
    gui_text(ui,x,y,ui->fg,false,"Refresh rate: %s",si->refresh);y+=22;
    gui_text(ui,x,y,ui->fg,false,"Active modes: %.155s",si->kms_modes);y+=22;
    gui_text(ui,x,y,ui->fg,false,"VRR / FreeSync / G-Sync: %s",si->vrr_state);y+=22;
    gui_text(ui,x,y,ui->dim,false,"VSync: %s",si->vsync_state);y+=22;
    gui_text(ui,x,y,ui->dim,false,"Buffering: %s",si->buffer_state);y+=28;
    gui_text(ui,x,y,ui->accent,true,"Outputs / KMS");y+=25;
    gui_text(ui,x,y,ui->fg,false,"%d connected: %.155s",si->connected_outputs,si->connector_summary);y+=22;
    gui_text(ui,x,y,ui->fg,false,"Active planes %d; allocators: %.150s",si->active_planes,si->plane_allocators);y+=28;
    gui_text(ui,x,y,ui->dim,false,"Version/build dates are from installed Arch packages (when available).");y+=21;
    gui_text(ui,x,y,ui->dim,false,"Refresh/VRR reflect DRM reports, not per-game presentation guarantees.");
}

static void draw_events_page(Gui *ui, const EventLog *el) {
    int y=UI_PAGE_Y+16;
    gui_text(ui,16,y,ui->accent,true,"Diagnostic flight recorder - newest first"); y+=24;
    int printed=0;
    for(int n=0;n<el->count && printed<15;n++) {
        int idx=el->head-1-n; while(idx<0) idx+=MAX_DIAG_EVENTS;
        const DiagEvent *ev=&el->ring[idx];
        /* High-frequency samples do not displace the meaningful fault/control history. */
        if(!strcmp(ev->kind,"DPM CHANGE") || !strcmp(ev->kind,"GFXOFF")) continue;
        gui_text(ui,16,y,ui->fg,false,"%2d  %-12.12s %-42.42s actor %-14.14s %5.1f%%",
                 ++printed,ev->kind,ev->detail,ev->actor,ev->actor_activity);
        y+=18;
    }
    gui_text(ui,16,y+20,ui->dim,false,"Last 15 major events | Total %llu   CSV logging %s%s%s",
             (unsigned long long)el->total,el->logging?"ON":"off",el->logging?"  ":"",el->logging?el->log_path:"");
}

static void draw_trace_page(Gui *ui, const TraceState *tr) {
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"Kernel tracefs diagnostics"); y+=30;
    gui_text(ui,x,y,ui->fg,false,"Available: %s   Active: %s   total lines %llu",tr->available?"yes":"no",tr->active?"YES":"no",(unsigned long long)tr->lines); y+=24;
    gui_text(ui,x,y,ui->fg,false,"AMDGPU events %d   scheduler events %d   DRM events %d",tr->amdgpu_events,tr->drm_sched_events,tr->drm_events); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Lines: amdgpu %llu   scheduler %llu   drm/display %llu",
             (unsigned long long)tr->amdgpu_lines,(unsigned long long)tr->sched_lines,(unsigned long long)tr->drm_lines); y+=28;
    gui_text(ui,x,y,ui->fg,false,"Last actor: %s",tr->last_actor[0]?tr->last_actor:"none"); y+=24;
    gui_text(ui,x,y,ui->dim,false,"Last trace: %.150s",tr->last_line[0]?tr->last_line:"none"); y+=36;
    gui_text(ui,x,y,ui->warn,true,"T toggles tracing. It is OFF by default because tracing is diagnostic, not normal monitoring.");
}

static Rect settings_option_rect(const Gui *ui,int row) {
    return (Rect){ui->width-UI_MARGIN-272,UI_PAGE_Y+41+row*59,272,24};
}
static Rect settings_path_rect(const Gui *ui) {return settings_option_rect(ui,3);}
static Rect settings_gpu_switch_rect(const Gui *ui) {
    (void)ui;
    return (Rect){UI_MARGIN,UI_PAGE_Y+41+8*59,112,24};
}
static Rect settings_unlock_rect(const Gui *ui) {return settings_option_rect(ui,7);}
static void draw_settings_page(Gui *ui,const Gpu *g,const Gpu *gpus,int ngpu) {
    int x=UI_MARGIN;
    static const char *const sources[]={"Power heuristic","llama.cpp API","Ollama API","vLLM API","LM Studio API","Project Maya (log)"};
    gui_text(ui,x,UI_PAGE_Y+16,ui->accent,true,"SETTINGS / CONFIGURATION");
    for(int i=0;i<8;i++) {
        Rect r=settings_option_rect(ui,i);
        const char *label=i==0?"AI DATA SOURCE":i==1?"AI LOG PARSER":i==2?"AI LOG DISCOVERY":
                          i==3?"AI LOG FILENAME":i==4?"AI NVME CACHE SSD":i==5?"COMPOSITOR SOURCE":
                          i==6?"AMD GPU SELECTOR":"OVERCLOCK UNLOCK";
        gui_text(ui,x,r.y+17,ui->fg,true,"%s",label);
    }
    draw_button(ui,settings_option_rect(ui,0),sources[g_settings.token_source],false);
    draw_button(ui,settings_option_rect(ui,1),g_parser_names[g_settings.log_parser],false);
    draw_button(ui,settings_option_rect(ui,2),g_settings.ai_log_manual?"Manual file":"Auto detect newest",false);
    draw_button(ui,settings_path_rect(ui),g_edit_ai_path?"Editing (Enter to save)":"EDIT FILENAME",g_edit_ai_path);
    ui->text_right=ui->width-UI_MARGIN;
    gui_text(ui,x,settings_option_rect(ui,3).y+43,ui->dim,false,"Path: %.190s",
             g_edit_ai_path?g_edit_path_buffer:g_settings.ai_log_file[0]?g_settings.ai_log_file:"(not configured)");
    ui->text_right=0;
    char nv_label[190];
    snprintf(nv_label,sizeof(nv_label),"%s",g_nvme.selected[0]?g_nvme.selected:"No NVMe found");
    for(int i=0;i<g_nvme.device_count;i++) if(!strcmp(g_nvme.selected,g_nvme.devices[i].name)) {
        snprintf(nv_label,sizeof(nv_label),"%s | %.65s",g_nvme.selected,g_nvme.devices[i].model);
        break;
    }
    draw_button(ui,settings_option_rect(ui,4),nv_label,false);
    draw_button(ui,settings_option_rect(ui,5),g_compositor_names[g_settings.compositor_source],false);
    int desired=g_requested_gpu>=0?g_requested_gpu:g->card_num;
    char card[160];
    snprintf(card,sizeof(card),"card%d (unavailable)",desired);
    for(int i=0;i<ngpu;i++) if(gpus[i].card_num==desired)
        snprintf(card,sizeof(card),"card%d - %.80s",desired,gpus[i].full_name);
    draw_button(ui,settings_option_rect(ui,6),card,false);
    gui_text(ui,x,settings_option_rect(ui,6).y+44,ui->dim,false,"%d card(s) detected  |  active: card%d",ngpu,g->card_num);
    draw_button(ui,settings_unlock_rect(ui),"LOCKED - planned feature",false);
    draw_button(ui,settings_gpu_switch_rect(ui),"APPLY GPU",false);
    gui_text(ui,x+126,settings_gpu_switch_rect(ui).y+17,ui->warn,false,
             "Rebind pauses watchdog, clears clock locks, disarms tuning.");
    int y=settings_gpu_switch_rect(ui).y+51;
    gui_text(ui,x,y,ui->dim,false,"Configuration: ~/.config/amd-gpu-diagnostic/settings.ini (invoking user)");y+=22;
    gui_text(ui,x,y,ui->dim,false,"Project Maya engine lines and llama.cpp/Bionic timing supported.");y+=22;
    gui_text(ui,x,y,ui->dim,false,"Manual log file requires an absolute readable path. Changes auto-save.");
}

static void draw_help_page(Gui *ui) {
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"Keyboard / mouse controls"); y+=30;
    const char *lines[] = {
        "1 Auto    2 Low    3 High    4 Standard    5 Min-GFX    6 Min-MEM    7 Peak    8 Manual",
        "0 Home  P Clients  G Power  U Tuning  W Desktop  E Events  R Trace  F Settings  H Help  I About",
        "Space pause/resume watchdog    A adaptive burst    C DPM clock telemetry",
        "+ faster watchdog    - slower watchdog    D cycle DPM telemetry rate",
        "S cycle manual GFX level    M cycle manual memory level    X clear manual level locks",
        "U tuning page    T kernel trace on/off    L CSV event logging on/off    Q quit",
        "Tuning writes require ARM then APPLY. RESET STOCK uses driver reset and captured stock power cap.",
        "All tabs, power modes, and toggles are clickable. Help is the small ? tab.",
        "The GUI uses plain Xlib: no GTK, Qt, ncurses, Electron, XCB, or libdrm dependency."
    };
    for(size_t i=0;i<sizeof(lines)/sizeof(lines[0]);i++) { gui_text(ui,x,y,ui->fg,false,"%s",lines[i]); y+=28; }
}

static void draw_about_page(Gui *ui) {
    int x=UI_MARGIN,y=UI_PAGE_Y+16;
    gui_text(ui,x,y,ui->accent,true,"AMD GPU Performance & Diagnostic Tool"); y+=28;
    gui_text(ui,x,y,ui->fg,true,"v%s  |  beta release  |  compiled %s",VERSION,__DATE__); y+=24;
    gui_text(ui,x,y,ui->fg,false,"Developer: kevito666   |   GitHub: github.com/kevito666"); y+=29;
    gui_text(ui,x,y,ui->accent,true,"How it started"); y+=25;
    gui_text(ui,x,y,ui->fg,false,"What started as a GPU coil-whine reducer / power-state controller"); y+=21;
    gui_text(ui,x,y,ui->fg,false,"morphed into a full-blown AI and gaming desktop monitoring"); y+=21;
    gui_text(ui,x,y,ui->fg,false,"and configuration tool!"); y+=27;
    gui_text(ui,x,y,ui->fg,true,"Built in Tohoku, Japan."); y+=32;
    gui_text(ui,x,y,ui->accent,true,"Technology"); y+=25;
    gui_text(ui,x,y,ui->fg,false,"C / Xlib / AMDGPU sysfs / procfs / debugfs / tracefs / DRM fdinfo"); y+=21;
    gui_text(ui,x,y,ui->fg,false,"Lightweight X11/XLibre; no GTK, Qt or Electron."); y+=28;
    gui_text(ui,x,y,ui->accent,true,"License"); y+=25;
    gui_text(ui,x,y,ui->fg,true,"SPDX: GPL-3.0-or-later"); y+=21;
    gui_text(ui,x,y,ui->fg,false,"GNU General Public License, version 3 or (at your option) later."); y+=22;
    gui_text(ui,x,y,ui->dim,false,"Copyright (C) 2026 Kevin Kolasa. See LICENSE in the source archive."); y+=26;
    gui_text(ui,x,y,ui->warn,false,"Overclock unlock remains disabled in this beta.");
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

static int gui_max_scroll(const Gui *ui, View view);

static void gui_draw(Gui *ui, const Gpu *g, Sysfs *s, const Target *t, const Stats *st,
                     int interval_us, int dpm_interval_us, bool monitor_clocks, bool adaptive,
                     const ClientMonitor *clients, const PowerTelemetry *power, const SystemTelemetry *system, const SessionInfo *session,
                     const EventLog *events, const TraceState *trace, const TuningState *tuning,
                     View view, const char *message) {
    gui_fill(ui,(Rect){0,0,ui->width,ui->height},ui->bg);
    draw_header(ui,g,s,t,power,system,st);
    draw_tabs(ui,view);
    ui->draw_offset_y = UI_CONTENT_TOP - UI_PAGE_Y - ui->scroll_y;
    XRectangle clip = {(short)0, (short)UI_CONTENT_TOP, (unsigned short)ui->width,
                       (unsigned short)gui_content_height(ui)};
    XSetClipRectangles(ui->dpy,ui->gc,0,0,&clip,1,Unsorted);
    ui->text_right = 0;
    switch(view) {
        case VIEW_DASHBOARD: draw_dashboard(ui,g,s,t,st,interval_us,adaptive,monitor_clocks,clients,power,session,events,trace,system); break;
        case VIEW_CLIENTS: draw_clients_page(ui,clients); break;
        case VIEW_POWER: draw_power_page(ui,t,st,power,s,system,monitor_clocks,dpm_interval_us); break;
        case VIEW_TUNING: draw_tuning_page(ui,tuning); break;
        case VIEW_DESKTOP: draw_desktop_page(ui,session); break;
        case VIEW_EVENTS: draw_events_page(ui,events); break;
        case VIEW_TRACE: draw_trace_page(ui,trace); break;
        case VIEW_SETTINGS: draw_settings_page(ui,g,g_gpu_inventory,g_gpu_inventory_count); break;
        case VIEW_HELP: draw_help_page(ui); break;
        case VIEW_ABOUT: draw_about_page(ui); break;
    }
    XSetClipMask(ui->dpy,ui->gc,None);
    ui->draw_offset_y = 0;
    ui->text_right = 0;
    if(message && *message) gui_text(ui,UI_MARGIN,ui->height-45,ui->warn,false,"%s",message);
    draw_bottom_controls(ui,t,adaptive,monitor_clocks,trace,events);
    /* Subtle vertical scroll thumb; content continues below when this is visible. */
    int max_scroll = gui_max_scroll(ui, view);
    if (max_scroll > 0) {
        int track_h = gui_content_height(ui) - 10;
        int thumb_h = (track_h * gui_content_height(ui)) /
                      (gui_content_height(ui) + max_scroll);
        if (thumb_h < 24) thumb_h = 24;
        int thumb_y = UI_CONTENT_TOP + 5 +
                      (track_h - thumb_h) * ui->scroll_y / max_scroll;
        gui_fill(ui,(Rect){ui->width-6,UI_CONTENT_TOP+5,3,track_h},ui->panel_alt);
        gui_fill(ui,(Rect){ui->width-6,thumb_y,3,thumb_h},ui->accent);
    }
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
    /* Only commit the target after both driver writes succeed. A rejected
     * level must not leave the watchdog enforcing a phantom manual target. */
    if (apply_policy(s,"manual") < 0) {
        snprintf(message,cap,"Cannot enter manual mode for %s: %s",sclk?"GFX":"memory",strerror(errno));
        return -1;
    }
    if (apply_level(wfd,wanted) < 0) {
        int saved = errno;
        (void)apply_policy(s,t->policy); /* best-effort driver rollback */
        snprintf(message,cap,"Driver rejected manual %s level %d: %s",sclk?"GFX":"memory",wanted,strerror(saved));
        return -1;
    }
    snprintf(t->policy,sizeof(t->policy),"manual");
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
    printf("AMD GPU Performance and Diagnostic Tool %s - X11/XLibre GUI\n",VERSION);
    printf("Usage: %s [--card N] [--interval-us N] [--dpm-interval-us N] [--mode MODE]\n",argv0);
    printf("          [--monitor-dpm] [--fixed] [--unsafe-fast] [--normal-priority] [--benchmark]\n");
    printf("Modes: auto low high manual profile_standard profile_min_sclk profile_min_mclk profile_peak\n");
    printf("Launch GUI through sudo with X11 credentials preserved: sudo -E %s\n",argv0);
}

static void set_mode(Sysfs *s, Target *t, int idx, char *message, size_t cap) {
    if(idx<0||idx>=(int)POLICY_COUNT)return;
    const char *requested = POLICIES[idx];
    if (apply_policy(s,requested) == 0) {
        /* Desired policy now matches the driver-accepted write. */
        text_copy(t->policy,sizeof(t->policy),requested);
        if(strcmp(requested,"manual")!=0){t->lock_sclk=false;t->lock_mclk=false;}
        snprintf(message,cap,"Target changed to %s",t->policy);
    } else {
        snprintf(message,cap,"Failed to apply %s: %s",requested,strerror(errno));
    }
}

static void handle_action(char key, Sysfs *s, Target *target, int *interval_us, int *dpm_interval_us,
                          bool *monitor_clocks, bool *adaptive, long long *burst_until_ns,
                          TraceState *trace, EventLog *events, View *view, char *message, size_t message_cap) {
    message[0]='\0';
    if(key>='1'&&key<='8') set_mode(s,target,key-'1',message,message_cap);
    else if(key=='0') *view=VIEW_DASHBOARD;
    else if(key=='p'||key=='P') *view=VIEW_CLIENTS;
    else if(key=='g'||key=='G') *view=VIEW_POWER;
    else if(key=='u'||key=='U') *view=VIEW_TUNING;
    else if(key=='w'||key=='W') *view=VIEW_DESKTOP;
    else if(key=='e'||key=='E') *view=VIEW_EVENTS;
    else if(key=='r'||key=='R') *view=VIEW_TRACE;
    else if(key=='h'||key=='H'||key=='?') *view=VIEW_HELP;
    else if(key=='f'||key=='F') *view=VIEW_SETTINGS;
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

/* Full logical content extent per page. All content retains its original coordinates. */
static int gui_page_end(View view) {
    switch (view) {
        case VIEW_DASHBOARD: return g_workflow == WORKFLOW_GAMING ? UI_PAGE_Y+4*103+8 : UI_PAGE_Y+180+7+2*103+8;
        case VIEW_CLIENTS: return 760;
        case VIEW_POWER: return 1020;
        case VIEW_TUNING: return 797;
        case VIEW_DESKTOP: return UI_PAGE_Y+560;
        case VIEW_EVENTS: return UI_PAGE_Y+15*18+75;
        case VIEW_SETTINGS: return UI_PAGE_Y+41+8*59+125;
        case VIEW_TRACE: return 670;
        case VIEW_HELP: return 715;
        case VIEW_ABOUT: return UI_PAGE_Y+430;
        default: return 745;
    }
}

static int gui_max_scroll(const Gui *ui, View view) {
    int excess = gui_page_end(view) - UI_PAGE_Y - gui_content_height(ui);
    return excess > 0 ? excess : 0;
}

static void gui_scroll(Gui *ui, View view, int delta) {
    ui->scroll_y += delta;
    if (ui->scroll_y < 0) ui->scroll_y = 0;
    int max = gui_max_scroll(ui, view);
    if (ui->scroll_y > max) ui->scroll_y = max;
    ui->dirty = true;
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

static bool tuning_row_available(const TuningState *t, int row) {
    switch (row) {
        case 0: return t->has_sclk || t->has_sclk_offset;
        case 1: return t->has_mclk;
        case 2: return t->has_vdd_offset;
        case 3: return t->has_power_cap;
        case 4: return t->has_fan;
        default: return false;
    }
}

/* Launch viewer without a root shell or shell interpolation of filenames. */
static bool open_ai_log_viewer(const char *path) {
    if (!path || path[0] != '/') return false;
    pid_t starter = fork();
    if (starter < 0) return false;
    if (starter == 0) {
        pid_t viewer = fork();
        if (viewer < 0) _exit(127);
        if (viewer > 0) _exit(0); /* double fork: no retained GUI zombie */
        const char *sudo_user = getenv("SUDO_USER");
        if (geteuid() == 0) {
            /* Never launch xdg-open with root privileges. Require a genuine
             * invoking-user identity, even on a direct-root invocation. */
            if (!sudo_user || !*sudo_user) _exit(126);
            struct passwd *pw = getpwnam(sudo_user);
            if (!pw || pw->pw_uid == 0 || setgroups(0, NULL) != 0 ||
                setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) _exit(126);
            (void)setenv("HOME", pw->pw_dir, 1);
        }
        execlp("xdg-open", "xdg-open", path, (char *)NULL);
        _exit(127);
    }
    (void)waitpid(starter, NULL, 0);
    return true;
}

static void handle_click(Gui *ui, int x, int y, Sysfs *s, Target *target, TuningState *tuning,
                         int *interval_us, int *dpm_interval_us, bool *monitor_clocks, bool *adaptive,
                         long long *burst_until_ns, TraceState *trace, EventLog *events,
                         View *view, char *message, size_t message_cap) {
    /* Workflow mode buttons */
    if(point_in(workflow_rect(ui, 0), x, y)) {
        g_workflow = WORKFLOW_AI;
        ui->scroll_y = 0;
        snprintf(message, message_cap, "AI Workflow mode");
        return;
    }
    if(point_in(workflow_rect(ui, 1), x, y)) {
        g_workflow = WORKFLOW_GAMING;
        ui->scroll_y = 0;
        snprintf(message, message_cap, "Gaming Workflow mode");
        return;
    }

    if(*view==VIEW_SETTINGS && y>=UI_CONTENT_TOP && y<gui_content_bottom(ui)) {
        int ly=y-(UI_CONTENT_TOP-UI_PAGE_Y-ui->scroll_y);
        if(point_in(settings_option_rect(ui,0),x,ly)) {
            g_settings.token_source=(TokenSource)((g_settings.token_source+1)%TOKEN_SOURCE_COUNT);
            g_token_source=g_settings.token_source;
        } else if(point_in(settings_option_rect(ui,1),x,ly)) {
            g_settings.log_parser=(LogParser)((g_settings.log_parser+1)%LOG_COUNT);
        } else if(point_in(settings_option_rect(ui,2),x,ly)) {
            g_settings.ai_log_manual=!g_settings.ai_log_manual;
        } else if(point_in(settings_path_rect(ui),x,ly)) {
            g_edit_ai_path=true;
            text_copy(g_edit_path_buffer,sizeof(g_edit_path_buffer),g_settings.ai_log_file);
            snprintf(message,message_cap,"Type an absolute path; Enter saves, Esc cancels.");
            return;
        } else if(point_in(settings_option_rect(ui,4),x,ly)) {
            if(g_nvme.device_count) {
                int idx=-1;
                for(int i=0;i<g_nvme.device_count;i++) if(!strcmp(g_nvme.selected,g_nvme.devices[i].name))idx=i;
                nvme_select(&g_nvme,g_nvme.devices[(idx+1)%g_nvme.device_count].name);
                text_copy(g_settings.ai_nvme_device,sizeof(g_settings.ai_nvme_device),g_nvme.selected);
            }
        } else if(point_in(settings_option_rect(ui,5),x,ly)) {
            g_settings.compositor_source=(CompositorSource)((g_settings.compositor_source+1)%COMP_COUNT);
        } else if(point_in(settings_option_rect(ui,6),x,ly)) {
            int pos=0;
            for(int i=0;i<g_gpu_inventory_count;i++) if(g_gpu_inventory[i].card_num==g_requested_gpu) pos=i;
            if(g_gpu_inventory_count>0) g_requested_gpu=g_gpu_inventory[(pos+1)%g_gpu_inventory_count].card_num;
        } else if(point_in(settings_unlock_rect(ui),x,ly)) {
            snprintf(message,message_cap,"Overclock unlock is not implemented in v0.7.0b (safe placeholder).");
            return;
        } else if(point_in(settings_gpu_switch_rect(ui),x,ly)) {
            g_gpu_switch_requested=true;
            snprintf(message,message_cap,"GPU switch requested; tuning will be disarmed.");
            return;
        } else return;
        if(settings_save()) snprintf(message,message_cap,"Settings saved.");
        else snprintf(message,message_cap,"Could not save settings to user config directory.");
        return;
    }

    /* View AI log button */
    if (g_ai_log_path[0] != 0 && *view == VIEW_DASHBOARD && g_workflow == WORKFLOW_AI) {
        Rect log_btn = ai_log_button_rect(ui);
        int content_y = y - (UI_CONTENT_TOP - UI_PAGE_Y - ui->scroll_y);
        if (y >= UI_CONTENT_TOP && y < gui_content_bottom(ui) && point_in(log_btn, x, content_y)) {
            snprintf(message, message_cap, "%s", open_ai_log_viewer(g_ai_log_path) ?
                     "Opening AI log" : "Unable to start AI log viewer");
            return;
        }
    }

    if(point_in(help_rect(ui),x,y)) { *view=VIEW_HELP; ui->scroll_y=0; message[0]=0; return; }
    for(int i=0;i<9;i++) {
        if(point_in(tab_rect(ui,i),x,y)) {
            *view=(View)i;
            ui->scroll_y=0;
            message[0]=0;
            return;
        }
    }
    for(int i=0;i<8;i++) {
        if(point_in(mode_rect(ui,i),x,y)) {
            set_mode(s,target,i,message,message_cap);
            return;
        }
    }

    if (*view == VIEW_TUNING && y >= UI_CONTENT_TOP && y < gui_content_bottom(ui)) {
        int logical_y = y - (UI_CONTENT_TOP - UI_PAGE_Y - ui->scroll_y);
        for (int row=0; row<5; row++) {
            if (point_in(tuning_minus_rect(ui,row),x,logical_y) || point_in(tuning_plus_rect(ui,row),x,logical_y)) {
                if (!tuning_row_available(tuning,row)) {
                    snprintf(message,message_cap,"That tuning control is not exposed by this ASIC/driver.");
                    return;
                }
                int dir=point_in(tuning_minus_rect(ui,row),x,logical_y)?-1:+1;
                tuning_adjust(tuning,row,dir);
                snprintf(message,message_cap,"Tuning edit changed; ARM + APPLY required to write.");
                return;
            }
        }
        if (point_in(tuning_action_rect(ui,0),x,logical_y)) {
            tuning->armed = !tuning->armed;
            snprintf(message,message_cap,"Tuning writes %s.",tuning->armed?"ARMED":"disarmed");
            return;
        }
        if (point_in(tuning_action_rect(ui,1),x,logical_y)) {
            (void)tuning_apply(s,target,tuning,message,message_cap);
            return;
        }
        if (point_in(tuning_action_rect(ui,2),x,logical_y)) {
            (void)tuning_reset(s,target,tuning,message,message_cap);
            return;
        }
        if (point_in(tuning_action_rect(ui,3),x,logical_y)) {
            tuning_refresh(s,tuning,false);
            snprintf(message,message_cap,"Tuning state refreshed from driver.");
            return;
        }
    }

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
    settings_load();
    if(g_settings.ai_nvme_device[0]) nvme_select(&g_nvme,g_settings.ai_nvme_device);
    nvme_discover(&g_nvme);
    requested_card=g_settings.preferred_card;

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
    g_gpu_inventory_count=ngpu>0?ngpu:0;
    if(ngpu>0) memcpy(g_gpu_inventory,gpus,(size_t)ngpu*sizeof(Gpu));
    if(ngpu<=0){fprintf(stderr,"No AMD DRM GPUs found.\n");return 1;}
    int gi=choose_gpu(gpus,ngpu,requested_card);
    if(gi<0){fprintf(stderr,"Requested AMD GPU not found.\n");return 1;}
    Gpu *g=&gpus[gi]; Sysfs s;
    g_requested_gpu=g->card_num;
    if(sysfs_open(g,&s)<0){fprintf(stderr,"Cannot open AMDGPU controls: %s\n",strerror(errno));return 1;}
    if(apply_policy(&s,target.policy)<0){fprintf(stderr,"Cannot set initial policy %s: %s\n",target.policy,strerror(errno));sysfs_close(&s);return 1;}
    if(bench_at_start){run_benchmark_cli(&s);sysfs_close(&s);return 0;}

    Stats st={0}; st.last_active_sclk=st.last_active_mclk=INT_MIN;
    ClientMonitor clients={0};
    PowerTelemetry power={.gpu_busy=-1,.mem_busy=-1,.temp_c=-1,.power_w=-1,.power_cap_w=-1,.power_cap_min_w=-1,.power_cap_max_w=-1,
                          .voltage_v=-1,.current_a=-1,.equivalent_input_ohm=-1,.fan_rpm=-1,.fan_pct=-1,
                          .gfx_hz=-1,.mem_hz=-1,.gfxoff_enabled=-1,.gfxoff_status=-1,.last_gfxoff_status=-1};
    for (int i=0;i<MAX_GPU_TEMPS;i++) power.temps_c[i]=-1.0;
    SystemTelemetry system={0};
    system.cpu_power_w=-1;
    system.fd_cpu_temp=-1;
    system.fd_cpu_tctl=-1;
    system.cpu_tctl_c=-1;
    system.cpu_boost=-1;
    system.cpu_smt=-1;
    system.cpu_temp_c=-1.0;
    system.cpu_temp_crit_mc=-1;
    TuningState tuning={0};
    tuning.spec=gpu_spec_match(g->full_name,g->vram_bytes);
    SessionInfo session={0}; EventLog events={0}; TraceState trace; trace_discover(&trace);

    /* Initialize curl and detect AI servers */
    curl_global_init(CURL_GLOBAL_DEFAULT);
    system.token_source = g_token_source;
    detect_ai_servers(&system);
    detect_ai_model_info(&system);
    detect_ai_log(&system);

    sample_power(&s,&power); sample_system(&system); sample_clients(g,&clients); sample_session(g,&s,&session,&clients);
    tuning_refresh(&s,&tuning,true);

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
        if(diff_ns(loop_now,next_system)>=0){sample_system(&system);nvme_sample(&g_nvme);system.token_source=g_token_source;sample_tokens(&system,power.power_w);parse_ai_log(&system);next_system=loop_now;add_ns(&next_system,SYSTEM_SAMPLE_NS);ui.dirty=true;}
        if(diff_ns(loop_now,next_session)>=0){sample_session(g,&s,&session,&clients);x11_probe_session(&ui,&session);detect_ai_model_info(&system);next_session=loop_now;add_ns(&next_session,SESSION_SAMPLE_NS);ui.dirty=true;}
        trace_pump(&trace);

        while(XPending(ui.dpy)>0){
            XEvent ev;XNextEvent(ui.dpy,&ev);
            if(ev.type==Expose){ui.dirty=true;}
            else if(ev.type==ConfigureNotify){
                ui.width=ev.xconfigure.width;ui.height=ev.xconfigure.height;
                gui_scroll(&ui,view,0);
            }
            else if(ev.type==ClientMessage&&(Atom)ev.xclient.data.l[0]==ui.wm_delete){g_stop=1;break;}
            else if(ev.type==KeyPress){
                KeySym ks = XLookupKeysym(&ev.xkey,0);
                if(g_edit_ai_path) {
                    if(ks==XK_Escape) { g_edit_ai_path=false; snprintf(message,sizeof(message),"Log path edit cancelled."); }
                    else if(ks==XK_Return || ks==XK_KP_Enter) {
                        if(g_edit_path_buffer[0]=='/' && strlen(g_edit_path_buffer)<MAX_PATH_LEN-1) {
                            text_copy(g_settings.ai_log_file,sizeof(g_settings.ai_log_file),g_edit_path_buffer);
                            g_settings.ai_log_manual=true;
                            g_edit_ai_path=false;
                            snprintf(message,sizeof(message),settings_save()?"AI log path saved.":"Failed to save AI log path.");
                            system.ai_log_last_detect=0;
                        } else snprintf(message,sizeof(message),"Absolute path required (begins with /). Esc to cancel.");
                    } else if(ks==XK_BackSpace) {
                        size_t n=strlen(g_edit_path_buffer); if(n)g_edit_path_buffer[n-1]=0;
                    } else {
                        char ch[16]={0}; KeySym temp;
                        int len=XLookupString(&ev.xkey,ch,sizeof(ch),&temp,NULL);
                        size_t n=strlen(g_edit_path_buffer);
                        if(len>0 && n+(size_t)len<sizeof(g_edit_path_buffer)-1) {
                            bool printable=true;
                            for(int k=0;k<len;k++) if((unsigned char)ch[k]<32 || ch[k]=='\n' || ch[k]=='\r') printable=false;
                            if(printable) strncat(g_edit_path_buffer,ch,sizeof(g_edit_path_buffer)-n-1);
                        }
                    }
                    ui.dirty=true;
                    continue;
                }

                if (ks == XK_Page_Down || ks == XK_Page_Up || ks == XK_Home || ks == XK_End) {
                    int delta = ks == XK_Page_Down ? gui_content_height(&ui)-40 :
                                ks == XK_Page_Up ? -(gui_content_height(&ui)-40) :
                                ks == XK_Home ? -100000 : 100000;
                    gui_scroll(&ui,view,delta);
                    continue;
                }
                char key=keysym_to_action(&ev.xkey); if(key=='q'||key=='Q'){g_stop=1;break;}
                if(key){
                    View old_view = view;
                    handle_action(key,&s,&target,&interval_us,&dpm_interval_us,&monitor_clocks,&adaptive,&burst_until_ns,&trace,&events,&view,message,sizeof(message));
                    if (view != old_view) ui.scroll_y = 0;
                    ui.dirty=true;
                }
            } else if(ev.type==ButtonPress){
                if ((ev.xbutton.button == Button4 || ev.xbutton.button == Button5) &&
                    ev.xbutton.y >= UI_CONTENT_TOP && ev.xbutton.y < gui_content_bottom(&ui)) {
                    gui_scroll(&ui,view,ev.xbutton.button == Button4 ? -54 : 54);
                    continue;
                }
                if (ev.xbutton.button != Button1) continue;
                handle_click(&ui,ev.xbutton.x,ev.xbutton.y,&s,&target,&tuning,&interval_us,&dpm_interval_us,&monitor_clocks,&adaptive,&burst_until_ns,&trace,&events,&view,message,sizeof(message));ui.dirty=true;
            }
        }

        if(g_gpu_switch_requested) {
            g_gpu_switch_requested=false;
            int newgi=-1;
            for(int i=0;i<ngpu;i++) if(gpus[i].card_num==g_requested_gpu) newgi=i;
            if(newgi<0) snprintf(message,sizeof(message),"Selected AMD GPU no longer available.");
            else if(newgi==gi) snprintf(message,sizeof(message),"Already monitoring selected GPU.");
            else {
                Sysfs nextsys;
                if(sysfs_open(&gpus[newgi],&nextsys)!=0)
                    snprintf(message,sizeof(message),"Cannot open selected card%d: %s",g_requested_gpu,strerror(errno));
                else {
                    /* No new GPU policy writes until the user re-enables the watchdog. */
                    target.enabled=false; target.lock_sclk=false; target.lock_mclk=false;
                    tuning.armed=false;
                    sysfs_close(&s); s=nextsys;
                    gi=newgi;g=&gpus[gi];
                    memset(&power,0,sizeof(power));power.gpu_busy=power.mem_busy=-1;
                    power.power_w=power.voltage_v=power.current_a=-1;
                    power.gfx_hz=power.mem_hz=-1;power.fan_rpm=-1;
                    power.gfxoff_enabled=power.gfxoff_status=-1;
                    for(int i=0;i<MAX_GPU_TEMPS;i++) power.temps_c[i]=-1;
                    memset(&clients,0,sizeof(clients));memset(&tuning,0,sizeof(tuning));
                    tuning.spec=gpu_spec_match(g->full_name,g->vram_bytes);
                    sample_power(&s,&power);sample_clients(g,&clients);
                    tuning_refresh(&s,&tuning,true);
                    sample_session(g,&s,&session,&clients);x11_probe_session(&ui,&session);
                    g_settings.preferred_card=g->card_num;
                    (void)settings_save();
                    event_log_add(&events,"GPU SWITCH","Monitoring rebound; watchdog paused",&clients,&trace);
                    snprintf(message,sizeof(message),"Switched to card%d. Watchdog PAUSED; tuning DISARMED.",g->card_num);
                }
            }
            ui.dirty=true;
        }
        clock_gettime(CLOCK_MONOTONIC,&loop_now);
        if(diff_ns(loop_now,stat_wall0)>=1000000000LL){
            struct timespec cpu1;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu1);double wall=diff_ns(loop_now,stat_wall0)/1e9,cpu=diff_ns(cpu1,stat_cpu0)/1e9;uint64_t dc=st.checks-stat_checks0;
            st.loop_cpu_pct=wall>0?cpu/wall*100.0:0.0;st.loop_hz=wall>0?dc/wall:0.0;stat_wall0=loop_now;stat_cpu0=cpu1;stat_checks0=st.checks;ui.dirty=true;
        }
        if(ui.dirty&&diff_ns(loop_now,last_ui)>=250000000LL){
            gui_draw(&ui,g,&s,&target,&st,interval_us,dpm_interval_us,monitor_clocks,adaptive,&clients,&power,&system,&session,&events,&trace,&tuning,view,message);
            last_ui=loop_now;
        }

        add_ns(&next,(long long)active_interval_us*1000LL);struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
        if(diff_ns(next,now)>0)clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&next,NULL);else{st.deadline_misses++;next=now;}
    }

    trace_stop(&trace);if(events.logf)fclose(events.logf);cleanup_curl_handle();curl_global_cleanup();gui_close(&ui);if(system.fd_cpu_temp>=0)close(system.fd_cpu_temp);
    if(system.fd_cpu_tctl>=0)close(system.fd_cpu_tctl);
    sysfs_close(&s);return 0;
}
