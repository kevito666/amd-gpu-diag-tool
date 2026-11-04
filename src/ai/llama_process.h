/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AMD_DIAG_AI_LLAMA_PROCESS_H
#define AMD_DIAG_AI_LLAMA_PROCESS_H
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    bool found;
    int port;
    int context_size;
    char api_key[256];
    char model_name[128];
    char cache_k[16];
    char cache_v[16];
    bool flash_attn;
    bool mtp;
} LlamaProcessInfo;
/* The returned pointer is a read-only snapshot until next refresh. */
const LlamaProcessInfo *llama_process_info(void);
bool llama_parse_cmdline(const char *argv, size_t used, LlamaProcessInfo *out);
void refresh_llama_process(void);
int find_llama_port(void);
void find_llama_api_key(char *out, size_t cap);
bool ai_cache_process_limit(double *limit_mib);
#endif
