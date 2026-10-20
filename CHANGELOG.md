# Changelog

## 0.6.0

- Added guarded software tuning for firmware-exposed GFX clock/min-max or RDNA4 SCLK offset, memory max clock, VDDGFX offset, and board power cap.
- Added an explicit ARM → APPLY write gate plus RESET STOCK and REFRESH controls.
- Added green / amber / red aggressiveness zones relative to captured stock values and firmware-exported ranges.
- Kept kernel/firmware ranges authoritative; the reference database never overrides driver limits.
- Added prominent always-visible GPU thermal bars for every AMDGPU hwmon temperature channel and CPU package temperature when available.
- Added detailed thermal critical/emergency values and power-cap ranges to the Power page.
- Added a compiled-in 50-model desktop Radeon reference database spanning RX 400/500, Vega/Radeon VII, RX 5000, RX 6000, RX 7000, and RX 9000.
- Added VRAM-aware matching for same-name capacity variants.
- Added RDNA4 SCLK-offset support, including detection of the legacy pre-Linux-6.14 indexed representation.
- Added RDNA1 maximum-clock support while deliberately leaving legacy Polaris/Vega10 multi-state voltage tables out of the modern slider model.
- Added no new runtime library dependency.


## 0.5.2

- Added a built-in About page with developer, project, technology, development, and GPL information.
- Added a dominant full-width board-power consumption bar to the always-visible hardware header.
- Added the `I` keyboard shortcut and clickable About tab.
- Expanded the window/header layout to keep the richer telemetry readable.
- Kept the GUI plain Xlib and added no runtime dependencies.


## 0.5.1

- Moved power-policy status and all eight power-mode controls to the top of the application.
- Added always-visible GPU-load, VRAM, CPU-load, and system-RAM bar graphs.
- Added an always-visible GPU-voltage bar.
- Added current monitoring: real hwmon `curr*_input` when exposed, otherwise an explicitly labelled board-current estimate using `P / 12 V`.
- Added 12 V equivalent-load resistance to the detailed Power page.
- Added lightweight `/proc/stat` CPU utilization and `/proc/meminfo` RAM utilization sampling with no new library dependencies.
- Increased the default window height slightly so the expanded hardware/control header does not crowd diagnostic pages.
- Kept the GUI direct Xlib + libc only.

## 0.5.0

- Replaced the terminal UI with a minimal native Xlib GUI.
- Targeted X11/XLibre + XFCE as the primary desktop environment.
- Added exact X server vendor detection.
- Added authoritative X11 compositor detection using `_NET_WM_CM_S0`.
- Added X11 window-manager detection.
- Added clickable views, power modes, and common toggles.
- Kept explicit keyboard shortcuts visible in the GUI.
- Preserved the hardware summary, VRAM bar, per-process DRM attribution, GFXOFF diagnostics, event recorder, tracefs support, and watchdog behavior.
- Retained `profile_standard` as the default power target.
- Kept expensive DPM telemetry disabled by default.
- Reduced GUI dependencies to direct `libX11` + libc only.
- Cleaned path/string handling and strict compiler warnings.

## 0.4.x

- Introduced the AMD GPU Diagnostic Tool dashboard and correlation diagnostics.
