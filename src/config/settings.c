/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "settings.h"
#include "../core/util.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#define MAX_PATH_LEN AMD_CONFIG_PATH_CAP

/* Configuration is intentionally simple, version tolerant and uses no shell execution. */
static const char *settings_user_home(void) {
#ifdef AMD_GPU_DIAG_TEST
    const char *override=getenv("AMD_GPU_DIAG_TEST_HOME");
    if (override && *override) return override;
#endif
    const char *su=getenv("SUDO_UID");
    if (geteuid()==0 && su && *su) {
        char *end=NULL; errno=0;
        unsigned long n=strtoul(su,&end,10);
        if (!errno && end && !*end && n<=UINT32_MAX) {
            struct passwd *pw=getpwuid((uid_t)n);
            if (pw && pw->pw_dir) return pw->pw_dir;
        }
    }
    struct passwd *pw=getpwuid(getuid());
    return pw && pw->pw_dir ? pw->pw_dir : getenv("HOME");
}
bool settings_paths(char *folder, size_t fc, char *file, size_t pc) {
    const char *home=settings_user_home();
    if (!home || !*home || snprintf(folder,fc,"%s/.config/amd-gpu-diagnostic",home)>=(int)fc) return false;
    return snprintf(file,pc,"%s/settings.ini",folder)<(int)pc;
}
/* Reject malformed, negative, overflowed and fractional enum config values.
 * atoi() silently turns a corrupted config line into option zero. */
bool settings_int_in_range(const char *value, int min, int max, int *out) {
    if (!value || !*value || isspace((unsigned char)*value)) return false;
    errno = 0;
    char *end = NULL;
    long v = strtol(value, &end, 10);
    if (errno || end == value || *end || v < min || v > max) return false;
    *out = (int)v;
    return true;
}

bool config_settings_load(AppSettings *settings) {
    char folder[MAX_PATH_LEN],file[MAX_PATH_LEN];
    if (!settings_paths(folder,sizeof(folder),file,sizeof(file))) return false;
    FILE *f=fopen(file,"r");
    if (!f) return false;
    char line[1024];
    while(fgets(line,sizeof(line),f)) {
        trim(line);
        char *eq=strchr(line,'='); if(!eq) continue;
        *eq++=0;
        if(!strcmp(line,"token_source")) {
            int n; if(settings_int_in_range(eq,0,TOKEN_SOURCE_COUNT-1,&n)) settings->token_source=(TokenSource)n;
        } else if(!strcmp(line,"log_parser")) {
            int n; if(settings_int_in_range(eq,0,LOG_COUNT-1,&n)) settings->log_parser=(LogParser)n;
        } else if(!strcmp(line,"compositor_source")) {
            int n; if(settings_int_in_range(eq,0,COMP_COUNT-1,&n)) settings->compositor_source=(CompositorSource)n;
        } else if(!strcmp(line,"ai_log_manual")) settings->ai_log_manual=!strcmp(eq,"1");
        else if(!strcmp(line,"ai_log_file") && eq[0]=='/' && !strchr(eq,'\n') &&
                strlen(eq)<sizeof(settings->ai_log_file)) text_copy(settings->ai_log_file,sizeof(settings->ai_log_file),eq);
        else if(!strcmp(line,"ai_nvme_device") && strlen(eq)<sizeof(settings->ai_nvme_device)) {
            /* Store only a kernel-provided device basename, never an arbitrary path. */
            bool safe = !strncmp(eq,"nvme",4);
            for(const unsigned char *q=(const unsigned char*)eq; *q; ++q)
                if(!isalnum(*q)) safe=false;
            if(safe) text_copy(settings->ai_nvme_device,sizeof(settings->ai_nvme_device),eq);
        }
        else if(!strcmp(line,"preferred_card")) {
            int n; if(settings_int_in_range(eq,-1,255,&n)) settings->preferred_card=n;
        }
    }
    fclose(f);
    return true;
}
bool config_settings_save(const AppSettings *settings) {
    char folder[MAX_PATH_LEN],file[MAX_PATH_LEN];
    if (!settings_paths(folder,sizeof(folder),file,sizeof(file))) return false;
    /* A root-run diagnostic must never leave root-owned configuration files in HOME.
     * Fork, drop privileges first, then create/write atomically as the desktop user. */
    pid_t pid=fork();
    if(pid<0) return false;
    if(pid==0) {
        const char *su=getenv("SUDO_UID"),*sg=getenv("SUDO_GID");
        if(geteuid()==0 && su && sg) {
            char *e1=NULL,*e2=NULL;
            unsigned long uid=strtoul(su,&e1,10),gid=strtoul(sg,&e2,10);
            if(!e1||*e1||!e2||*e2||uid>UINT32_MAX||gid>UINT32_MAX ||
               setgroups(0,NULL)!=0 || setgid((gid_t)gid)!=0 || setuid((uid_t)uid)!=0) _exit(2);
        }
        char config[MAX_PATH_LEN];
        const char *home=settings_user_home();
        /* After privilege drop, resolve the user's own HOME again. */
        if(!home || snprintf(config,sizeof(config),"%s/.config",home)>=(int)sizeof(config)) _exit(2);
        if(mkdir(config,0700)!=0 && errno!=EEXIST) _exit(2);
        if(mkdir(folder,0700)!=0 && errno!=EEXIST) _exit(2);
        char tmp[MAX_PATH_LEN];
        if(snprintf(tmp,sizeof(tmp),"%s/.settings-XXXXXX",folder)>=(int)sizeof(tmp)) _exit(2);
        int fd=mkstemp(tmp);
        if(fd<0) _exit(2);
        (void)fchmod(fd,0600);
        FILE *f=fdopen(fd,"w");
        if(!f) {close(fd);unlink(tmp);_exit(2);}
        fprintf(f,"# amd-gpu-diagnostic v0.7 configuration\n");
        fprintf(f,"token_source=%d\nlog_parser=%d\ncompositor_source=%d\n",(int)settings->token_source,
                (int)settings->log_parser,(int)settings->compositor_source);
        fprintf(f,"ai_log_manual=%d\nai_log_file=%s\npreferred_card=%d\nai_nvme_device=%s\n",settings->ai_log_manual?1:0,
                settings->ai_log_file,settings->preferred_card,settings->ai_nvme_device);
        int ok=ferror(f)==0 && fflush(f)==0 && fsync(fd)==0;
        if(fclose(f)!=0) ok=0;
        if(ok) ok=rename(tmp,file)==0;
        if(!ok) unlink(tmp);
        _exit(ok?0:2);
    }
    int status=0;
    return waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0;
}
