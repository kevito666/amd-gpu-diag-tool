/* SPDX-License-Identifier: GPL-3.0-or-later
 * True standalone tests: linked with extracted modules, not the old monolith.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../src/core/util.h"
#include "../src/monitor/nvme.h"
#include "../src/ai/maya_log.h"

int main(void) {
    char b[24];
    text_copy(b, sizeof(b), "  value  ");
    trim(b);
    assert(strcmp(b, "value") == 0);
    assert(path_join(b, sizeof(b), "/tmp", "data") && strcmp(b, "/tmp/data") == 0);
    assert(!path_join(b, 4, "/long", "name") && errno == ENAMETOOLONG);
    struct timespec t = ns_ts(1250000000LL);
    assert(ts_ns(t) == 1250000000LL);
    add_ns(&t, 250000000LL);
    assert(diff_ns(t, ns_ts(1000000000LL)) == 500000000LL);
    assert(nvme_namespace_name("nvme12n3") && !nvme_namespace_name("nvme12n3p1"));
    uint64_t r=0,w=0,busy=0;
    assert(nvme_parse_stat("10 0 20480 30 7 0 10240 20 0 100 130", &r, &w, &busy));
    assert(r == 20480 && w == 10240 && busy == 100);
    NvmeMonitor nv = {0};
    nvme_select(&nv, "nvme0n1");
    nv.have_prev = true;
    nvme_select(&nv, "nvme1n1");
    assert(!nv.have_prev && !nv.valid);
    MayaLogMonitor m;
    maya_parse_lines("glm prefill: 1000 tokens in 500 ms\n"
                     "glm stat: decode 25 ms/tok (40 tok/s) | vram hit 75%\n", &m);
    assert(m.prefill_valid && fabs(m.prefill_tps - 2000.0) < 0.001);
    assert(m.decode_valid && fabs(m.decode_tps - 40.0) < 0.001);
    assert(m.vram_hit_valid && fabs(m.vram_hit_pct - 75.0) < 0.001);
    puts("Modular core/NVMe/Maya API tests: PASS");
    return 0;
}
