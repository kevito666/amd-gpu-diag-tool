/* SPDX-License-Identifier: GPL-3.0-or-later
 * AI log discovery and bounded tail loading. Never opens a log for writing.
 */
#ifndef AMD_DIAG_AI_LOG_FILES_H
#define AMD_DIAG_AI_LOG_FILES_H
#include <stddef.h>
#include <time.h>
#include "../config/settings.h"
#define AI_LOG_TAIL_BYTES (512u * 1024u)
void ai_log_discover(const AppSettings *settings, char *path, size_t path_cap,
                     char *source, size_t source_cap);
/* Returns owned NUL-terminated complete-line tail, or NULL on error/empty. */
char *ai_log_read_tail(const char *path, time_t *mtime);
#endif
