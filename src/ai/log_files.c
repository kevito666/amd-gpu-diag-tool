/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "log_files.h"
#include "../core/util.h"
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define MAX_PATH_LEN 512
/* Select by modification time, not alphabetical filename, and ignore directories.
 * Bionic uses nested month directories; the latest file can be in any month. */
static void ai_log_consider(char *best, size_t cap, time_t *best_mtime, const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode) &&
        (!best[0] || st.st_mtime > *best_mtime ||
         (st.st_mtime == *best_mtime && strcmp(path, best) > 0))) {
        text_copy(best, cap, path);
        *best_mtime = st.st_mtime;
    }
}

/* NVMe namespaces only (nvme0n1 etc.); avoid partition counters and
 * double-counting by never summing namespace and partition traffic. */

/* Dedicated Maya parser: no attempt to pass its records through the task/slot
 * timing grammar used by llama.cpp. Each log scan is a bounded tail. */

void ai_log_discover(const AppSettings *settings, char *found_path, size_t path_cap, char *source, size_t source_cap) {
    if (!settings || !found_path || !source || path_cap < 2 || source_cap < 2) return;
    if (settings->ai_log_manual) {
        text_copy(found_path,path_cap,settings->ai_log_file);
        text_copy(source,source_cap,"Manual");
        return;
    }
    const char *home = getenv("HOME");
    const char *sudo_user = getenv("SUDO_USER");
    if (sudo_user) {
        struct passwd *pw = getpwnam(sudo_user);
        if (pw && pw->pw_dir) home = pw->pw_dir;
    }
    found_path[0] = '\0';
    source[0] = '\0';
    if (!home) return;
    char root[MAX_PATH_LEN], month[MAX_PATH_LEN], candidate[MAX_PATH_LEN];
    char best[MAX_PATH_LEN] = "";
    time_t best_mtime = 0;
    if (snprintf(root, sizeof(root), "%s/.lmstudio/apps/bionic/server-logs", home) < (int)sizeof(root)) {
        DIR *months = opendir(root);
        if (months) {
            struct dirent *m;
            while ((m = readdir(months))) {
                if (m->d_name[0] == '.' || !path_join(month, sizeof(month), root, m->d_name)) continue;
                DIR *logs = opendir(month);
                if (!logs) continue;
                struct dirent *f;
                while ((f = readdir(logs))) {
                    if (f->d_name[0] == '.' || !path_join(candidate, sizeof(candidate), month, f->d_name)) continue;
                    ai_log_consider(best, sizeof(best), &best_mtime, candidate);
                }
                closedir(logs);
            }
            closedir(months);
        }
    }
    if (settings->log_parser == LOG_MAYA || (settings->log_parser==LOG_AUTO && !best[0])) {
        const char *roots[]={"src/project-maya","project-maya","Downloads/project-maya"};
        char maya_best[MAX_PATH_LEN]=""; time_t maya_mtime=0;
        for(size_t i=0;i<sizeof(roots)/sizeof(roots[0]);++i) {
            if(snprintf(root,sizeof(root),"%s/%s",home,roots[i])>=(int)sizeof(root)) continue;
            DIR *d=opendir(root);
            if(!d) continue;
            struct dirent *e;
            while((e=readdir(d))) {
                if(strncmp(e->d_name,"maya-",5)) continue;
                size_t l=strlen(e->d_name);
                if(l<9 || strcmp(e->d_name+l-4,".log")) continue;
                if(path_join(candidate,sizeof(candidate),root,e->d_name))
                    ai_log_consider(maya_best,sizeof(maya_best),&maya_mtime,candidate);
            }
            closedir(d);
        }
        if(maya_best[0]) {
            text_copy(found_path,path_cap,maya_best);
            text_copy(source,source_cap,"Project Maya");
            return;
        }
        if(settings->log_parser==LOG_MAYA) return;
    }
    if (best[0]) {
        text_copy(found_path, path_cap, best);
        text_copy(source, source_cap, "Bionic");
        return;
    }
    if (snprintf(candidate, sizeof(candidate), "%s/.ollama/logs/server.log", home) < (int)sizeof(candidate) &&
        access(candidate, R_OK) == 0) {
        text_copy(found_path, path_cap, candidate);
        text_copy(source, source_cap, "Ollama");
    } else if (snprintf(candidate, sizeof(candidate), "%s/.cache/vllm/log.txt", home) < (int)sizeof(candidate) &&
               access(candidate, R_OK) == 0) {
        text_copy(found_path, path_cap, candidate);
        text_copy(source, source_cap, "vLLM");
    }
}


char *ai_log_read_tail(const char *path, time_t *mtime) {
    if (mtime) *mtime = 0;
    if (!path || !*path) return NULL;
    /* Never block indefinitely on a user-selected FIFO/device. Open with
     * O_NONBLOCK first, then validate the opened inode (not a pre-open stat,
     * which would have a path-replacement race). We only support regular logs.
     * This is important because shutdown joins the read-only sampler thread. */
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        close(fd);
        return NULL;
    }
    FILE *f = fdopen(fd, "rb");
    if (!f) { close(fd); return NULL; }
    if (mtime) *mtime = st.st_mtime;
    const long offset = st.st_size > AI_LOG_TAIL_BYTES ? (long)(st.st_size - AI_LOG_TAIL_BYTES) : 0;
    if (fseek(f, offset, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buffer = malloc(AI_LOG_TAIL_BYTES + 1);
    if (!buffer) { fclose(f); return NULL; }
    size_t len = fread(buffer, 1, AI_LOG_TAIL_BYTES, f);
    int failed = ferror(f);
    fclose(f);
    if (failed || !len) { free(buffer); return NULL; }
    buffer[len] = 0;
    if (offset) {
        /* Skip the incomplete first line. No newline means no complete record. */
        char *nl = memchr(buffer, '\n', len);
        if (!nl || nl == buffer + len - 1) { free(buffer); return NULL; }
        size_t skip = (size_t)(nl + 1 - buffer);
        memmove(buffer, buffer + skip, len + 1 - skip);
    }
    return buffer;
}
