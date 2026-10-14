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
