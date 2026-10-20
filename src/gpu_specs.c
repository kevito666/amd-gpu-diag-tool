/* SPDX-License-Identifier: GPL-3.0-or-later
 * AMD GPU reference specification database.
 *
 * Values are reference-card specifications, not guarantees for partner boards.
 * Runtime firmware/kernel limits remain authoritative for tuning operations.
 */
#include "gpu_specs.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#define GIB_MIB(gib) ((gib) * 1024)

static const GpuSpec SPECS[] = {
    /* RDNA 4 / Radeon RX 9000 */
    {"AMD Radeon RX 9070 XT",       "RX 9000", "RDNA 4", 2400, 2970, 304, GIB_MIB(16), 20000, 256, "GDDR6"},
    {"AMD Radeon RX 9070",          "RX 9000", "RDNA 4", 2070, 2520, 220, GIB_MIB(16), 20000, 256, "GDDR6"},
    {"AMD Radeon RX 9070 GRE",      "RX 9000", "RDNA 4", 2220, 2790, 220, GIB_MIB(12), 18000, 192, "GDDR6"},
    {"AMD Radeon RX 9060 XT 16GB",  "RX 9000", "RDNA 4", 2530, 3130, 160, GIB_MIB(16), 20000, 128, "GDDR6"},
    {"AMD Radeon RX 9060 XT 8GB",   "RX 9000", "RDNA 4", 2530, 3130, 150, GIB_MIB(8),  20000, 128, "GDDR6"},
    {"AMD Radeon RX 9060 XT LP",    "RX 9000", "RDNA 4",    0,    0, 140, GIB_MIB(16), 20000, 128, "GDDR6"},
    {"AMD Radeon RX 9060",          "RX 9000", "RDNA 4", 2400, 2990, 132, GIB_MIB(8),  18000, 128, "GDDR6"},
    {"AMD Radeon RX 9050",          "RX 9000", "RDNA 4", 1920, 2600,  92, GIB_MIB(8),  18000, 128, "GDDR6"},
    {"AMD Radeon RX 9050 4GB",      "RX 9000", "RDNA 4", 1920, 2600,  92, GIB_MIB(4),  18000,  64, "GDDR6"},

    /* RDNA 3 / Radeon RX 7000 */
    {"AMD Radeon RX 7900 XTX",      "RX 7000", "RDNA 3", 2300, 2500, 355, GIB_MIB(24), 20000, 384, "GDDR6"},
    {"AMD Radeon RX 7900 XT",       "RX 7000", "RDNA 3", 2000, 2400, 315, GIB_MIB(20), 20000, 320, "GDDR6"},
    {"AMD Radeon RX 7900 GRE",      "RX 7000", "RDNA 3", 1880, 2245, 260, GIB_MIB(16), 18000, 256, "GDDR6"},
    {"AMD Radeon RX 7800 XT",       "RX 7000", "RDNA 3", 2124, 2430, 263, GIB_MIB(16), 19500, 256, "GDDR6"},
    {"AMD Radeon RX 7700 XT",       "RX 7000", "RDNA 3", 2171, 2544, 245, GIB_MIB(12), 18000, 192, "GDDR6"},
    {"AMD Radeon RX 7700",          "RX 7000", "RDNA 3",    0,    0, 263, GIB_MIB(16), 19500, 256, "GDDR6"},
    {"AMD Radeon RX 7600 XT",       "RX 7000", "RDNA 3", 2470, 2755, 190, GIB_MIB(16), 18000, 128, "GDDR6"},
    {"AMD Radeon RX 7600",          "RX 7000", "RDNA 3", 2250, 2655, 165, GIB_MIB(8),  18000, 128, "GDDR6"},
    {"AMD Radeon RX 7400",          "RX 7000", "RDNA 3",    0,    0,  55, GIB_MIB(8),  10800, 128, "GDDR6"},

    /* RDNA 2 / Radeon RX 6000 */
    {"AMD Radeon RX 6950 XT",       "RX 6000", "RDNA 2", 2100, 2310, 335, GIB_MIB(16), 18000, 256, "GDDR6"},
    {"AMD Radeon RX 6900 XT",       "RX 6000", "RDNA 2", 2015, 2250, 300, GIB_MIB(16), 16000, 256, "GDDR6"},
    {"AMD Radeon RX 6800 XT",       "RX 6000", "RDNA 2", 2015, 2250, 300, GIB_MIB(16), 16000, 256, "GDDR6"},
    {"AMD Radeon RX 6800",          "RX 6000", "RDNA 2", 1815, 2105, 250, GIB_MIB(16), 16000, 256, "GDDR6"},
    {"AMD Radeon RX 6750 XT",       "RX 6000", "RDNA 2", 2495, 2600, 250, GIB_MIB(12), 18000, 192, "GDDR6"},
    {"AMD Radeon RX 6750 GRE 12GB", "RX 6000", "RDNA 2", 2439, 2581, 250, GIB_MIB(12), 16000, 192, "GDDR6"},
    {"AMD Radeon RX 6750 GRE 10GB", "RX 6000", "RDNA 2", 2189, 2450, 170, GIB_MIB(10), 16000, 160, "GDDR6"},
    {"AMD Radeon RX 6700 XT",       "RX 6000", "RDNA 2", 2424, 2581, 230, GIB_MIB(12), 16000, 192, "GDDR6"},
    {"AMD Radeon RX 6700",          "RX 6000", "RDNA 2", 2174, 2450, 175, GIB_MIB(10), 16000, 160, "GDDR6"},
    {"AMD Radeon RX 6650 XT",       "RX 6000", "RDNA 2", 2410, 2635, 180, GIB_MIB(8),  17500, 128, "GDDR6"},
    {"AMD Radeon RX 6600 XT",       "RX 6000", "RDNA 2", 2359, 2589, 160, GIB_MIB(8),  16000, 128, "GDDR6"},
    {"AMD Radeon RX 6600",          "RX 6000", "RDNA 2", 2044, 2491, 132, GIB_MIB(8),  14000, 128, "GDDR6"},
    {"AMD Radeon RX 6500 XT",       "RX 6000", "RDNA 2", 2610, 2815, 113, GIB_MIB(8),  18000,  64, "GDDR6"},
    {"AMD Radeon RX 6400",          "RX 6000", "RDNA 2", 2039, 2321,  53, GIB_MIB(4),  16000,  64, "GDDR6"},

    /* RDNA 1 / Radeon RX 5000 */
    {"AMD Radeon RX 5700 XT 50th Anniversary", "RX 5000", "RDNA 1", 1830, 1980, 235, GIB_MIB(8), 14000, 256, "GDDR6"},
    {"AMD Radeon RX 5700 XT",       "RX 5000", "RDNA 1", 1755, 1905, 225, GIB_MIB(8),  14000, 256, "GDDR6"},
    {"AMD Radeon RX 5700",          "RX 5000", "RDNA 1", 1625, 1725, 180, GIB_MIB(8),  14000, 256, "GDDR6"},
    {"AMD Radeon RX 5600 XT",       "RX 5000", "RDNA 1", 1375, 1560, 150, GIB_MIB(6),  14000, 192, "GDDR6"},
    {"AMD Radeon RX 5500 XT",       "RX 5000", "RDNA 1", 1717, 1845, 130, GIB_MIB(8),  14000, 128, "GDDR6"},
    {"AMD Radeon RX 5500",          "RX 5000", "RDNA 1", 1670, 1845, 150, GIB_MIB(4),  14000, 128, "GDDR6"},
    {"AMD Radeon RX 5300",          "RX 5000", "RDNA 1", 1448, 1645, 100, GIB_MIB(3),  14000,  96, "GDDR6"},

    /* Vega */
    {"AMD Radeon VII",              "Radeon VII", "GCN 5.1", 1400, 1750, 300, GIB_MIB(16), 2000, 4096, "HBM2"},
    {"AMD Radeon RX Vega 64",       "RX Vega", "GCN 5.0", 1247, 1546, 295, GIB_MIB(8),  1890, 2048, "HBM2"},
    {"AMD Radeon RX Vega 56",       "RX Vega", "GCN 5.0", 1156, 1471, 210, GIB_MIB(8),  1600, 2048, "HBM2"},

    /* Polaris / Radeon RX 500 */
    {"AMD Radeon RX 590",           "RX 500", "GCN 4", 1469, 1545, 225, GIB_MIB(8), 8000, 256, "GDDR5"},
    {"AMD Radeon RX 580",           "RX 500", "GCN 4", 1257, 1340, 185, GIB_MIB(8), 8000, 256, "GDDR5"},
    {"AMD Radeon RX 570",           "RX 500", "GCN 4", 1168, 1244, 150, GIB_MIB(8), 7000, 256, "GDDR5"},
    {"AMD Radeon RX 560",           "RX 500", "GCN 4", 1175, 1275,  80, GIB_MIB(4), 7000, 128, "GDDR5"},
    {"AMD Radeon RX 550",           "RX 500", "GCN 4",    0, 1183,  50, GIB_MIB(4), 7000, 128, "GDDR5"},

    /* Polaris / Radeon RX 400 */
    {"AMD Radeon RX 480",           "RX 400", "GCN 4", 1120, 1266, 150, GIB_MIB(8), 7000, 256, "GDDR5"},
    {"AMD Radeon RX 470",           "RX 400", "GCN 4",  926, 1206, 120, GIB_MIB(4), 6600, 256, "GDDR5"},
    {"AMD Radeon RX 460",           "RX 400", "GCN 4", 1090, 1200,  70, GIB_MIB(4), 7000, 128, "GDDR5"},
};

static void normalize(const char *in, char *out, size_t cap) {
    size_t n = 0;
    bool pending_space = false;
    if (!in || cap == 0) return;
    while (*in && n + 1 < cap) {
        unsigned char c = (unsigned char)*in++;
        if (isalnum(c)) {
            if (pending_space && n && n + 1 < cap) out[n++] = ' ';
            out[n++] = (char)tolower(c);
            pending_space = false;
        } else {
            pending_space = true;
        }
    }
    out[n] = '\0';
}

static void model_key(const char *name, char *out, size_t cap) {
    char norm[160];
    normalize(name, norm, sizeof(norm));
    const char *p = strstr(norm, "radeon ");
    if (p) p += 7;
    else p = norm;
    if (strncmp(p, "rx ", 3) == 0 || strncmp(p, "vii", 3) == 0) {
        /* keep RX/VII model identity */
    }
    size_t n = strnlen(p, cap ? cap - 1 : 0);
    if (cap) {
        memcpy(out, p, n);
        out[n] = '\0';
        /* Marketing names often omit capacity while database entries need it
           to distinguish variants. Match the model first, then use VRAM size
           as the tie-breaker. */
        static const char *suffixes[] = {" 4gb", " 6gb", " 8gb", " 10gb", " 12gb", " 16gb", " 20gb", " 24gb"};
        for (size_t i = 0; i < sizeof(suffixes)/sizeof(suffixes[0]); i++) {
            size_t olen = strlen(out), slen = strlen(suffixes[i]);
            if (olen >= slen && strcmp(out + olen - slen, suffixes[i]) == 0) {
                out[olen - slen] = '\0';
                break;
            }
        }
    }
}

static int vram_distance_mb(uint64_t bytes, int mb) {
    if (!bytes || mb <= 0) return 0;
    uint64_t actual = bytes / (1024ULL * 1024ULL);
    uint64_t ref = (uint64_t)mb;
    uint64_t d = actual > ref ? actual - ref : ref - actual;
    return d > 1000000ULL ? 1000000 : (int)d;
}

const GpuSpec *gpu_spec_match(const char *detected_name, uint64_t vram_bytes) {
    char detected[192];
    model_key(detected_name, detected, sizeof(detected));
    const GpuSpec *best = NULL;
    size_t best_len = 0;
    int best_vram = 1000001;

    for (size_t i = 0; i < sizeof(SPECS) / sizeof(SPECS[0]); i++) {
        char key[160];
        model_key(SPECS[i].name, key, sizeof(key));
        if (!key[0] || !strstr(detected, key)) continue;
        size_t len = strlen(key);
        int vd = vram_distance_mb(vram_bytes, SPECS[i].vram_mb);
        if (!best || len > best_len || (len == best_len && vd < best_vram)) {
            best = &SPECS[i];
            best_len = len;
            best_vram = vd;
        }
    }

    /* Some PCI descriptions include only the model key with no AMD/Radeon prefix. */
    if (!best) {
        for (size_t i = 0; i < sizeof(SPECS) / sizeof(SPECS[0]); i++) {
            char key[160];
            model_key(SPECS[i].name, key, sizeof(key));
            if (!key[0]) continue;
            const char *rx = strstr(key, "rx ");
            if (!rx) continue;
            if (strstr(detected, rx)) {
                size_t len = strlen(rx);
                int vd = vram_distance_mb(vram_bytes, SPECS[i].vram_mb);
                if (!best || len > best_len || (len == best_len && vd < best_vram)) {
                    best = &SPECS[i];
                    best_len = len;
                    best_vram = vd;
                }
            }
        }
    }
    return best;
}

size_t gpu_spec_count(void) {
    return sizeof(SPECS) / sizeof(SPECS[0]);
}

const GpuSpec *gpu_spec_at(size_t index) {
    return index < gpu_spec_count() ? &SPECS[index] : NULL;
}
