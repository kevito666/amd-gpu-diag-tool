/* SPDX-License-Identifier: GPL-3.0-or-later
 * Opt-in wall-clock profiling; a diagnostic, not a realtime guarantee.
 */
#ifndef AMD_GPU_DIAG_POLL_TIMING_H
#define AMD_GPU_DIAG_POLL_TIMING_H
#include <stdint.h>
#include <stdio.h>

typedef enum {
    POLL_CLIENTS, POLL_SYSTEM_AI, POLL_SESSION, POLL_RENDER,
    POLL_DPM, POLL_POWER, POLL_TRACE, POLL_INPUT, POLL_GPU_SWITCH,
    POLL_KIND_COUNT
} PollKind;
typedef struct { uint64_t count, total_ns, max_ns; } PollSample;
typedef struct {
    PollSample samples[POLL_KIND_COUNT];
    /* Start-to-start intervals measure real watchdog service gaps, including stalls. */
    uint64_t first_tick_ns, last_tick_ns, previous_period_ns;
    uint64_t tick_intervals, tick_gap_total_ns, tick_gap_max_ns;
    uint64_t tick_gap_over_10ms, tick_gap_over_50ms, tick_gap_over_500ms;
    uint64_t tick_gap_over_2_periods;
    uint64_t late_count;
    uint64_t total_late_ns;
    uint64_t max_late_ns;
    uint64_t missed_periods; /* missed scheduled intervals beyond current cycle */
    uint64_t late_over_1ms;
    uint64_t late_over_5ms;
    uint64_t late_over_50ms;
    uint64_t late_over_500ms;
    /* Distribution of positive lateness only: fixed memory, optional profiling. */
    uint64_t late_histogram[8];
} PollTiming;
uint64_t poll_timing_now_ns(void);
void poll_timing_record(PollTiming *timing, PollKind kind, uint64_t elapsed_ns);
/* Called only for positive deadline lateness, before scheduler resets deadline.
 * Skipped periods are an estimate, not additional enforcement attempts. */
void poll_timing_watchdog_tick(PollTiming *timing, uint64_t tick_ns, uint64_t target_period_ns);
void poll_timing_watchdog_late(PollTiming *timing, uint64_t late_ns, uint64_t period_ns);
void poll_timing_report(const PollTiming *timing, FILE *output, uint64_t deadline_misses);
#endif
