/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AMD_GPU_DIAG_CLIENTS_H
#define AMD_GPU_DIAG_CLIENTS_H
#include "session.h"
/* Read-only /proc fdinfo scan; m carries previous counters for delta rates. */
/* 1=matching valid client, 0=unrelated, -1=I/O error. */
int client_monitor_parse_fdinfo(const char *path, const char *pci, GpuClient *out);
void client_monitor_sample(const Gpu *gpu, ClientMonitor *monitor);
#endif
