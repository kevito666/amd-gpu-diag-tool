# Interfaces used

The program may read or write the following classes of Linux/AMDGPU interfaces when they are available:

- `/sys/class/drm/card*/device/power_dpm_force_performance_level`
- `/sys/class/drm/card*/device/pp_dpm_sclk`
- `/sys/class/drm/card*/device/pp_dpm_mclk`
- `/sys/class/drm/card*/device/gpu_busy_percent`
- `/sys/class/drm/card*/device/mem_busy_percent`
- AMDGPU hwmon temperature, fan, power, voltage, and frequency files
- VRAM usage and PCIe statistics
- `gpu_metrics`
- `/proc/<pid>/fdinfo/*` DRM accounting
- AMDGPU/DRM debugfs state and GFXOFF data
- tracefs event instances when tracing is explicitly enabled

The GUI additionally uses Xlib and standard X11/EWMH properties/selections for X server, window-manager, and compositor information.

## Tuning writes

When available and explicitly armed by the user, v0.6.0 may write:

- `/sys/class/drm/card*/device/pp_od_clk_voltage`
  - `s 0 <MHz>` / `s 1 <MHz>` for modern min/max GFX clocks
  - `s <offset>` for modern RDNA4 SCLK offset
  - `s 1 <offset>` only when the legacy indexed RDNA4 representation is detected
  - `m 1 <MHz>` for maximum memory-controller clock
  - `vo <mV>` for VDDGFX offset where exposed
  - `c` to commit staged overdrive values
  - `r` to request the driver's default/reset state
- AMDGPU hwmon `power1_cap` in microwatts, clamped to `power1_cap_min` / `power1_cap_max`

Before those writes, the application selects `manual` through `power_dpm_force_performance_level`. Unsupported controls remain unavailable in the GUI.

## Thermal reads

The persistent header reads all available AMDGPU `temp[1-3]_input` channels plus their labels and critical/emergency thresholds. CPU package temperature is discovered from the CPU hwmon device (normally `k10temp` on Ryzen).
