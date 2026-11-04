/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "desktop_probe.h"
#include "../core/util.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
bool desktop_parse_env(const char *data, size_t size, const char *key,
                       char *out, size_t cap) {
    if (!data || !key || !out || cap < 2 || !*key) return false;
    size_t klen = strlen(key);
    for (size_t pos = 0; pos < size;) {
        size_t len = strnlen(data + pos, size - pos);
        if (len == size - pos) break; /* Ignore truncated last record. */
        if (len > klen && !memcmp(data + pos, key, klen) && data[pos + klen] == '=') {
            text_copy(out, cap, data + pos + klen + 1);
            return true;
        }
        pos += len + 1;
    }
    return false;
}

void desktop_read_env_value(pid_t pid, const char *key, char *out, size_t cap) {
    if (!out || cap == 0 || !key) return;
    char p[64];
    snprintf(p, sizeof(p), "/proc/%ld/environ", (long)pid);
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    char buf[16384];
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0) return;
    (void)desktop_parse_env(buf, (size_t)n, key, out, cap);
}

static bool directory_has_prefix(const char *dirpath, const char *prefix) {
    DIR *d = opendir(dirpath);
    if (!d) return false;
    bool found = false;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, prefix, strlen(prefix)) == 0) { found = true; break; }
    }
    closedir(d);
    return found;
}

bool desktop_xlibre_installed(void) {
    /* Arch-family package database is the cleanest zero-dependency signal on
       systems where XLibre replaces xorg-server. Keep generic file fallbacks
       for other packaging layouts. */
    if (directory_has_prefix("/var/lib/pacman/local", "xlibre-xserver-")) return true;
    if (directory_has_prefix("/var/lib/pacman/local", "xlibre-meta-")) return true;
    if (path_exists("/usr/lib/pkgconfig/xlibre-server.pc")) return true;
    if (path_exists("/usr/share/pkgconfig/xlibre-server.pc")) return true;
    return false;
}
