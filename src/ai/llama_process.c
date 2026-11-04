/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "llama_process.h"
#include "log_parser.h"
#include "../config/settings.h"
#include "../core/util.h"
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* Discover llama-server once per 15 seconds from the kernel process table.
 * This replaces four fork/ps/grep/awk subprocesses per second in AI mode.
 * argv is NUL-delimited, so filenames with whitespace remain intact.
 * Never log argv: it can contain an API key or private model pathname. */


static LlamaProcessInfo g_llama_process;
static time_t g_llama_last_scan = 0;

static bool llama_server_executable(const char *argv0) {
    const char *name = strrchr(argv0, '/');
    name = name ? name + 1 : argv0;
    return strstr(name, "llama-server") != NULL || strstr(name, "llama_server") != NULL;
}

/* Testable, bounded parser for a single /proc/PID/cmdline buffer. */
bool llama_parse_cmdline(const char *argv, size_t used, LlamaProcessInfo *out) {
    if (!argv || !used || !out) return false;
    size_t exe_len = strnlen(argv, used);
    if (exe_len == used || !llama_server_executable(argv)) return false;
    LlamaProcessInfo info = { .found = true, .port = 8080 };
    for (size_t pos = exe_len + 1; pos < used;) {
        const char *arg = argv + pos;
        size_t len = strnlen(arg, used - pos);
        if (len == used - pos) break;
        pos += len + 1;
        if (!len) continue;
        const char *value = NULL;
        char option[64];
        const char *eq = strchr(arg, '=');
        if (eq && (size_t)(eq - arg) < sizeof(option)) {
            size_t n = (size_t)(eq - arg);
            memcpy(option, arg, n);
            option[n] = '\0';
            value = eq + 1;
        } else {
            if (len >= sizeof(option)) continue;
            memcpy(option, arg, len + 1);
            if (pos < used) {
                size_t next_len = strnlen(argv + pos, used - pos);
                if (next_len < used - pos && next_len && argv[pos] != '-') {
                    value = argv + pos;
                    pos += next_len + 1;
                }
            }
        }
        if (!value) continue;
        if (!strcmp(option, "--port")) {
            int port;
            if (settings_int_in_range(value, 1, 65535, &port)) info.port = port;
        } else if (!strcmp(option, "--api-key")) {
            if (strlen(value) < sizeof(info.api_key)) text_copy(info.api_key, sizeof(info.api_key), value);
        } else if (!strcmp(option, "--model") || !strcmp(option, "-m")) {
            const char *base = strrchr(value, '/');
            text_copy(info.model_name, sizeof(info.model_name), base ? base+1 : value);
        } else if (!strcmp(option, "--ctx-size") || !strcmp(option, "-c")) {
            int ctx;
            if (settings_int_in_range(value, 1, INT_MAX, &ctx)) info.context_size = ctx;
        } else if (!strcmp(option, "--cache-type-k")) {
            text_copy(info.cache_k, sizeof(info.cache_k), value);
        } else if (!strcmp(option, "--cache-type-v")) {
            text_copy(info.cache_v, sizeof(info.cache_v), value);
        } else if (!strcmp(option, "--flash-attn")) {
            info.flash_attn = !strcmp(value, "on") || !strcmp(value, "1");
        } else if (!strcmp(option, "--spec-type")) {
            info.mtp = strstr(value, "mtp") != NULL;
        }
    }
    *out = info;
    return true;
}

void refresh_llama_process(void) {
    time_t now = time(NULL);
    if (g_llama_last_scan && now >= g_llama_last_scan && now - g_llama_last_scan < 15) return;
    g_llama_last_scan = now;
    memset(&g_llama_process, 0, sizeof(g_llama_process));
    DIR *dir = opendir("/proc");
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        char path[96];
        int n = snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        char args[8192];
        size_t used = fread(args, 1, sizeof(args) - 1, f);
        fclose(f);
        args[used] = '\0';
        if (llama_parse_cmdline(args, used, &g_llama_process)) break;
    }
    closedir(dir);
}

int find_llama_port(void) {
    refresh_llama_process();
    return g_llama_process.found ? g_llama_process.port : 8080;
}

void find_llama_api_key(char *out, size_t cap) {
    refresh_llama_process();
    text_copy(out, cap, g_llama_process.api_key);
}


/* Read a running llama-server's configured --cache-ram/LLAMA_ARG_CACHE_RAM.
 * Bionic launches llama-server separately, so inspecting our own environment
 * would normally be wrong. Sample infrequently (30 s); never modify /proc. */
bool ai_cache_process_limit(double *limit_mib) {
    DIR *dir = opendir("/proc");
    if (!dir) return false;
    struct dirent *entry;
    bool found = false;
    while ((entry = readdir(dir)) != NULL && !found) {
        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        char path[96], argv[8192];
        int n = snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        size_t used = fread(argv, 1, sizeof(argv)-1, f);
        fclose(f);
        if (!used) continue;
        argv[used] = '\0';
        const char *exe = strrchr(argv, '/');
        exe = exe ? exe+1 : argv;
        if (!strstr(exe, "llama-server") && !strstr(exe, "llama_server")) continue;
        /* Command-line arguments are NUL-delimited. Check both syntaxes. */
        for (size_t i = strlen(argv)+1; i < used && !found;) {
            const char *a = argv+i;
            size_t remain = used-i;
            size_t len = strnlen(a, remain);
            if (len == remain) break;
            if (!strcmp(a, "--cache-ram") || !strcmp(a, "-cram")) {
                size_t j = i+len+1;
                if (j < used) found = ai_cache_parse_limit(argv+j, limit_mib);
            } else if (!strncmp(a, "--cache-ram=", 12)) {
                found = ai_cache_parse_limit(a+12, limit_mib);
            }
            i += len+1;
        }
        if (found) break;
        n = snprintf(path, sizeof(path), "/proc/%s/environ", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        f = fopen(path, "rb");
        if (!f) continue;
        char env[65536];
        used = fread(env, 1, sizeof(env)-1, f);
        fclose(f);
        env[used] = '\0';
        for (size_t i=0; i < used && !found;) {
            const char *a = env+i;
            size_t len = strnlen(a, used-i);
            if (len == used-i) break;
            if (!strncmp(a, "LLAMA_ARG_CACHE_RAM=", 20))
                found = ai_cache_parse_limit(a+20, limit_mib);
            i += len+1;
        }
    }
    closedir(dir);
    return found;
}


const LlamaProcessInfo *llama_process_info(void) { refresh_llama_process(); return &g_llama_process; }
