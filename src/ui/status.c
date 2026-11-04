/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include "status.h"
unsigned long tune_zone_color(Gui *ui, TuneZone zone) {
    switch (zone) {
        case TUNE_ZONE_SAFE: return ui->good;
        case TUNE_ZONE_WARNING: return ui->warn;
        case TUNE_ZONE_DANGER: return ui->bad;
        case TUNE_ZONE_UNAVAILABLE: default: return ui->dim;
    }
}

TuneZone temperature_zone(double c, long crit_mc, long emergency_mc) {
    if (c < 0.0) return TUNE_ZONE_UNAVAILABLE;
    double crit = crit_mc > 0 ? crit_mc / 1000.0 : 90.0;
    double emergency = emergency_mc > 0 ? emergency_mc / 1000.0 : fmax(crit + 10.0, 105.0);
    if (c >= emergency || c >= 100.0) return TUNE_ZONE_DANGER;
    if (c >= crit || c >= 85.0) return TUNE_ZONE_WARNING;
    return TUNE_ZONE_SAFE;
}
