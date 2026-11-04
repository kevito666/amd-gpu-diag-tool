/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AMD_DIAG_MONITOR_SENSORS_H
#define AMD_DIAG_MONITOR_SENSORS_H
#include "system_state.h"
void discover_cpu_temp(SystemTelemetry *s);
void discover_cpu_ccx_temps(SystemTelemetry *s);
void discover_cpu_fan(SystemTelemetry *s);
void discover_cpu_power(SystemTelemetry *s);
void discover_motherboard_temps(SystemTelemetry *s);
long read_cpu_fan_pwm_max(const char *pwm_path);
/* Close every discovered sensor handle; owner calls once after worker joins. */
void monitor_sensors_close(SystemTelemetry *s);
/* Strip thread-owned FD numbers from copied telemetry; never close them here. */
void monitor_sensors_detach_snapshot(SystemTelemetry *snapshot);
#endif
