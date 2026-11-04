# v0.8.0-dev.5 — GPU control and watchdog boundaries

This is an **unreleased refactor checkpoint**. No system-wide installation,
firmware overclocking, Git commit, tag, or change to the published v0.7.0b beta
is performed by this package.

## Architecture

- **`src/gpu/device`** owns `Sysfs` handle opening, discovery of hwmon,
  debugfs, and cleanup. Calls to `sysfs_open()` and `sysfs_close()` remain in
  application startup, GPU switching, and shutdown.
- **`src/gpu/readback`** owns telemetry reading and never writes to sysfs.
- **`src/gpu/policy`** owns `Target`, `Stats`, DPM parsing, quick policy
  enforcement and slow clock-mask inspections; its only GPU writes are through
  `src/gpu/writes`.
- **`src/gpu/tuning`** owns `TuningState`, range checks, ARM/APPLY/RESET, and
  edit increments. A rejected manual-policy write now clears ARM immediately.
- **`src/gpu/writes`** is the only low-level GPU control write boundary and
  delegates to the original descriptor writer in `core/util`. It handles policy,
  manual DPM selection, OverDrive commands, fan PWM, and power caps. This is a
  synchronous API **not** a background hardware thread or elevated daemon.

The X11 UI still draws the same geometry, handles user actions in one thread,
and never directly writes GPU control descriptors. Linux kernel tracefs control
writes are intentionally outside `gpu/writes` because they are not GPU tuning
or policy writes.

## Safety and lifecycle

- Normal policy changes only commit `Target.policy` after an accepted write.
- Manual DPM selection requires a successful driver policy and clock-level
  write before updating the target's lock fields; driver rollback is best-effort
  if the second write fails.
- Tuning APPLY requires an explicit ARM and de-arms on success or driver
  rejection. On partial OverDrive/power/fan failures, the runtime refreshes
  values from driver state and reports failure.
- Reset performs **best-effort** multi-write cleanup and reports partial
  failures. These sysfs interfaces do not provide an application-level atomic
  multi-device transaction.
- GPU selection in Settings rebinds descriptors only after successfully opening
  the next card; the watchdog is then paused and tuning disarmed.

## Validation

`make check-control-module` links only `gpu/device`, `gpu/policy`,
`gpu/tuning`, `gpu/writes`, and `core/util`. It verifies DPM parsing,
accepted and rejected policy writes, requested state persistence on write
failures, tuning ARM handling and readback, and fake Sysfs open/close. No
actual `/sys/class/drm` write occurs in the fixture.

The full app tests, six virtual X11 screen checks and strict build should pass
before promotion. Live RX 7900 XTX tests remain necessary to validate driver
interactions, unsupported firmware operations, timing jitter, and switching
between actual AMD cards.

## Remaining for dev.6+

1. Extract Xlib layout, widgets, and page renderer while preserving
   old coordinates and hitbox mapping.
2. Introduce a read-only telemetry snapshot API before considering async work.
3. Benchmark watchdog latency against v0.7.0b and investigate moving blocking
   API/process reads out of the enforcement loop, without creating concurrent
   hardware writers.
4. Complete real-GPU tuning and multi-card safety checks before v0.8.0b.
