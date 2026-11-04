/* SPDX-License-Identifier: GPL-3.0-or-later
 * Driver-bound tuning state and explicit, disarmed-by-default edit operations.
 */
#ifndef AMD_GPU_DIAG_GPU_TUNING_H
#define AMD_GPU_DIAG_GPU_TUNING_H
#include "policy.h"
#include "../gpu_specs.h"
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
void tuning_refresh(Sysfs *sys, TuningState *tuning, bool capture_defaults);
TuneZone tuning_zone_long(long value, long baseline, long limit_max);
TuneZone tuning_zone_double(double value, double baseline, double limit_max);
TuneZone tuning_zone_offset(long value, long max_positive);
int tuning_apply(Sysfs *sys, Target *target, TuningState *tuning, char *message, size_t cap);
int tuning_reset(Sysfs *sys, Target *target, TuningState *tuning, char *message, size_t cap);
void tuning_adjust(TuningState *tuning, int row, int direction);
#endif
