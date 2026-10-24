# GitHub beta: v0.7.0b

**Tag:** `v0.7.0b`\
**Mark as:** Pre-release / beta\
**Artifact:** `amd-gpu-diagnostic-v0.7.0b-source.tar.gz`

The lightweight C/Xlib AMD GPU power-state watchdog now includes AMD card selection, persistent Settings, AI API and log-source configuration, a compositor detector selector, detailed desktop stack versions, an electrical-only Power page, and 15 significant event records. The overclock unlock button is a safe placeholder.

Settings are stored in the invoking desktop user's home directory, not `/root`, when launched with sudo. On GPU changes, watchdog enforcement is paused and tuning is disarmed. The old GPU's prior settings are **not restored automatically**; reset first if needed.

**Beta caveats:** Only Bionic/llama.cpp has complete task-aware log timing parsing. VSync and buffering are not verifiable from the currently exposed telemetry. This beta has passed strict compilation, parser/PCIe/config tests and synthetic X11 UI tests; hardware tuning and multiple AMD GPU devices remain to be tested on supported host machines.

**Build:** `make && make check check-config check-parser check-pcie check-audit check-ui`\
**Run:** `sudo -E ./amd-gpu-diagnostic`

Do not describe this release as hardware-validated until real-card testing is complete.

### Second-pass audit before beta tag

A further focused review fixed rejected power-policy state transitions, AI source/log cache invalidation, oversized HTTP response overhead, log-viewer privilege handling and repeated CPU-clock sysfs probes. See `docs/DEEP_AUDIT_2.md` for exact implementation details and validation evidence.

**Important beta limitation:** The 5 ms watchdog and synchronous HTTP/process monitoring still share one event loop; the configured interval is **not a real-time latency guarantee**. Validate watchdog loop rate/deadline misses during heavy local AI workloads before making latency claims.

### Pre-tag stabilization

Audited the beta for correct fd ownership, curl header lifetime, malformed settings, CPU process sampling, and AI discovery overhead. llama-server process detection now uses cached `/proc` discovery rather than repeated shell pipelines; the CPU-process display shows measured interval utilization. UI layout and power/tuning safeguards remain unchanged. See `docs/AUDIT_AND_ARCHITECTURE.md`.

Real v0.7.0b AI and Gaming screenshots are provided in `docs/screenshots/ai.png` and `docs/screenshots/gaming.png` (captured on an RX 7900 XTX). The selected AI capture includes actual prompt/decode log entries and an NVMe-monitoring snapshot; the Gaming capture shows live hardware status. Neither is a performance benchmark. The package uses a flat root layout so it can be applied to the existing Git working tree after creating a backup. It is not a published GitHub release.

### Additional AI monitoring

Settings now offers **Project Maya (engine log)** as a source and parser, with a manual
absolute path or optional auto-discovery in standard checkout locations. `glm stat`
and `glm prefill` report available completed performance metrics; other log lines
remain textual. The AI dashboard shows a separate selectable **AI NVMe CACHE I/O**
tile with read and write MiB/s, observed peak rates and disk busy %.

The selected NVMe device is persisted. Its block counters are **aggregate host I/O**,
so heavy background file transfers can affect the numbers. Live Maya-specific
request timings and per-process SSD attribution are not yet available. Test on
the actual Project Maya runtime before shipping.

New hardware-free regression: `make check-maya-nvme`.

### Screenshots and project credits

- [AI monitoring screenshot](docs/screenshots/ai.png)
- [Gaming monitoring screenshot](docs/screenshots/gaming.png)

**Developer:** kevito666 (Kevin Kolasa) · **Built in:** Tohoku, Japan.

What started as a coil-whine reduction and AMD GPU power-state utility grew into a lightweight AI, gaming and Linux desktop monitoring/configuration tool. Built in C/Xlib with no GTK, Qt or Electron dependency. Licensed **GPL-3.0-or-later**; full text in `LICENSE`.
