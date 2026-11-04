# AMD GPU Diagnostic Tool — v0.8.0 development

[![v0.8.0 development CI](https://github.com/kevito666/amd-gpu-diag-tool/actions/workflows/ci.yml/badge.svg?branch=refactor%2Fv0.8.0)](https://github.com/kevito666/amd-gpu-diag-tool/actions/workflows/ci.yml?query=branch%3Arefactor%2Fv0.8.0)
[![main CI](https://github.com/kevito666/amd-gpu-diag-tool/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/kevito666/amd-gpu-diag-tool/actions/workflows/ci.yml?query=branch%3Amain)

**Current checkpoint: `0.8.0-dev.16`** · C / Xlib / X11/XLibre · GPL-3.0-or-later

**This is the working branch, not a tagged beta.** The published [v0.7.0b release](https://github.com/kevito666/amd-gpu-diag-tool/releases/tag/v0.7.0b) and [`main`](https://github.com/kevito666/amd-gpu-diag-tool/tree/main) are still separate.

I started this project to get a handle on AMD GPU power states and coil whine. Now it covers most of the stuff I want to see while I'm running games, local AI models, or trying to figure out what my Linux desktop is doing. The goal for 0.8.0 is to make the code easier to maintain and keep slow monitoring calls out of the GPU watchdog's way—not to bolt on a pile of frameworks.

## What changed since v0.7.0b?

- **Modular source:** GPU control/readback, monitoring, AI parsers, settings, Xlib UI, events/trace, and application timing are split into separate C modules.
- **Two read-only workers:** One handles CPU/process/NVMe/AI work; the other scans DRM clients and desktop/compositor/session information. They return copied, generation-checked results, so an old GPU or AI-source selection cannot quietly overwrite the new one.
- **GPU writes are still single-writer.** Tuning ARM/APPLY/RESET, policy control, and watchdog enforcement remain on the foreground control path. No new overclocking permissions were added.
- **Watchdog profiler:** Opt-in timing now records check-to-check gaps, deadline misses, maximums, and latency buckets, so idle versus inference load can be compared on actual hardware.
- **UI cleanup (dev.16):** Larger GPU name, `Target = … | Actual = … | LOCKED/PAUSED | Corrections = …`, two-column Settings, fixed About versus `?` Help navigation, centered tuning controls, and a scrolling Radeon reference database browser.

The firmware's actual limits always override reference-card database values. The database is informational, **not** a set of safe overclock targets.

## What it does

The dashboards cover AMDGPU temperatures, power, clocks, DPM states, GPU clients, PCIe/BAR, CPU telemetry, display/VRR state, and compositor details. Local AI monitoring understands task timing from llama.cpp/Bionic and supported Project Maya engine logs. The NVMe tile reports **aggregate device I/O**, not per-model storage usage. Other AI sources and parser modes vary in completeness; unsupported measurements stay unavailable.

The app is native C/Xlib. No GTK, Qt, Electron, or Python runtime is required to run it.

### Screenshots

These are real **v0.7.0b** screenshots from an RX 7900 XTX. The dev.16 UI has since changed; these are not mockups or current performance results.

| AI monitoring | Gaming monitoring |
|:---:|:---:|
| ![AI dashboard](docs/screenshots/ai.png) | ![Gaming dashboard](docs/screenshots/gaming.png) |

## Build and test

Needs a C compiler, `make`, X11 development libraries, libcurl and pthreads. Headless UI checks need `xvfb-run` and `xauth`.

```sh
git clone https://github.com/kevito666/amd-gpu-diag-tool.git
cd amd-gpu-diag-tool
git switch refactor/v0.8.0
make -j"$(nproc)"
make check check-ui
./amd-gpu-diagnostic --version
```

The GitHub Actions workflow runs the larger regression suite, including the parser, settings, GPU readback/control, worker lifecycle, UI hitboxes and virtual-X11 pages. To run that same matrix locally, copy the `make` targets from [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

The existing GPU-control functionality needs appropriate privileges. Read the code before running it as root, and don't assume the write controls are safe on every AMDGPU driver. The `--version` and `--help` options don't need root. `--card N` selects a DRM card for that invocation.

### Watchdog timing under actual load

```sh
# Capture each run separately; close the app normally.
scripts/capture-watchdog-profile.sh idle
scripts/capture-watchdog-profile.sh ai-load

# Then compare the resulting log files.
scripts/compare-watchdog-logs.sh idle.log ai-load.log
```

The capture scripts do **not** start inference or modify tuning settings themselves. The numbers need to be taken on real hardware before making performance claims. Details: [beta-readiness testing](docs/V080_STEP15_BETA_READINESS.md).

## Where the source went

| Directory | What lives there |
|---|---|
| `src/gpu/` | Device handling, readback, policy, guarded tuning and firmware writes |
| `src/monitor/` | CPU/sensors, GPU client scans, NVMe, desktop discovery, background workers |
| `src/ai/` | llama.cpp/Bionic and Project Maya parsing, logs and HTTP sampling |
| `src/ui/` | Xlib drawing, layouts, page renderers, mouse/keyboard handling |
| `src/diagnostics/` | Events and tracefs |
| `src/app/` | Watchdog/poll timing diagnostics |
| `src/config/` | Persistent Settings |
| `tests/`, `docs/`, `scripts/` | Regression checks, engineering notes, development tools |

For the step-by-step refactor notes, see [the v0.8.0 roadmap](docs/V080_REFACTOR.md), [worker hardening](docs/V080_STEP14_HARDENING.md) and [beta readiness](docs/V080_STEP15_BETA_READINESS.md). The long technical history belongs there rather than on the front page.

## Still on the list

- **Scrollbar dragging:** mouse-wheel scrolling works; click-and-drag scrollbar thumbs are not implemented yet.
- **Hardware validation:** watchdog latency during sustained inference, multi-GPU switching, and tuning/rollback behavior need real-card testing.
- **Real-time guarantees:** the nominal 5 ms watchdog interval is best-effort, not a deterministic deadline. GPU readback, X11 and tracefs can still cause delays.
- **Settings/overclocking:** settings schema remains compatible with v0.7.0b. The overclock-unlock control remains an inert placeholder. Partially applied firmware writes are not atomic.

Found a bug? [Open an issue](https://github.com/kevito666/amd-gpu-diag-tool/issues) with your card, kernel, driver, desktop environment, steps and relevant logs. Please strip credentials and personal paths before posting logs.

Built by **Kevin Kolasa** ([@kevito666](https://github.com/kevito666)) in Tohoku, Japan. **GPL-3.0-or-later**; see [LICENSE](LICENSE).
