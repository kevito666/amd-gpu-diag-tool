/* SPDX-License-Identifier: GPL-3.0-or-later
 * Centralizes helper behavior shared by Linux monitors and GPU controllers.
 */
#define _GNU_SOURCE
#include "util.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

long long ts_ns(struct timespec t) {
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

struct timespec ns_ts(long long ns) {
    struct timespec t;
    if (ns < 0) ns = 0;
    t.tv_sec = ns / 1000000000LL;
    t.tv_nsec = ns % 1000000000LL;
    return t;
}

void add_ns(struct timespec *t, long long ns) {
    long long x = ts_ns(*t) + ns;
    *t = ns_ts(x);
}

long long diff_ns(struct timespec a, struct timespec b) {
    return ts_ns(a) - ts_ns(b);
}

void trim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

void text_copy(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) src = "";
    size_t n = strnlen(src, cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool path_join(char *out, size_t cap, const char *base, const char *leaf) {
    if (!out || cap == 0 || !base || !leaf) { errno = EINVAL; return false; }
    size_t a = strlen(base), b = strlen(leaf);
    bool slash = a > 0 && base[a - 1] != '/' && b > 0 && leaf[0] != '/';
    bool trim = a > 0 && base[a - 1] == '/' && b > 0 && leaf[0] == '/';
    size_t skip = trim ? 1u : 0u;
    if (a + (slash ? 1u : 0u) + b - skip + 1u > cap) { errno = ENAMETOOLONG; out[0] = '\0'; return false; }
    memcpy(out, base, a);
    size_t pos = a;
    if (slash) out[pos++] = '/';
    memcpy(out + pos, leaf + skip, b - skip);
    out[pos + b - skip] = '\0';
    return true;
}

int read_fd_text(int fd, char *buf, size_t cap) {
    if (fd < 0 || cap < 2) { errno = EINVAL; return -1; }
    ssize_t n;
    do { n = pread(fd, buf, cap - 1, 0); } while (n < 0 && errno == EINTR);
    if (n < 0) return -1;
    buf[n] = '\0';
    trim(buf);
    return (int)n;
}

int read_path_text(const char *path, char *buf, size_t cap) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    int rc = read_fd_text(fd, buf, cap);
    close(fd);
    return rc;
}

int write_fd_text(int fd, const char *text) {
    if (fd < 0 || !text) { errno = EINVAL; return -1; }
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "%s\n", text);
    if (len <= 0 || (size_t)len >= sizeof(buf)) { errno = EOVERFLOW; return -1; }
    (void)lseek(fd, 0, SEEK_SET);
    ssize_t n;
    do { n = write(fd, buf, (size_t)len); } while (n < 0 && errno == EINTR);
    if (n != len) { if (n >= 0) errno = EIO; return -1; }
    return 0;
}

int open_rw_pair(const char *path, int *rfd, int *wfd) {
    *rfd = -1; *wfd = -1;
    int r = open(path, O_RDONLY | O_CLOEXEC);
    if (r < 0) return -1;
    int w = open(path, O_WRONLY | O_CLOEXEC);
    if (w < 0) { int saved = errno; close(r); errno = saved; return -1; }
    *rfd = r; *wfd = w;
    return 0;
}

bool path_exists(const char *p) {
    return access(p, F_OK) == 0;
}

long read_optional_long_path(const char *path, long fallback) {
    char b[64];
    if (!path || !*path || read_path_text(path, b, sizeof(b)) < 0) return fallback;
    char *end = NULL;
    errno = 0;
    long v = strtol(b, &end, 10);
    if (errno || end == b) return fallback;
    return v;
}


/* Read an already-open numeric sysfs descriptor without taking ownership. */
long read_long_fd(int fd, long fallback) {
    char buf[128];
    if (fd < 0 || read_fd_text(fd, buf, sizeof(buf)) < 0) return fallback;
    errno = 0;
    char *end = NULL;
    long value = strtol(buf, &end, 10);
    if (errno || end == buf) return fallback;
    return value;
}
