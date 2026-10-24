/* Read/write roundtrip regression for invoking-user configuration. */
int diagnostic_program_main(int argc, char **argv);
#define AMD_GPU_DIAG_TEST 1
#define main diagnostic_program_main
#include "../src/amd-gpu-diagnostic.c"
#undef main

int main(void) {
    char home[]="/tmp/amd-gpu-config-test-XXXXXX";
    if(!mkdtemp(home) || setenv("AMD_GPU_DIAG_TEST_HOME",home,1)) return 1;
    g_settings.token_source=TOKEN_SOURCE_MAYA;
    g_settings.log_parser=LOG_MAYA;
    g_settings.compositor_source=COMP_PROCESS;
    g_settings.ai_log_manual=true;
    text_copy(g_settings.ai_log_file,sizeof(g_settings.ai_log_file),"/tmp/bionic-log-2026.log");
    g_settings.preferred_card=3;
    text_copy(g_settings.ai_nvme_device,sizeof(g_settings.ai_nvme_device),"nvme2n1");
    if(!settings_save()) {fprintf(stderr,"config save failed\n");return 2;}
    g_settings.token_source=TOKEN_SOURCE_HEURISTIC;
    g_settings.log_parser=LOG_AUTO;
    g_settings.compositor_source=COMP_OFF;
    g_settings.ai_log_manual=false;
    g_settings.ai_log_file[0]=0;
    g_settings.preferred_card=-1;
    g_settings.ai_nvme_device[0]=0;
    settings_load();
    if(g_settings.token_source!=TOKEN_SOURCE_MAYA || g_token_source!=TOKEN_SOURCE_MAYA ||
       g_settings.log_parser!=LOG_MAYA || g_settings.compositor_source!=COMP_PROCESS ||
       !g_settings.ai_log_manual || strcmp(g_settings.ai_log_file,"/tmp/bionic-log-2026.log") ||
       g_settings.preferred_card!=3 || strcmp(g_settings.ai_nvme_device,"nvme2n1")) {fprintf(stderr,"config roundtrip mismatch\n");return 3;}
    char folder[MAX_PATH_LEN],file[MAX_PATH_LEN];
    if(!settings_paths(folder,sizeof(folder),file,sizeof(file))) return 4;
    struct stat st;
    if(stat(file,&st)!=0 || (st.st_mode & 077)!=0) {fprintf(stderr,"config permissions wrong\n");return 5;}
    if(unlink(file)||rmdir(folder)) return 6;
    char conf[MAX_PATH_LEN];snprintf(conf,sizeof(conf),"%s/.config",home);
    if(rmdir(conf)||rmdir(home)) return 7;
    puts("Settings PASS: persistent Maya/parser/log/NVMe/card, 0600 user-owned atomic file");
    return 0;
}
