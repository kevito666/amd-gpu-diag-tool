/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AMD_GPU_DIAG_UI_CHROME_H
#define AMD_GPU_DIAG_UI_CHROME_H
#include "types.h"
#include "../gpu/policy.h"
#include "../monitor/system_state.h"
#include "../diagnostics/types.h"
/* Fixed header and footer; logical geometry comes from ui/layout.h. */
void ui_draw_header(Gui *ui, const Gpu *gpu, const Sysfs *sysfs,
                    const Target *target, const PowerTelemetry *power,
                    const SystemTelemetry *system, const Stats *stats,
                    WorkflowMode workflow);
void ui_draw_tabs(Gui *ui, View view);
void ui_draw_bottom_controls(Gui *ui, const Target *target, bool adaptive,
                             bool monitor_clocks, const TraceState *trace,
                             const EventLog *events);
#endif
