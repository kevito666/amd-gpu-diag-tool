/* SPDX-License-Identifier: GPL-3.0-or-later
 * GPU policy and tuning control tests against synthetic sysfs descriptors.
 * This executable never opens a real /sys GPU control file.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/gpu/device.h"
#include "../src/gpu/policy.h"
#include "../src/gpu/tuning.h"
#include "../src/gpu/writes.h"
#include "../src/core/util.h"

static int synthetic_fd(char path[64], const char *value) {
    strcpy(path, "/tmp/amd-dpm-control-XXXXXX");
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(write(fd, value, strlen(value)) == (ssize_t)strlen(value));
    return fd;
}

int main(void) {
    assert(POLICY_COUNT == 8);
    assert(valid_policy("profile_standard"));
    assert(!valid_policy("nonsense"));

    DpmLevel levels[8];
    const char *list = "0: 300Mhz\n1: 900Mhz *\n2: 1700Mhz\n";
    assert(parse_dpm_levels(list, levels, 8) == 3);
    assert(levels[1].active && levels[1].index == 1 && levels[1].mhz == 900);

    /* All fd writes share a single controlled entry point. */
    Sysfs bad = {.fd_policy_w = -1, .fd_od_w = -1,
                 .fd_power_cap_w = -1, .fd_pwm_w = -1};
    Target desired = {.policy = "profile_standard", .enabled = true,
                      .lock_sclk = true, .sclk_level = 1};
    char msg[256] = {0};
    set_mode(&bad, &desired, 1, msg, sizeof(msg));
    assert(!strcmp(desired.policy, "profile_standard") && desired.lock_sclk);
    assert(gpu_write_level(-1, -1) == -1 && errno == EINVAL);
    assert(gpu_write_power_cap(&bad, 110.0) == -1 && errno == ENOTSUP);
    assert(gpu_write_overdrive(&bad, "c") == -1);
    TuningState tune = {.available = true, .armed = false};
    assert(tuning_apply(&bad, &desired, &tune, msg, sizeof(msg)) == -1);
    assert(!tune.armed && !strcmp(desired.policy, "profile_standard"));
    tune.armed = true;
    assert(tuning_apply(&bad, &desired, &tune, msg, sizeof(msg)) == -1);
    assert(!tune.armed && !strcmp(desired.policy, "profile_standard"));
    assert(tuning_reset(&bad, &desired, &tune, msg, sizeof(msg)) == -1);
    assert(!strcmp(desired.policy, "profile_standard") && desired.lock_sclk);

    /* Fast-path watchdog read/write/verify on a regular test file. Same-length
     * strings preserve sysfs-like contents without truncating the fixture. */
    char filename[64];
    int fd = synthetic_fd(filename, "low\n");
    Sysfs sys = {.fd_policy_r = fd, .fd_policy_w = fd};
    Target policy = {.policy = "low", .enabled = true};
    Stats stats = {0};
    assert(enforce_policy_once(&sys, &policy, &stats) == 0);
    assert(stats.checks == 1 && stats.corrections == 0);
    strcpy(policy.policy, "high");
    assert(enforce_policy_once(&sys, &policy, &stats) == 1);
    assert(stats.checks == 2 && stats.policy_mismatches == 1 && stats.corrections == 1);
    assert(enforce_policy_once(&sys, &policy, &stats) == 0);
    assert(!stats.verify_failures);
    close(fd); unlink(filename);

    /* A failing driver write must not mutate the requested target, and the
     * watchdog reports failure instead of fabricating successful correction. */
    fd = synthetic_fd(filename, "low\n");
    sys.fd_policy_r = fd; sys.fd_policy_w = -1;
    stats = (Stats){0};
    assert(enforce_policy_once(&sys, &policy, &stats) == -1);
    assert(stats.checks == 1 && stats.write_errors == 1 && stats.corrections == 0);
    assert(!strcmp(policy.policy, "high"));
    close(fd); unlink(filename);

    /* Synthetic Sysfs lifecycle: only a policy node is required, and closing
     * the resulting state must release both owned descriptors. */
    char fake_dir[] = "/tmp/amd-gpu-device-XXXXXX";
    assert(mkdtemp(fake_dir));
    char node[512];
    assert(snprintf(node, sizeof(node), "%s/power_dpm_force_performance_level", fake_dir) < (int)sizeof(node));
    int tmpfd = open(node, O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    assert(tmpfd >= 0 && write(tmpfd, "auto\n", 5) == 5 && close(tmpfd) == 0);
    Gpu gpu = {0};
    text_copy(gpu.base, sizeof(gpu.base), fake_dir);
    sys = (Sysfs){0};
    assert(sysfs_open(&gpu, &sys) == 0);
    assert(sys.fd_policy_r >= 0 && sys.fd_policy_w >= 0);
    const int owned_policy_fd = sys.fd_policy_r;
    sysfs_close(&sys);
    assert(sys.fd_policy_r == -1 && sys.fd_policy_w == -1);
    assert(fcntl(owned_policy_fd, F_GETFD) == -1 && errno == EBADF);
    sysfs_close(&sys);
    assert(unlink(node) == 0 && rmdir(fake_dir) == 0);

    assert(gpu_write_power_cap(&sys, NAN) == -1);
    assert(gpu_write_power_cap(&sys, -0.5) == -1);
    puts("GPU control module PASS: policy, DPM, write faults, tuning disarm, readback");
    return 0;
}
