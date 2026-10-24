# v0.7.0b pre-release audit and architecture

**Audit date:** 2026-10-25. **Baseline:** packaged v0.7.0b source candidate (not the user's running Git checkout).

## Source map

| File / area | Responsibility | Sampling / safety boundary |
|---|---|---|
| `src/amd-gpu-diagnostic.c` — `Sysfs`, `Target`, `TuningState` | AMDGPU hardware selection, sysfs fd cache, watchdog and firmware-bounded power/tuning controls | Root required for existing writes; switching GPU pauses enforcement and disarms tuning |
| `SystemTelemetry`, `sample_system` | Linux CPU/RAM, thermal, power, clocks, per-core load | Lightweight 500 ms scheduler; expensive top-process sweep throttled to 2 s |
| `ClientMonitor` | DRM fdinfo clients and VRAM | 1 s |
| `PowerTelemetry`, `sample_power` | Electrical/temperature/clock sensors | 100 ms; use cached fds where available |
| `SessionInfo`, `x11_probe_session` | XLibre/X11/WM/compositor/DRM/pacman metadata | 10 s; unknown values must not be fabricated |
| `LlamaProcessInfo`, `sample_tokens`, `parse_ai_log` | Local model server discovery, token API polling, log and host-RAM prompt cache parsing | Cached `/proc` scan every 15 s; task-aware log parser every 2 s; prompt cache is distinct from VRAM |
| `Gui`, page draw routines | Xlib controls, telemetry visualization, shared header/footer and scroll viewport | X11 smoke tested; hardware rendering subject to fonts and host metrics |
| `Settings`, `settings_load/save` | Per-user AI/compositor/card settings | Mode 0600 atomic save with privilege drop on sudo invocation |
| `tests/` | Hardware-free parser, configuration, PCIe/BAR, X11, resource safety tests | CI under strict compiler warnings |

**Code organization:** The main C file remains monolithic (~6k lines). For the beta, changes stayed targeted to avoid destabilizing power writes. A future modularization can split `sysfs.c`, `system_telemetry.c`, `ai_monitor.c`, `settings.c` and `gui/` behind headers and explicit ownership rules, with regression tests first. Do not perform that structural migration as an incidental beta patch.

## Correctness and performance fixes in this audit

1. Close the CPU fan PWM-max sensor descriptor on every read, preserve the saved PWM path, and test 1,000 repeated reads without FD growth.
2. Bound the libcurl response allocation to 1 MiB; reject overflow and incomplete output; retain ownership when realloc fails; release the per-request auth header and detach stale libcurl pointer state.
3. Read llama-server command arguments from `/proc/PID/cmdline` rather than spawning `ps`, `grep` and `awk` repeatedly. Preserve spaces in model filenames, do not log API keys, and cache discovery for 15 seconds.
4. Parse selected settings as strict bounded integers, preventing malformed values from accidentally selecting option zero.
5. Rank top CPU processes by CPU-time **delta** across approximately 2 s, not lifetime CPU consumption. Report utilization percent per logical CPU (may exceed 100% on multicore workloads).
6. Don't call model-list availability an actual measured token rate for experimental LM Studio / vLLM polling; mark it unavailable. Reset stale llama throughput on API failures.
7. Update model metadata on session refresh and clear it if the server disappears. Remove unconditional private path/model debug prints.
8. Clear supplementary groups when opening a log viewer from a root process under the invoking user's UID.
9. Bound formerly compiler-warning-prone GPU and motherboard label formatting.

## Scope and known caveats

- The beta intentionally keeps the current 840×660 compact layout, all hardware tuning logic, and the overclock-unlock placeholder. No overclock unlock was implemented.
- CPU process ranking is intentionally capped at 512 inspected processes per sweep. The percentage is an interval estimate, not cgroup-normalized CPU allocation.
- llama.cpp `/slots` JSON parsing is still a lightweight substring parser and is not guaranteed to aggregate throughput across multiple parallel slots. The Bionic log parser remains the best per-task source.
- Ollama/vLLM/LM Studio task-log format support is experimental. A successful HTTP model-list response does not provide measured token throughput.
- Full Vulkan/driver frame-pacing metrics, VSync/triple-buffer detection and an overclock-unlock system are **not** implemented or asserted.
- GPU switching, fan readings and firmware writes cannot be validated without the real host and should be tested before publishing the beta tag.
- The source package is intentionally **flat**: extract into your existing `~/src/amd-gpu-diag-tool` Git checkout after a backup, not into an extra version-named directory.

## Validation

Run from project root:

```sh
make check check-config check-parser check-pcie check-audit check-ui
make
```

`check-ui` requires `xvfb-run` and `xauth`; others need the C toolchain, Xlib headers and libcurl headers. CI repeats the hardware-free checks. Audit-specific tests include strict numeric validation, test `/proc` argv/stat parsing, bounded HTTP buffering and repeated fake sysfs reads.

## Safe local promotion

Your Git checkout may contain important uncommitted work. Do **not** delete `.git`, your `archive/`, or local documentation. Before extracting this flat package:

```sh
cd ~/src/amd-gpu-diag-tool
mkdir -p archive/snapshots
stamp=$(date +%Y%m%d-%H%M%S)
tar --exclude='./archive' -czf "archive/snapshots/pre-audit-$stamp.tar.gz" .
# Then extract the downloaded flat source archive into this directory.
```

Run validation and inspect `git diff` before committing. The source package does not contain screenshots: use the upcoming real v0.7.0b AI and GAMING captures for GitHub documentation.
