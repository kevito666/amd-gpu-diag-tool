# Security

The current X11/XLibre GUI runs as root because the AMDGPU power-policy controls it manages normally require root access.

X11 is not designed as a strong privilege boundary between clients. Use this tool only on a trusted local desktop session and do not grant untrusted clients access to the X server while the program is running.

The application does not execute shell commands for normal diagnostics or control operations. Kernel and `/proc` interfaces are accessed directly.

A future privilege-separated helper may be considered if the additional IPC/service complexity is justified.

## Overclocking / tuning

v0.6.0 can write AMDGPU overdrive and power-cap controls when the kernel exposes them. The application clamps writes to the ranges reported by the running driver/firmware, but that does not guarantee stability or hardware safety. Partner-board cooling, VRM design, firmware, silicon quality, and workload behavior differ.

The GUI's green/amber/red colors represent aggressiveness relative to the captured stock state and exported range. They are not a warranty or a substitute for thermal/stability testing.

Tuning writes require an explicit ARM action followed by APPLY and are disarmed after each apply attempt.
