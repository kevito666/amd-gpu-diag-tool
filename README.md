# AMD GPU Performance and Diagnostic Tool

**Development checkpoint 0.8.0-dev.16 · GPL-3.0-or-later · C / Xlib / AMDGPU · X11/XLibre**

> **Development branch only.** The public beta remains `v0.7.0b`. This is
> modular UI and timing refactor toward `v0.8.0b`, not a tagged GitHub release. It preserves
> the existing interface and monitoring behavior. See
> [the v0.8.0 refactor roadmap](docs/V080_REFACTOR.md).

What started as a coil-whine reducer and GPU power-state controller morphed into a full-blown AI and gaming desktop monitoring and configuration tool. Built in **Tohoku, Japan**.

A lightweight X11/XLibre-first diagnostics utility for Linux AMD GPUs. No GTK, Qt, Electron or Python runtime. Uses the kernel's sysfs, procfs, debugfs, tracefs and DRM fdinfo interfaces and an Xlib desktop UI. It currently requires root for its existing AMDGPU power-policy writes. **Review the code before running with sudo.**

## Screenshots — v0.7.0b running on AMD Radeon RX 7900 XTX

These are unmodified captures of the actual compact X11 interface, not simulated mockups. Hardware and AI readings are momentary observations, not benchmark results.

| AI monitoring | Gaming monitoring |
|:---:|:---:|
| ![AI monitoring dashboard with llama.cpp activity, prompt-cache state and selected NVMe device I/O](docs/screenshots/ai.png) | ![Gaming monitoring dashboard showing GPU thermals, CPU clocks, PCIe/BAR status, and hardware statistics](docs/screenshots/gaming.png) |

The **AI** screenshot shows task-specific prompt/decode timing entries and the selected NVMe monitoring tile. The dashboard supports llama.cpp/Bionic timing logs and selectable Project Maya log parsing. **AI NVMe CACHE I/O** measures aggregate I/O on the selected NVMe namespace; it cannot identify which requests or processes generated the disk traffic. The **Gaming** dashboard reports AMDGPU and Linux hardware telemetry, not game-rendered FPS unless a real frame source is connected.

## v0.8.0 development modules

The `0.8.0-dev.14` checkpoint hardens the two-worker architecture by resetting worker-private AI state on source generations, rejecting blocking nonregular log files, and testing in-flight shutdown/restart. Dev.13 also introduced generation-checked snapshots and detailed watchdog lateness reporting. Its original foundational modules remain:

| Subsystem | Owner | Driver writes? |
|---|---|---|
| GPU device descriptors | `src/gpu/device.{c,h}` | Opens read/write handles; does not issue writes |
| GPU readback | `src/gpu/readback.{c,h}` | **No** |
| Watchdog policy + DPM inspection | `src/gpu/policy.{c,h}` | Through `gpu/writes` only |
| Tuning ARM/APPLY/RESET and ranges | `src/gpu/tuning.{c,h}` | Through `gpu/writes` only |
| Low-level GPU-control writes | `src/gpu/writes.{c,h}` | **Yes: one synchronous API** |
| CPU, session, AI and NVMe collectors | `src/monitor/`, `src/ai/` | No GPU controls |

| UI subsystem | Owner | Responsibility |
|---|---|---|
| Shared view state and page identifiers | `src/ui/types.h` | Shared between UI and controller |
| Drawing and bars | `src/ui/draw.{c,h}` | Xlib only; no device writes |
| Canonical hitboxes and scrolling | `src/ui/layout.{c,h}` | Pure geometry; headless testable |
| Window lifetime and session X11 metadata | `src/ui/window.{c,h}` | Xlib only; compositor source passed explicitly |
| AI / GAMING renderers | `src/ui/ai_dashboard.c`, `gaming.c`, `dashboards.h` | Read-only explicit inputs; no GPU writes |
| Shared thermal presentation | `src/ui/status.{c,h}` | Read-only thresholds/palette |

The `src/ui/input.c` module translates X11 actions and enforces bounds on
AI log-path editing. Each controller event batch is capped at 64 events;
this mitigates event-queue starvation but does not eliminate synchronous
monitoring delays. For details see [dev.10 scheduling diagnostics](docs/V080_STEP10_SCHEDULER.md).

The fixed dashboard header, power/thermal bars, policy buttons, tabs and footer
controls now live in `src/ui/chrome.c`, receiving the workflow explicitly.
The diagnostic event ring and tracefs instance lifecycle are implemented in
`src/diagnostics/events.c` and `src/diagnostics/trace.c`; no X11 dependency.
The opt-in `src/app/poll_timing.c` profiler records *wall-clock* costs for
GPU clients, System+AI, desktop sessions, GPU power, DPM, tracefs, input,
GPU switching and rendering, plus deadline lateness by severity bucket. The profile is printed to stderr on **normal application shutdown**;
it makes no scheduling change and does not provide hard-real-time guarantees.

The AI and GAMING pages remain in `src/ui/ai_dashboard.c` and `src/ui/gaming.c`.
The new `src/ui/pages_monitor.c` owns the Clients, Power, Desktop, Events and Trace
views, `pages_tuning.c` renders the 3×2 tuning tile grid, and `pages_settings.c`
renders Settings, Help and About. `ui/pages.h` declares **read-only input contracts**;
`diagnostics/types.h` exposes event and trace snapshots independently of Xlib.
Settings/desktop page renderers no longer access application-global settings,
GPU selection or NVMe state. UI mutations, GPU tuning writes and watchdog
scheduling remain in the controller and `gpu/` modules. All control operations
remain in the original single-threaded event loop.
Splitting the modules does **not** guarantee 5 ms watchdog deadlines during
blocking AI/network or Linux telemetry reads. Tuning remains disarmed by default;
if the driver rejects a requested manual mode, the ARM state is cleared. Partial
multi-write driver changes are not atomic and need real-hardware testing.

The UI, 840 × 660 default window, settings schema and public `v0.7.0b` GitHub
release remain unchanged. The `make check-control-module` target tests
error paths using temporary file descriptors, not a live GPU. For architecture
and remaining work, see [GPU write-boundary notes](docs/V080_STEP5_GPU_CONTROLS.md)
and the [refactor roadmap](docs/V080_REFACTOR.md). A new `make check-ui-layout-module` target validates hitboxes and scrolling without a running X server; see [dev.6 UI architecture](docs/V080_STEP6_UI.md) and [dev.8 page modules](docs/V080_STEP8_PAGES.md).

## Build and run (Arch Linux / X11)

Install a C compiler, `make`, X11 headers, libcurl development headers and X11 runtime. To build:

```sh
make
make check check-config check-parser check-pcie check-audit check-maya-nvme check-modules check-settings-module check-ai-module check-cpu-module check-sources-module check-session-module check-readback-module check-control-module check-ui-layout-module check-ui-input-module check-dashboard-module check-pages-module check-events-module check-poll-timing-module check-ui
sudo make install
sudo -E /usr/local/bin/amd-gpu-diagnostic
```

The visual smoke tests need `xvfb-run` and `xauth`. Running the program without root is not supported for GPU power-policy tuning. `--version` and `--help` work without root. `--card N` chooses a DRM card on startup and overrides the saved preference for that invocation.

## Screens and actions

- **AI / GAMING** workflow buttons select the main dashboard.
- **Header** uses a larger GPU model label and a consistent `Target = … | Actual = … | LOCKED/PAUSED | Corrections = …` status line.
- **Settings** contains AI token/API/Project Maya log source, AI log parser, Auto/Manual log detection with an editable absolute filename, a saved AI NVMe device selector, compositor source, detected AMD card selector and an inert overclock unlock placeholder.
- **Power** contains power, voltage, current, power caps, equivalent load estimates, power profile and policy. GPU thermals, clocks and VRR are under Gaming/Desktop.
- **Tuning** has compact 3×2 tiles, firmware-range labels, and a 2×2 ARM/APPLY/RESET/REFRESH group inside the safety tile. RESET STOCK is available; do not confuse colored zones with safety certification.
- **Desktop** reports session, X server, desktop, window manager, compositor, their installed Arch package versions and package build dates where known, plus DRM modes/refresh/VRR and explicitly unverified VSync/buffering.
- **Events** shows the 15 most recent significant events it still retains, leaving high-frequency GFXOFF/DPM changes out of that *view*. CSV events are unaffected.
- The square **?** opens keyboard and mouse help. **About** contains origin, license and development credits.

### Settings and privilege boundary

Settings persist in `~/.config/amd-gpu-diagnostic/settings.ini` in the **invoking user's** home directory (when started through sudo). Changes are saved atomically by dropping privileges before writing a mode-0600 file. The settings file is local, plain-text, and can be copied between systems; it is not a tuning profile. Editing the log filename accepts a typed absolute path; Enter saves; Esc cancels.

Settings changes do not require a restart. After choosing another AMD card, click **APPLY GPU**. The app opens the new device first, then pauses policy enforcement, clears clock locks and tuning ARM state, and rebinds telemetry. It **does not restore the old GPU's pre-existing policy**. It will not automatically enable the watchdog on the newly selected GPU. Do not switch during an active dangerous tuning session.

### Project Maya + AI NVMe monitoring (pre-beta addition)

The AI dashboard includes a compact **AI NVMe CACHE I/O** tile showing read and write
rates (MiB/s), observed peaks, and approximate device busy percentage from Linux
`/sys/block/nvmeXnY/stat`. Settings > **AI NVME CACHE SSD** cycles detected NVMe
namespaces and shows the kernel's model label. The chosen namespace (e.g. `nvme0n1`)
is saved in the invoking user's `settings.ini` as `ai_nvme_device`. An invalid or missing
device is shown as unavailable, never silently substituted for a different saved device.

**The readings count all I/O on that NVMe namespace, not exclusively Project Maya/DeepSeek
model traffic.** Sampling is approximately every 0.5 seconds, using monotonic elapsed time;
peaks reset upon changing drives or a counter reset. This is a monitoring-only feature;
there is no SSD writes, tuning, stress test, or hardware setting change.

Project Maya produces `maya-<model>.log` beside its model configuration. Select **Project
Maya (log)** for both AI DATA SOURCE and AI LOG PARSER, then choose the log path manually,
or leave discovery set to Auto for supported locations: `~/src/project-maya`,
`~/project-maya`, or `~/Downloads/project-maya`. Project Maya's verified GLM engine
log entries include `glm stat:`, `glm prefill:`, `glm fast:` and `glm split`.
The parser also retains `ds4`-prefixed engine lines without pretending a different
format is compatible. The dashboard shows last reported decode rate, prefill rate
when explicitly available, VRAM expert hit %, RAM fetches per token and SSD reads/
wait time per token when logged. Values are completed log summaries, **not live
per-token measurements**. Missing metrics are labeled unavailable, not inferred.

If Project Maya uses an unusual custom location or log naming, use the **manual absolute
log path** setting. For troubleshooting, the engine log is typically named
`maya-<model>.log` in the Project Maya root. Project Maya supports model
expert tiers spanning GPU VRAM, system RAM and NVMe; the filesystem I/O monitor is
independent of its log parser. Upstream reference:
[Project Maya](https://github.com/mw00/project-maya).

### AI parser notes

The llama.cpp/Bionic task-aware parser is fully implemented, including prompt/eval rates and host-RAM prompt cache messages from a 512 KiB log tail. Other log parser modes are **experimental placeholders**: users can select them, but task timing is not synthesized from unrelated log formats. Ollama/vLLM/LM Studio API token-source detection remains separate from log-file parsing. Prompt cache occupancy is an event-based *last-reported* value, not a sampled RSS reading. The 24 GiB Bionic host cache is separate from GPU VRAM.

### Desktop diagnostics notes

Version and **package build dates** come from local `/var/lib/pacman/local/*/desc` metadata, not a binary compile timestamp. On unsupported distributions the dates may show *unknown*. Display refresh is read from reported DRM CRTC state when debugfs access permits. DRM `vrr_enabled` describes the current kernel-reported state if exposed; it does not verify per-game frame pacing. **VSync/number of buffers cannot be reliably inferred from the compositor process, so they are marked unverified.**

### Known issues / before tagging the beta

- Validate on your RX 7900 XTX with picom/XFCE/XLibre, including the Settings screen's GPU selection and custom Bionic log path.
- Validate a second AMD GPU, if possible, and test the pause/re-arm behavior during switching.
- The code has been built under strict `-Werror` and tested against synthetic X11 telemetry; this does not guarantee safe hardware write behavior across drivers.
- The overclock unlock feature does nothing in this beta by design.


## v0.7.0b stabilization audit

Before the GitHub beta tag, this source package received a correctness and performance pass. The main fixes are: CPU fan file-descriptor/path handling, libcurl authentication lifetime and bounded response buffering, cached `/proc`-based llama-server discovery, strict integer settings parsing, real interval CPU-process activity, and removal of stale/unverified AI rate and debug output. See [`docs/AUDIT_AND_ARCHITECTURE.md`](docs/AUDIT_AND_ARCHITECTURE.md) for tests, architecture and remaining limitations. The current window/tile organization and tuning safeguards are unchanged.

This tarball has a **flat project-root layout** for updating your existing checkout directly. Your previous builds remain in `archive/`, which is excluded from Git. The screenshots above are genuine v0.7.0b captures; they show actual device readings, not synthetic performance claims.

## License

Copyright (C) 2026 Kevin Kolasa. Source and redistribution under **GPL-3.0-or-later**. Full text in `LICENSE`.

### Second-pass pre-release audit

The v0.7.0b source includes a second-pass stability audit, regression tests, and hardware testing checklist in [`docs/DEEP_AUDIT_2.md`](docs/DEEP_AUDIT_2.md). In particular, watchdog enforcement and synchronous telemetry share one event loop: configured microsecond intervals are desired sampling intervals, **not hard real-time guarantees**.

For GitHub release assets, the source package contains a flat root layout; extract into an existing checkout only after backing up local changes. The X11 UI test harness compiles without optimization to keep headless CI responsive; the production binary uses the standard optimized build flags.

## Polling profiler (opt-in, dev.9)

`AMD_GPU_DIAG_TIMING=1` enables monotonic wall-clock timing for the four
expensive non-watchdog collectors. The summary prints to stderr after a normal
GUI exit (no periodic logging or disk writes). For example, from a terminal:

```sh
AMD_GPU_DIAG_TIMING=1 ./amd-gpu-diagnostic
```

Preserve the environment variable if the existing GPU-write setup requires
privileged launch (e.g. `sudo -E`). These numbers are elapsed poll costs, not
proof of causality for watchdog deadline misses. As of **0.8.0-dev.11**, the CPU/process/NVMe and AI HTTP/log samplers run on one
background *read-only* worker with copied inputs and snapshots. The foreground
thread retains exclusive GPU-control authority and Xlib. The timing summary's
System+AI duration is background wall time, **not foreground blocking time**.
Client scans, DRM/session probes, tracefs and screen draws still run in the
foreground, so configured microsecond intervals remain best-effort rather than
hard deadlines. If worker startup fails, the app prints a warning and falls back
to synchronous sampling (no isolation). See
[`docs/DEV11_MONITOR_WORKER.md`](docs/DEV11_MONITOR_WORKER.md) for synchronization,
shutdown and testing details.


## v0.8.0-dev.11–dev.13 monitoring workers (development checkpoints)

The read-only monitoring worker samples system CPU, processes, NVMe and AI
HTTP/log data without holding up each 5 ms GPU policy iteration. Only completed
snapshots are consumed by the GUI. The GPU policy/tuning writers remain on the
main thread; no new control privileges or overclocking are enabled. The monitor
is designed to coalesce bursts of requests instead of queueing unlimited work.
Run `make check-monitor-worker-module` for the isolated concurrency tests.

In **dev.12**, expensive DRM client enumeration (`/proc/*/fdinfo`),
compositor process discovery, DRM/KMS state, and Arch package version/build-date
reads run in a second independent read-only thread. A 1-second request cadence
coalesces stale pending requests; desktop/package sampling runs at ~10-second
intervals. The foreground only consumes complete copied results, performs
X11-specific enrichment, and rejects snapshots from old card selections or
compositor-source settings. No GPU descriptor or control-writer is shared
with the discovery worker. See [dev.12 discovery design](docs/V080_STEP12_DISCOVERY.md).

**Dev.13:** The AI monitor and discovery workers now carry generation IDs to
reject results from earlier A → B → A selections. The previous synchronous
`lms ps` subprocess was removed because it did not supply real token-rate data
and could block shutdown. Use `AMD_GPU_DIAG_TIMING=1` during matched idle and
AI-load sessions, then compare log files with
`scripts/compare-watchdog-logs.sh idle.log inference.log`. Quantiles are
approximate histogram ceilings of *late* cycles, not hard-real-time guarantees.
See [dev.13 stability notes](docs/V080_STEP13_STABILITY.md).

`make check-discovery-worker-module check-worker-lifecycle check-clients-module` exercises concurrency,
provenance, and DRM fdinfo parsing without hardware. Worker durations are
reported by `AMD_GPU_DIAG_TIMING=1`, but those durations represent **worker
wall time**, not blocking time on the watchdog thread. The remaining foreground
operations (GPU power/DPM, tracefs, X11 rendering and event handling) can still
miss scheduling deadlines. No hard-real-time claim is made.

The app still requires physical GPU and workload testing before v0.8.0b.

### dev.14 worker and log-file hardening

A source generation change resets **worker-private** AI counters and prompt-cache
history, not just the GUI copy. Manually selected log paths are accepted only
when they resolve to regular files; FIFOs/devices/directories are rejected
without a blocking open. A new `make check-worker-lifecycle` fixture verifies
orderly stop, pending-sample cancellation and restart for both read-only workers.
These changes do not promise hard-real-time GPU enforcement or immediate
thread cancellation; active read-only callbacks still finish before shutdown.
See [dev.14 hardening notes](docs/V080_STEP14_HARDENING.md).

### dev.15 — beta-readiness diagnostics

The optional watchdog profiler now measures **start-to-start intervals between
actual watchdog checks** alongside its deadline-miss and lateness statistics.
It reports the maximum observed gap, average gap, counts exceeding 10/50/500 ms,
gaps more than twice the preceding configured interval, and misses/minute.
Unlike the previous positive-lateness-only percentiles, these intervals include
cycles that met their scheduled deadlines. They do not prove hard-real-time safety.

Monitoring snapshots no longer copy live worker-owned Linux sensor file-descriptor
numbers into the foreground; the worker retains responsibility for closing the
real handles. The UI still receives sampled temperatures, power and labels.
Use `scripts/capture-watchdog-profile.sh idle` followed by
`scripts/capture-watchdog-profile.sh ai-load`, close each normally, then compare
reports with `scripts/compare-watchdog-logs.sh`. These scripts are opt-in and do
not start inference or tune the GPU themselves. See
[dev.15 hardware-validation procedure](docs/V080_STEP15_BETA_READINESS.md).



### v0.8.0-dev.16 UI refinements

Tuning includes **Browse AMD Reference Database**, a read-only scrolling catalog of the built-in Radeon reference specifications (game/boost clocks, typical board power, VRAM and architecture). These are not tuning limits; the firmware controls still take precedence. Settings displays its controls in two columns, with actions below their labels. The About tab is distinct from the `?` Help button.
