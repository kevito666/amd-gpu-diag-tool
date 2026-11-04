/* SPDX-License-Identifier: GPL-3.0-or-later
 * Selected AMDGPU device descriptor lifecycle. The caller owns each open Sysfs.
 */
#ifndef AMD_GPU_DIAG_GPU_DEVICE_H
#define AMD_GPU_DIAG_GPU_DEVICE_H
#include "readback.h"
int sysfs_open(const Gpu *gpu, Sysfs *sys);
void sysfs_close(Sysfs *sys);
#endif
