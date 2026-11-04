# v0.8.0-dev.7 — AI and GAMING renderer boundaries

This is a development checkpoint, not a replacement for the public v0.7.0b
release. The interface and watchdog are intentionally unchanged.

## Extracted modules

- `src/ui/gaming.c`: read-only gaming page. Receives GPU `Gpu` / `Sysfs`
  metadata and independently sampled `PowerTelemetry` and `SystemTelemetry`.
- `src/ui/ai_dashboard.c`: read-only AI page. Receives selected `Target` and
  `Stats`, GPU/CPU and DRM-client telemetry and a `UiAiSources` value.
- `src/ui/dashboards.h`: API and immutable view of AI settings, parser state,
  Project Maya, recent log entries, NVMe counters, and last diagnostic event.
- `src/ui/status.c`: shared temperature zones/colors. Neither page opens
  device descriptors, mutates GPU targets, parses logs, or writes sysfs.

## State ownership and safety

The controller remains responsible for assembling `UiAiSources`, including an
owned-by-controller `AiLogEvent[]` array, references to app settings and Maya/NVMe
state, and a *copied descriptor* of the latest diagnostic event. Pointers are
borrowed only for the duration of a synchronous draw. Renderers do not retain
these pointers or trigger IO operations. All GPU writes remain in `gpu/writes`.

The original 5 ms policy watchdog still shares its event loop with potentially
blocking collectors. Do not describe it as deterministic or real-time until
independently scheduled and measured on physical hardware.

## Validation

- `make` links the new renderer and shared status objects.
- `make check-dashboard-module` compiles/links the renderer APIs without
  the legacy app controller or X11 server; checks thermal edge cases.
- Original regression and six X11 smoke tests still cover the full app.
- Rendered 1280x900 synthetic AI and GAMING frames matched dev.6 pixel for
  pixel using equal Xvfb settings; no changes to controls or hitboxes.

## Next checkpoint

Extract Clients, Power, Tuning, Desktop, Events, Trace, Settings, Help and
About page renderers, then revisit controller input dispatch. Do not combine
that extraction with the watchdog scheduling/concurrency redesign.
