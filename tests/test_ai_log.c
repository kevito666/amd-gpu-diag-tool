/* Offline parser regression suite. Never opens GPU sysfs nor writes GPU settings. */
int diagnostic_program_main(int argc, char **argv);
#define main diagnostic_program_main
#include "../src/amd-gpu-diagnostic.c"
#undef main
#include <assert.h>

int main(void) {
    char sample[] =
        "[2026-10-21 21:48:19][DEBUG] slot print_timing: id  0 | task 830 | prompt eval time = 1932.28 ms / 416 tokens (4.64 ms per token, 215.29 tokens per second)\n"
        "[2026-10-21 21:48:20][DEBUG] slot print_timing: id  1 | task 831 | prompt eval time = 1000.00 ms / 300 tokens (3.33 ms per token, 300.00 tokens per second)\n"
        "[2026-10-21 21:48:25][DEBUG] slot print_timing: id  0 | task 830 | eval time = 3000.00 ms / 150 tokens (20.00 ms per token, 50.00 tokens per second)\n"
        "[2026-10-21 21:48:26][DEBUG] slot print_timing: id  0 | task 830 | total time = 5000.00 ms / 566 tokens\n"
        "[2026-10-21 21:48:27][DEBUG] slot print_timing: id  1 | task 831 | eval time = 2000.00 ms / 100 tokens (20.00 ms per token, 50.00 tokens per second)\n";
    AiLogEvent e[AI_LOG_MAX_EVENTS] = {{0}};
    int n = ai_log_parse_lines(sample, e, AI_LOG_MAX_EVENTS);
    assert(n == 2);
    assert(e[0].task_id == 830 && e[0].slot_id == 0);
    assert(e[0].has_prompt && e[0].has_eval && e[0].has_total);
    assert(e[0].prompt_tokens == 416 && e[0].eval_tokens == 150);
    assert(fabs(e[0].prompt_tps - 215.29) < 0.01);
    assert(e[0].total_ms == 5000);
    assert(e[1].task_id == 831 && e[1].eval_tokens == 100);
    assert(e[1].total_ms == 3000);
    char many[24000] = "";
    for (int i=0;i<30;i++) {
        char line[256];
        snprintf(line,sizeof(line),"[2026-10-21 21:48:19][DEBUG] slot print_timing: id 0 | task %d | prompt eval time = 200.00 ms / 100 tokens (2 ms per token, 500 tokens per second)\n",i);
        strcat(many,line);
    }
    memset(e,0,sizeof(e));
    n=ai_log_parse_lines(many,e,AI_LOG_MAX_EVENTS);
    assert(n == AI_LOG_MAX_EVENTS);
    assert(e[0].task_id == 18 && e[n-1].task_id == 29);
    assert(e[n-1].prompt_tokens == 100);
    /* Realistic Bionic cache lines: MiB means host RAM, not VRAM. */
    const char *cache_log =
        "[2026-10-21 21:48:20][TRACE] srv load_model: prompt cache is enabled, size limit: 24576 MiB\n"
        "[2026-10-21 21:48:21][TRACE] srv prompt_save: - saving prompt with length 87606, total state size = 8201.877 MiB\n"
        "[2026-10-21 21:48:21][INFO] srv load: - found better prompt with f_keep = 0.986, sim = 0.995\n"
        "[2026-10-21 21:48:22][TRACE] srv update: - cache state: 3 prompts, 12442.000 MiB (limits: 24576.000 MiB, 131072 tokens, 131072 est)\n"
        "[2026-10-21 21:48:23][WARN] srv alloc: - making room for prompt cache entry, removing oldest entry (size = 2497.528 MiB)\n"
        "[2026-10-21 21:48:24][WARN] srv alloc: - prompt state size 26000.000 MiB exceeds cache size limit 24576.000 MiB, skipping\n"
        "[2026-10-21 21:48:25][WARN] slot prompt_load: id 3 | task -1 | failed to load prompt from cache\n"
        "[2026-10-21 21:48:26][TRACE] slot update_slots: forcing full prompt re-processing due to lack of cache data\n"
        "[2026-10-21 21:48:27][INFO] srv update: - cache state: 2 prompts, 9400.250 MiB (limits: 24576.000 MiB, 131072 tokens, 131072 est)\n";
    AiPromptCache c;
    ai_cache_parse_lines(cache_log, &c);
    assert(c.limit_valid && c.usage_valid && c.limit_mib == 24576);
    assert(c.entries == 2 && fabs(c.used_mib-9400.250) < .001);
    assert(c.save_attempts == 1 && c.matches == 1 && c.evictions == 1);
    assert(c.rejected == 1 && c.load_failures == 1 && c.forced_reprocess == 1);
    assert(fabs(c.evicted_mib - 2497.528) < .001);
    assert(c.last_rejected_mib == 26000);
    assert(ai_cache_parse_limit("24576", &c.limit_mib) && c.limit_mib == 24576);
    assert(ai_cache_parse_limit("0", &c.limit_mib) && c.limit_mib == 0);
    assert(ai_cache_parse_limit("-1", &c.limit_mib) && c.limit_mib == -1);
    assert(!ai_cache_parse_limit("bad", &c.limit_mib));
    assert(!ai_cache_parse_limit("-2", &c.limit_mib));
    ai_cache_parse_lines("[info] prompt cache is enabled, size limit: 8192 MiB\n", &c);
    assert(!c.usage_valid && c.limit_valid && c.limit_mib == 8192);
    ai_cache_parse_lines("srv load_model: prompt cache is disabled - use `--cache-ram N` to enable it\n", &c);
    assert(c.limit_valid && c.limit_mib == 0 && !c.usage_valid);
    ai_cache_parse_lines("srv update: - cache state: 0 prompts, 0.000 MiB (limits: 24576.000 MiB, 0 tokens, 0 est)\n", &c);
    assert(c.usage_valid && c.entries == 0 && c.used_mib == 0);
    ai_cache_parse_lines("slot print_timing: id 0 | task 4 | eval time = 120 ms / 2 tokens\n", &c);
    assert(!c.usage_valid && !c.limit_valid && c.matches == 0);
    printf("Prompt RAM cache parser PASS: 24 GiB, occupancy, matches, evictions, rejection and unknowns\n");
    printf("AI parser PASS: interleaved tasks, phase pairing, timing values, newest 12 retained\n");
    return 0;
}
