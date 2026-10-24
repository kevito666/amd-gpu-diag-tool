/* Hardware-free tests for negotiated PCIe link and active BAR aperture.
 * Never touches the host's real PCIe sysfs or GPU controls. */
int diagnostic_program_main(int argc, char **argv);
#define main diagnostic_program_main
#include "../src/amd-gpu-diagnostic.c"
#undef main

#include <assert.h>

static void write_fixture(const char *root, const char *name, const char *data) {
    char path[MAX_PATH_LEN];
    assert(path_join(path, sizeof(path), root, name));
    FILE *f=fopen(path,"w");
    assert(f);
    assert(fputs(data,f)>=0);
    assert(fclose(f)==0);
}

int main(void) {
    char tmp[]="/tmp/amdgpu-pcie-XXXXXX";
    char *dir=mkdtemp(tmp);
    assert(dir);
    write_fixture(dir,"current_link_speed","8.0 GT/s PCIe\n");
    write_fixture(dir,"max_link_speed","16.0 GT/s PCIe\n");
    write_fixture(dir,"current_link_width","8\n");
    write_fixture(dir,"max_link_width","16\n");
    write_fixture(dir,"resource","0000000100000000 00000008ffffffff 0000000000000200\n");
    write_fixture(dir,"resource0_resize","00000000010001c0\n");
    Sysfs sys={0};
    PowerTelemetry power={0};
    text_copy(sys.gpu_base,sizeof(sys.gpu_base),dir);
    power.gpu_busy=75;
    sample_pcie_link(&sys,&power);
    assert(power.pcie_link_valid);
    assert(power.pcie_current_width==8 && power.pcie_max_width==16);
    assert(power.bar0_valid && power.bar0_bytes == 32ULL*1073741824ULL);
    assert(power.rebar_supported);
    assert(pcie_downshift_under_load(&power)==1);
    power.gpu_busy=10; /* idle downshift should not be flagged as bottleneck */
    assert(pcie_downshift_under_load(&power)==0);

    write_fixture(dir,"current_link_speed","16.0 GT/s PCIe\n");
    write_fixture(dir,"current_link_width","16\n");
    power.gpu_busy=75;
    sample_pcie_link(&sys,&power);
    assert(pcie_downshift_under_load(&power)==0);

    char path[MAX_PATH_LEN];
    const char *leaves[]={"current_link_speed","max_link_speed","current_link_width",
                          "max_link_width","resource","resource0_resize"};
    for (size_t i=0;i<sizeof(leaves)/sizeof(leaves[0]);i++) {
        assert(path_join(path,sizeof(path),dir,leaves[i]));
        assert(unlink(path)==0);
    }
    sample_pcie_link(&sys,&power);
    assert(!power.pcie_link_valid && !power.bar0_valid && !power.rebar_supported);
    assert(pcie_downshift_under_load(&power)==-1);
    assert(rmdir(dir)==0);
    puts("PCIe/BAR monitor PASS: actual aperture, width/speed, idle downshift, and unknown-state reset");
    return 0;
}
