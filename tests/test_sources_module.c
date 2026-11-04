/* SPDX-License-Identifier: GPL-3.0-or-later
 * Completely GUI-free validation of dev.3 process, I/O, log, and desktop APIs.
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
#include <sys/stat.h>
#include <time.h>
#include "../src/ai/http.h"
#include "../src/ai/llama_process.h"
#include "../src/ai/log_files.h"
#include "../src/monitor/desktop_probe.h"
#include "../src/monitor/sensors.h"

int main(void) {
    const char argv[] = "/usr/bin/llama-server\0--port\0" "19090\0"
        "--model\0/tmp/a model.gguf\0--ctx-size=65536\0"
        "--cache-type-k\0q8_0\0";
    LlamaProcessInfo info = {0};
    assert(llama_parse_cmdline(argv, sizeof(argv) - 1, &info));
    assert(info.port == 19090 && info.context_size == 65536);
    assert(!strcmp(info.model_name, "a model.gguf"));
    assert(!strcmp(info.cache_k, "q8_0"));
    const char bad[] = "bash\0--port\0" "8080\0";
    assert(!llama_parse_cmdline(bad, sizeof(bad) - 1, &info));

    struct http_buf h = { .data = malloc(4), .capacity = 4, .limit = 8 };
    assert(h.data);
    assert(http_response_limit(32) == 31);
    assert(http_write_cb("abcd", 1, 4, &h) == 4);
    assert(http_write_cb("efgh", 1, 4, &h) == 4);
    assert(http_write_cb("x", 1, 1, &h) == 0);
    assert(!strcmp(h.data, "abcdefgh"));
    free(h.data);

    const char environment[] = "TEST_VERSION=0.8.0-dev.3\0HOME=/tmp/a b\0MISSINGVALUE";
    char value[64] = "?";
    assert(desktop_parse_env(environment, sizeof(environment) - 1,
                             "HOME", value, sizeof(value)));
    assert(!strcmp(value, "/tmp/a b"));
    assert(!desktop_parse_env(environment, sizeof(environment) - 1,
                              "MISSINGVALUE", value, sizeof(value)));
    assert(!desktop_parse_env(environment, sizeof(environment) - 1,
                              "NO_SUCH_KEY", value, sizeof(value)));

    char folder[] = "/tmp/amd-dev3-XXXXXX";
    assert(mkdtemp(folder));
    char logfile[512], pwm[512], pwm_max[512];
    assert(snprintf(logfile, sizeof(logfile), "%s/log.txt", folder) < (int)sizeof(logfile));
    assert(snprintf(pwm, sizeof(pwm), "%s/pwm1", folder) < (int)sizeof(pwm));
    assert(snprintf(pwm_max, sizeof(pwm_max), "%s/pwm1_max", folder) < (int)sizeof(pwm_max));
    FILE *f = fopen(logfile, "wb");
    assert(f);
    /* A non-line-aligned offset must discard the first incomplete record. */
    for (int i = 0; i < 25000; i++) assert(fputs("unused data 123456789012345678901234567890\n", f) >= 0);
    assert(fputs("FINAL_TASK=123\n", f) >= 0);
    assert(fclose(f) == 0);
    time_t mtime = 0;
    char *tail = ai_log_read_tail(logfile, &mtime);
    assert(tail && mtime > 0);
    assert(strstr(tail, "FINAL_TASK=123\n"));
    assert(!strncmp(tail, "unused data", 11));
    assert(strlen(tail) <= AI_LOG_TAIL_BYTES);
    free(tail);

    AppSettings cfg = { .ai_log_manual = true, .log_parser = LOG_LLAMA };
    snprintf(cfg.ai_log_file, sizeof(cfg.ai_log_file), "%s", logfile);
    char selected[512] = "", source[32] = "";
    ai_log_discover(&cfg, selected, sizeof(selected), source, sizeof(source));
    assert(!strcmp(selected, logfile) && !strcmp(source, "Manual"));
    f = fopen(pwm_max, "w");
    assert(f && fputs("512\n", f) >= 0 && fclose(f) == 0);
    assert(read_cpu_fan_pwm_max(pwm) == 512);
    /* A configured log may be a FIFO or device: opening it must never stall
     * worker shutdown. Non-regular files must be rejected after fstat. */
    char fifo[512], linkpath[512];
    assert(snprintf(fifo, sizeof(fifo), "%s/blocked.log", folder) < (int)sizeof(fifo));
    assert(snprintf(linkpath, sizeof(linkpath), "%s/link.log", folder) < (int)sizeof(linkpath));
    assert(mkfifo(fifo, 0600) == 0);
    struct timespec start, finish;
    assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    assert(ai_log_read_tail(fifo, &mtime) == NULL);
    assert(clock_gettime(CLOCK_MONOTONIC, &finish) == 0);
    double elapsed = (finish.tv_sec - start.tv_sec) +
                     (finish.tv_nsec - start.tv_nsec) / 1e9;
    assert(elapsed < 1.0);
    assert(symlink(fifo, linkpath) == 0);
    assert(ai_log_read_tail(linkpath, NULL) == NULL);
    assert(ai_log_read_tail(folder, NULL) == NULL);
    assert(unlink(linkpath) == 0 && unlink(fifo) == 0);
    assert(unlink(logfile) == 0 && unlink(pwm_max) == 0 && rmdir(folder) == 0);
    /* Every sampler-owned sensor FD is released exactly once at shutdown. */
    SystemTelemetry fake = {0};
    fake.fd_cpu_temp = -1;
    fake.fd_cpu_tctl = -1;
    int pfd[2];
    assert(pipe(pfd) == 0);
    fake.fd_cpu_temp = pfd[0];
    fake.fd_cpu_fan_rpm = pfd[1];
    fake.fd_cpu_fan_pct = -1;
    fake.cpu_fan_discovered = true;
    SystemTelemetry copied = fake;
    copied.fd_cpu_power = pfd[0];
    copied.mb_temp_fds[0] = pfd[1];
    copied.cpu_temp_c = 53.5;
    monitor_sensors_detach_snapshot(&copied);
    assert(copied.fd_cpu_temp == -1 && copied.fd_cpu_power == -1 &&
           copied.mb_temp_fds[0] == -1 && copied.cpu_temp_c == 53.5);
    assert(fcntl(pfd[0], F_GETFD) != -1 && fcntl(pfd[1], F_GETFD) != -1);
    monitor_sensors_close(&fake);
    assert(fake.fd_cpu_temp == -1 && fake.fd_cpu_fan_rpm == -1);
    assert(fcntl(pfd[0], F_GETFD) == -1 && errno == EBADF);
    assert(fcntl(pfd[1], F_GETFD) == -1 && errno == EBADF);
    monitor_sensors_close(&fake); /* idempotent when FDs are -1 */
    puts("Standalone AI sources, log files, CPU sensors, desktop probe: PASS");
    return 0;
}
