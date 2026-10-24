# Changelog

## v0.7.0b — GitHub beta release candidate (2026-10-25)

- Added real AI and Gaming screenshots (latest AI performance log capture and original v0.7.0b Gaming capture); finalized GitHub release documentation and About-page credits.
- Keep synthetic X11 smoke-test compilation unoptimized for timely CI; release application remains optimized.

### Second-pass stability and performance audit

- Make policy target changes transactional with respect to driver write failures; preserve the existing watchdog target/lock state when a write is rejected.
- Guard manual DPM clock selection and tuning reset against stale desired-policy state after rejected writes; note that multi-write driver sequences remain non-atomic.
- Clear stale token-rate counters on AI-source change; refresh log discovery immediately after parser/path changes; clear prompt-cache history when log parsing is disabled.
- Limit local HTTP downloads to the destination buffer instead of accumulating up to 1 MiB only to discard oversized responses.
- Run expensive per-core sysfs CPU-frequency scans at two-second cadence, without slowing 500 ms CPU-load/temperature sampling.
- Prevent the external log viewer from executing as root without a valid non-root sudo invoker.
- Extend audit regressions; document the unresolved single-threaded watchdog scheduling risk in `docs/DEEP_AUDIT_2.md`.

### Pre-release stabilization audit

- Fix CPU fan PWM-max sensor descriptor leak and prevent mutation of the saved live sensor path.
- Bound HTTP response allocation to 1 MiB; fix libcurl reused-handle header lifetime and reject truncated/invalid API bodies.
- Replace repeated llama-server `ps|grep|awk` subprocess discovery with one cached `/proc` scan (15-second TTL); preserve arguments containing spaces and avoid printing model paths/API keys to debug logs.
- Reject malformed numeric settings rather than silently converting them to option zero.
- Replace top-process lifetime CPU-time ranking with true two-second CPU deltas and display activity percent.
- Do not report inferred token throughput from experimental model-list/metrics API responses; clear stale live activity on missing llama API data.
- Periodically refresh llama model metadata; drop supplementary groups before launching the user-facing log viewer.
- Bound GPU/motherboard label formatting; add hardware-free audit regression tests and architecture notes.


- New persistent Settings view, replacing the full-width Help tab with Settings and a small `?` Help button.
- Move AI API source choice to Settings (llama.cpp, Ollama, vLLM, LM Studio, heuristic).
- Add log format selection and manually editable absolute log filename with automatic detection option. llama.cpp/Bionic timing parsing is implemented; alternative formats are explicitly experimental.
- Add configurable compositor detector (auto, X11 owner, DRM client, process scan, off).
- Enumerate available AMD DRM GPUs and allow on-demand rebinding in Settings. A switch pauses the watchdog and clears tuning locks/ARM; users must manually reactivate enforcement. Selection persists.
- Add safe, nonfunctional overclock-unlock placeholder. There is no firmware unlocking or unsafe write behind it.
- Simplify Power to measured electrical quantities, estimates labelled as such, electrical limits and policy; move temperature and clock details to Gaming.
- Show session, X server, desktop, window manager and compositor package versions and build dates when available from Arch package metadata; show unknown when not available.
- Show DRM-reported refresh rate, active modes and VRR state when exposed; VSync and buffering clearly shown as unverified because they are per-compositor/app and cannot be inferred reliably from X11 existence.
- Events now shows up to 15 significant recent records, filtering high-frequency GFXOFF/DPM noise from the display; ring capacity expanded to 512.
- Update project origin story, Tohoku attribution, and GPL license wording; adjust About layout so license is reachable by scrolling.
- Refined Tuning action controls into the SAFETY/WORKFLOW tile as a compact 2×2 ARM/APPLY/RESET/REFRESH grid, reachable at the default window size.
- New config persistence test, additional X11 screen smoke tests, GitHub Actions build gate, LICENSE, release notes, and source packaging.

### Beta limitations

- Hardware switching, policy writes and telemetry should be tested on real multi-GPU AMD configurations.
- GPU switching does not undo any policy or prior firmware changes on the previously selected GPU; use RESET STOCK if necessary **before** switching.
- Only llama.cpp/Bionic has detailed task-aware timing parsing; other parser selections are visible but experimental and do not claim metrics.
- VSync, triple buffering, G-Sync compatibility and VRR state can be unreported or unavailable, depending on the running driver and compositor.
- Package version/build dates are currently Arch/pacman oriented.

## v0.6.7
- Consolidated Gaming power, temperature and memory tiles; added CPU clocks, PCIe link and actual BAR aperture diagnostics.

## v0.6.6
- Prompt-history cache monitor for llama.cpp/Bionic host RAM, including save/load/eviction/rejection status.

## v0.6.5
- Compact Tuning and 10 event rows; expanded AI performance log and GPU monitoring.

## v0.6.4
- Compact X11 UI overhaul.
