/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only Linux/XLibre session detection helpers.
 */
#ifndef AMD_DIAG_DESKTOP_PROBE_H
#define AMD_DIAG_DESKTOP_PROBE_H
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
void desktop_read_env_value(pid_t pid, const char *key, char *out, size_t cap);
bool desktop_xlibre_installed(void);
/* Parse a bounded NUL-delimited /proc environment snapshot. */
bool desktop_parse_env(const char *data, size_t size, const char *key, char *out, size_t cap);
#endif
