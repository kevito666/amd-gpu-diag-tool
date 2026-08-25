# Changelog

## v0.4.1 - 2026-08-25

First public release under the **AMD GPU Diagnostic Tool** name.

### Diagnostic features

- Low-overhead AMDGPU power-policy watchdog with adaptive correction bursts.
- Default `profile_standard` lock target.
- Per-process DRM `fdinfo` GPU-client attribution.
- Power, temperature, voltage, fan, clock, VRAM and GFXOFF telemetry.
- X11/X11Libre/Wayland session discovery and compositor detection, including picom.
- DRM connector/KMS summary.
- Event flight recorder with optional CSV logging.
- Opt-in isolated tracefs capture for AMDGPU/DRM scheduler/display events.
- Optional SCLK/MCLK DPM-state monitoring.
- Built-in sysfs polling benchmark.

### Safety / performance

- 5 ms steady policy watchdog.
- 500 us adaptive burst only after a real mismatch.
- Expensive DPM telemetry disabled by default.
- Process defaults to `nice +10` to reduce interference with latency-sensitive audio.
