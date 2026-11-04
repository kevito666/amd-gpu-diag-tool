/* SPDX-License-Identifier: GPL-3.0-or-later
 * AMDGPU readback: fast hwmon sampling; slow PCIe/BAR and firmware metadata.
 * Sysfs handles are borrowed from the owning GPU controller. No writes.
 */
#define _GNU_SOURCE
#include "readback.h"
#include "../core/util.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define MAX_TEXT 4096
#define NOMINAL_BOARD_VOLTAGE 12.0
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
void gpu_sample_pcie_link(const Sysfs *s, PowerTelemetry *p) {
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
int gpu_pcie_downshift_under_load(const PowerTelemetry *p) {
    if (!p->pcie_link_valid || !p->pcie_max_speed[0] || p->pcie_max_width <= 0 || p->gpu_busy < 0)
        return -1;
    if (p->gpu_busy < 40) return 0;
    double cur = strtod(p->pcie_current_speed, NULL);
    double max = strtod(p->pcie_max_speed, NULL);
    if (cur <= 0.0 || max <= 0.0) return -1;
    return (p->pcie_current_width < p->pcie_max_width || cur + 0.5 < max) ? 1 : 0;
}

void gpu_sample_power(Sysfs *s, PowerTelemetry *p) {
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
        gpu_sample_pcie_link(s, p);
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

    p->pcie_link_loaded_low = gpu_pcie_downshift_under_load(p);
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
