/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AMD_GPU_DIAG_GPU_SPECS_H
#define AMD_GPU_DIAG_GPU_SPECS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const char *series;
    const char *architecture;
    int game_mhz;          /* 0 when AMD did not publish a game clock */
    int boost_mhz;         /* AMD reference boost/up-to clock */
    int tbp_w;             /* AMD reference typical/total board power */
    int vram_mb;           /* largest/reference configuration represented */
    int memory_mbps;       /* advertised DRAM data rate in Mb/s */
    int memory_bus_bits;
    const char *memory_type;
} GpuSpec;

const GpuSpec *gpu_spec_match(const char *detected_name, uint64_t vram_bytes);
size_t gpu_spec_count(void);
const GpuSpec *gpu_spec_at(size_t index);

#endif
