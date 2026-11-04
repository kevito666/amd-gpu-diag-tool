# Changelog

## 0.8.0-dev.16 — tuning/settings UX and GPU reference browser

- Enlarge only the top-line GPU model name by one X11 font step (9x15 bold where available) and standardize the policy status to `Target = … | Actual = … | LOCKED/PAUSED | Corrections = …`. Keep the compact header positions and clipping.
- Fix About tab routing: ninth ordinary tab now selects About, while the separate `?` button exclusively selects Help.
- Remove overlapping AMD reference/database labels from Tuning; add a read-only, two-column, scrollable GPU reference browser with a return-to-tuning button.
- Center disabled and enabled tuning stepper labels using the same X11 font geometry.
- Reorganize Settings as a two-column grid with each option button immediately below its heading; preserve settings behavior, persistence, and GPU-switch safeguards.
- Expand headless layout/navigation regressions and virtual-X11 page coverage.


## 0.8.0-dev.15 — measurable watchdog intervals and beta validation

- Record actual watchdog check-to-check gaps under optional `AMD_GPU_DIAG_TIMING=1`; include mean, worst gap, severity counts (>10/50/500 ms), gaps >2× the prior requested interval, duration and missed deadlines/minute. No new polling I/O, allocations or GPU control writers.
- Detach worker-owned hwmon FD numbers from published telemetry snapshots, leaving their actual ownership and closure unchanged. Preserve sensor readings and UI behavior.
- Add an opt-in local idle/AI-load capture script and enhance comparison reporting without modifying workloads, tuning or installed binaries.
- Add deterministic profiler interval/descriptor-isolation regression tests. Real RX 7900 XTX inference-load timing and multi-GPU switching remain unverified.

## 0.8.0-dev.14 — worker lifecycle and log-file hardening

- Reset worker-private AI token counters, task history, parser state and prompt-cache readings when its source configuration generation changes, including A -> B -> A changes with coalesced requests; retain independent CPU sensor samples and their descriptor ownership.
- Open user-selected AI logs nonblocking, validate the opened inode with `fstat`, and reject FIFO, device, directory and other nonregular inputs before attempting buffered reads. Prevent indefinite FIFO-open waits from blocking worker shutdown.
- Add synthetic, synchronized worker lifecycle tests for in-flight shutdown, cancellation of queued requests, idle restart and repeated stop on both read-only workers.
- Expand existing standalone sources and audit fixtures to cover nonregular log paths and backend history resets. Preserve layout, GPU write ownership, on-disk settings and current watchdog interval.
- Limitations: pthread join still waits for the active bounded sampler callback; Linux scheduling remains best-effort. RX 7900 XTX latency and multi-GPU switching require live verification.


## 0.8.0-dev.13 — worker provenance and latency diagnostics

- Add a monotonically increasing AI/NVMe source generation to read-only
  monitor requests and results; reject stale results even if settings move
  A -> B -> A before a slow sample completes.
- Increment discovery generations for compositor source changes as well as
  GPU switches, invalidating old client/session results after A -> B -> A.
- Trigger a fresh asynchronous monitor request immediately after a source
  change instead of waiting for the next periodic sampler tick.
- Remove the unbounded `lms ps` shell pipeline from LM Studio token polling.
  It never reported a measured token rate and could indefinitely delay the
  monitor worker and orderly shutdown. Bionic/llama.cpp logs and /slots remain.
- Add optional fixed-size positive-lateness histograms and approximate p50,
  p95, p99 bucket ceilings; add an offline idle/load log comparison script.
- Expand deterministic worker, compositor, and timing regression coverage.
- Preserve single-writer GPU control, X11 behavior and the 5 ms best-effort
  watchdog; physical RX 7900 XTX measurements remain pending.

## 0.8.0-dev.12 — off-thread GPU-client and desktop discovery

- Extract Linux AMDGPU client fdinfo parsing and /proc enumeration into
  `monitor/clients.c`, retaining per-process activity deltas and roles.
- Use a separate read-only `monitor/scan_worker.c` pthread to collect client
  and desktop/session snapshots, avoiding foreground 1-second client scans.
- Worker scans desktop/KMS/compositor and package metadata at a ~10-second
  cadence; foreground handles only the X11-specific window-manager queries.
- Use copied GPU identity and debugfs pathname, never a shared live sysfs
  descriptor. Discard results when GPU generation or compositor source changes.
- Coalesce unstarted requests and preserve existing telemetry behavior and
  the single GPU-control writer. Synchronous fallback is documented if worker
  creation fails; it does not guarantee watchdog isolation.
- Add module-level DRM fdinfo and concurrency fixtures. No new non-test
  dependencies beyond the existing pthread/Xlib/libcurl toolchain.
- Watchdog remains best effort, with no real-hardware latency benchmark yet.


## 0.8.0-dev.11 — asynchronous read-only monitoring and watchdog isolation

- Move CPU/core/process sampling, NVMe counters, AI HTTP polling, llama-process
  discovery and AI log parsing onto one read-only pthread worker. GPU policy
  enforcement, tuning writes, GPU switching and all Xlib work remain on the
  foreground thread; no second GPU control writer is created.
- Use a bounded latest-request queue, immutable input copies and synchronized
  result snapshots. Foreground settings changes invalidate stale AI readings;
  outputs from outdated backend/parser/NVMe selections are discarded.
- Keep sampler-owned hwmon descriptors private and close them on orderly worker
  shutdown, including CCX, fan, power and motherboard handles.
- Report background sampler wall time through opt-in polling instrumentation;
  mark token activity delayed if a result is older than 5 seconds.
- Provide a synchronous degraded fallback if pthread creation fails (with a
  warning); this fallback does not isolate watchdog timing.
- Add hardware-free concurrency, settings-provenance and descriptor-lifetime
  tests. Link with pthreads, retain C/Xlib and the existing compact GUI.
- **Limitations:** GPU client scans, desktop/DRM sampling, rendering and tracefs
  remain foreground work. Linux scheduling offers no hard 5 ms guarantee.


## 0.8.0-dev.10 — watchdog lateness profiler and X11 input boundaries

- Add monotonic watchdog *lateness duration* measurement when a scheduled
  loop deadline is exceeded, including max/average lateness, >=1/5/50/500 ms
  severity counts, and estimated skipped intervals. Preserve existing
  scheduling: **this is not a hard-real-time watchdog**.
- Extend optional `AMD_GPU_DIAG_TIMING=1` wall-time profiling to DPM
  monitoring, GPU power sampling, tracefs pumping, X11 input, and GPU
  switching. No periodic log I/O; data are reported only on normal exit.
- Move X11 keyboard-action translation, page-navigation deltas, and
  bounds-checked AI log-path input into `ui/input.c` with a standalone test.
- Bound each X11 event-dispatch batch to 64 events, leaving further work
  for the next watchdog cycle rather than draining an unbounded queue.
- Retain the same GPU-policy write authority, ARM/APPLY safeguards,
  persistent settings, readback formats, and compact UI geometry.
- All existing strict build, module regressions and six X11 screen tests pass.
- AI HTTP, log parsing, and other synchronous monitoring work can still
  block enforcement; no concurrent GPU write thread is introduced here.


## 0.8.0-dev.9 — controller and diagnostic boundaries (development only)

- Extract fixed Xlib header, tabs and footer into `ui/chrome.c`, passing an
  explicit workflow value; no change to geometry or device write operations.
- Extract event history and optional CSV logging into `diagnostics/events.c`.
- Extract tracefs instance discovery, lifecycle and nonblocking pump into
  `diagnostics/trace.c`; no new access permissions or trace events.
- Add opt-in monotonic polling telemetry (`AMD_GPU_DIAG_TIMING=1`) for client
  scans, system/AI sampling, desktop sampling and rendering. Report only on
  normal shutdown, outside the watchdog loop. No concurrent GPU writer.
- Ignore object/dependency build products and local archive snapshots in Git.
- Add independent event-ring and poll-timing module tests. All previous
  regression and X11 smoke tests continue to pass.
- Main application translation unit reduced from 1,998 to 1,663 lines.
- Keep 840x660 layout, 5 ms default policy setting, GPU-control safeguards,
  source compatibility and persistent settings unchanged. The watchdog
  still runs on the same thread as blocking monitor operations.


## 0.8.0-dev.8 — remaining UI page modules (development only)

- Move Clients, electrical Power, Desktop/session, 15-event view, and trace
  presentation into `src/ui/pages_monitor.c`.
- Move the read-only 3x2 Tuning grid into `src/ui/pages_tuning.c`; ARM/APPLY
  and all firmware writes remain exclusively in the established GPU control API.
- Move Settings, Help, and About rendering into `src/ui/pages_settings.c`.
  Pass a `UiSettingsView` snapshot for selected GPU, persistent settings,
  parser labels, NVMe and path-edit state rather than accessing app globals.
- Move diagnostic ring/trace data types to `src/diagnostics/types.h`, so the
  renderer can read events without including the app controller.
- Add an independent page-module API/link fixture and retain the complete
  strict application, parser, GPU-control, and virtual X11 regression suites.
- Preserve visual layouts, hitboxes, settings schema, polling and watchdog
  scheduling; the main C source now contains 1,998 lines (2,322 in dev.7).
- This checkpoint makes **no new hardware-write or concurrency claims**.

## 0.8.0-dev.7 — Independent AI and GAMING dashboard renderers

- Move `draw_ai_dashboard` into `src/ui/ai_dashboard.c` and
  `draw_gaming_dashboard` into `src/ui/gaming.c`, compiled and linked as
  independent objects. AI and Gaming renderers take only read-only telemetry,
  an explicit AI source snapshot and precomputed event summary. No direct
  access to the application's mutable global settings, event ring or runtime
  parser state from these page modules.
- Add `src/ui/dashboards.h` for immutable renderer input contracts, and
  consolidate the shared thermal threshold/palette routines in `ui/status`.
- Keep the UI's central dispatcher and remaining Clients, Power, Tuning,
  Desktop, Events, Trace, Settings, Help and About views in the controller
  until subsequent extraction checkpoints.
- Add `make check-dashboard-module` to link page renderers independently of
  `src/amd-gpu-diagnostic.c`, with temperature boundary and input tests.
- Verify AI and GAMING renders are pixel-identical to dev.6 on a 1280x900
  virtual X11 display. All six page smoke tests pass.
- Main controller decreased from 2,667 to 2,322 lines. No layout, click,
  polling, settings, sysfs write or watchdog scheduling changes.

## 0.8.0-dev.6 — Shared Xlib UI infrastructure

- Extract shared UI view identifiers, window state, and compact layout constants
  into `src/ui/types.h`, without bringing hardware control into the UI.
- Extract X11 colors, typography, clipping-aware text, bars, buttons and panel
  headers into `src/ui/draw.{c,h}`.
- Extract canonical tab, footer, workflow, Tuning, AI log and Settings hitboxes,
  viewport measurement and scrolling into `src/ui/layout.{c,h}`.
  Drawing and click handling continue to use the same functions.
- Extract X11 window initialization/resource teardown and compositor/WM property
  queries into `src/ui/window.{c,h}`; compositor source is an explicit argument.
- Add `check-ui-layout-module` pure-geometry tests for hitbox edges, page
  extents, resizing and scroll clamping; keep all existing X11 smoke tests.
- Compare AI, GAMING, Settings and Tuning synthetic captures at 1280x900:
  pixel-identical to dev.5, using the same virtual-X11 test harness.
- Main application file reduced from 3,120 to 2,667 lines. No page redesign,
  firmware-write changes, watchdog timing changes or new dependencies.

## 0.8.0-dev.5 — GPU write boundary and tuning/watchdog extraction

- Extract selected GPU sysfs/hwmon descriptor lifecycle into `gpu/device`, including
  initialization, discovery, and closing all owned handles.
- Extract policy selection, DPM levels, 5 ms policy watchdog implementation and
  slower DPM observation into `gpu/policy`; preserve the single event-loop
  schedule and enforcement semantics. No new threads or privilege expansion.
- Extract tuning state, refresh, ARM/APPLY/RESET, and edits into `gpu/tuning`,
  retaining driver/firmware range checks and the original compact Xlib UI.
- Route **all GPU-control sysfs writes** through `gpu/writes` (policy, DPM,
  overdrive, power cap, fan PWM). Tracefs debug controls remain independent.
- Disarm the tuning session immediately when the driver rejects manual mode,
  avoiding a stale armed state after a failed APPLY. Multi-write updates are
  still non-atomic and hardware testing is required.
- Add independent `check-control-module` synthetic descriptor tests for
  readback, policy rejection, ARM safety, clock-level parsing and descriptor
  cleanup; retain the full integration and X11 regression suite.
- Main file now about 3,120 lines (4,140 in dev.4), with no UI, settings
  format, polling cadence, or externally visible control redesign.


## 0.8.0-dev.4 — GPU readback and desktop session ownership

- Move AMDGPU telemetry (hwmon, fan, clocks, power, GFXOFF, PCIe negotiated
  link and active BAR aperture) to `src/gpu/readback.{c,h}` with *read-only*
  functions and borrowed sysfs handles. Preserve controller ownership of GPU
  sysfs policy/tuning writes and their established order.
- Move Linux desktop/session and DRM/KMS collection into
  `src/monitor/session.{c,h}`, keeping Xlib window/selection properties in the
  application's UI. Pass compositor source explicitly, eliminating one global
  settings dependency from the session collector.
- Centralize `Gpu`, `Sysfs`, `PowerTelemetry`, `GpuClient`, `ClientMonitor`
  and `SessionInfo` types in narrow shared headers instead of duplicate copies
  in the main file. Move the reused numeric fd helper into `core/util`.
- Make PCIe/BAR regression independent of the monolithic GUI, and add
  `check-readback-module` and `check-session-module` for synthetic hwmon,
  compositor, and KMS/VRR/refresh data. Extend GitHub CI gates.
- Reduce the main translation unit to around 4,140 lines (from 4,849 in dev.3).
  No UI layout, configuration schema, polling cadence, watchdog scheduling or
  firmware-write behavior changes. No public Git tag.

## 0.8.0-dev.3 — AI discovery, bounded log I/O, hardware sensors, desktop probes

- Extract local API HTTP transport, bounded response accumulation and connection
  checks to `src/ai/http.{c,h}` with the original caller limits preserved.
- Extract 15-second cached llama-server `/proc` discovery, command-line parsing,
  model metadata, and host-RAM cache-limit lookup to `src/ai/llama_process.{c,h}`.
- Separate log auto-discovery (Bionic, Project Maya, Ollama, vLLM) and the
  512 KiB complete-line tail reader into `src/ai/log_files.{c,h}`. Return
  owned buffers through an explicit allocation/cleanup API.
- Move CPU/CCX, CPU fan/power and motherboard hwmon discovery to
  `src/monitor/sensors.{c,h}` and share read-only sysfs numeric parsing via
  `core/util`. Introduce one internal `monitor/system_state.h` for shared
  telemetry types; hardware policy objects remain separate.
- Extract XLibre package probes and bounded `/proc/PID/environ` reads to
  `src/monitor/desktop_probe.{c,h}`. Truncated environment fields are ignored.
- Add isolated backend/desktop/sensor tests, maintain legacy regression and
  synthetic X11 checks; preserve the root-owned GPU tuning/write paths.
- No concurrency changes or public release tag. Version is `0.8.0-dev.3`.

## 0.8.0-dev.2 — Settings, llama log parsers, CPU telemetry modules

- Isolate user configuration loading and atomic persistence behind `config_settings_load/save`, preserving the original settings file and privilege boundary.
- Move deterministic llama.cpp/Bionic task timing and host prompt-cache parsing into `src/ai/log_parser.{c,h}`; the app retains log discovery/runtime state.
- Move `/proc/PID/stat`, CPU per-core load and interval-based process ranking into `src/monitor/cpu.{c,h}` with a narrow application adapter.
- Add three standalone module regressions and wire them into the Makefile / CI. Existing integration and X11 UI smoke tests remain unchanged.
- Preserve display, control paths, polling cadence and original beta `v0.7.0b`; no GPU writes or scheduler changes.


## 0.8.0-dev.1 — Modularization checkpoint 1 (development only)

- Begin the post-`v0.7.0b` modular C/Xlib refactor, without touching the
  published beta release.
- Extract shared file/text/time helpers into `src/core/util.{c,h}`.
- Extract read-only NVMe namespace telemetry into `src/monitor/nvme.{c,h}`.
- Extract Project Maya / DS4 log parsing into `src/ai/maya_log.{c,h}`.
- Add explicit module headers and independent module API regression coverage.
- Compile application modules as separate object files with dependency tracking.
- Keep production `-O2`; compile test fixtures with strict warnings at `-O0`
  to avoid repeatedly optimizing the legacy monolith in each fixture.
- Preserve settings format, GPU policy/tuning behavior, original dashboard
  layout, and all existing test fixtures.
- Document planned extraction stages and non-goals in `docs/V080_REFACTOR.md`.


## v0.7.0b — GitHub beta release candidate (2026-10-25)

- Added real AI and Gaming screenshots (latest AI performance log capture and original v0.7.0b Gaming capture); finalized GitHub release documentation and About-page credits.
- Keep synthetic X11 smoke-test compilation unoptimized for timely CI; release application remains optimized.

### Second-pass stability and performance audit

- Make policy target changes transactional with respect to driver write failures; preserve the existing watchdog target/lock state when a write is rejected.
- Guard manual DPM clock selection and tuning reset against stale desired-policy state after rejected writes; note that multi-write driver sequences remain non-atomic.
- Clear stale token-rate counters on AI-source change; refresh log discovery immediately after parser/path changes; clear prompt-cache history when log parsing is disabled.
- Limit local HTTP downloads to the destination buffer instead of accumulating up to 1 MiB only to discard oversized responses.
- Run expensive per-core sysfs CPU-frequency scans at two-second cadence, without slowing 500 ms CPU-load/temperature sampling.
- Prevent the external log viewer from executing as root without a valid non-root sudo invoker.
- Extend audit regressions; document the unresolved single-threaded watchdog scheduling risk in `docs/DEEP_AUDIT_2.md`.

### Pre-release stabilization audit

- Fix CPU fan PWM-max sensor descriptor leak and prevent mutation of the saved live sensor path.
- Bound HTTP response allocation to 1 MiB; fix libcurl reused-handle header lifetime and reject truncated/invalid API bodies.
- Replace repeated llama-server `ps|grep|awk` subprocess discovery with one cached `/proc` scan (15-second TTL); preserve arguments containing spaces and avoid printing model paths/API keys to debug logs.
- Reject malformed numeric settings rather than silently converting them to option zero.
- Replace top-process lifetime CPU-time ranking with true two-second CPU deltas and display activity percent.
- Do not report inferred token throughput from experimental model-list/metrics API responses; clear stale live activity on missing llama API data.
- Periodically refresh llama model metadata; drop supplementary groups before launching the user-facing log viewer.
- Bound GPU/motherboard label formatting; add hardware-free audit regression tests and architecture notes.


- New persistent Settings view, replacing the full-width Help tab with Settings and a small `?` Help button.
- Move AI API source choice to Settings (llama.cpp, Ollama, vLLM, LM Studio, heuristic).
- Add log format selection and manually editable absolute log filename with automatic detection option. llama.cpp/Bionic timing parsing is implemented; alternative formats are explicitly experimental.
- Add configurable compositor detector (auto, X11 owner, DRM client, process scan, off).
- Enumerate available AMD DRM GPUs and allow on-demand rebinding in Settings. A switch pauses the watchdog and clears tuning locks/ARM; users must manually reactivate enforcement. Selection persists.
- Add safe, nonfunctional overclock-unlock placeholder. There is no firmware unlocking or unsafe write behind it.
- Simplify Power to measured electrical quantities, estimates labelled as such, electrical limits and policy; move temperature and clock details to Gaming.
- Show session, X server, desktop, window manager and compositor package versions and build dates when available from Arch package metadata; show unknown when not available.
- Show DRM-reported refresh rate, active modes and VRR state when exposed; VSync and buffering clearly shown as unverified because they are per-compositor/app and cannot be inferred reliably from X11 existence.
- Events now shows up to 15 significant recent records, filtering high-frequency GFXOFF/DPM noise from the display; ring capacity expanded to 512.
- Update project origin story, Tohoku attribution, and GPL license wording; adjust About layout so license is reachable by scrolling.
- Refined Tuning action controls into the SAFETY/WORKFLOW tile as a compact 2×2 ARM/APPLY/RESET/REFRESH grid, reachable at the default window size.
- New config persistence test, additional X11 screen smoke tests, GitHub Actions build gate, LICENSE, release notes, and source packaging.

### Beta limitations

- Hardware switching, policy writes and telemetry should be tested on real multi-GPU AMD configurations.
- GPU switching does not undo any policy or prior firmware changes on the previously selected GPU; use RESET STOCK if necessary **before** switching.
- Only llama.cpp/Bionic has detailed task-aware timing parsing; other parser selections are visible but experimental and do not claim metrics.
- VSync, triple buffering, G-Sync compatibility and VRR state can be unreported or unavailable, depending on the running driver and compositor.
- Package version/build dates are currently Arch/pacman oriented.

## v0.6.7
- Consolidated Gaming power, temperature and memory tiles; added CPU clocks, PCIe link and actual BAR aperture diagnostics.

## v0.6.6
- Prompt-history cache monitor for llama.cpp/Bionic host RAM, including save/load/eviction/rejection status.

## v0.6.5
- Compact Tuning and 10 event rows; expanded AI performance log and GPU monitoring.

## v0.6.4
- Compact X11 UI overhaul.
