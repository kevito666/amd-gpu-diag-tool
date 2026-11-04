# v0.8.0-dev.4 — GPU readback and session module contracts

**Status:** unpublished refactor checkpoint based on `0.8.0-dev.3`.

## Ownership and safety

| Module | Reads | Owns writes? | Caller responsibility |
|---|---|---|---|
| `gpu/readback` | GPU hwmon, power, clocks, fan, GFXOFF, PCIe link, BAR, power-profile readback | **No** | Controller opens/closes `Sysfs` descriptors; module borrows them |
| `monitor/session` | Desktop process, DRM connector/KMS state, session environment, pacman package info | **No** | Caller supplies `Gpu`, `Sysfs`, client snapshot, compositor source |
| `app` legacy main file | UI/X11 WM hints, event loop, GPU policy/tuning, selected-card state | **Yes, single writer** | Maintains ARM/APPLY, stock reset, GPU switch and watchdog behavior |
| `core/util` | Bounded numeric reads on caller-owned fds | **No** | Caller manages descriptor lifetime |

## Compatibility

- The original settings.ini schema, intervals, graphical page geometry,
  telemetry units and values are unchanged by this extraction.
- `gpu_sample_power()` accepts live already-open handles from the original
  controller; unexposed fields remain unavailable rather than fabricated.
- `desktop_sample_session()` resets the passed snapshot and enriches it with
  DRM, `/proc` and environment data. X11 WM/selection details are added
  afterward by the existing GUI path.
- VSync and buffering remain unverified when the operating system does not
  expose trustworthy per-application state.
- PCIe's `resource0_resize` indicates resize *capability*, whereas active
  BAR aperture comes from the `resource` table.

## Verification

Run `make check-pcie check-session-module check-readback-module` for
X11-free synthetic hardware fixtures, then the full existing integration
and Xvfb UI checks. These are not substitutes for physical GPU validation.

## Still outstanding

- Isolate the firmware write controller, tuning reset and watchdog policy
  in one coherent module with fault injection before adding worker threads.
- Split X11 primitives, common tiles/hitboxes and per-page drawing.
- Measure scheduling jitter under concurrent AI/network requests on real
  hardware before making strict timing or overclocking promises.
