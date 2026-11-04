/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit single API for GPU policy, DPM and tuning sysfs writes.
 * This layer is deliberately synchronous to preserve v0.7.0b behavior.
 */
#include "writes.h"
#include "../core/util.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int gpu_write_policy(const Sysfs *sys, const char *policy) {
    if (!sys || !policy) { errno = EINVAL; return -1; }
    return write_fd_text(sys->fd_policy_w, policy);
}
int gpu_write_level(int fd, int level) {
    char buf[32];
    if (level < 0) { errno = EINVAL; return -1; }
    snprintf(buf, sizeof(buf), "%d", level);
    return write_fd_text(fd, buf);
}
int gpu_write_overdrive(const Sysfs *sys, const char *command) {
    if (!sys || !command) { errno = EINVAL; return -1; }
    return write_fd_text(sys->fd_od_w, command);
}
int gpu_write_power_cap(const Sysfs *sys, double watts) {
    if (!sys || sys->fd_power_cap_w < 0) { errno = ENOTSUP; return -1; }
    if (!isfinite(watts) || watts < 0.0 || watts > 10000.0) { errno = EINVAL; return -1; }
    long long microwatts = llround(watts * 1000000.0);
    char buf[64];
    snprintf(buf, sizeof(buf), "%lld", microwatts);
    return write_fd_text(sys->fd_power_cap_w, buf);
}
int gpu_write_fan_pwm(const Sysfs *sys, const char *pwm) {
    if (!sys || !pwm) { errno = EINVAL; return -1; }
    return write_fd_text(sys->fd_pwm_w, pwm);
}
