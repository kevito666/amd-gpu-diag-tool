/* SPDX-License-Identifier: GPL-3.0-or-later
 * Watchdog target, counters, and DPM policy API. Hardware control writes are
 * funneled through gpu/writes.c. No background worker is introduced here.
 */
#ifndef AMD_GPU_DIAG_GPU_POLICY_H
#define AMD_GPU_DIAG_GPU_POLICY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "readback.h"
#ifndef MAX_LEVELS
#define MAX_LEVELS 32
#endif
typedef struct {
    int index;
    long mhz;
    bool active;
    bool sleep_level;
    char raw[128];
} DpmLevel;

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

extern const char *const POLICIES[];
extern const size_t POLICY_COUNT;
int parse_dpm_levels(const char *text, DpmLevel *levels, int max);
bool valid_policy(const char *policy);
int apply_policy(Sysfs *sys, const char *policy);
int apply_level(int fd, int level);
int enforce_policy_once(Sysfs *sys, const Target *target, Stats *stats);
void monitor_dpm_once(Sysfs *sys, const Target *target, Stats *stats, bool enforce);
int cycle_manual_level(Sysfs *sys, Target *target, bool sclk, char *message, size_t cap);
void set_mode(Sysfs *sys, Target *target, int idx, char *message, size_t cap);
#endif
