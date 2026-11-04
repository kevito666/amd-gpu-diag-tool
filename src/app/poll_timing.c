/* SPDX-License-Identifier: GPL-3.0-or-later
 * Lightweight opt-in polling costs. Reports only on shutdown; no dynamic
 * allocation or file I/O in the watchdog loop. Monotonic elapsed wall time.
 */
#define _POSIX_C_SOURCE 200809L
#include "poll_timing.h"
#include <inttypes.h>
#include <time.h>
#include <limits.h>

static const char *const poll_labels[POLL_KIND_COUNT] = {
    "GPU clients", "System + AI", "Desktop session", "X11 render",
    "DPM monitoring", "GPU power", "tracefs pump", "X11 events", "GPU switch"
};
/* Upper bounds: 0.1, 0.5, 1, 5, 10, 50, 500 ms, then unbounded. */
static const uint64_t late_bounds_ns[8] = {
    100000, 500000, 1000000, 5000000, 10000000,
    50000000, 500000000, UINT64_MAX
};

static uint64_t saturating_add(uint64_t a, uint64_t b) {
    return UINT64_MAX - a < b ? UINT64_MAX : a + b;
}
uint64_t poll_timing_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
void poll_timing_record(PollTiming *timing, PollKind kind, uint64_t elapsed_ns) {
    if (!timing || kind < 0 || kind >= POLL_KIND_COUNT) return;
    PollSample *sample = &timing->samples[kind];
    sample->count = saturating_add(sample->count, 1);
    sample->total_ns = saturating_add(sample->total_ns, elapsed_ns);
    if (elapsed_ns > sample->max_ns) sample->max_ns = elapsed_ns;
}
/* Record watchdog check start-to-start gaps. This catches blocking enforcement,
 * X11 and GPU monitoring without claiming hard-real-time scheduling accuracy.
 * The provided period is the interval selected for the *upcoming* iteration;
 * the gap just observed is compared against the previous period. */
void poll_timing_watchdog_tick(PollTiming *timing, uint64_t tick_ns, uint64_t target_period_ns) {
    if (!timing || !tick_ns) return;
    if (!timing->first_tick_ns) timing->first_tick_ns = tick_ns;
    if (timing->last_tick_ns && tick_ns >= timing->last_tick_ns) {
        const uint64_t gap = tick_ns - timing->last_tick_ns;
        timing->tick_intervals = saturating_add(timing->tick_intervals, 1);
        timing->tick_gap_total_ns = saturating_add(timing->tick_gap_total_ns, gap);
        if (gap > timing->tick_gap_max_ns) timing->tick_gap_max_ns = gap;
        if (gap > 10000000ULL) timing->tick_gap_over_10ms++;
        if (gap > 50000000ULL) timing->tick_gap_over_50ms++;
        if (gap > 500000000ULL) timing->tick_gap_over_500ms++;
        if (timing->previous_period_ns && gap > timing->previous_period_ns &&
            (gap - timing->previous_period_ns) > timing->previous_period_ns)
            timing->tick_gap_over_2_periods++;
    }
    timing->last_tick_ns = tick_ns;
    timing->previous_period_ns = target_period_ns;
}
void poll_timing_watchdog_late(PollTiming *timing, uint64_t late_ns, uint64_t period_ns) {
    if (!timing || late_ns == 0) return;
    timing->late_count = saturating_add(timing->late_count, 1);
    timing->total_late_ns = saturating_add(timing->total_late_ns, late_ns);
    if (late_ns > timing->max_late_ns) timing->max_late_ns = late_ns;
    if (period_ns) timing->missed_periods = saturating_add(timing->missed_periods, late_ns / period_ns);
    for (int i = 0; i < 8; i++) {
        if (late_ns <= late_bounds_ns[i]) {
            timing->late_histogram[i] = saturating_add(timing->late_histogram[i], 1);
            break;
        }
    }
    if (late_ns >= 1000000ULL) timing->late_over_1ms = saturating_add(timing->late_over_1ms, 1);
    if (late_ns >= 5000000ULL) timing->late_over_5ms = saturating_add(timing->late_over_5ms, 1);
    if (late_ns >= 50000000ULL) timing->late_over_50ms = saturating_add(timing->late_over_50ms, 1);
    if (late_ns >= 500000000ULL) timing->late_over_500ms = saturating_add(timing->late_over_500ms, 1);
}
void poll_timing_report(const PollTiming *timing, FILE *output, uint64_t misses) {
    if (!timing || !output) return;
    fprintf(output, "Poll timing (monotonic wall time; optional AMD_GPU_DIAG_TIMING=1):\n");
    for (int i = 0; i < POLL_KIND_COUNT; i++) {
        const PollSample *sample = &timing->samples[i];
        fprintf(output, "  %-18s calls=%" PRIu64 " avg=%.3f ms max=%.3f ms\n",
                poll_labels[i], sample->count,
                sample->count ? (double)sample->total_ns / (double)sample->count / 1e6 : 0.0,
                (double)sample->max_ns / 1e6);
    }
    fprintf(output, "  watchdog deadline misses=%" PRIu64 " (total; includes scheduler and monitor stalls)\n", misses);
    fprintf(output, "  watchdog tick gaps: intervals=%" PRIu64
                    " avg=%.3f ms max=%.3f ms >10ms=%" PRIu64
                    " >50ms=%" PRIu64 " >500ms=%" PRIu64
                    " >2x_period=%" PRIu64 "\n",
            timing->tick_intervals,
            timing->tick_intervals ? (double)timing->tick_gap_total_ns /
                (double)timing->tick_intervals / 1e6 : 0.0,
            (double)timing->tick_gap_max_ns / 1e6,
            timing->tick_gap_over_10ms, timing->tick_gap_over_50ms,
            timing->tick_gap_over_500ms, timing->tick_gap_over_2_periods);
    if (timing->first_tick_ns && timing->last_tick_ns > timing->first_tick_ns) {
        const double seconds = (double)(timing->last_tick_ns - timing->first_tick_ns) / 1e9;
        fprintf(output, "  measured duration=%.3f s; watchdog misses/min=%.3f\n",
                seconds, seconds > 0 ? (double)misses * 60.0 / seconds : 0.0);
    }
    fprintf(output, "  lateness: max=%.3f ms avg=%.3f ms; >=1ms=%" PRIu64
                    " >=5ms=%" PRIu64 " >=50ms=%" PRIu64 " >=500ms=%" PRIu64 "\n",
            (double)timing->max_late_ns / 1e6,
            timing->late_count ? (double)timing->total_late_ns / (double)timing->late_count / 1e6 : 0.0,
            timing->late_over_1ms, timing->late_over_5ms,
            timing->late_over_50ms, timing->late_over_500ms);
    fprintf(output, "  estimated skipped periods=%" PRIu64
                    " (not extra enforcement attempts)\n", timing->missed_periods);
    /* These are bucketed upper bounds, not exact percentiles and not samples
     * of cycles that met their deadlines. Useful for comparing two runs. */
    if (timing->late_count) {
        uint64_t cumulative = 0;
        double p50 = 0, p95 = 0, p99 = 0;
        const uint64_t ranks[3] = {
            timing->late_count / 2 + timing->late_count % 2,
            timing->late_count / 20 * 19 + (timing->late_count % 20 * 19 + 19) / 20,
            timing->late_count / 100 * 99 + (timing->late_count % 100 * 99 + 99) / 100
        };
        double *values[3] = {&p50, &p95, &p99};
        for (int i = 0; i < 8; i++) {
            cumulative = saturating_add(cumulative, timing->late_histogram[i]);
            for (int j = 0; j < 3; j++) {
                if (*values[j] == 0 && cumulative >= ranks[j])
                    *values[j] = i == 7 ? -1.0 : (double)late_bounds_ns[i] / 1e6;
            }
        }
        fprintf(output, "  positive-lateness bucket upper bounds (ms): "
                        "p50=%g p95=%g p99=%g (-1 means >500ms)\n",
                p50, p95, p99);
    }
}
