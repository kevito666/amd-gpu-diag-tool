/* SPDX-License-Identifier: GPL-3.0-or-later
 * Linux hwmon sensor discovery. Never writes hwmon/device policy.
 */
#define _GNU_SOURCE
#include "sensors.h"
#include "../core/util.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
void discover_cpu_temp(SystemTelemetry *s) {
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

void discover_cpu_ccx_temps(SystemTelemetry *s) {
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

void discover_cpu_fan(SystemTelemetry *s) {
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

void discover_cpu_power(SystemTelemetry *s) {
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

void discover_motherboard_temps(SystemTelemetry *s) {
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


long read_cpu_fan_pwm_max(const char *pwm_path) {
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


/* Sensor file handles never escape their sampling thread; the foreground
 * may copy read-only telemetry but must not close descriptor numbers. */
void monitor_sensors_close(SystemTelemetry *s) {
    if (!s) return;
    if (s->fd_cpu_temp >= 0) { close(s->fd_cpu_temp); s->fd_cpu_temp = -1; }
    if (s->fd_cpu_tctl >= 0) { close(s->fd_cpu_tctl); s->fd_cpu_tctl = -1; }
    if (s->cpu_ccx_temp_discovered)
        for (int i = 0; i < s->cpu_ccx_temp_count; i++)
            if (s->fd_cpu_ccx_temp[i] >= 0) { close(s->fd_cpu_ccx_temp[i]); s->fd_cpu_ccx_temp[i] = -1; }
    if (s->cpu_fan_discovered) {
        if (s->fd_cpu_fan_rpm >= 0) { close(s->fd_cpu_fan_rpm); s->fd_cpu_fan_rpm = -1; }
        if (s->fd_cpu_fan_pct >= 0) { close(s->fd_cpu_fan_pct); s->fd_cpu_fan_pct = -1; }
    }
    if (s->cpu_power_discovered && s->fd_cpu_power >= 0) {
        close(s->fd_cpu_power); s->fd_cpu_power = -1;
    }
    if (s->mb_temp_discovered)
        for (int i = 0; i < s->mb_temp_count; i++)
            if (s->mb_temp_fds[i] >= 0) { close(s->mb_temp_fds[i]); s->mb_temp_fds[i] = -1; }
}

/* Sensor paths and sampled readings remain useful to the renderer. The actual
 * open descriptors belong exclusively to the sampling worker and should never
 * appear in foreground snapshots (even as integers). */
void monitor_sensors_detach_snapshot(SystemTelemetry *snapshot) {
    if (!snapshot) return;
    snapshot->fd_cpu_temp = snapshot->fd_cpu_tctl = -1;
    for (int i = 0; i < 4; i++) {
        snapshot->fd_cpu_ccx_temp[i] = -1;
        snapshot->mb_temp_fds[i] = -1;
    }
    snapshot->fd_cpu_fan_rpm = snapshot->fd_cpu_fan_pct = -1;
    snapshot->fd_cpu_power = -1;
}
