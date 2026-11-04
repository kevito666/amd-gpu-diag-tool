/* SPDX-License-Identifier: GPL-3.0-or-later
 * Fake DRM fdinfo fixture: correct PCI selection, engine summing and VRAM.
 */
#define _GNU_SOURCE
#include "../src/monitor/clients.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/amd-diag-fdinfo-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *f = fdopen(fd, "w");
    assert(f);
    fputs("drm-driver:\tamdgpu\n"
          "drm-pdev:\t0000:03:00.0\n"
          "drm-client-id:\t18\n"
          "drm-engine-gfx:\t200000000\n"
          "drm-engine-compute:\t30000000\n"
          "drm-engine-capacity-gfx:\t100\n"
          "drm-memory-vram:\t20 MiB\n", f);
    assert(fclose(f) == 0);
    GpuClient client = {0};
    assert(client_monitor_parse_fdinfo(path, "0000:03:00.0", &client) == 1);
    assert(client.client_id == 18);
    assert(client.engine_ns == 230000000ULL);
    assert(client.gfx_ns == 200000000ULL);
    assert(client.compute_ns == 30000000ULL);
    assert(client.vram_bytes == 20ULL*1024*1024);
    assert(client_monitor_parse_fdinfo(path, "0000:04:00.0", &client) == 0);
    assert(client_monitor_parse_fdinfo("/missing/drm-fdinfo", "0000:03:00.0", &client) == -1);
    unlink(path);
    puts("DRM clients PASS: PCI discrimination, engine counters, VRAM units");
    return 0;
}
