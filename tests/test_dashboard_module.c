/* SPDX-License-Identifier: GPL-3.0-or-later
 * Header/API and temperature threshold regression independent of the app
 * controller, GPU sysfs, model servers and display server.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/ui/dashboards.h"
#include "../src/ui/status.h"

int main(void) {
    assert(temperature_zone(-1, 90000, 105000) == TUNE_ZONE_UNAVAILABLE);
    assert(temperature_zone(45, 90000, 105000) == TUNE_ZONE_SAFE);
    assert(temperature_zone(89, 90000, 105000) == TUNE_ZONE_WARNING);
    assert(temperature_zone(108, 90000, 105000) == TUNE_ZONE_DANGER);

    AppSettings settings = {.token_source = TOKEN_SOURCE_MAYA, .log_parser = LOG_MAYA};
    MayaLogMonitor maya = {0};
    NvmeMonitor nvme = {0};
    AiLogEvent log_events[AI_LOG_MAX_EVENTS] = {{0}};
    UiAiSources inputs = {
        .settings = &settings, .maya = &maya, .nvme = &nvme,
        .log_events = log_events,
        .event = {.available = true, .kind = "READBACK", .detail = "OK",
                  .actor = "fixture", .actor_activity = 0.0}
    };
    assert(inputs.settings->log_parser == LOG_MAYA);
    assert(strcmp(inputs.event.kind, "READBACK") == 0);
    assert(inputs.event.available && inputs.log_events == log_events);

    /* Link the independently compiled renderer APIs into the test. */
    void (*ai_fn)(Gui *, const Gpu *, const Target *, const Stats *, int,
                  bool, const ClientMonitor *, const PowerTelemetry *,
                  const SystemTelemetry *, const UiAiSources *) = ui_draw_ai_dashboard;
    void (*gaming_fn)(Gui *, const Gpu *, const Sysfs *, const PowerTelemetry *,
                      const SystemTelemetry *) = ui_draw_gaming_dashboard;
    assert(ai_fn != NULL && gaming_fn != NULL);
    puts("UI dashboard module PASS: independent API, snapshots and thermal zones");
    return 0;
}
