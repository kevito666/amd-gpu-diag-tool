/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/app/poll_timing.h"
#include <assert.h>
#include <limits.h>
#include <string.h>
int main(void) {
    PollTiming p={0};
    poll_timing_watchdog_tick(&p, 1000000000ULL, 5000000ULL);
    poll_timing_watchdog_tick(&p, 1005000000ULL, 5000000ULL);
    poll_timing_watchdog_tick(&p, 1026000000ULL, 5000000ULL);
    poll_timing_watchdog_tick(&p, 1106000000ULL, 10000000ULL);
    assert(p.tick_intervals == 3);
    assert(p.tick_gap_max_ns == 80000000ULL);
    assert(p.tick_gap_total_ns == 106000000ULL);
    assert(p.tick_gap_over_10ms == 2);
    assert(p.tick_gap_over_50ms == 1);
    assert(p.tick_gap_over_2_periods == 2);
    poll_timing_record(&p,POLL_CLIENTS,1000000);
    poll_timing_record(&p,POLL_CLIENTS,3000000);
    assert(p.samples[POLL_CLIENTS].count==2);
    assert(p.samples[POLL_CLIENTS].max_ns==3000000);
    assert(p.samples[POLL_CLIENTS].total_ns==4000000);
    poll_timing_record(&p,POLL_SESSION,UINT64_MAX);
    poll_timing_record(&p,POLL_SESSION,1);
    assert(p.samples[POLL_SESSION].total_ns==UINT64_MAX);
    assert(poll_timing_now_ns()>0);
    poll_timing_watchdog_late(&p, 500000, 5000000);
    poll_timing_watchdog_late(&p, 6000000, 5000000);
    poll_timing_watchdog_late(&p, 65000000, 5000000);
    poll_timing_watchdog_late(&p, 750000000, 5000000);
    assert(p.late_count == 4);
    assert(p.late_histogram[1] == 1); /* 0.5ms */
    assert(p.late_histogram[4] == 1); /* 6ms */
    assert(p.late_histogram[6] == 1); /* 65ms */
    assert(p.late_histogram[7] == 1); /* 750ms */
    assert(p.max_late_ns == 750000000);
    assert(p.late_over_1ms == 3 && p.late_over_5ms == 3);
    assert(p.late_over_50ms == 2 && p.late_over_500ms == 1);
    assert(p.missed_periods == 164);
    poll_timing_watchdog_late(&p, 0, 0);
    assert(p.late_count == 4);
    FILE *f=tmpfile();assert(f);
    poll_timing_report(&p,f,7);
    rewind(f);char buffer[2048]={0};assert(fread(buffer,1,sizeof(buffer)-1,f)>0);
    assert(strstr(buffer,"GPU clients") && strstr(buffer,"2.000 ms"));
    assert(strstr(buffer,"watchdog tick gaps: intervals=3"));
    assert(strstr(buffer,"measured duration=0.106 s"));
    assert(strstr(buffer,"misses=7"));
    assert(strstr(buffer,"lateness:") && strstr(buffer,"skipped periods=164"));
    assert(strstr(buffer,"bucket upper bounds") && strstr(buffer,"p95=-1"));
    fclose(f);
    return 0;
}
