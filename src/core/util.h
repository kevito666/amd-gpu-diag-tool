/* SPDX-License-Identifier: GPL-3.0-or-later
 * Cross-subsystem time, text, path and descriptor helpers.
 * All descriptor ownership remains with the caller except read_path_text().
 */
#ifndef AMD_GPU_DIAG_CORE_UTIL_H
#define AMD_GPU_DIAG_CORE_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

long long ts_ns(struct timespec t);
struct timespec ns_ts(long long ns);
void add_ns(struct timespec *t, long long ns);
long long diff_ns(struct timespec a, struct timespec b);
void trim(char *s);
void text_copy(char *dst, size_t cap, const char *src);
bool path_join(char *out, size_t cap, const char *base, const char *leaf);
int read_fd_text(int fd, char *buf, size_t cap);
int read_path_text(const char *path, char *buf, size_t cap);
int write_fd_text(int fd, const char *text);
int open_rw_pair(const char *path, int *rfd, int *wfd);
bool path_exists(const char *path);

long read_long_fd(int fd, long fallback);
long read_optional_long_path(const char *path, long fallback);
#endif
