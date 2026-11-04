/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only GPU metrics and sysfs-handle state. GPU writes are routed through gpu/writes.c; this header owns readback data.
 */
#ifndef AMD_GPU_DIAG_GPU_READBACK_H
#define AMD_GPU_DIAG_GPU_READBACK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 512
#endif
#ifndef MAX_GPU_TEMPS
#define MAX_GPU_TEMPS 3
#endif
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

/* These functions perform reads only. They never write to GPU controls. */
void gpu_sample_pcie_link(const Sysfs *sys, PowerTelemetry *power);
int gpu_pcie_downshift_under_load(const PowerTelemetry *power);
void gpu_sample_power(Sysfs *sys, PowerTelemetry *power);
#endif
