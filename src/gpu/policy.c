/* SPDX-License-Identifier: GPL-3.0-or-later
 * GPU policy and DPM controls. The single event-loop thread continues to own
 * and invoke all operations until watchdog scheduling is isolated separately.
 */
#define _GNU_SOURCE
#include "policy.h"
#include "writes.h"
#include "../core/util.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_TEXT 4096

const char *const POLICIES[] = {
    "auto",
    "low",
    "high",
    "profile_standard",
    "profile_min_sclk",
    "profile_min_mclk",
    "profile_peak",
    "manual"
};
const size_t POLICY_COUNT = sizeof(POLICIES) / sizeof(POLICIES[0]);
int parse_dpm_levels(const char *text, DpmLevel *levels, int max) {
    int count = 0;
    const char *p = text;
    while (*p && count < max) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len >= sizeof(levels[count].raw)) len = sizeof(levels[count].raw) - 1;
        char line[128];
        memcpy(line, p, len);
        line[len] = '\0';
        trim(line);

        DpmLevel l;
        memset(&l, 0, sizeof(l));
        l.index = -1;
        l.mhz = -1;
        l.active = strchr(line, '*') != NULL;
        l.sleep_level = false;
        snprintf(l.raw, sizeof(l.raw), "%s", line);

        if (strncmp(line, "S:", 2) == 0) {
            l.sleep_level = true;
            l.index = -2;
            char *mhz = strstr(line, "Mhz");
            if (mhz) {
                char *q = mhz;
                while (q > line && isdigit((unsigned char)q[-1])) q--;
                l.mhz = strtol(q, NULL, 10);
            }
            levels[count++] = l;
        } else {
            char *end = NULL;
            long idx = strtol(line, &end, 10);
            if (end && end != line && *end == ':') {
                l.index = (int)idx;
                const char *mhz = strstr(end, "Mhz");
                if (mhz) {
                    const char *q = mhz;
                    while (q > end && isdigit((unsigned char)q[-1])) q--;
                    l.mhz = strtol(q, NULL, 10);
                }
                levels[count++] = l;
            }
        }
        if (!e) break;
        p = e + 1;
    }
    return count;
}

static int active_level_from_text(const char *text) {
    DpmLevel levels[MAX_LEVELS];
    int n = parse_dpm_levels(text, levels, MAX_LEVELS);
    for (int i = 0; i < n; i++) if (levels[i].active) return levels[i].index;
    return INT_MIN;
}

bool valid_policy(const char *p) {
    for (size_t i = 0; i < POLICY_COUNT; i++) if (strcmp(p, POLICIES[i]) == 0) return true;
    return false;
}

int apply_policy(Sysfs *s, const char *policy) {
    return gpu_write_policy(s, policy);
}

int apply_level(int fd, int level) {
    return gpu_write_level(fd, level);
}

static bool selected_level_enabled(const char *text, int level) {
    DpmLevel levels[MAX_LEVELS];
    int n = parse_dpm_levels(text, levels, MAX_LEVELS);
    /* With one manually enabled level it should normally be the active (*) level.
       If firmware does not expose an asterisk reliably, presence + active is the strongest
       generic verification available through pp_dpm_* without ASIC-specific ioctls. */
    for (int i = 0; i < n; i++) {
        if (levels[i].index == level) return levels[i].active;
    }
    return false;
}

static void clock_line_from_text(const char *text, char *out, size_t cap) {
    DpmLevel levels[MAX_LEVELS];
    int n = parse_dpm_levels(text, levels, MAX_LEVELS);
    bool first = true;
    out[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (!levels[i].active) continue;
        char item[64];
        if (levels[i].index == -2) snprintf(item, sizeof(item), "S:%ld MHz", levels[i].mhz);
        else if (levels[i].mhz >= 0) snprintf(item, sizeof(item), "L%d %ld MHz", levels[i].index, levels[i].mhz);
        else snprintf(item, sizeof(item), "L%d", levels[i].index);
        size_t used = strlen(out);
        if (!first && used + 2 < cap) { strncat(out, ", ", cap - used - 1); used = strlen(out); }
        if (used + 1 < cap) strncat(out, item, cap - used - 1);
        first = false;
    }
    if (first) snprintf(out, cap, "no active marker");
}

static int reapply_manual_locks(Sysfs *s, const Target *t, Stats *st) {
    if (strcmp(t->policy, "manual") != 0) return 0;
    if (t->lock_sclk) {
        if (apply_level(s->fd_sclk_w, t->sclk_level) == 0) st->corrections++;
        else st->write_errors++;
    }
    if (t->lock_mclk) {
        if (apply_level(s->fd_mclk_w, t->mclk_level) == 0) st->corrections++;
        else st->write_errors++;
    }
    return 0;
}

/* Fast path: only touch power_dpm_force_performance_level.
   pp_dpm_* reads are intentionally kept out of this loop because they are
   dramatically more expensive on some RDNA cards. */
int enforce_policy_once(Sysfs *s, const Target *t, Stats *st) {
    char policy[128], verify[128];

    if (read_fd_text(s->fd_policy_r, policy, sizeof(policy)) < 0) {
        st->read_errors++;
        return -1;
    }
    st->checks++;

    if (strcmp(policy, t->policy) == 0) return 0;

    st->policy_mismatches++;
    if (apply_policy(s, t->policy) < 0) {
        st->write_errors++;
        return -1;
    }
    st->corrections++;

    /* A policy reset can also discard manual DPM masks. Restore them
       immediately instead of waiting for the slow telemetry pass. */
    reapply_manual_locks(s, t, st);

    /* Immediate readback. Sysfs writes are synchronous enough that this is
       a useful "did it stick?" check without adding delay to the normal path. */
    if (read_fd_text(s->fd_policy_r, verify, sizeof(verify)) < 0) {
        st->read_errors++;
        return 1;
    }
    if (strcmp(verify, t->policy) != 0) {
        st->verify_failures++;
        return 1;
    }
    return 1;
}

/* Slow path: inspect the expensive pp_dpm_* attributes and repair manual
   clock locks. This runs independently of the fast policy watchdog rate. */
void monitor_dpm_once(Sysfs *s, const Target *t, Stats *st, bool enforce) {
    char policy[128], sclk[MAX_TEXT], mclk[MAX_TEXT];
    bool manual_ready = false;
    st->dpm_checks++;

    if (s->fd_sclk_r >= 0) {
        if (read_fd_text(s->fd_sclk_r, sclk, sizeof(sclk)) >= 0) {
            clock_line_from_text(sclk, st->sclk_display, sizeof(st->sclk_display));
            int a = active_level_from_text(sclk);
            if (st->have_last_sclk && a != INT_MIN && a != st->last_active_sclk)
                st->observed_sclk_changes++;
            if (a != INT_MIN) {
                st->last_active_sclk = a;
                st->have_last_sclk = true;
            }

            if (enforce && t->lock_sclk && !selected_level_enabled(sclk, t->sclk_level)) {
                st->sclk_mismatches++;
                if (read_fd_text(s->fd_policy_r, policy, sizeof(policy)) >= 0) {
                    manual_ready = strcmp(policy, "manual") == 0;
                } else {
                    st->read_errors++;
                }
                if (!manual_ready) {
                    if (apply_policy(s, "manual") == 0) {
                        st->corrections++;
                        manual_ready = true;
                    } else {
                        st->write_errors++;
                    }
                }
                if (manual_ready) {
                    if (apply_level(s->fd_sclk_w, t->sclk_level) == 0) st->corrections++;
                    else st->write_errors++;
                }
            }
        } else st->read_errors++;
    }

    if (s->fd_mclk_r >= 0) {
        if (read_fd_text(s->fd_mclk_r, mclk, sizeof(mclk)) >= 0) {
            clock_line_from_text(mclk, st->mclk_display, sizeof(st->mclk_display));
            int a = active_level_from_text(mclk);
            if (st->have_last_mclk && a != INT_MIN && a != st->last_active_mclk)
                st->observed_mclk_changes++;
            if (a != INT_MIN) {
                st->last_active_mclk = a;
                st->have_last_mclk = true;
            }

            if (enforce && t->lock_mclk && !selected_level_enabled(mclk, t->mclk_level)) {
                st->mclk_mismatches++;
                if (!manual_ready) {
                    if (read_fd_text(s->fd_policy_r, policy, sizeof(policy)) >= 0)
                        manual_ready = strcmp(policy, "manual") == 0;
                    else
                        st->read_errors++;
                }
                if (!manual_ready) {
                    if (apply_policy(s, "manual") == 0) {
                        st->corrections++;
                        manual_ready = true;
                    } else {
                        st->write_errors++;
                    }
                }
                if (manual_ready) {
                    if (apply_level(s->fd_mclk_w, t->mclk_level) == 0) st->corrections++;
                    else st->write_errors++;
                }
            }
        } else st->read_errors++;
    }
}



int cycle_manual_level(Sysfs *s, Target *t, bool sclk, char *message, size_t cap) {
    int rfd=sclk?s->fd_sclk_r:s->fd_mclk_r;
    int wfd=sclk?s->fd_sclk_w:s->fd_mclk_w;
    if(rfd<0||wfd<0) { snprintf(message,cap,"%s DPM levels unavailable",sclk?"GFX":"Memory"); return -1; }
    char buf[MAX_TEXT];
    if(read_fd_text(rfd,buf,sizeof(buf))<0) { snprintf(message,cap,"Cannot read %s DPM levels",sclk?"GFX":"memory"); return -1; }
    DpmLevel levels[MAX_LEVELS];
    int n=parse_dpm_levels(buf,levels,MAX_LEVELS);
    int valid[MAX_LEVELS], nv=0, active=INT_MIN;
    for(int i=0;i<n;i++) { if(levels[i].index>=0) valid[nv++]=levels[i].index; if(levels[i].active) active=levels[i].index; }
    if(nv==0) { snprintf(message,cap,"No selectable %s DPM levels",sclk?"GFX":"memory"); return -1; }
    int current=sclk?(t->lock_sclk?t->sclk_level:active):(t->lock_mclk?t->mclk_level:active);
    int pos=-1; for(int i=0;i<nv;i++) if(valid[i]==current){pos=i;break;}
    int wanted=valid[(pos+1+nv)%nv];
    /* Only commit the target after both driver writes succeed. A rejected
     * level must not leave the watchdog enforcing a phantom manual target. */
    if (apply_policy(s,"manual") < 0) {
        snprintf(message,cap,"Cannot enter manual mode for %s: %s",sclk?"GFX":"memory",strerror(errno));
        return -1;
    }
    if (apply_level(wfd,wanted) < 0) {
        int saved = errno;
        (void)apply_policy(s,t->policy); /* best-effort driver rollback */
        snprintf(message,cap,"Driver rejected manual %s level %d: %s",sclk?"GFX":"memory",wanted,strerror(saved));
        return -1;
    }
    snprintf(t->policy,sizeof(t->policy),"manual");
    if(sclk){t->lock_sclk=true;t->sclk_level=wanted;}else{t->lock_mclk=true;t->mclk_level=wanted;}
    snprintf(message,cap,"Manual %s level -> %d",sclk?"GFX":"memory",wanted);
    return 0;
}


void set_mode(Sysfs *s, Target *t, int idx, char *message, size_t cap) {
    if(idx<0||idx>=(int)POLICY_COUNT)return;
    const char *requested = POLICIES[idx];
    if (apply_policy(s,requested) == 0) {
        /* Desired policy now matches the driver-accepted write. */
        text_copy(t->policy,sizeof(t->policy),requested);
        if(strcmp(requested,"manual")!=0){t->lock_sclk=false;t->lock_mclk=false;}
        snprintf(message,cap,"Target changed to %s",t->policy);
    } else {
        snprintf(message,cap,"Failed to apply %s: %s",requested,strerror(errno));
    }
}
