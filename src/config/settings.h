/* SPDX-License-Identifier: GPL-3.0-or-later
 * User-owned persistent configuration; no GPU or GUI dependencies.
 */
#ifndef AMD_GPU_DIAG_CONFIG_SETTINGS_H
#define AMD_GPU_DIAG_CONFIG_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#define AMD_CONFIG_PATH_CAP 512
typedef enum {
    TOKEN_SOURCE_HEURISTIC = 0,
    TOKEN_SOURCE_LLAMA,
    TOKEN_SOURCE_OLLAMA,
    TOKEN_SOURCE_VLLM,
    TOKEN_SOURCE_LMSTUDIO,
    TOKEN_SOURCE_MAYA, /* engine log, no API polling */
    TOKEN_SOURCE_COUNT
} TokenSource;
typedef enum { LOG_AUTO, LOG_LLAMA, LOG_BIONIC, LOG_OLLAMA, LOG_VLLM, LOG_LMSTUDIO, LOG_OFF, LOG_MAYA, LOG_COUNT } LogParser;
typedef enum { COMP_AUTO, COMP_X11, COMP_DRM, COMP_PROCESS, COMP_OFF, COMP_COUNT } CompositorSource;
typedef struct {
    TokenSource token_source;
    LogParser log_parser;
    CompositorSource compositor_source;
    bool ai_log_manual;
    char ai_log_file[AMD_CONFIG_PATH_CAP];
    int preferred_card;
    char ai_nvme_device[32]; /* selected NVMe namespace, e.g. nvme0n1 */
} AppSettings;

/* On failure to read a file, existing defaults in *settings are preserved.
 * The app owns the struct, and applies runtime backend selection separately. */
bool config_settings_load(AppSettings *settings);
bool config_settings_save(const AppSettings *settings);
bool settings_paths(char *folder, size_t fc, char *file, size_t pc);
bool settings_int_in_range(const char *value, int min, int max, int *out);
#endif
