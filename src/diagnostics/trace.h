/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AMD_GPU_DIAG_TRACE_H
#define AMD_GPU_DIAG_TRACE_H
#include "types.h"
/* Tracefs instance lifecycle. Never modifies the global tracefs root. */
void trace_discover(TraceState *trace);
int trace_start(TraceState *trace);
void trace_stop(TraceState *trace);
void trace_pump(TraceState *trace);
#endif
