/* SPDX-License-Identifier: GPL-3.0-or-later
 * X11 session identification and the application's window resources.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "window.h"
#include "draw.h"
#include "../monitor/desktop_probe.h"
#include "../core/util.h"

static int x11_window_pid(Display *dpy, Window w, pid_t *pid_out) {
    Atom pid_atom = XInternAtom(dpy, "_NET_WM_PID", True);
    if (pid_atom == None) return -1;
    Atom type = None;
    int format = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *data = NULL;
    int rc = XGetWindowProperty(dpy, w, pid_atom, 0, 1, False, XA_CARDINAL,
                                &type, &format, &nitems, &bytes_after, &data);
    if (rc != Success || !data || nitems < 1 || format != 32) {
        if (data) XFree(data);
        return -1;
    }
    unsigned long value = *(unsigned long *)data;
    XFree(data);
    *pid_out = (pid_t)value;
    return 0;
}

static int x11_window_name(Display *dpy, Window w, char *out, size_t cap) {
    Atom name_atom = XInternAtom(dpy, "_NET_WM_NAME", True);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", True);
    if (name_atom != None && utf8 != None) {
        Atom type = None;
        int format = 0;
        unsigned long nitems = 0, bytes_after = 0;
        unsigned char *data = NULL;
        if (XGetWindowProperty(dpy, w, name_atom, 0, 256, False, utf8,
                               &type, &format, &nitems, &bytes_after, &data) == Success &&
            data && nitems > 0) {
            size_t n = nitems < cap - 1 ? nitems : cap - 1;
            memcpy(out, data, n);
            out[n] = '\0';
            XFree(data);
            return 0;
        }
        if (data) XFree(data);
    }
    char *legacy = NULL;
    if (XFetchName(dpy, w, &legacy) && legacy) {
        snprintf(out, cap, "%s", legacy);
        XFree(legacy);
        return 0;
    }
    return -1;
}

void ui_window_probe(Gui *ui, SessionInfo *si, CompositorSource source) {
    const char *vendor = ServerVendor(ui->dpy);
    snprintf(si->xserver_vendor, sizeof(si->xserver_vendor), "%s", vendor ? vendor : "unknown");
    if (vendor && strcasestr(vendor, "xlibre"))
        snprintf(si->session_type, sizeof(si->session_type), "X11Libre");
    else if (desktop_xlibre_installed())
        snprintf(si->session_type, sizeof(si->session_type), "X11Libre");
    else
        snprintf(si->session_type, sizeof(si->session_type), "X11");
    snprintf(si->display, sizeof(si->display), "%s", DisplayString(ui->dpy));
    si->x11_present_possible = 1;
    si->wayland_present_possible = 0;

    char atom_name[64];
    snprintf(atom_name, sizeof(atom_name), "_NET_WM_CM_S%d", ui->screen);
    Atom cm = XInternAtom(ui->dpy, atom_name, True);
    if (cm != None && (source==COMP_AUTO || source==COMP_X11)) {
        Window owner = XGetSelectionOwner(ui->dpy, cm);
        if (owner != None) {
            pid_t pid = 0;
            char comm[128] = "";
            char label[64] = "";
            if (x11_window_pid(ui->dpy, owner, &pid) == 0 && desktop_read_proc_comm(pid, comm, sizeof(comm)) == 0) {
                if (!desktop_compositor_process(comm, label, sizeof(label))) text_copy(label, sizeof(label), comm);
                snprintf(si->compositor, sizeof(si->compositor), "%s", label);
                si->compositor_pid = pid;
            } else {
                char name[128] = "";
                if (x11_window_name(ui->dpy, owner, name, sizeof(name)) == 0)
                    text_copy(si->compositor, sizeof(si->compositor), name);
            }
        }
    }

    Atom wmcheck = XInternAtom(ui->dpy, "_NET_SUPPORTING_WM_CHECK", True);
    if (wmcheck != None) {
        Atom type = None;
        int format = 0;
        unsigned long nitems = 0, bytes_after = 0;
        unsigned char *data = NULL;
        if (XGetWindowProperty(ui->dpy, ui->root, wmcheck, 0, 1, False, XA_WINDOW,
                               &type, &format, &nitems, &bytes_after, &data) == Success &&
            data && nitems > 0) {
            Window w = *(Window *)data;
            char name[128] = "";
            if (x11_window_name(ui->dpy, w, name, sizeof(name)) == 0)
                text_copy(si->window_manager, sizeof(si->window_manager), name);
        }
        if (data) XFree(data);
    }
    if (!si->window_manager[0]) snprintf(si->window_manager, sizeof(si->window_manager), "unknown");
    /* Version/build-date filesystem discovery runs on the read-only
     * desktop worker. Never scan the pacman database in the watchdog loop. */
}

int ui_window_open(Gui *ui, SessionInfo *si, const char *version, CompositorSource source) {
    memset(ui, 0, sizeof(*ui));
    ui->dpy = XOpenDisplay(NULL);
    if (!ui->dpy) {
        fprintf(stderr, "Cannot open X11 display. This GUI is X11/XLibre-only.\n");
        fprintf(stderr, "When launching through sudo, preserve DISPLAY/XAUTHORITY, e.g. sudo -E ./amd-gpu-diagnostic\n");
        return -1;
    }
    ui->screen = DefaultScreen(ui->dpy);
    ui->root = RootWindow(ui->dpy, ui->screen);
    ui->width = 840;
    ui->height = 660;
    ui->bg = gui_color(ui, "#101214", BlackPixel(ui->dpy, ui->screen));
    ui->panel = gui_color(ui, "#1a1d21", ui->bg);
    ui->panel_alt = gui_color(ui, "#262a2f", ui->panel);
    ui->fg = gui_color(ui, "#e5e7eb", WhitePixel(ui->dpy, ui->screen));
    ui->dim = gui_color(ui, "#7f8792", ui->fg);
    ui->accent = gui_color(ui, "#4ea1ff", ui->fg);
    ui->good = gui_color(ui, "#68d391", ui->fg);
    ui->warn = gui_color(ui, "#f6c85f", ui->fg);
    ui->bad = gui_color(ui, "#ff6b6b", ui->fg);
    ui->text_dark = gui_color(ui, "#1a1d21", ui->bg);

    ui->win = XCreateSimpleWindow(ui->dpy, ui->root, 80, 80, (unsigned)ui->width, (unsigned)ui->height,
                                  0, ui->dim, ui->bg);
    char title[128];
    snprintf(title, sizeof(title), "AMD GPU Performance and Diagnostic Tool v%s", version);
    XStoreName(ui->dpy, ui->win, title);
    XSelectInput(ui->dpy, ui->win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    ui->wm_delete = XInternAtom(ui->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ui->dpy, ui->win, &ui->wm_delete, 1);
    XSizeHints hints = {0};
    hints.flags = PMinSize;
    hints.min_width = 780;
    hints.min_height = 610;
    XSetWMNormalHints(ui->dpy, ui->win, &hints);

    ui->gc = XCreateGC(ui->dpy, ui->win, 0, NULL);
    ui->font = XLoadQueryFont(ui->dpy, "8x13");
    if (!ui->font) ui->font = XLoadQueryFont(ui->dpy, "9x15");
    if (!ui->font) ui->font = XLoadQueryFont(ui->dpy, "fixed");
    ui->font_bold = XLoadQueryFont(ui->dpy, "8x13bold");
    if (!ui->font_bold) ui->font_bold = XLoadQueryFont(ui->dpy, "9x15bold");
    if (!ui->font_bold) ui->font_bold = ui->font;
    /* One font step larger for the GPU name; keep other UI typography compact. */
    ui->font_title = XLoadQueryFont(ui->dpy, "9x15bold");
    if (!ui->font_title) ui->font_title = XLoadQueryFont(ui->dpy, "9x15");
    if (!ui->font_title) ui->font_title = ui->font_bold;
    gui_set_font(ui, false);
    XMapWindow(ui->dpy, ui->win);
    XFlush(ui->dpy);
    ui->dirty = true;
    ui_window_probe(ui, si, source);
    return 0;
}

void ui_window_close(Gui *ui) {
    if (!ui->dpy) return;
    if (ui->font_title && ui->font_title != ui->font_bold && ui->font_title != ui->font)
        XFreeFont(ui->dpy, ui->font_title);
    if (ui->font_bold && ui->font_bold != ui->font) XFreeFont(ui->dpy, ui->font_bold);
    if (ui->font) XFreeFont(ui->dpy, ui->font);
    if (ui->gc) XFreeGC(ui->dpy, ui->gc);
    if (ui->win) XDestroyWindow(ui->dpy, ui->win);
    XCloseDisplay(ui->dpy);
    memset(ui, 0, sizeof(*ui));
}
