/* SPDX-License-Identifier: GPL-3.0-or-later
 * UI page renderer contracts. Read-only snapshots; no device writes.
 */
#ifndef AMD_GPU_DIAG_UI_PAGES_H
#define AMD_GPU_DIAG_UI_PAGES_H
#include "types.h"
#include "../gpu/policy.h"
#include "../gpu/tuning.h"
#include "../monitor/system_state.h"
#include "../monitor/session.h"
#include "../monitor/nvme.h"
#include "../config/settings.h"
#include "../diagnostics/types.h"

typedef struct {
    const Gpu *gpu;
    const Gpu *gpus;
    int gpu_count;
    int requested_gpu;
    const AppSettings *settings;
    const NvmeMonitor *nvme;
    const char *const *parser_names;
    const char *const *compositor_names;
    bool editing_path;
    const char *edit_path_buffer;
} UiSettingsView;

void ui_draw_clients_page(Gui *ui, const ClientMonitor *clients);
void ui_draw_power_page(Gui *ui, const Target *target, const Stats *stats,
                        const PowerTelemetry *power, const Sysfs *sysfs,
                        const SystemTelemetry *system, bool monitor_clocks,
                        int dpm_interval_us);
void ui_draw_tuning_page(Gui *ui, const TuningState *tuning);
void ui_draw_gpu_database_page(Gui *ui);
void ui_draw_desktop_page(Gui *ui, const SessionInfo *session,
                          const char *compositor_detector);
void ui_draw_events_page(Gui *ui, const EventLog *events);
void ui_draw_trace_page(Gui *ui, const TraceState *trace);
void ui_draw_settings_page(Gui *ui, const UiSettingsView *settings);
void ui_draw_help_page(Gui *ui);
void ui_draw_about_page(Gui *ui, const char *version, const char *build_date);
#endif
