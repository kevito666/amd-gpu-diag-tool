/* SPDX-License-Identifier: GPL-3.0-or-later
 * Parses explicit glm/ds4 timing records; does not invent unavailable metrics.
 */
#define _GNU_SOURCE
#include "maya_log.h"
#include "../core/util.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void maya_parse_lines(const char *log, MayaLogMonitor *out) {
    memset(out,0,sizeof(*out));
    if(!log) return;
    for(const char *p=log;*p;) {
        const char *end=strchr(p,'\n');
        size_t len=end?(size_t)(end-p):strlen(p);
        if(len>0 && len<1024) {
            char line[1024];memcpy(line,p,len);line[len]=0;
            char *mark=NULL;
            const char *prefixes[]={"glm prefill:","glm fast:","glm stat:","glm split", "glm spec:",
                                    "ds4 prefill:","ds4 fast:","ds4 stat:","ds4 split"};
            for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);++i) {
                mark=strstr(line,prefixes[i]); if(mark) break;
            }
            if(mark) {
                out->recognized++;
                if(out->count<3) out->count++;
                for(int j=out->count-1;j>0;--j) text_copy(out->recent[j],sizeof(out->recent[j]),out->recent[j-1]);
                text_copy(out->recent[0],sizeof(out->recent[0]),mark);
                if(strstr(mark,"glm stat:") || strstr(mark,"ds4 stat:")) {
                    const char *dec=strstr(mark,"decode ");
                    double mspt=0,tps=0;
                    if(dec && sscanf(dec,"decode %lf ms/tok (%lf tok/s)",&mspt,&tps)==2 &&
                       isfinite(mspt) && isfinite(tps) && mspt>0 && tps>0) {
                        out->decode_valid=true;out->decode_ms_per_tok=mspt;out->decode_tps=tps;
                    }
                    const char *hit=strstr(mark,"vram hit ");
                    if(hit && sscanf(hit,"vram hit %lf%%",&out->vram_hit_pct)==1 &&
                       isfinite(out->vram_hit_pct) && out->vram_hit_pct>=0 && out->vram_hit_pct<=100)
                        out->vram_hit_valid=true;
                    const char *ram=strstr(mark,"ram fetches ");
                    if(ram && sscanf(ram,"ram fetches %lf/tok",&out->ram_fetches_per_token)==1 &&
                       isfinite(out->ram_fetches_per_token) && out->ram_fetches_per_token>=0)
                        out->ram_fetch_valid=true;
                    const char *disk=strstr(mark,"disk ");
                    double reads=0,waits=0,ms=0;
                    if(disk && sscanf(disk,"disk %lf reads/tok in %lf waits/tok, %lf ms/tok",
                                     &reads,&waits,&ms)==3 &&
                       isfinite(reads) && isfinite(ms) && reads>=0 && ms>=0) {
                        out->disk_valid=true;out->disk_reads_per_token=reads;out->disk_wait_ms_per_token=ms;
                    }
                }
                if(strstr(mark,"prefill:")) {
                    long tokens=0;double ms=0;
                    const char *v=strchr(mark,':');
                    if(v && sscanf(v+1," %ld tokens in %lf ms",&tokens,&ms)==2 && tokens>0 && ms>0) {
                        out->prefill_valid=true;out->prefill_tps=tokens*1000.0/ms;out->prompt_tokens=tokens;
                    }
                }
                /* Decoder speed records are not guaranteed to share a format
                 * across Maya models; display the original line unaltered. */
            }
        }
        if(!end) break;
        p=end+1;
    }
}
