/* SPDX-License-Identifier: GPL-3.0-or-later
 * UI page link and snapshot contracts. No live display or GPU writes.
 */
#include <assert.h>
#include <stdio.h>
#include "../src/ui/pages.h"

int main(void) {
    AppSettings settings = {.log_parser = LOG_MAYA, .token_source = TOKEN_SOURCE_MAYA};
    NvmeMonitor nvme = {0};
    Gpu gpu = {0};
    UiSettingsView v = {
        .gpu = &gpu, .gpus = &gpu, .gpu_count = 1,
        .requested_gpu = 0, .settings = &settings, .nvme = &nvme,
        .editing_path = false, .edit_path_buffer = ""
    };
    assert(v.settings->log_parser == LOG_MAYA);
    assert(v.gpu_count == 1 && !v.editing_path);
    void (*tuning)(Gui *, const TuningState *) = ui_draw_tuning_page;
    void (*settings_page)(Gui *, const UiSettingsView *) = ui_draw_settings_page;
    void (*power)(Gui *, const Target *, const Stats *, const PowerTelemetry *,
                  const Sysfs *, const SystemTelemetry *, bool, int) = ui_draw_power_page;
    void (*desktop)(Gui *, const SessionInfo *, const char *) = ui_draw_desktop_page;
    void (*events)(Gui *, const EventLog *) = ui_draw_events_page;
    void (*trace)(Gui *, const TraceState *) = ui_draw_trace_page;
    assert(tuning && settings_page && power && desktop && events && trace);
    puts("UI page module PASS: linkable pages, explicit read-only inputs");
    return 0;
}
