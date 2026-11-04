/* SPDX-License-Identifier: GPL-3.0-or-later
 * Synthetic AMDGPU hwmon readback contract. No GPU sysfs writes.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "../src/gpu/readback.h"

static int fixture(const char *dir, const char *leaf, const char *value) {
    char path[512];
    assert(snprintf(path,sizeof(path),"%s/%s",dir,leaf)<(int)sizeof(path));
    FILE *f=fopen(path,"w");
    assert(f && fputs(value,f)>=0 && fclose(f)==0);
    int fd=open(path,O_RDONLY | O_CLOEXEC);
    assert(fd>=0);
    return fd;
}

int main(void) {
    char tmp[]="/tmp/amdgpu-readback-XXXXXX";
    char *root=mkdtemp(tmp);
    assert(root);
    Sysfs sys={0};
    int *fds[]={&sys.fd_busy,&sys.fd_mem_busy,&sys.fd_power,&sys.fd_power_cap_r,
        &sys.fd_power_cap_min,&sys.fd_power_cap_max,&sys.fd_voltage,&sys.fd_current,
        &sys.fd_fan_rpm,&sys.fd_pwm,&sys.fd_pwm_max,&sys.fd_freq1,&sys.fd_freq2,
        &sys.fd_vram_used,&sys.fd_power_profile,&sys.fd_pcie_bw};
    for(size_t i=0;i<sizeof(fds)/sizeof(fds[0]);i++) *fds[i]=-1;
    for(int i=0;i<MAX_GPU_TEMPS;i++) sys.fd_temp[i]=-1;
    sys.fd_busy=fixture(root,"gpu_busy","75\n");
    sys.fd_mem_busy=fixture(root,"mem_busy","40\n");
    sys.fd_power=fixture(root,"power","135000000\n");
    sys.fd_voltage=fixture(root,"voltage","1050\n");
    sys.fd_temp[0]=fixture(root,"edge","62000\n");
    sys.fd_temp[1]=fixture(root,"junction","77000\n");
    sys.fd_fan_rpm=fixture(root,"fan","1450\n");
    sys.fd_pwm=fixture(root,"pwm","128\n");
    sys.fd_pwm_max=fixture(root,"pwm_max","255\n");
    sys.temp_count=2;
    PowerTelemetry power={0};
    gpu_sample_power(&sys,&power);
    assert(power.gpu_busy==75 && power.mem_busy==40);
    assert(power.temp_count==2 && power.temps_c[0]==62.0 && power.temps_c[1]==77.0);
    assert(fabs(power.power_w-135.0)<0.01 && fabs(power.voltage_v-1.05)<0.01);
    assert(power.current_is_estimate && fabs(power.current_a-11.25)<0.01);
    assert(power.fan_rpm==1450 && power.fan_pct>50.0 && power.fan_pct<51.0);
    assert(!power.pcie_link_valid && !power.bar0_valid);
    for(size_t i=0;i<sizeof(fds)/sizeof(fds[0]);i++) {
        if(*fds[i]>=0) assert(close(*fds[i])==0);
    }
    for(int i=0;i<MAX_GPU_TEMPS;i++) if(sys.fd_temp[i]>=0) assert(close(sys.fd_temp[i])==0);
    const char *leaves[]={"gpu_busy","mem_busy","power","voltage","edge","junction","fan","pwm","pwm_max"};
    char path[512];
    for(size_t i=0;i<sizeof(leaves)/sizeof(leaves[0]);i++) {
        assert(snprintf(path,sizeof(path),"%s/%s",root,leaves[i])<(int)sizeof(path));
        assert(unlink(path)==0);
    }
    assert(rmdir(root)==0);
    puts("GPU readback module PASS: hwmon sensors, current estimate, missing PCIe");
    return 0;
}
