# Architecture

AMD GPU Diagnostic Tool separates enforcement from diagnostics so expensive
telemetry cannot sit in the latency-sensitive watchdog path.

## Sampling tiers

| Tier | Default rate | Purpose |
|---|---:|---|
| Policy watchdog | 200 Hz | Verify and repair `power_dpm_force_performance_level` |
| Adaptive burst | 2 kHz for 250 ms | Rapid follow-up after a real policy mismatch |
| Basic hardware telemetry | 10 Hz | Busy, power, temperature, clocks, fan, voltage, GFXOFF |
| Slow driver telemetry | ~1 Hz | Power profile, PCIe accounting, gpu_metrics header |
| DRM client attribution | 2 Hz | Per-process GPU activity from `/proc/<pid>/fdinfo` |
| Desktop/KMS discovery | 0.2 Hz | Session, compositor, connectors and display state |
| DPM state telemetry | Off / 4 Hz | Optional `pp_dpm_sclk` and `pp_dpm_mclk` reads |
| Kernel tracing | Off | Opt-in tracefs event correlation |

## Event correlation

A circular in-memory flight recorder records policy resets, corrections, power
steps, GFXOFF transitions and optional DPM-state changes. Each event snapshots
the highest-activity DRM client and the latest kernel trace actor when tracing
is enabled.

## Root requirement

The UI and diagnostics can read many kernel interfaces without root, but the
program currently requires root because it also writes AMDGPU power-policy
sysfs attributes and manages isolated tracefs instances.
