/* Isolated configuration serialization roundtrip, no app/X11 linkage. */
#define _GNU_SOURCE
#include "../src/config/settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    char temp[] = "/tmp/amd-v080-settings-XXXXXX";
    assert(mkdtemp(temp));
    assert(setenv("AMD_GPU_DIAG_TEST_HOME",temp,1)==0);
    AppSettings s = {.token_source=TOKEN_SOURCE_MAYA, .log_parser=LOG_MAYA,
        .compositor_source=COMP_DRM, .ai_log_manual=true, .preferred_card=5};
    snprintf(s.ai_log_file,sizeof(s.ai_log_file),"/tmp/maya test.log");
    snprintf(s.ai_nvme_device,sizeof(s.ai_nvme_device),"nvme3n1");
    assert(config_settings_save(&s));
    AppSettings r = {.token_source=TOKEN_SOURCE_HEURISTIC, .preferred_card=-1};
    assert(config_settings_load(&r));
    assert(r.token_source==s.token_source && r.log_parser==s.log_parser);
    assert(r.compositor_source==COMP_DRM && r.preferred_card==5 && r.ai_log_manual);
    assert(strcmp(r.ai_log_file,s.ai_log_file)==0 && strcmp(r.ai_nvme_device,"nvme3n1")==0);
    char dir[AMD_CONFIG_PATH_CAP],filename[AMD_CONFIG_PATH_CAP];
    assert(settings_paths(dir,sizeof(dir),filename,sizeof(filename)));
    struct stat st;
    assert(stat(filename,&st)==0 && (st.st_mode & 077)==0);
    /* Malformed enum/device values cannot corrupt valid loaded settings. */
    FILE *f=fopen(filename,"a"); assert(f);
    fputs("token_source=abc\nai_nvme_device=../../foo\npreferred_card=99999999999\n",f);
    fclose(f);
    assert(config_settings_load(&r));
    assert(r.token_source==TOKEN_SOURCE_MAYA && !strcmp(r.ai_nvme_device,"nvme3n1") && r.preferred_card==5);
    assert(unlink(filename)==0 && rmdir(dir)==0);
    char conf[AMD_CONFIG_PATH_CAP];snprintf(conf,sizeof(conf),"%s/.config",temp);
    assert(rmdir(conf)==0 && rmdir(temp)==0);
    puts("Standalone settings module PASS");
    return 0;
}
