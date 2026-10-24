/* Hardware-free regressions for beta stabilization. */
#include <assert.h>
#include <dirent.h>

int diagnostic_program_main(int argc, char **argv);
#define AMD_GPU_DIAG_TEST 1
#define main diagnostic_program_main
#include "../src/amd-gpu-diagnostic.c"
#undef main

static int fd_count(void) {
    DIR *d = opendir("/proc/self/fd");
    assert(d);
    int n = 0;
    while (readdir(d)) ++n;
    closedir(d);
    return n;
}

int main(void) {
    int parsed = -99;
    assert(settings_int_in_range("0", 0, 4, &parsed) && parsed == 0);
    assert(settings_int_in_range("-1", -1, 4, &parsed) && parsed == -1);
    assert(settings_int_in_range("4", 0, 4, &parsed) && parsed == 4);
    assert(!settings_int_in_range("garbage", 0, 4, &parsed));
    assert(!settings_int_in_range("", 0, 4, &parsed));
    assert(!settings_int_in_range("1garbage", 0, 4, &parsed));
    assert(!settings_int_in_range(" 1", 0, 4, &parsed));
    assert(!settings_int_in_range("99999999999999999", 0, 4, &parsed));
    assert(!settings_int_in_range("5", 0, 4, &parsed));

    /* NUL-separated argv, including a model path containing whitespace. */
    const char cmd[] = "/usr/bin/llama-server\0"
                       "--port\0" "18080\0"
                       "--api-key=hidden-test-value\0"
                       "--model\0" "/home/user/models/Model With Spaces.gguf\0"
                       "--ctx-size\0" "128818\0"
                       "--cache-type-k\0" "q8_0\0"
                       "--cache-type-v=f16\0"
                       "--flash-attn\0" "on\0"
                       "--spec-type\0" "mtp\0";
    LlamaProcessInfo server = {0};
    assert(llama_parse_cmdline(cmd, sizeof(cmd)-1, &server));
    assert(server.found && server.port == 18080 && server.context_size == 128818);
    assert(!strcmp(server.api_key, "hidden-test-value"));
    assert(!strcmp(server.model_name, "Model With Spaces.gguf"));
    assert(!strcmp(server.cache_k, "q8_0") && !strcmp(server.cache_v, "f16"));
    assert(server.flash_attn && server.mtp);
    const char unrelated[] = "/usr/bin/bash\0--port\0" "8000\0";
    assert(!llama_parse_cmdline(unrelated, sizeof(unrelated)-1, &server));

    char statline[1024];
    size_t offset = (size_t)snprintf(statline, sizeof(statline), "123 (my special app) S");
    for (int field=4; field<=24; ++field) {
        unsigned val = field==14?300:field==15?100:field==22?7200:0;
        int wrote = snprintf(statline+offset, sizeof(statline)-offset, " %u", val);
        assert(wrote > 0 && (size_t)wrote < sizeof(statline)-offset);
        offset += (size_t)wrote;
    }
    uint64_t cpu_ticks=0, start_ticks=0;
    assert(parse_proc_cpu_stat(statline, &cpu_ticks, &start_ticks));
    assert(cpu_ticks == 400 && start_ticks == 7200);
    assert(!parse_proc_cpu_stat("bogus stat", &cpu_ticks, &start_ticks));

    struct http_buf b = { .data = malloc(4), .capacity = 4, .limit = 16 };
    assert(b.data);
    char first[] = "hello", second[] = " world", too_big[] = "12345678901234567";
    assert(http_write_cb(first, 1, 5, &b) == 5);
    assert(http_write_cb(second, 1, 6, &b) == 6);
    assert(b.size == 11 && !strcmp(b.data, "hello world"));
    assert(http_write_cb(too_big, 1, 17, &b) == 0);
    assert(http_write_cb(too_big, SIZE_MAX, 2, &b) == 0);
    assert(!strcmp(b.data, "hello world") && b.size == 11);
    free(b.data);
    assert(http_response_limit(2) == 1);
    assert(http_response_limit(4096) == 4095);
    assert(http_response_limit(HTTP_MAX_RESPONSE * 2) == HTTP_MAX_RESPONSE);

    /* Failed firmware writes must not alter the target that the watchdog
     * enforces or discard existing manual clock locks. */
    Sysfs failed = { .fd_policy_w = -1 };
    Target desired = { .policy = "profile_standard", .enabled = true,
                       .lock_sclk = true, .sclk_level = 2 };
    char mode_message[256];
    set_mode(&failed, &desired, 1, mode_message, sizeof(mode_message));
    assert(!strcmp(desired.policy, "profile_standard"));
    assert(desired.lock_sclk && desired.sclk_level == 2);

    /* Changing telemetry backend must reset previous source counters. */
    SystemTelemetry telemetry = {0};
    telemetry.token_source = TOKEN_SOURCE_LLAMA;
    token_rate_prepare_source(&telemetry);
    telemetry.tokens.has_data = true;
    telemetry.tokens.prev_decoded = 2000;
    telemetry.tokens.tokens_per_sec = 180;
    telemetry.tokens.last_api_poll.tv_sec = 99;
    token_rate_prepare_source(&telemetry);
    assert(telemetry.tokens.prev_decoded == 2000); /* same source retained */
    telemetry.token_source = TOKEN_SOURCE_LMSTUDIO;
    token_rate_prepare_source(&telemetry);
    assert(telemetry.tokens.source_initialized);
    assert(telemetry.tokens.source_in_use == TOKEN_SOURCE_LMSTUDIO);
    assert(!telemetry.tokens.has_data && telemetry.tokens.prev_decoded == 0);
    assert(telemetry.tokens.tokens_per_sec == 0);
    assert(telemetry.tokens.last_api_poll.tv_sec == 0);

    /* Turning off log parsing removes the cached history and cache occupancy. */
    g_settings.log_parser = LOG_OFF;
    telemetry.ai_prompt_cache.usage_valid = true;
    telemetry.ai_prompt_cache.used_mib = 12345;
    telemetry.ai_log_event_count = 4;
    text_copy(telemetry.ai_log_path, sizeof(telemetry.ai_log_path), "/tmp/old-bionic.log");
    g_ai_log_events[0].task_id = 123;
    parse_ai_log(&telemetry);
    assert(!telemetry.ai_prompt_cache.usage_valid);
    assert(telemetry.ai_log_event_count == 0 && telemetry.ai_log_path[0] == '\0');
    assert(g_ai_log_events[0].task_id == 0);
    g_settings.log_parser = LOG_AUTO;

    char home[] = "/tmp/amd-diag-audit-XXXXXX";
    assert(mkdtemp(home));
    char pwm[MAX_PATH_LEN], limit[MAX_PATH_LEN];
    assert(snprintf(pwm, sizeof(pwm), "%s/pwm1", home) < (int)sizeof(pwm));
    assert(snprintf(limit, sizeof(limit), "%s/pwm1_max", home) < (int)sizeof(limit));
    FILE *f=fopen(limit, "w");
    assert(f);
    assert(fputs("512\n", f) >= 0 && fclose(f) == 0);
    char unchanged[MAX_PATH_LEN];
    text_copy(unchanged, sizeof(unchanged), pwm);
    int fds = fd_count();
    for(int i=0; i<1000; ++i) assert(read_cpu_fan_pwm_max(pwm) == 512);
    assert(fd_count() == fds);
    assert(!strcmp(pwm, unchanged));
    assert(unlink(limit) == 0 && rmdir(home) == 0);

    puts("Audit PASS: strict config, proc argv/stat, bounded HTTP, fan path and descriptor lifetime");
    return 0;
}
