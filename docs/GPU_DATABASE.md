# GPU Reference Database

AMD GPU Diagnostic Tool v0.6.0 contains a small compiled-in database of AMD **desktop reference-card** specifications. It is used for identification, context, reference clocks/TBP, and color-zone baselines. It is **not** used to override the running kernel/firmware tuning limits.

## Runtime authority

The application always treats the values exposed by the live AMDGPU driver as authoritative for writes:

- `pp_od_clk_voltage` / `OD_RANGE` for overdrive clock and voltage ranges
- hwmon `power1_cap_min` and `power1_cap_max` for board-power cap range

Partner cards may ship with different clocks, power limits, cooling, memory capacity, or firmware. The program captures the actual startup values and uses those as the local stock baseline whenever possible.

## Matching

Matching is based on the detected Radeon marketing/model name. Capacity suffixes such as `8GB` and `16GB` are normalized, then actual VRAM size is used as the tie-breaker for variants such as RX 9060 XT and RX 6750 GRE.

## Coverage

Current database size: **50 desktop Radeon reference entries**.

| Model | Series | Architecture | Game MHz | Boost MHz | TBP W | VRAM | Memory | Bus |
|---|---|---:|---:|---:|---:|---:|---|---:|
| AMD Radeon RX 9070 XT | RX 9000 | RDNA 4 | 2400 | 2970 | 304 | 16 GB | GDDR6 20 Gbps | 256-bit |
| AMD Radeon RX 9070 | RX 9000 | RDNA 4 | 2070 | 2520 | 220 | 16 GB | GDDR6 20 Gbps | 256-bit |
| AMD Radeon RX 9070 GRE | RX 9000 | RDNA 4 | 2220 | 2790 | 220 | 12 GB | GDDR6 18 Gbps | 192-bit |
| AMD Radeon RX 9060 XT 16GB | RX 9000 | RDNA 4 | 2530 | 3130 | 160 | 16 GB | GDDR6 20 Gbps | 128-bit |
| AMD Radeon RX 9060 XT 8GB | RX 9000 | RDNA 4 | 2530 | 3130 | 150 | 8 GB | GDDR6 20 Gbps | 128-bit |
| AMD Radeon RX 9060 XT LP | RX 9000 | RDNA 4 | — | — | 140 | 16 GB | GDDR6 20 Gbps | 128-bit |
| AMD Radeon RX 9060 | RX 9000 | RDNA 4 | 2400 | 2990 | 132 | 8 GB | GDDR6 18 Gbps | 128-bit |
| AMD Radeon RX 9050 | RX 9000 | RDNA 4 | 1920 | 2600 | 92 | 8 GB | GDDR6 18 Gbps | 128-bit |
| AMD Radeon RX 9050 4GB | RX 9000 | RDNA 4 | 1920 | 2600 | 92 | 4 GB | GDDR6 18 Gbps | 64-bit |
| AMD Radeon RX 7900 XTX | RX 7000 | RDNA 3 | 2300 | 2500 | 355 | 24 GB | GDDR6 20 Gbps | 384-bit |
| AMD Radeon RX 7900 XT | RX 7000 | RDNA 3 | 2000 | 2400 | 315 | 20 GB | GDDR6 20 Gbps | 320-bit |
| AMD Radeon RX 7900 GRE | RX 7000 | RDNA 3 | 1880 | 2245 | 260 | 16 GB | GDDR6 18 Gbps | 256-bit |
| AMD Radeon RX 7800 XT | RX 7000 | RDNA 3 | 2124 | 2430 | 263 | 16 GB | GDDR6 19.5 Gbps | 256-bit |
| AMD Radeon RX 7700 XT | RX 7000 | RDNA 3 | 2171 | 2544 | 245 | 12 GB | GDDR6 18 Gbps | 192-bit |
| AMD Radeon RX 7700 | RX 7000 | RDNA 3 | — | — | 263 | 16 GB | GDDR6 19.5 Gbps | 256-bit |
| AMD Radeon RX 7600 XT | RX 7000 | RDNA 3 | 2470 | 2755 | 190 | 16 GB | GDDR6 18 Gbps | 128-bit |
| AMD Radeon RX 7600 | RX 7000 | RDNA 3 | 2250 | 2655 | 165 | 8 GB | GDDR6 18 Gbps | 128-bit |
| AMD Radeon RX 7400 | RX 7000 | RDNA 3 | — | — | 55 | 8 GB | GDDR6 10.8 Gbps | 128-bit |
| AMD Radeon RX 6950 XT | RX 6000 | RDNA 2 | 2100 | 2310 | 335 | 16 GB | GDDR6 18 Gbps | 256-bit |
| AMD Radeon RX 6900 XT | RX 6000 | RDNA 2 | 2015 | 2250 | 300 | 16 GB | GDDR6 16 Gbps | 256-bit |
| AMD Radeon RX 6800 XT | RX 6000 | RDNA 2 | 2015 | 2250 | 300 | 16 GB | GDDR6 16 Gbps | 256-bit |
| AMD Radeon RX 6800 | RX 6000 | RDNA 2 | 1815 | 2105 | 250 | 16 GB | GDDR6 16 Gbps | 256-bit |
| AMD Radeon RX 6750 XT | RX 6000 | RDNA 2 | 2495 | 2600 | 250 | 12 GB | GDDR6 18 Gbps | 192-bit |
| AMD Radeon RX 6750 GRE 12GB | RX 6000 | RDNA 2 | 2439 | 2581 | 250 | 12 GB | GDDR6 16 Gbps | 192-bit |
| AMD Radeon RX 6750 GRE 10GB | RX 6000 | RDNA 2 | 2189 | 2450 | 170 | 10 GB | GDDR6 16 Gbps | 160-bit |
| AMD Radeon RX 6700 XT | RX 6000 | RDNA 2 | 2424 | 2581 | 230 | 12 GB | GDDR6 16 Gbps | 192-bit |
| AMD Radeon RX 6700 | RX 6000 | RDNA 2 | 2174 | 2450 | 175 | 10 GB | GDDR6 16 Gbps | 160-bit |
| AMD Radeon RX 6650 XT | RX 6000 | RDNA 2 | 2410 | 2635 | 180 | 8 GB | GDDR6 17.5 Gbps | 128-bit |
| AMD Radeon RX 6600 XT | RX 6000 | RDNA 2 | 2359 | 2589 | 160 | 8 GB | GDDR6 16 Gbps | 128-bit |
| AMD Radeon RX 6600 | RX 6000 | RDNA 2 | 2044 | 2491 | 132 | 8 GB | GDDR6 14 Gbps | 128-bit |
| AMD Radeon RX 6500 XT | RX 6000 | RDNA 2 | 2610 | 2815 | 113 | 8 GB | GDDR6 18 Gbps | 64-bit |
| AMD Radeon RX 6400 | RX 6000 | RDNA 2 | 2039 | 2321 | 53 | 4 GB | GDDR6 16 Gbps | 64-bit |
| AMD Radeon RX 5700 XT 50th Anniversary | RX 5000 | RDNA 1 | 1830 | 1980 | 235 | 8 GB | GDDR6 14 Gbps | 256-bit |
| AMD Radeon RX 5700 XT | RX 5000 | RDNA 1 | 1755 | 1905 | 225 | 8 GB | GDDR6 14 Gbps | 256-bit |
| AMD Radeon RX 5700 | RX 5000 | RDNA 1 | 1625 | 1725 | 180 | 8 GB | GDDR6 14 Gbps | 256-bit |
| AMD Radeon RX 5600 XT | RX 5000 | RDNA 1 | 1375 | 1560 | 150 | 6 GB | GDDR6 14 Gbps | 192-bit |
| AMD Radeon RX 5500 XT | RX 5000 | RDNA 1 | 1717 | 1845 | 130 | 8 GB | GDDR6 14 Gbps | 128-bit |
| AMD Radeon RX 5500 | RX 5000 | RDNA 1 | 1670 | 1845 | 150 | 4 GB | GDDR6 14 Gbps | 128-bit |
| AMD Radeon RX 5300 | RX 5000 | RDNA 1 | 1448 | 1645 | 100 | 3 GB | GDDR6 14 Gbps | 96-bit |
| AMD Radeon VII | Radeon VII | GCN 5.1 | 1400 | 1750 | 300 | 16 GB | HBM2 2 Gbps | 4096-bit |
| AMD Radeon RX Vega 64 | RX Vega | GCN 5.0 | 1247 | 1546 | 295 | 8 GB | HBM2 1.89 Gbps | 2048-bit |
| AMD Radeon RX Vega 56 | RX Vega | GCN 5.0 | 1156 | 1471 | 210 | 8 GB | HBM2 1.6 Gbps | 2048-bit |
| AMD Radeon RX 590 | RX 500 | GCN 4 | 1469 | 1545 | 225 | 8 GB | GDDR5 8 Gbps | 256-bit |
| AMD Radeon RX 580 | RX 500 | GCN 4 | 1257 | 1340 | 185 | 8 GB | GDDR5 8 Gbps | 256-bit |
| AMD Radeon RX 570 | RX 500 | GCN 4 | 1168 | 1244 | 150 | 8 GB | GDDR5 7 Gbps | 256-bit |
| AMD Radeon RX 560 | RX 500 | GCN 4 | 1175 | 1275 | 80 | 4 GB | GDDR5 7 Gbps | 128-bit |
| AMD Radeon RX 550 | RX 500 | GCN 4 | — | 1183 | 50 | 4 GB | GDDR5 7 Gbps | 128-bit |
| AMD Radeon RX 480 | RX 400 | GCN 4 | 1120 | 1266 | 150 | 8 GB | GDDR5 7 Gbps | 256-bit |
| AMD Radeon RX 470 | RX 400 | GCN 4 | 926 | 1206 | 120 | 4 GB | GDDR5 6.6 Gbps | 256-bit |
| AMD Radeon RX 460 | RX 400 | GCN 4 | 1090 | 1200 | 70 | 4 GB | GDDR5 7 Gbps | 128-bit |

## Source provenance

Primary modern-card source:

- AMD Graphics Specifications: https://www.amd.com/en/products/specifications/graphics.html
- AMD Radeon desktop graphics model/specification pages: https://www.amd.com/en/products/graphics/desktops/radeon.html

Additional AMD product/support pages were used for older generations where the current master table is less convenient, including RX 5000, RX 500/400, and Vega product pages. The database intentionally stores only a compact set of factual reference fields rather than copying marketing text.

AMDGPU tuning/thermal behavior is based on Linux kernel documentation:

- AMDGPU thermal/hwmon controls: https://docs.kernel.org/gpu/amdgpu/thermal.html
- AMDGPU overdrive (`pp_od_clk_voltage`) documentation: https://www.kernel.org/doc/html/v5.15/gpu/amdgpu.html

RDNA4 SCLK-offset compatibility behavior was cross-checked against the open-source `amdgpu-clocks` project, which documents both the current offset syntax and the pre-Linux-6.14 legacy representation:

- https://github.com/sibradzic/amdgpu-clocks

## Deliberate limitations

- This is a reference-card database, not an AIB/partner-SKU database.
- Laptop/mobile Radeon parts are not currently included.
- Professional Radeon Pro/Instinct parts are not currently included.
- Unknown future GPUs still work for diagnostics and can still expose tuning controls because runtime driver limits are independent of this database.
- A database entry never makes an otherwise-unavailable kernel control writable.

## Updating the database

Add new verified entries to `src/gpu_specs.c`. Prefer AMD-published specifications. Where several capacities share a marketing name, create separate entries and let VRAM-aware matching choose the closest variant.
