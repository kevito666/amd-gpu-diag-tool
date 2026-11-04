/* SPDX-License-Identifier: GPL-3.0-or-later
 * Desktop/session Linux collection. UI-specific X11 properties are gathered
 * separately, avoiding network/UI access during system snapshot collection.
 */
#define _GNU_SOURCE
#include "session.h"
#include "desktop_probe.h"
#include "../core/util.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
bool desktop_numeric_name(const char *s) {
    if (!s || !*s) return 0;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

int desktop_read_proc_comm(pid_t pid, char *out, size_t cap) {
    char p[64];
    snprintf(p, sizeof(p), "/proc/%d/comm", pid);
    return read_path_text(p, out, cap);
}

bool desktop_compositor_process(const char *comm, char *label, size_t cap) {
    if (!comm || !*comm) return false;
    if (strstr(comm, "kwin_wayland")) snprintf(label, cap, "KWin Wayland");
    else if (strstr(comm, "kwin_x11")) snprintf(label, cap, "KWin X11");
    else if (strcasecmp(comm, "picom") == 0) snprintf(label, cap, "picom");
    else if (strcasecmp(comm, "compton") == 0) snprintf(label, cap, "Compton");
    else if (strcasecmp(comm, "xcompmgr") == 0) snprintf(label, cap, "xcompmgr");
    else if (strcasecmp(comm, "gnome-shell") == 0) snprintf(label, cap, "GNOME Shell");
    else if (strcasecmp(comm, "mutter") == 0) snprintf(label, cap, "Mutter");
    else if (strcasecmp(comm, "cinnamon") == 0) snprintf(label, cap, "Cinnamon");
    else if (strcasecmp(comm, "muffin") == 0) snprintf(label, cap, "Muffin");
    else if (strcasecmp(comm, "xfwm4") == 0) snprintf(label, cap, "Xfwm4");
    else if (strcasecmp(comm, "marco") == 0) snprintf(label, cap, "Marco");
    else if (strcasecmp(comm, "compiz") == 0) snprintf(label, cap, "Compiz");
    else if (strcasecmp(comm, "sway") == 0) snprintf(label, cap, "Sway");
    else if (strstr(comm, "Hyprland")) snprintf(label, cap, "Hyprland");
    else if (strcasecmp(comm, "weston") == 0) snprintf(label, cap, "Weston");
    else if (strstr(comm, "gamescope")) snprintf(label, cap, "gamescope");
    else return false;
    return true;
}

void desktop_parse_kms_state(const char *path, SessionInfo *si) {
    if(!path || !*path) return;
    FILE *f=fopen(path,"r");
    if(!f)return;
    char line[768];
    bool in_plane=false, plane_active=false;
    bool first_mode=true, first_alloc=true;
    while(fgets(line,sizeof(line),f)) {
        trim(line);
        if(strncmp(line,"plane[",6)==0) {
            in_plane=true; plane_active=false;
            continue;
        }
        if(strncmp(line,"crtc[",5)==0 || strncmp(line,"connector[",10)==0) {
            in_plane=false;
        }
        if(in_plane && strncmp(line,"crtc=",5)==0) {
            plane_active = strcmp(line+5,"(null)")!=0;
            if(plane_active) si->active_planes++;
        } else if(in_plane && plane_active && strncmp(line,"allocated by = ",15)==0) {
            const char *who=line+15;
            char who_short[64];
            snprintf(who_short,sizeof(who_short),"%.63s",who);
            if(!strstr(si->plane_allocators,who_short)) {
                if(!first_alloc) strncat(si->plane_allocators,", ",sizeof(si->plane_allocators)-strlen(si->plane_allocators)-1);
                strncat(si->plane_allocators,who_short,sizeof(si->plane_allocators)-strlen(si->plane_allocators)-1);
                first_alloc=false;
            }
        } else if(strncmp(line,"vrr_enabled=",12)==0 || strncmp(line,"vrr_enabled:",12)==0) {
            const char *v=line+12;
            while(*v==' ' || *v=='\t')v++;
            snprintf(si->vrr_state,sizeof(si->vrr_state),"%s (DRM CRTC)",(*v=='1')?"ENABLED":(*v=='0')?"DISABLED":"UNKNOWN");
        } else if(strncmp(line,"mode: \"",7)==0 && !strstr(line,"mode: \"\":")) {
            const char *q=line+7;
            const char *end=strchr(q,'"');
            if(end) {
                char name[80]={0};
                size_t n=(size_t)(end-q); if(n>=sizeof(name))n=sizeof(name)-1;
                memcpy(name,q,n);
                const char *colon=strchr(end,':');
                long refresh=-1;
                if(colon) refresh=strtol(colon+1,NULL,10);
                char one[96];
                if(refresh>1000) refresh=(refresh+500)/1000; /* DRM reports millihertz on some kernels */
                if(refresh>0) {
                    snprintf(one,sizeof(one),"%.70s@%ldHz",name,refresh);
                    if(!si->refresh[0]) snprintf(si->refresh,sizeof(si->refresh),"%ld Hz (DRM CRTC)",refresh);
                }
                else snprintf(one,sizeof(one),"%s",name);
                if(!strstr(si->kms_modes,one)) {
                    if(!first_mode)strncat(si->kms_modes,", ",sizeof(si->kms_modes)-strlen(si->kms_modes)-1);
                    strncat(si->kms_modes,one,sizeof(si->kms_modes)-strlen(si->kms_modes)-1);
                    first_mode=false;
                }
            }
        }
    }
    fclose(f);
    if(!si->kms_modes[0])snprintf(si->kms_modes,sizeof(si->kms_modes),"current KMS modes unavailable");
    if(!si->plane_allocators[0])snprintf(si->plane_allocators,sizeof(si->plane_allocators),"not reported");
}

static bool session_is_x11(const SessionInfo *si) {
    return strcasestr(si->session_type, "x11") != NULL;
}

static bool session_is_wayland(const SessionInfo *si) {
    return strcasestr(si->session_type, "wayland") != NULL;
}

static void set_compositor_from_pid(SessionInfo *si, pid_t pid, const char *comm, const char *label) {
    snprintf(si->compositor,sizeof(si->compositor),"%s",label && *label ? label : comm);
    si->compositor_pid=pid;
    if(!si->session_type[0]) desktop_read_env_value(pid,"XDG_SESSION_TYPE",si->session_type,sizeof(si->session_type));
    if(!si->desktop[0]) desktop_read_env_value(pid,"XDG_CURRENT_DESKTOP",si->desktop,sizeof(si->desktop));
    if(!si->display[0]) desktop_read_env_value(pid,"DISPLAY",si->display,sizeof(si->display));
    if(!si->wayland_display[0]) desktop_read_env_value(pid,"WAYLAND_DISPLAY",si->wayland_display,sizeof(si->wayland_display));
}

void desktop_sample_session(const Gpu *g, const Sysfs *s, SessionInfo *si, const ClientMonitor *cm, CompositorSource source) {
    memset(si,0,sizeof(*si));
    const char *e;
    if ((e=getenv("XDG_SESSION_TYPE"))) snprintf(si->session_type,sizeof(si->session_type),"%s",e);
    if ((e=getenv("XDG_CURRENT_DESKTOP"))) snprintf(si->desktop,sizeof(si->desktop),"%s",e);
    if ((e=getenv("DISPLAY"))) snprintf(si->display,sizeof(si->display),"%s",e);
    if ((e=getenv("WAYLAND_DISPLAY"))) snprintf(si->wayland_display,sizeof(si->wayland_display),"%s",e);

    uid_t wanted=(uid_t)-1;
    const char *su=getenv("SUDO_UID");
    if(su && *su) wanted=(uid_t)strtoul(su,NULL,10);

    /* First reuse the DRM client inventory. If picom/KWin/etc already owns a
       DRM client, that is stronger evidence than an unrelated /proc name scan. */
    if (cm && (source==COMP_AUTO || source==COMP_DRM)) {
        for (int i=0; i<cm->count && !si->compositor[0]; i++) {
            char label[64];
            if (desktop_compositor_process(cm->items[i].name,label,sizeof(label)))
                set_compositor_from_pid(si,cm->items[i].pid,cm->items[i].name,label);
        }
    }

    DIR *pd=(source==COMP_AUTO || source==COMP_PROCESS) ? opendir("/proc") : NULL;
    if(pd) {
        struct dirent *pe;
        while((pe=readdir(pd))) {
            if(!desktop_numeric_name(pe->d_name)) continue;
            pid_t pid=(pid_t)atoi(pe->d_name);
            char pp[64]; snprintf(pp,sizeof(pp),"/proc/%d",pid);
            struct stat st;
            if(stat(pp,&st)<0) continue;
            if(wanted!=(uid_t)-1 && st.st_uid!=wanted) continue;
            char comm[128];
            if(desktop_read_proc_comm(pid,comm,sizeof(comm))<0) continue;
            char label[64];
            if(!si->compositor[0] && desktop_compositor_process(comm,label,sizeof(label))) {
                set_compositor_from_pid(si,pid,comm,label);
            }
        }
        closedir(pd);
    }
    if(!si->session_type[0]) {
        if(si->wayland_display[0] || strstr(si->compositor,"wayland") || strstr(si->compositor,"Hypr")) snprintf(si->session_type,sizeof(si->session_type),"wayland");
        else if(si->display[0] || strstr(si->compositor,"x11")) snprintf(si->session_type,sizeof(si->session_type),"x11");
        else snprintf(si->session_type,sizeof(si->session_type),"unknown");
    }
    if (strcasecmp(si->session_type,"x11") == 0 && desktop_xlibre_installed())
        snprintf(si->session_type,sizeof(si->session_type),"X11Libre");
    else if (strcasecmp(si->session_type,"x11") == 0)
        snprintf(si->session_type,sizeof(si->session_type),"X11");
    else if (strcasecmp(si->session_type,"wayland") == 0)
        snprintf(si->session_type,sizeof(si->session_type),"Wayland");
    if(!si->desktop[0]) snprintf(si->desktop,sizeof(si->desktop),"unknown");
    if(source==COMP_OFF) snprintf(si->compositor,sizeof(si->compositor),"Detection disabled");
    else if(!si->compositor[0]) snprintf(si->compositor,sizeof(si->compositor),"unavailable via selected source");
    si->x11_present_possible = session_is_x11(si);
    si->wayland_present_possible = session_is_wayland(si);

    DIR *dd=opendir("/sys/class/drm");
    if(dd) {
        struct dirent *de;
        char prefix[64]; snprintf(prefix,sizeof(prefix),"%s-",g->card_name);
        bool first=true;
        while((de=readdir(dd)) && si->connected_outputs<MAX_CONNECTORS) {
            if(strncmp(de->d_name,prefix,strlen(prefix))!=0) continue;
            char p[512],status[64];
            snprintf(p,sizeof(p),"/sys/class/drm/%s/status",de->d_name);
            if(read_path_text(p,status,sizeof(status))<0 || strcmp(status,"connected")!=0) continue;
            si->connected_outputs++;
            snprintf(p,sizeof(p),"/sys/class/drm/%s/modes",de->d_name);
            char modes[256]="";
            read_path_text(p,modes,sizeof(modes));
            char *nl=strchr(modes,'\n'); if(nl)*nl=0;
            const char *conn=de->d_name+strlen(prefix);
            char one[96];
            snprintf(one,sizeof(one),"%.31s%s%.55s",conn,modes[0]?" ":"",modes);
            if(!first) strncat(si->connector_summary,", ",sizeof(si->connector_summary)-strlen(si->connector_summary)-1);
            strncat(si->connector_summary,one,sizeof(si->connector_summary)-strlen(si->connector_summary)-1);
            first=false;
        }
        closedir(dd);
    }
    if(!si->connector_summary[0]) snprintf(si->connector_summary,sizeof(si->connector_summary),"no connected outputs detected");
    desktop_parse_kms_state(s->drm_state_path,si);
}

/* Local Arch package metadata: versions and *package* build dates, never guessed
 * from process names. Non-Arch hosts legitimately show 'unavailable'. */
static bool pacman_package_info(const char *pkg, char *version, size_t vc, char *built, size_t bc) {
    DIR *d=opendir("/var/lib/pacman/local");
    if(!d) return false;
    char best[MAX_PATH_LEN]="";
    struct dirent *de;
    size_t n=strlen(pkg);
    while((de=readdir(d))) {
        if(strncmp(de->d_name,pkg,n)!=0 || de->d_name[n]!='-') continue;
        if(strcmp(de->d_name,best)>0) snprintf(best,sizeof(best),"%s",de->d_name);
    }
    closedir(d);
    if(!best[0]) return false;
    char path[MAX_PATH_LEN];
    if(snprintf(path,sizeof(path),"/var/lib/pacman/local/%s/desc",best)>=(int)sizeof(path)) return false;
    FILE *f=fopen(path,"r");
    if(!f) return false;
    char line[256];
    enum {NONE,VERSION_FIELD,BUILD_FIELD} mode=NONE;
    while(fgets(line,sizeof(line),f)) {
        trim(line);
        if(line[0]=='%') {
            mode=!strcmp(line,"%VERSION%")?VERSION_FIELD:!strcmp(line,"%BUILDDATE%")?BUILD_FIELD:NONE;
            continue;
        }
        if(!line[0]) continue;
        if(mode==VERSION_FIELD) text_copy(version,vc,line);
        else if(mode==BUILD_FIELD) {
            char *end=NULL;errno=0;
            long long timestamp=strtoll(line,&end,10);
            if(!errno && end && !*end && timestamp>0) {
                time_t ts=(time_t)timestamp;
                struct tm tm_value;
                if(gmtime_r(&ts,&tm_value)) strftime(built,bc,"%Y-%m-%d UTC",&tm_value);
            }
        }
        mode=NONE;
    }
    fclose(f);
    return version[0]!=0;
}
void desktop_session_package_info(SessionInfo *si, int vendor_release) {
    const char *xname=desktop_xlibre_installed()?"xlibre-xserver":"xorg-server";
    if(!pacman_package_info(xname,si->xserver_version,sizeof(si->xserver_version),si->xserver_built,sizeof(si->xserver_built)))
        snprintf(si->xserver_version,sizeof(si->xserver_version),"vendor release %d",vendor_release);
    const char *sess=strcasestr(si->desktop,"xfce")?"xfce4-session":
                     strcasestr(si->desktop,"kde")?"plasma-workspace":
                     strcasestr(si->desktop,"gnome")?"gnome-session":"unknown";
    if(strcmp(sess,"unknown")) {
        (void)pacman_package_info(sess,si->session_version,sizeof(si->session_version),si->session_built,sizeof(si->session_built));
        const char *desktop=strcasestr(si->desktop,"xfce")?"xfdesktop":
                             strcasestr(si->desktop,"kde")?"plasma-desktop":"gnome-shell";
        (void)pacman_package_info(desktop,si->desktop_version,sizeof(si->desktop_version),si->desktop_built,sizeof(si->desktop_built));
    }
    const char *wm=strcasestr(si->window_manager,"xfwm4")?"xfwm4":
                   strcasestr(si->window_manager,"openbox")?"openbox":
                   strcasestr(si->window_manager,"i3")?"i3-wm":
                   strcasestr(si->window_manager,"kwin")?"kwin-x11":"";
    if(*wm) (void)pacman_package_info(wm,si->wm_version,sizeof(si->wm_version),si->wm_built,sizeof(si->wm_built));
    const char *comp=strcasestr(si->compositor,"picom")?"picom":
                     strcasestr(si->compositor,"xfwm4")?"xfwm4":
                     strcasestr(si->compositor,"kwin")?"kwin-x11":"";
    if(*comp) (void)pacman_package_info(comp,si->compositor_version,sizeof(si->compositor_version),si->compositor_built,sizeof(si->compositor_built));
    if(!si->refresh[0]) snprintf(si->refresh,sizeof(si->refresh),"KMS mode not reported");
    /* Neither compositing nor Present support proves vsync/buffering is enabled. */
    snprintf(si->vsync_state,sizeof(si->vsync_state),"unverified (compositor / app-specific)");
    snprintf(si->buffer_state,sizeof(si->buffer_state),"unverified (driver / application-specific)");
    if(!si->vrr_state[0]) snprintf(si->vrr_state,sizeof(si->vrr_state),"unreported by DRM state");
}
