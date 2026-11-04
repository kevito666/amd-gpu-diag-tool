#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Interactive, opt-in on-device profiling. No inference workload is launched,
# no tuning changes are requested by the script, and logs remain local.
set -u
if [[ $# -ne 1 || ! "$1" =~ ^(idle|ai-load)$ ]]; then
    printf 'Usage: %s idle|ai-load\n' "$0" >&2
    exit 2
fi
label="$1"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)" || exit 2
if [[ ! -x "$root/amd-gpu-diagnostic" ]]; then
    echo 'Build the project first (make).' >&2
    exit 2
fi
if [[ -z "${DISPLAY:-}" ]]; then
    echo 'DISPLAY is unset; run this from your active X11/XLibre session.' >&2
    exit 2
fi
outdir="$root/archive/logs/watchdog"
umask 077
mkdir -p -- "$outdir" || exit 2
stamp="$(date +%Y%m%d-%H%M%S)"
output="$outdir/${label}-${stamp}.log"
if ! (set -o noclobber; : > "$output") 2>/dev/null; then
    echo "Cannot create profile log: $output" >&2
    exit 2
fi
printf 'Starting %s profile with AMD_GPU_DIAG_TIMING=1.\n' "$label"
printf 'Close the app normally to finish the timing report.\n'
printf 'The app itself can write GPU power policies; this script does not change its settings.\n'
if [[ "$label" == ai-load ]]; then
    printf 'Start your own Project Maya/DeepSeek or Bionic workload after the window appears.\n'
else
    printf 'Keep inference and games idle for a comparable run.\n'
fi
printf 'Output: %s\n' "$output"
sudo -E env AMD_GPU_DIAG_TIMING=1 "$root/amd-gpu-diagnostic" 2> "$output"
rc=$?
if grep -q 'watchdog tick gaps: intervals=' "$output"; then
    printf '\nProfile captured successfully: %s\n' "$output"
    grep -E 'watchdog deadline misses=|watchdog tick gaps:|measured duration=|lateness:' "$output" || true
else
    printf '\nNo complete timing report. Application exit status=%d; see %s\n' "$rc" "$output" >&2
fi
exit "$rc"
