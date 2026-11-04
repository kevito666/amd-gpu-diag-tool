/* SPDX-License-Identifier: GPL-3.0-or-later
 * Stateless AI/Gaming renderers: application supplies read-only snapshots.
 * Rendering performs no sysfs writes, log reads, polling, or policy changes.
 */
#ifndef AMD_GPU_DIAG_UI_DASHBOARDS_H
#define AMD_GPU_DIAG_UI_DASHBOARDS_H
#include "types.h"
#include "../gpu/policy.h"
#include "../monitor/system_state.h"
#include "../monitor/session.h"
#include "../monitor/nvme.h"
#include "../ai/maya_log.h"
#include "../ai/log_parser.h"
#include "../config/settings.h"

/* A value snapshot avoids exposing the diagnostic event ring to a UI page. */
typedef struct {
    const char *kind;
    const char *detail;
    const char *actor;
    double actor_activity;
    bool available;
} UiEventSummary;

typedef struct {
    const AppSettings *settings;
    const MayaLogMonitor *maya;
    const NvmeMonitor *nvme;
    const AiLogEvent *log_events;
    UiEventSummary event;
} UiAiSources;

void ui_draw_gaming_dashboard(Gui *ui, const Gpu *gpu, const Sysfs *sysfs,
                              const PowerTelemetry *power, const SystemTelemetry *sys);
void ui_draw_ai_dashboard(Gui *ui, const Gpu *gpu, const Target *target,
                          const Stats *stats, int interval_us, bool adaptive,
                          const ClientMonitor *clients, const PowerTelemetry *power,
                          const SystemTelemetry *sys, const UiAiSources *sources);
#endif
