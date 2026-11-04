/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only thermal status palette and thresholds for all UI pages.
 */
#ifndef AMD_GPU_DIAG_UI_STATUS_H
#define AMD_GPU_DIAG_UI_STATUS_H
#include "types.h"
#include "../gpu/tuning.h"
unsigned long tune_zone_color(Gui *ui, TuneZone zone);
TuneZone temperature_zone(double c, long crit_mc, long emergency_mc);
#endif
