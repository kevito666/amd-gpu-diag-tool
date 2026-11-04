/* SPDX-License-Identifier: GPL-3.0-or-later
 * Single GPU sysfs write boundary. Callers own descriptors; functions do not
 * open device files. The caller enforces ARM/APPLY policy before invoking them.
 */
#ifndef AMD_GPU_DIAG_GPU_WRITES_H
#define AMD_GPU_DIAG_GPU_WRITES_H
#include "readback.h"
int gpu_write_policy(const Sysfs *sys, const char *policy);
int gpu_write_level(int fd, int level);
int gpu_write_overdrive(const Sysfs *sys, const char *command);
int gpu_write_power_cap(const Sysfs *sys, double watts);
int gpu_write_fan_pwm(const Sysfs *sys, const char *pwm);
#endif
