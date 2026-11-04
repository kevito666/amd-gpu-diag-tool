# v0.8.0-dev.15 — beta-readiness, watchdog gap measurement

## What was changed and why

The dev.10–dev.14 instrumentation recorded positive deadline lateness and the
cumulative number of missed deadlines, but not **how long the foreground went
between actually starting successive enforcement checks**. These are different:
a stalled main thread can skip a substantial interval without a single huge
lateness sample adequately describing the live enforcement gap.

With `AMD_GPU_DIAG_TIMING=1`, the main loop records the monotonic start of each
watchdog check (regardless of whether enforcement is enabled). On normal exit,
reports now include:

- `watchdog tick gaps`: intervals sampled, mean and largest start-to-start gap
  (ms), counts >10, >50 and >500 ms, and counts >2 times the *preceding*
  configured interval. This changes dynamically with the burst interval.
- `measured duration` and `watchdog misses/min` to compare unequal runs more
  sensibly. Raw misses remain visible for the original counter.
- The pre-existing late-time histogram's p50/p95/p99 represent **upper bounds
  over positive-lateness samples only**, not all watchdog checks.

The timing path uses only in-memory fixed-size counters. It adds no thread,
allocation, print, filesystem access or GPU write to the critical loop.

Worker-owned hwmon descriptor integers no longer appear in copies of CPU
telemetry sent to the UI, reducing accidental cross-thread resource ownership.
Only the sampler closes those live descriptors; sensor readings/labels remain.

## On-device two-run comparison

1. Build with `make -j$(nproc)` and install/run from your X11/XLibre session.
   The app requires root for its existing AMDGPU policy controls. Be aware it
   still applies its configured initial policy, just as prior versions did.
2. Capture one **idle** session with the application running for a repeatable
   duration (suggest 2–5 minutes), then close the window normally:

   ```sh
   scripts/capture-watchdog-profile.sh idle
   ```

3. Capture a second session for the same duration, while running your usual
   Project Maya / DeepSeek Flash or Bionic inference manually:

   ```sh
   scripts/capture-watchdog-profile.sh ai-load
   ```

4. View log files at `archive/logs/watchdog/*.log` and compare:

   ```sh
   scripts/compare-watchdog-logs.sh \
     archive/logs/watchdog/idle-<timestamp>.log \
     archive/logs/watchdog/ai-load-<timestamp>.log
   ```

5. If desired, capture baseline dev.10 and new dev.15 with matching watchdog
   intervals, GPU policy and workloads; do **not** ascribe improvements to
   worker isolation without this physical measurement.

The script does not invoke AI inference, adjust system priorities or tuning,
or change policy settings. It only starts the existing app with diagnostic
logging and saves stderr to a private per-user log (`umask 077`). A normal GUI
shutdown is required for a complete timing report. Treat logs as potentially
sensitive when sharing because they can contain local process paths or errors.

## Minimum beta release gate (still pending hardware)

- Confirm watchdog gaps, misses/min and CPU overhead are acceptable at idle
  and under sustained inference. A 5 ms user setting is **best-effort**.
- Confirm GPU switching disarms tuning and pauses watchdog, and invalidates
  any old GPU client/desktop state. Prefer a second physical AMD card if present.
- Verify ARM/APPLY/RESET STOCK behavior and manual-policy write failures on
  the actual driver; synthetic tests cannot prove rollback is atomic.
- Verify logs (Project Maya + Bionic), NVMe SSD selection and Settings
  persistence after restarting under `sudo -E`.
- Confirm frontend shutdown doesn't hang when network requests time out,
  and that no root-owned configuration files are left in user directories.
- Review the output of strict build/tests and X11 smoke/geometry checks.

No source in this checkpoint implies that GPU firmware writes are transactional
or that Linux/X11 offers a hard 5 ms real-time deadline. Continue to use
`v0.7.0b` as the stable published prerelease until validated.
