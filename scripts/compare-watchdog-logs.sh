#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Inspect opt-in watchdog timing reports from comparable host sessions.
# Usage: scripts/compare-watchdog-logs.sh idle.log inference.log
# No root, GPU access or extra packages required beyond standard awk.
if [[ "$#" -ne 2 ]]; then
    echo 'Usage: compare-watchdog-logs.sh IDLE_TIMING_LOG INFERENCE_TIMING_LOG' >&2
    exit 2
fi
for log in "$@"; do
    if [[ ! -r "$log" ]]; then echo "Cannot read: $log" >&2; exit 2; fi
    if ! grep -q 'watchdog deadline misses=' "$log" || ! grep -q 'positive-lateness bucket upper bounds' "$log"; then
        echo "No dev.13 timing report in $log (close the app normally with AMD_GPU_DIAG_TIMING=1)" >&2
        exit 2
    fi
done
printf '%-22s %12s %12s %12s %12s %12s %12s\n' \
    'Session' 'Misses' 'Max(ms)' 'Avg(ms)' 'p95<=ms' 'p99<=ms' 'Skipped'
for log in "$@"; do
    awk -v name="$(basename "$log")" '
        /watchdog deadline misses=/ {
            for(i=1;i<=NF;i++) if($i ~ /^misses=/) { split($i,a,"="); misses=a[2] }
        }
        /lateness: max=/ {
            for(i=1;i<=NF;i++) {
                if($i ~ /^max=/) { split($i,a,"="); max=a[2] }
                if($i ~ /^avg=/) { split($i,a,"="); avg=a[2] }
            }
        }
        /estimated skipped periods=/ {
            for(i=1;i<=NF;i++) if($i ~ /^periods=/) { split($i,a,"="); skipped=a[2] }
        }
        /positive-lateness bucket upper bounds/ {
            for(i=1;i<=NF;i++) {
                if($i ~ /^p95=/) { split($i,a,"="); p95=a[2] }
                if($i ~ /^p99=/) { split($i,a,"="); p99=a[2] }
            }
        }
        END { printf "%-22s %12s %12s %12s %12s %12s %12s\n", name, misses, max, avg, p95, p99, skipped }
    ' "$log"
done
echo 'p95/p99 are upper bounds of positive-lateness buckets; -1 means >500ms.'
echo 'Run similar durations with identical watchdog intervals; counts are duration-dependent.'
echo
printf '%-22s %12s %12s %12s %12s %12s %12s\n' \
    'Session' 'Duration(s)' 'Miss/min' 'GapMax(ms)' 'Gaps>10ms' 'Gaps>50ms' 'Gaps>500ms'
for log in "$@"; do
    awk -v name="$(basename "$log")" '
        /measured duration=/ {
            for(i=1;i<=NF;i++) {
                if($i ~ /^duration=/) { split($i,a,"="); duration=a[2] }
                if($i ~ /^misses\/min=/) { split($i,a,"="); rate=a[2] }
            }
        }
        /watchdog tick gaps:/ {
            for(i=1;i<=NF;i++) {
                if($i ~ /^max=/) { split($i,a,"="); gapmax=a[2] }
                if($i ~ /^>10ms=/) { split($i,a,"="); g10=a[2] }
                if($i ~ /^>50ms=/) { split($i,a,"="); g50=a[2] }
                if($i ~ /^>500ms=/) { split($i,a,"="); g500=a[2] }
            }
        }
        END { printf "%-22s %12s %12s %12s %12s %12s %12s\n", name,
              duration==""?"n/a":duration, rate==""?"n/a":rate,
              gapmax==""?"n/a":gapmax, g10==""?"n/a":g10,
              g50==""?"n/a":g50, g500==""?"n/a":g500 }
    ' "$log"
done
echo 'Gap metrics are start-to-start times between watchdog checks, not real-time guarantees.'
