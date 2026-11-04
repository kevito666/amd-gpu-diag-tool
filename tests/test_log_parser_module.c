/* Parser tests with no GPU, libcurl, or X11 linkage. */
#include "../src/ai/log_parser.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char log[] =
        "[2026-10-21 10:12:00][DEBUG] slot print_timing: id 0 | task 10 | prompt eval time = 1000 ms / 200 tokens (5 ms per token, 200 tokens per second)\n"
        "[2026-10-21 10:12:01][DEBUG] slot print_timing: id 1 | task 11 | eval time = 500 ms / 20 tokens\n"
        "[2026-10-21 10:12:02][DEBUG] slot print_timing: id 0 | task 10 | eval time = 1000 ms / 50 tokens\n";
    AiLogEvent e[AI_LOG_MAX_EVENTS]={{0}};
    int n=ai_log_parse_lines(log,e,AI_LOG_MAX_EVENTS);
    assert(n==2 && e[0].task_id==11 && e[1].task_id==10);
    assert(fabs(e[1].total_ms-2000)<.01 && fabs(e[1].total_tokens-250)<.01);
    assert(e[1].has_prompt && e[1].has_eval);
    AiPromptCache c;
    ai_cache_parse_lines("[INFO] srv: cache state: 2 prompts, 2048.000 MiB (limits: 24576.000 MiB)\n",&c);
    assert(c.usage_valid && c.limit_valid && c.entries==2 && c.used_mib==2048 && c.limit_mib==24576);
    double limit=0;
    assert(ai_cache_parse_limit("-1",&limit) && limit==-1);
    assert(!ai_cache_parse_limit("NaN",&limit));
    puts("Standalone AI log parser module PASS");
    return 0;
}
