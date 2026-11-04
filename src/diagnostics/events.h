/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AMD_GPU_DIAG_EVENTS_H
#define AMD_GPU_DIAG_EVENTS_H
#include "types.h"
#include "../monitor/session.h"
/* The producer provides snapshots. This module owns the event ring and CSV. */
void event_log_add(EventLog *el, const char *kind, const char *detail,
                   const ClientMonitor *clients, const TraceState *trace);
const DiagEvent *event_last(const EventLog *events);
void toggle_event_logging(EventLog *events);
#endif
