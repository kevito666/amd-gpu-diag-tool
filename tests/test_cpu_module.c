/* CPU parser/sampler independent of application state. */
#include "../src/monitor/cpu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    uint64_t cpu=0,start=0;
    assert(parse_proc_cpu_stat("123 (name with spaces) S 0 0 0 0 0 0 0 0 0 0 9 4 0 0 0 0 0 0 700 0 0",&cpu,&start));
    assert(cpu==13 && start==700);
    assert(!parse_proc_cpu_stat("5 (bad) S 0",&cpu,&start));
    CpuCoreLoad cores={0};
    cpu_sample_core_load(&cores);
    assert(cores.core_count>=1 && cores.core_count<=MAX_CPU_CORES);
    TopProcess top[3]={{0}};int count=0;
    cpu_sample_top_processes(top,&count);
    assert(count>=0 && count<=3);
    puts("Standalone CPU /proc module PASS");
    return 0;
}
