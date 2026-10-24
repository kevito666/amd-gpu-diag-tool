/* Project Maya engine-log parsing and NVMe counter interpretation;
 * hardware-free. These are log samples, NOT fabricated runtime metrics. */
#include <assert.h>
#include <math.h>

int diagnostic_program_main(int argc, char **argv);
#define AMD_GPU_DIAG_TEST 1
#define main diagnostic_program_main
#include "../src/amd-gpu-diagnostic.c"
#undef main

int main(void) {
    assert(nvme_namespace_name("nvme0n1"));
    assert(nvme_namespace_name("nvme12n3"));
    assert(!nvme_namespace_name("nvme0n1p1"));
    assert(!nvme_namespace_name("sda"));
    assert(!nvme_namespace_name("nvme0"));
    assert(!nvme_namespace_name("nvme0n1/../"));
    uint64_t rd=0, wr=0, busy=0;
    /* /sys/block/<device>/stat fields 3, 7, 10 */
    assert(nvme_parse_stat("10 0 20480 30 7 0 10240 20 0 100 130 0 0 0", &rd,&wr,&busy));
    assert(rd==20480 && wr==10240 && busy==100);
    assert(!nvme_parse_stat("truncated",&rd,&wr,&busy));
    NvmeMonitor nv={0};
    nvme_select(&nv,"nvme0n1");
    nv.have_prev=true;nv.valid=true;
    nv.peak_read_mib_s=100.0;
    nvme_select(&nv,"nvme1n1");
    assert(!nv.have_prev && !nv.valid && nv.peak_read_mib_s==0.0);

    MayaLogMonitor m={0};
    const char *log=
       "glm fast: CUDA0 VRAM before the expert pool: 7.56 of 15.89 GB in use\n"
       "glm prefill: CUDA0 prestage buffer 160 -> 124 experts\n"
       "glm prefill: 6500 tokens in 4802 ms (0.74 ms/token, 1353 tok/s)\n"
       "glm stat: decode 132.86 ms/tok (7.53 tok/s) | vram hit 53.94% | "
       "ram fetches 86.66/tok | disk 35.91 reads/tok in 26.84 waits/tok, 87.69 ms/tok | "
       "promotions 60.25/tok\n"
       "glm split CUDA0 ...\n";
    maya_parse_lines(log,&m);
    assert(m.recognized==5 && m.count==3);
    assert(m.prefill_valid && m.prompt_tokens==6500);
    assert(fabs(m.prefill_tps-6500.0*1000/4802.0)<0.01);
    assert(m.decode_valid && fabs(m.decode_tps-7.53)<0.001);
    assert(m.disk_valid && fabs(m.disk_reads_per_token-35.91)<0.001);
    assert(m.vram_hit_valid && fabs(m.vram_hit_pct-53.94)<0.001);
    assert(m.ram_fetch_valid && fabs(m.ram_fetches_per_token-86.66)<0.001);
    assert(strstr(m.recent[0],"glm split")!=NULL);

    maya_parse_lines("glm fast: CPU lane: 8 threads, an expert 0.273 ms\n",&m);
    assert(!m.prefill_valid && !m.decode_valid && !m.disk_valid);
    maya_parse_lines("random unrelated line\n",&m);
    assert(m.count==0);
    puts("Project Maya/NVMe parser regression: PASS");
    return 0;
}
