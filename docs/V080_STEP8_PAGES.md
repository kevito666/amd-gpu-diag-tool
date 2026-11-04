# 0.8.0-dev.8 — read-only Xlib page modules

## Scope

The existing dev.7 dashboard architecture already separated AI and GAMING.
This checkpoint extracts the remaining read-only pages without changing
layout, controls, settings format, telemetry provenance or GPU policies.

| Module | Pages | Inputs |
|---|---|---|
| `ui/pages_monitor.c` | Clients, Power, Desktop, Events, Trace | Linux/AMDGPU snapshots; compositor detector label passed by caller |
| `ui/pages_tuning.c` | Tuning | `const TuningState*`, firmware ranges, spec DB; no sysfs writes |
| `ui/pages_settings.c` | Settings, Help, About | `const UiSettingsView*`, version and build-date strings |
| `ui/pages.h` | Interface | Explicit parameter contracts and `UiSettingsView` |
| `diagnostics/types.h` | Diagnostic state | Event ring and trace structures without X11 dependence |

## State ownership

The application controller still collects telemetry, owns UI interactions,
handles GPU switching, and coordinates the single-threaded watchdog. Renderers
receive pointers to const data or an immutable view snapshot. They do not
update configuration, operate the selected hardware, or inspect global settings.
The tuning renderer only draws: `gpu/tuning.c`, `gpu/policy.c`, and `gpu/writes.c`
retain the existing authority to change the device.

`ui/layout.c` remains the source for all button rectangles and scrolling; no
parallel click hitboxes were introduced.

## Verification and caveats

- Build using `make` (individual object compilation and dependency files).
- `make check-pages-module` links the extracted page APIs without including
  `src/amd-gpu-diagnostic.c` and verifies explicit settings snapshot input.
- The existing integration and six Xvfb screen checks remain active.
- Render comparisons with the same Xvfb fixture are pixel-identical for
  Tuning, Settings, Desktop, Events and Power. Different build stamps affect
  the About screen's version/build text by design.
- No real GPU writes or watchdog deadline benchmarks were performed; the
  5 ms enforcement setting does not constitute a hard real-time guarantee.
- Reducing file size through extraction alone is not a runtime speedup.

Main application C file: **2,322 → 1,998 lines**. Remaining presenter logic
includes the shared header, navigation, control bar, and application dispatch.
