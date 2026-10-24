/* Rendering-only smoke harness: fake readings; no sysfs or GPU writes. */
int diagnostic_program_main(int argc, char **argv);
#define main diagnostic_program_main
#include "../src/amd-gpu-diagnostic.c"
#undef main

int main(int argc, char **argv) {
    static Gpu gpu;
    static Sysfs sysfs;
    static Target target;
    static Stats stats;
    static ClientMonitor clients;
    static PowerTelemetry power;
    static SystemTelemetry system;
    static SessionInfo session;
    static EventLog events;
    static TraceState trace;
    static TuningState tuning;
    snprintf(gpu.full_name,sizeof(gpu.full_name),"AMD Radeon RX 7900 XTX");
    snprintf(gpu.card_name,sizeof(gpu.card_name),"card1");
    snprintf(gpu.pci_addr,sizeof(gpu.pci_addr),"0000:09:00.0");
    snprintf(gpu.device_id,sizeof(gpu.device_id),"744c");
    gpu.vram_bytes=24ULL*1073741824ULL;
    sysfs.fd_policy_r=-1;
    snprintf(sysfs.temp_label[0],sizeof(sysfs.temp_label[0]),"GPU Edge");
    snprintf(sysfs.temp_label[1],sizeof(sysfs.temp_label[1]),"Hotspot");
    snprintf(sysfs.temp_label[2],sizeof(sysfs.temp_label[2]),"Memory");
    snprintf(sysfs.voltage_label,sizeof(sysfs.voltage_label),"GFX Voltage");
    for(int i=0;i<3;i++) { sysfs.temp_crit_mc[i]=90000;sysfs.temp_emergency_mc[i]=105000; }
    snprintf(target.policy,sizeof(target.policy),"profile_standard"); target.enabled=true;
    power.power_w=186.5;power.power_cap_w=355;power.power_cap_max_w=420;
    power.gpu_busy=58;power.mem_busy=34;power.vram_used=15800000000ULL;power.vram_usage_valid=true;
    power.pcie_link_valid=true;power.pcie_current_width=16;power.pcie_max_width=16;
    snprintf(power.pcie_current_speed,sizeof(power.pcie_current_speed),"16.0 GT/s PCIe");
    snprintf(power.pcie_max_speed,sizeof(power.pcie_max_speed),"16.0 GT/s PCIe");
    power.pcie_link_loaded_low=0;power.bar0_valid=true;power.bar0_bytes=32ULL*1073741824ULL;
    power.rebar_supported=true;power.gfxoff_enabled=1;power.amdgpu_dpm=-1;
    power.temps_c[0]=58.2;power.temps_c[1]=75.8;power.temps_c[2]=62.5;
    power.temp_c=75.8;power.fan_rpm=1240;power.fan_pct=40;
    power.voltage_v=0.91;power.current_a=15.5;
    snprintf(power.current_label,sizeof(power.current_label),"measured");
    power.gfx_hz=2200000000.0;power.mem_hz=2500000000.0;
    stats.loop_hz=200;stats.checks=238294;
    system.cpu_pct=23.4;system.cpu_threads=16;system.ram_total_bytes=34359738368ULL;
    system.ram_used_bytes=12345678900ULL; system.ram_pct=35.9;
    system.cpu_temp_c=48.8;system.cpu_tctl_c=50.2;system.cpu_temp_crit_mc=90000;
    snprintf(system.cpu_tctl_label,sizeof(system.cpu_tctl_label),"CPU Tctl (k10temp)");
    snprintf(system.cpu_temp_label,sizeof(system.cpu_temp_label),"CPU");
    system.cpu_cores.core_count=8;
    for(int i=0;i<8;i++) system.cpu_cores.load_pct[i]=(double)(i+1)*8;
    system.mb_temp_count=2;
    system.mb_temp_c[0]=39;system.mb_temp_c[1]=43;
    snprintf(system.mb_temp_labels[0],sizeof(system.mb_temp_labels[0]),"Board");
    snprintf(system.mb_temp_labels[1],sizeof(system.mb_temp_labels[1]),"Chipset");
    system.cpu_power_w=84;
    system.cpu_clock_count=16;system.cpu_clock_min_mhz=3200;
    system.cpu_clock_avg_mhz=4600;system.cpu_clock_max_mhz=5000;
    system.cpu_clock_cap_mhz=5050;system.cpu_boost=1;system.cpu_smt=1;
    snprintf(system.cpu_governor,sizeof(system.cpu_governor),"schedutil");
    system.tokens.has_data=true;system.tokens.tokens_per_sec=78.8;
    snprintf(system.tokens.source_label,sizeof(system.tokens.source_label),"llama.cpp");
    system.tokens.total_tokens=440221;
    system.ai_model_info_valid=true; system.ai_ctx_size=131072;
    snprintf(system.ai_cache_type_k,sizeof(system.ai_cache_type_k),"q8_0");
    snprintf(system.ai_cache_type_v,sizeof(system.ai_cache_type_v),"q8_0");
    system.ai_flash_attn=true;
    system.ai_prompt_cache.limit_valid=true;
    system.ai_prompt_cache.usage_valid=true;
    system.ai_prompt_cache.limit_mib=24576;
    system.ai_prompt_cache.used_mib=11776;
    system.ai_prompt_cache.entries=4;
    system.ai_prompt_cache.matches=6;
    system.ai_prompt_cache.save_attempts=8;
    system.ai_prompt_cache.evictions=2;
    system.ai_prompt_cache.evicted_mib=1024;
    system.ai_prompt_cache.rejected=1;
    snprintf(system.ai_model_activity,sizeof(system.ai_model_activity),"Recent decoding");
    snprintf(system.ai_log_source,sizeof(system.ai_log_source),"Bionic");
    snprintf(system.ai_log_path,sizeof(system.ai_log_path),"/tmp/mock-bionic.log");
    system.ai_log_event_count=3;
    for (int i=0;i<3;i++) {
        g_ai_log_events[i].task_id=830-i;g_ai_log_events[i].slot_id=i%2;
        snprintf(g_ai_log_events[i].time_str,sizeof(g_ai_log_events[i].time_str),"21:48:%02d",19-i*3);
        g_ai_log_events[i].prompt_tokens=416+i*33;
        g_ai_log_events[i].prompt_eval_ms=1932+i*150;
        g_ai_log_events[i].prompt_tps=215.3-i*12;
        g_ai_log_events[i].eval_tokens=150+i*32;
        g_ai_log_events[i].eval_ms=3000+i*350;
        g_ai_log_events[i].eval_tps=50-i*2;
        g_ai_log_events[i].has_prompt=true;
        g_ai_log_events[i].has_eval=true;
        g_ai_log_events[i].has_total=true;
    }
    for (int i=0;i<12;i++) {
        events.ring[i].kind[0]=0;
        snprintf(events.ring[i].kind,sizeof(events.ring[i].kind),"POWER STEP");
        snprintf(events.ring[i].detail,sizeof(events.ring[i].detail),"%d W -> %d W",130+i,135+i);
        snprintf(events.ring[i].actor,sizeof(events.ring[i].actor),"game");
        events.ring[i].actor_activity=12.3;
    }
    events.count=12; events.head=12; events.total=12;
    for(int i=0;i<3;i++) {
        system.top_procs[i].pid=2320+i;
        snprintf(system.top_procs[i].name,sizeof(system.top_procs[i].name),"llama-server-%d",i);
    }
    system.top_proc_count=3;
    clients.count=1;clients.items[0].pid=2345;
    snprintf(clients.items[0].name,sizeof(clients.items[0].name),"llama-server");
    clients.items[0].activity_pct=62.8;
    trace.available=true;
    tuning.armed=false;
    tuning.has_sclk=true;tuning.edit_sclk_max_mhz=2650;
    tuning.has_mclk=true;tuning.edit_mclk_max_mhz=2500;
    tuning.has_vdd_offset=true;tuning.edit_vdd_offset_mv=0;
    tuning.has_power_cap=true;tuning.edit_power_cap_w=350;
    tuning.has_fan=true;tuning.edit_fan_pwm=122;tuning.fan_pwm_max=255;
    tuning.fan_rpm=1240;
    tuning.default_sclk_max_mhz=2500;tuning.sclk_limit_min_mhz=1000;tuning.sclk_limit_max_mhz=3000;
    tuning.default_mclk_max_mhz=2400;tuning.mclk_limit_min_mhz=800;tuning.mclk_limit_max_mhz=2600;
    tuning.default_vdd_offset_mv=0;tuning.vdd_offset_min_mv=-50;tuning.vdd_offset_max_mv=50;
    tuning.default_power_cap_w=355;tuning.power_cap_min_w=100;tuning.power_cap_max_w=420;
    /* Explicitly exercise the beta settings/desktop pages without hardware writes. */
    g_gpu_inventory_count=2;
    g_gpu_inventory[0]=gpu; g_gpu_inventory[0].card_num=1;
    g_gpu_inventory[1]=gpu; g_gpu_inventory[1].card_num=2;
    snprintf(g_gpu_inventory[1].full_name,sizeof(g_gpu_inventory[1].full_name),"AMD Radeon RX 6800 XT");
    g_requested_gpu=2;
    snprintf(session.session_type,sizeof(session.session_type),"X11Libre");
    snprintf(session.desktop,sizeof(session.desktop),"XFCE");
    snprintf(session.session_version,sizeof(session.session_version),"4.20.1");
    snprintf(session.desktop_version,sizeof(session.desktop_version),"4.20.1");
    snprintf(session.xserver_vendor,sizeof(session.xserver_vendor),"XLibre");
    snprintf(session.xserver_version,sizeof(session.xserver_version),"25.1.0");
    snprintf(session.window_manager,sizeof(session.window_manager),"xfwm4");
    snprintf(session.wm_version,sizeof(session.wm_version),"4.20.0");
    snprintf(session.compositor,sizeof(session.compositor),"picom");
    snprintf(session.compositor_version,sizeof(session.compositor_version),"12.5");
    snprintf(session.session_built,sizeof(session.session_built),"2026-10-20 UTC");
    snprintf(session.xserver_built,sizeof(session.xserver_built),"2026-10-18 UTC");
    snprintf(session.desktop_built,sizeof(session.desktop_built),"2026-10-20 UTC");
    snprintf(session.wm_built,sizeof(session.wm_built),"2026-10-20 UTC");
    snprintf(session.compositor_built,sizeof(session.compositor_built),"2026-10-19 UTC");
    snprintf(session.refresh,sizeof(session.refresh),"144 Hz (DRM CRTC)");
    snprintf(session.kms_modes,sizeof(session.kms_modes),"2560x1440@144Hz");
    snprintf(session.vrr_state,sizeof(session.vrr_state),"ENABLED (DRM CRTC)");
    snprintf(session.vsync_state,sizeof(session.vsync_state),"unverified (compositor / app-specific)");
    snprintf(session.buffer_state,sizeof(session.buffer_state),"unverified (driver / application-specific)");
    snprintf(session.connector_summary,sizeof(session.connector_summary),"DP-1 2560x1440");
    session.connected_outputs=1;
    View view=VIEW_DASHBOARD;
    if(argc>1) {if(strcmp(argv[1],"gaming")==0)g_workflow=WORKFLOW_GAMING;
                 else if(strcmp(argv[1],"tuning")==0)view=VIEW_TUNING;
                 else if(strcmp(argv[1],"power")==0)view=VIEW_POWER;
                 else if(strcmp(argv[1],"help")==0)view=VIEW_HELP;
                 else if(strcmp(argv[1],"about")==0)view=VIEW_ABOUT;
                 else if(strcmp(argv[1],"events")==0)view=VIEW_EVENTS;
                 else if(strcmp(argv[1],"settings")==0)view=VIEW_SETTINGS;
                 else if(strcmp(argv[1],"desktop")==0)view=VIEW_DESKTOP;
                 else if(strcmp(argv[1],"clients")==0)view=VIEW_CLIENTS;}
    Gui ui; if(gui_init(&ui,&session)) return 1;
    if(argc>2) gui_scroll(&ui,view,atoi(argv[2]));
    gui_draw(&ui,&gpu,&sysfs,&target,&stats,5000,250000,false,true,&clients,
             &power,&system,&session,&events,&trace,&tuning,view,"");
    XSync(ui.dpy,False);
    if (getenv("GPU_PREVIEW_PAUSE")) sleep(3);
    else usleep(150000);
    gui_close(&ui);
    return 0;
}
