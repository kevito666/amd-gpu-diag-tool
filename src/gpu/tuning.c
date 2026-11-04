/* SPDX-License-Identifier: GPL-3.0-or-later
 * AMDGPU tuning refresh, edits and ARM/APPLY/RESET safety lifecycle.
 * No UI state, CLI parsing, or X11 dependencies belong in this module.
 */
#define _GNU_SOURCE
#include "tuning.h"
#include "writes.h"
#include "../core/util.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_TEXT 4096

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

void tuning_refresh(Sysfs *s, TuningState *t, bool capture_defaults) {
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

TuneZone tuning_zone_long(long value, long baseline, long limit_max) {
    if (value < 0 || baseline < 0) return TUNE_ZONE_UNAVAILABLE;
    if (value <= baseline || limit_max <= baseline) return TUNE_ZONE_SAFE;
    double f = (double)(value - baseline) / (double)(limit_max - baseline);
    return f >= 0.75 ? TUNE_ZONE_DANGER : TUNE_ZONE_WARNING;
}

TuneZone tuning_zone_double(double value, double baseline, double limit_max) {
    if (value < 0.0 || baseline < 0.0) return TUNE_ZONE_UNAVAILABLE;
    if (value <= baseline || limit_max <= baseline) return TUNE_ZONE_SAFE;
    double f = (value - baseline) / (limit_max - baseline);
    return f >= 0.75 ? TUNE_ZONE_DANGER : TUNE_ZONE_WARNING;
}

TuneZone tuning_zone_offset(long value, long max_positive) {
    if (max_positive <= 0 || value <= 0) return TUNE_ZONE_SAFE;
    return value >= (long)llround((double)max_positive * 0.75) ?
        TUNE_ZONE_DANGER : TUNE_ZONE_WARNING;
}

int tuning_apply(Sysfs *s, Target *target, TuningState *t,
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
        /* A rejected driver write must not leave a prior ARM authorisation
         * pending for a second, potentially different user action. */
        t->armed = false;
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
                if (gpu_write_overdrive(s, cmd) < 0) goto od_fail;
            }
            snprintf(cmd, sizeof(cmd), "s 1 %ld", t->edit_sclk_max_mhz);
            if (gpu_write_overdrive(s, cmd) < 0) goto od_fail;
            od_changed = true;
        } else if (t->has_sclk_offset) {
            t->edit_sclk_offset_mhz = clamp_long_value(t->edit_sclk_offset_mhz,
                                                        t->sclk_offset_min_mhz,
                                                        t->sclk_offset_max_mhz);
            if (t->sclk_offset_legacy)
                snprintf(cmd, sizeof(cmd), "s 1 %ld", t->edit_sclk_offset_mhz);
            else
                snprintf(cmd, sizeof(cmd), "s %ld", t->edit_sclk_offset_mhz);
            if (gpu_write_overdrive(s, cmd) < 0) goto od_fail;
            od_changed = true;
        }

        if (t->has_mclk) {
            t->edit_mclk_max_mhz = clamp_long_value(t->edit_mclk_max_mhz,
                                                     t->mclk_limit_min_mhz,
                                                     t->mclk_limit_max_mhz);
            snprintf(cmd, sizeof(cmd), "m 1 %ld", t->edit_mclk_max_mhz);
            if (gpu_write_overdrive(s, cmd) < 0) goto od_fail;
            od_changed = true;
        }

        if (t->has_vdd_offset) {
            t->edit_vdd_offset_mv = clamp_long_value(t->edit_vdd_offset_mv,
                                                      t->vdd_offset_min_mv,
                                                      t->vdd_offset_max_mv);
            snprintf(cmd, sizeof(cmd), "vo %ld", t->edit_vdd_offset_mv);
            if (gpu_write_overdrive(s, cmd) < 0) goto od_fail;
            od_changed = true;
        }

        if (od_changed && gpu_write_overdrive(s, "c") < 0) goto od_fail;
    }

    if (t->has_power_cap) {
        t->edit_power_cap_w = clamp_double_value(t->edit_power_cap_w,
                                                  t->power_cap_min_w,
                                                  t->power_cap_max_w);
        if (gpu_write_power_cap(s, t->edit_power_cap_w) < 0) {
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
        if (gpu_write_fan_pwm(s, pwm_buf) < 0) {
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
        (void)gpu_write_overdrive(s, "r");
        errno = saved;
        snprintf(message, cap, "Overdrive write rejected by driver: %s", strerror(errno));
        t->armed = false;
        tuning_refresh(s, t, false);
        return -1;
    }
}

int tuning_reset(Sysfs *s, Target *target, TuningState *t,
                        char *message, size_t cap) {
    int rc = 0;
    if (s->fd_od_w >= 0) {
        if (gpu_write_overdrive(s, "r") < 0) rc = -1;
    }
    if (t->has_power_cap && t->default_power_cap_w > 0.0) {
        if (gpu_write_power_cap(s, t->default_power_cap_w) < 0) rc = -1;
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

void tuning_adjust(TuningState *t, int row, int direction) {
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
