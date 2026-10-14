# Security

The current X11/XLibre GUI runs as root because the AMDGPU power-policy controls it manages normally require root access.

X11 is not designed as a strong privilege boundary between clients. Use this tool only on a trusted local desktop session and do not grant untrusted clients access to the X server while the program is running.

The application does not execute shell commands for normal diagnostics or control operations. Kernel and `/proc` interfaces are accessed directly.

A future privilege-separated helper may be considered if the additional IPC/service complexity is justified.
