/* SPDX-License-Identifier: GPL-3.0-or-later
 * Open and close the selected AMDGPU sysfs/hwmon/debugfs handles.
 */
#define _GNU_SOURCE
#include "device.h"
#include "../core/util.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void sysfs_close(Sysfs *s) {
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

int sysfs_open(const Gpu *g, Sysfs *s) {
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
