# AMD GPU Diagnostic Tool — v0.8.0 modularization

## Release discipline

- Published baseline: tag `v0.7.0b`, commit `595f6d6`.
- Development branch: `refactor/v0.8.0`.
- Current checkpoint: `0.8.0-dev.16` (**not** a public beta).
- No Git push, tag, or system-wide install is required for this checkpoint.
- Core invariants: same settings format, same tuning safeguards and single
  writer to AMDGPU firmware/sysfs paths, same Xlib UI behavior, same telemetry
  units/provenance and same 840x660 default geometry.

## Step 1 (implemented)

- `src/core/util.{c,h}`: shared time, text, path, and fd utilities.
- `src/monitor/nvme.{c,h}`: NVMe namespace parsing/discovery/rates.
- `src/ai/maya_log.{c,h}`: Project Maya / DS4 log parsing.
- `src/gpu_specs.{c,h}`: existing device/spec database, unchanged.
- Main implementation reduced by 243 lines (6313 -> 6070).
- `make` uses separate `.o` files and `-MMD -MP` dependency files.
- Existing integration/regression tests temporarily include the legacy
  application source. New `make check-modules` is genuinely independent.
- No multithreading, GPU control changes, or visual redesign in this step.

## Step 2 — staged extraction (dev.2 completed; more remains)

Implemented in `0.8.0-dev.2`:

1. `src/config/settings.{c,h}` loads/saves the same `settings.ini` format.
   The app retains ownership of its current settings struct and applies source
   selection after loading; the configuration module never changes GPU policy.
2. `src/ai/log_parser.{c,h}` parses llama.cpp/Bionic timing entries and host-RAM
   prompt cache reports. Parsing is independent of the GUI, libcurl, and GPU.
   The timing parser modifies its input buffer, documented in its interface.
3. `src/monitor/cpu.{c,h}` samples /proc core load and interval-based top
   processes, and supplies the strict `/proc/PID/stat` parser. The app keeps a
   minimal state adapter while hardware temperature and CPU clock discovery
   remain local to the legacy application source.
4. Add three module-only regressions (Settings, AI log parser, CPU sampling),
   while preserving the original integration test suite and six X11 renders.
5. No change to the 840x660 UI, NVMe/Project Maya behavior, sysfs writes,
   watchdog scheduling, CLI flags, or on-disk settings schema.

Remaining for the next checkpoint:

1. Separate AI log discovery, process cache-limit discovery and live API
   backend polling (currently depend on SystemTelemetry and application state).
2. Extract system sensor discovery and desktop/session collection with
   explicit owned file descriptors and sample snapshots.
3. Replace further monolith-inclusion fixtures with exported testable APIs.

## Step 2 continuation — dev.3 completed

- `src/ai/http.{c,h}` owns libcurl request buffering and reusable handle.
- `src/ai/llama_process.{c,h}` owns cached llama-server process metadata,
  model settings and host-RAM prompt-cache limit discovery.
- `src/ai/log_files.{c,h}` owns log path detection and bounded line-aligned
  log tail reads. The application still correlates parsed events and activity.
- `src/monitor/sensors.{c,h}` owns read-only hwmon discovery; its file
  descriptors remain in `SystemTelemetry` until future lifecycle cleanup.
- `src/monitor/desktop_probe.{c,h}` owns desktop process environment reads and
  XLibre package probing. Full session assembly remains in the main unit.
- `src/monitor/system_state.h` contains shared internal sampling types; no
  public ABI is established, and no GPU sysfs write authority moves.
- `make check-sources-module` tests new interfaces independently of X11.
- Main unit reduced from 5,611 lines to about 4,800; extraction is not
  claimed to make runtime faster by itself.

Remaining before Step 3: isolate more of desktop/session aggregation, AI API
state handling, and shared GUI models; retire monolith-inclusion fixtures
without weakening their existing assertions.

## Step 2 completion — dev.4 readback/session extraction

- `src/gpu/readback.{c,h}` handles **reads only**, preserving sysfs handle
  ownership in the controller and reusing the 10 Hz fast/1 Hz slow sampling
  cadence. The existing firmware-write functions remain in the main unit.
- `src/monitor/session.{c,h}` owns Linux desktop, DRM/KMS and compositor
  process collection. Source selection is passed as `CompositorSource`, not read
  via the application's global settings. The X11 enrichment remains in the
  GUI's Xlib code; future extraction will move it into `ui`.
- Shared type headers are now independent of Xlib, but several original
  integration fixtures still include the full source. The PCIe/BAR regression
  no longer does, and two new standalone tests validate readback and session.
- Main source reduced to ~4,140 lines, with no expected runtime change.

## Step 3 (hardware write boundary + UI primitives)

1. Consolidate sysfs device ownership and permission/error handling.
2. Extract watchdog and tuning policy logic without changing scheduling yet.
3. Extract Xlib primitives, tile geometry, button hitboxes, and pages;
   assert click rectangles agree with rendered rectangles.
4. Add synthetic fault injection for write errors, GPU switches, and reset.

## Step 3 GPU control extraction — dev.5 completed

- `gpu/device` now owns GPU sysfs fd opening/discovery and closing.
- `gpu/policy` owns fast policy enforcement, slower DPM inspection, manual
  clock-selection, and target transitions; it does not import Xlib state.
- `gpu/tuning` owns current/edit/default tuning settings and ARM/APPLY/RESET.
- `gpu/writes` is the **single low-level entry point for AMDGPU control
  sysfs writes**; callers retain the established single-threaded ordering.
- Tests use a fake card directory and descriptors, including unavailable
  driver nodes, mismatched policy, and failed ARM/APPLY. No real GPU writes.
- UI page extraction, background telemetry scheduling,
  real-hardware verification, and thread separation are still outstanding.

## Step 3 UI page extraction — dev.7 (AI + GAMING)

- `ui/ai_dashboard.c` and `ui/gaming.c` implement the two workflow pages;
  `ui/dashboards.h` declares only read-only render data and an event summary.
- The dispatcher in the application controller assembles snapshots from
  settings, selected NVMe, Maya, AI log events, and the event ring. The
  renderers cannot change the running AI source or GPU firmware state.
- `ui/status.c` centralizes the GPU temperature/color threshold logic reused
  by the top header and the Gaming page.
- The 840x660 layout, 1280x900 fixture screenshots and all original UI
  interactions are unchanged; AI and GAMING test frames are pixel-identical
  to dev.6. Independent link/contract test: `check-dashboard-module`.
- Main controller now 2,322 lines. Clients, Power, Tuning, Settings and
  auxiliary pages will be moved only after explicit API boundaries are set.
- No event-loop or watchdog scheduling changes in this checkpoint.

## Step 4 (scheduler performance; hardware validation required)

1. Measure 5 ms watchdog jitter and missed deadlines on a real AMD GPU.
2. Move potentially blocking HTTP/telemetry polling off the enforcement path.
3. Introduce a bounded, observable sampling snapshot interface; keep a
   **single** GPU sysfs write authority and explicit synchronization.
4. Compare CPU overhead, polling latency, and source behavior against the
   published v0.7.0b binary before tagging v0.8.0b.

## Regression gate for each checkpoint

```sh
make clean
make -j"$(nproc)"
make check check-config check-parser check-pcie check-audit check-maya-nvme check-modules check-settings-module check-ai-module check-cpu-module check-sources-module check-session-module check-readback-module check-control-module check-ui-layout-module check-ui
```

The X11 smoke tests require `xvfb-run` and `xauth`. Source extraction by itself
is not a speedup for running code; incremental compilation speed is the
immediate measurable benefit. Firmware writes, multiple physical GPUs, and
live AI/NVMe samples need hardware validation before the next public beta.

## Step 3 UI infrastructure extraction — dev.6 completed

- `src/ui/types.h` centralizes `Gui`, `Rect`, `View`, `WorkflowMode` and layout constants.
- `src/ui/draw.c` owns typography, bars, panel borders and shared buttons.
- `src/ui/layout.c` contains canonical geometry used by both rendering and
  pointer dispatch; scrolling now receives workflow mode explicitly.
- `src/ui/window.c` owns X11 window/GC/fonts lifecycle plus read-only X11
  compositor/window manager property discovery, with explicit compositor choice.
- The main C file is 2,667 lines. Larger page rendering and event dispatch
  functions remain; they will be split in the next checkpoint.
- `make check-ui-layout-module` adds X-server-free rectangle and scrolling tests.
- AI, Gaming, Settings and Tuning captures were pixel-identical to dev.5 under
  Xvfb. No changes to GPU writes, settings schema, or scheduling.

## Step 3 UI page extraction — dev.8 (remaining pages)

- `ui/pages_monitor.c`: Clients, electrical Power, Desktop, Events (up to 15
  significant entries), Trace. Display data comes from collector-owned
  read-only data structures, with the chosen compositor source label supplied
  explicitly rather than fetched via global settings.
- `ui/pages_tuning.c`: read-only 3×2 tile drawing and ARM/APPLY/RESET/REFRESH
  button appearance. The GPU-control module continues to own firmware writes,
  limits, and ARM state; layout rectangles still come from `ui/layout.c`.
- `ui/pages_settings.c`: Settings, Help, About. `UiSettingsView` holds pointers
  to current settings, NVMe, and card inventory plus the path-editing state;
  no read/write access to controller globals from rendering.
- `diagnostics/types.h` shares event/trace state between collector and UI,
  independent of X11. Do not enlarge this into a circular app-wide state header.
- Main application unit: 1,998 lines. Standalone read-only page API checks:
  `make check-pages-module`. New pages must not directly open sysfs files,
  parse API logs, save settings, or perform GPU writes.
- UI and watchdog behavior remains unchanged. Remaining work: extract shared
  top header, tabs and bottom controls where useful; migrate lingering tests
  that include the whole application unit; bound blocking telemetry before
  making any high-frequency watchdog guarantees.

## Step 4 controller/diagnostics extraction — dev.9

- `ui/chrome.c` owns the fixed header temperature/power bars, policy buttons,
  workflow selectors, tabs, and footer. Workflow passed explicitly, without
  accessing the application controller's global state.
- `diagnostics/events.c` owns the bounded 512-event ring, event attribution
  from the sampled GPU-client array, and optional CSV output.
- `diagnostics/trace.c` owns tracefs instance lifecycle and the nonblocking
  trace pipe pump. Tracefs privileges and event selections stay unchanged.
- `app/poll_timing.c` adds optional sampled wall-time observations, enabled
  only with `AMD_GPU_DIAG_TIMING=1`. Measurements are in-memory until exit.
- New pure fixtures: `make check-events-module check-poll-timing-module`.
- Controller reduced to 1,663 lines. State and X11 input dispatch remain
  there for the next pass. No threading, GPU sysfs-write change, scheduling
  guarantee, or altered settings schema in this checkpoint.

**Next:** instrument actual deadline lateness under GPU/AI load and establish
a safety test plan for moving blocking monitoring work away from enforcement.
Avoid introducing concurrent GPU writers. Verify hardware-specific behavior
on the target RX 7900 XTX before any public v0.8.0b release.

## Step 4 continued — dev.10 watchdog latency baselining and X11 input

- `ui/input.{c,h}` owns keyboard action translation, page scroll-key mapping,
  and safe, length-bounded AI log-path editing. It does not own GPU writes,
  settings persistence or device selection.
- X11 events are processed in batches of at most 64 before enforcement can
  run again. Input handling still executes on the main event loop.
- Optional `AMD_GPU_DIAG_TIMING=1` now reports duration breakdown for
  DPM, GPU power, tracefs, X11 input, GPU switching, clients, System+AI,
  desktop session and X11 rendering.
- A missed watchdog deadline now records how late the cycle was, using a
  monotonic clock, with max/average and >=1/5/50/500 ms severity counts.
  'Skipped periods' are derived from lateness / configured interval, not
  additional checks. The original late-cycle deadline reset remains.
- **Not completed**: decouple synchronous AI HTTP and /proc/sysfs polling
  from enforcement, formalize cross-thread read-only telemetry snapshot
  ownership, and validate scheduler behavior on the physical GPU. No hard
  real-time warranty or concurrent GPU writers.
- Module fixture: `make check-ui-input-module`. Existing regression and
  simulated X11 tests remain mandatory.

## Step 4 continued — dev.12 asynchronous DRM/client/desktop discovery

- GPU client `/proc/PID/fdinfo` discovery, activity deltas, and client role
  classification live in `monitor/clients.c`, separate from the UI controller.
- A second independent `monitor/scan_worker.c` thread processes the potentially
  slow Linux client and desktop scans without X11 calls or GPU writes.
- Requests are coalesced; results are copied across a short mutex and checked
  against card-selection generation, card identity and compositor source.
- GPU rebinding retains its existing safety behavior (watchdog disabled,
  tuning disarmed); no worker receives or retains the selected card's live
  `Sysfs` descriptor. Only an immutable pathname is copied for read-only DRM
  state parsing. Expensive package metadata lookup runs off the watchdog loop.
- The existing `AMD_GPU_DIAG_TIMING=1` profiler records worker wall time, not
  blocked foreground time. Best-effort scheduling remains unchanged.
- Final v0.8.0b preparation: run host idle/inference comparative profiling;
  inspect shutdown/GPU-switch worker behavior and physical telemetry values;
  conduct a source cleanup and public release audit.

## Step 4 continued — dev.13 provenance and performance baselining

- Generation checks for asynchronous monitor results prevent A→B→A settings
  changes from accepting old source data. Compositor changes advance the
  discovery generation, independently of GPU rebinding.
- Delete an unbounded `lms ps` shell invocation that returned no actual token
  rate. Detailed llama.cpp/Bionic/Project Maya parsing is preserved.
- Report fixed-size positive-lateness histogram bucket ceilings (p50/p95/p99)
  without allocating or writing files in the watchdog loop. Use the bundled
  comparison script to inspect host idle/inference profiles.
- Before v0.8.0b: collect actual hardware reports under matched settings;
  test multi-GPU switch, suspend/shutdown, tracefs and policy fallback on Arch.

## Release hardening — dev.14

- Background AI state is reset on every source configuration generation (even
  after A -> B -> A); CPU/core hwmon descriptors and history are retained.
- The AI log tail reader only opens regular files, using nonblocking `open`
  and opened-inode validation to avoid FIFO/device hangs during worker join.
- Both worker types have deterministic in-flight stop and restart fixtures.
- Existing GPU sysfs writer, polling cadences, Xlib rendering and persisted
  settings stay unchanged; verify actual watchdog latency on RX 7900 XTX.

## Release validation — dev.15

- Add explicit start-to-start watchdog check interval measurements, including
  late or blocked cycles, plus comparable misses/minute for different run lengths.
- Strip background worker-owned sensor FD numbers from foreground telemetry.
- Capture user-controlled idle and local-inference runs using the scripts in
  `scripts/`, recording results only upon a normal app shutdown.
- Do not claim watchdog timing improved until the user's RX 7900 XTX profiles
  demonstrate it; this checkpoint makes no new sysfs writes or UI changes.
