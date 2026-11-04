# v0.8.0-dev.12 — GPU-client / desktop discovery isolation

**Development-only checkpoint.** Public v0.7.0b is unchanged.

## Motivation and execution paths

Before dev.12, the watchdog/UI event loop opened and walked `/proc/*/fd` and
`fdinfo` for each GPU-client scan, and every 10 seconds also discovered desktop
processes, KMS state and Arch package version/build-date metadata. These reads
can be slow on systems with many processes or disk pressure, delaying watchdog
checks irrespective of the configured 5 ms interval.

- **Foreground (only GPU writer):** watchdog/DPM/firmware state, tuning,
  GPU selection and safety disarm, GPU power readback, tracefs, X11 events,
  window-manager property requests, UI rendering and timing aggregation.
- **Existing monitor worker:** CPU/process telemetry, AI HTTP/log parsing,
  NVMe read/write throughput, hwmon CPU sensors.
- **New discovery worker:** DRM GPU-client fdinfo traversal (1-second request
  cadence), desktop/compositor `/proc` discovery, DRM/KMS state and package
  metadata (approximately 10-second cadence). The worker never calls Xlib,
  never writes to sysfs, and never uses the live GPU `Sysfs` file descriptors.

## Input/output ownership

`DiscoveryInput` copies GPU identity, an immutable read-only debugfs state
pathname, compositor source, the current window manager name, Xserver release
number and a monotonic selection generation. Only the latest **unstarted**
request is retained. `DiscoveryOutput` provides copied ClientMonitor and
optional SessionInfo snapshots, source provenance, and measured worker-wall
costs. The producer keeps per-client counter history private.

Card rebinding increases the generation and requests a fresh sample. Main
thread refuses results from a mismatched generation, card or compositor
selection. Successful switching retains the existing **watchdog paused,
tuning disarmed** policy; there are no additional GPU control writers.

`ui_window_probe` performs X11-specific queries only; non-X11 package version
and date lookups run in the worker. A worker creation failure is reported and
falls back to synchronous discovery (with slower 10-second session cadence).
This fallback is less responsive and does not claim deadline isolation.

## Verification

`make check-discovery-worker-module`: fake 25-ms scanner ensures requests
are nonblocking, stale generations/sources are rejected, queues coalesce and
shutdown joins cleanly. `make check-clients-module`: synthetic AMDGPU fdinfo
checks PCI matching, GFX/compute counters, VRAM units and absent files.
The standard strict build, application regression suite and all X11 page tests
must continue to pass. Neither synthetic test touches GPU firmware.

## Remaining risks / host verification

- **Not hard real time:** the single foreground watchdog still shares the
  thread with GPU power readback, tracefs, DPM, X11 rendering and input.
- **No hardware benchmark here:** only the target machine can establish the
  actual improvement in mean/max watchdog lateness under AI+GPU load.
- Repeated client snapshots may be delayed by a very busy `/proc`; pending
  work coalesces instead of allocating an unbounded queue.
- User should test fast GPU card switching, compositors, and clean exit while
  the scanner is busy. Mark missing or permission-protected DRM data as
  unavailable rather than claiming an idle device.

### Suggested comparison on the same machine

Run dev.11 and dev.12 under similar idle/inference loads with
`AMD_GPU_DIAG_TIMING=1`, close the GUI normally and save stderr reports.
Compare maximum watchdog lateness, counts >=5ms/50ms, skipped intervals,
background-client scan wall time and the responsiveness of GPU switching.
Because both versions are best-effort Linux user-space watchdogs, results
vary with scheduler load and aren't a hard scheduling guarantee.
