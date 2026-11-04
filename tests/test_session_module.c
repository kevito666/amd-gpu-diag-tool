/* SPDX-License-Identifier: GPL-3.0-or-later
 * Hardware-free DRM state and desktop/session module regression tests.
 * No GPU hardware, X11 server or root privileges are required.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/monitor/session.h"

int main(void) {
    assert(desktop_numeric_name("1234"));
    assert(!desktop_numeric_name("12x"));
    assert(!desktop_numeric_name(""));
    char label[64]="";
    assert(desktop_compositor_process("picom",label,sizeof(label)));
    assert(strcmp(label,"picom") == 0);
    assert(!desktop_compositor_process("not_a_compositor",label,sizeof(label)));
    char path[]="/tmp/amd-session-state-XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    FILE *f=fdopen(fd,"w");
    assert(f);
    assert(fputs("crtc[23]:\n  vrr_enabled=1\n  mode: \"2560x1440\": 144000\n"
                 "plane[9]:\n  crtc=crtc-23\n  allocated by = amdgpu\n"
                 "connector[0]:\n", f)>=0);
    assert(fclose(f)==0);
    SessionInfo si={0};
    desktop_parse_kms_state(path,&si);
    assert(si.active_planes==1);
    assert(strstr(si.refresh,"144 Hz"));
    assert(strstr(si.vrr_state,"ENABLED"));
    assert(strstr(si.kms_modes,"2560x1440@144Hz"));
    assert(strstr(si.plane_allocators,"amdgpu"));
    assert(unlink(path)==0);
    desktop_parse_kms_state(path,&si); /* safely tolerates removed state file */
    Gpu gpu={0};
    memcpy(gpu.card_name,"card65535",10); /* nonexistent virtual DRM card */
    Sysfs sys={0};
    ClientMonitor clients={0};
    desktop_sample_session(&gpu,&sys,&si,&clients,COMP_OFF);
    assert(!strcmp(si.compositor,"Detection disabled"));
    assert(si.connected_outputs==0);
    puts("Session module PASS: KMS/VRR, compositor classification, disabled mode");
    return 0;
}
