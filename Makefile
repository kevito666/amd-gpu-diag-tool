CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2 -std=gnu11 -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wundef
LDFLAGS ?=
LDLIBS ?= -lX11 -lm -lcurl -pthread
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

TARGET := amd-gpu-diagnostic
MODULE_SRC := src/core/util.c src/monitor/nvme.c src/ai/maya_log.c src/ai/log_parser.c src/config/settings.c src/monitor/cpu.c src/monitor/sensors.c src/ai/http.c src/ai/llama_process.c src/ai/log_files.c src/monitor/desktop_probe.c src/monitor/session.c src/monitor/worker.c src/monitor/clients.c src/monitor/scan_worker.c src/gpu/readback.c src/gpu/device.c src/gpu/writes.c src/gpu/policy.c src/gpu/tuning.c src/ui/draw.c src/ui/layout.c src/ui/window.c src/ui/status.c src/ui/ai_dashboard.c src/ui/gaming.c src/ui/pages_monitor.c src/ui/pages_tuning.c src/ui/pages_settings.c src/ui/chrome.c src/ui/input.c src/diagnostics/events.c src/diagnostics/trace.c src/app/poll_timing.c
SRC := src/amd-gpu-diagnostic.c src/gpu_specs.c $(MODULE_SRC)
OBJ := $(SRC:.c=.o)
DEP := $(OBJ:.o=.d)
TEST_SRC := src/gpu_specs.c $(MODULE_SRC)
# Fixtures include the large legacy application translation unit. Use strict
# diagnostics without optimizing it six separate times; `make` remains -O2.
TEST_CFLAGS := $(filter-out -O%,$(CFLAGS)) -O0 -Werror

.PHONY: all clean check check-ui check-parser check-pcie check-config check-audit check-maya-nvme check-modules check-settings-module check-ai-module check-cpu-module check-sources-module check-session-module check-readback-module check-control-module check-ui-layout-module check-ui-input-module check-dashboard-module check-pages-module check-poll-timing-module check-events-module check-monitor-worker-module check-discovery-worker-module check-clients-module check-worker-lifecycle install uninstall

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

# Object dependencies prevent full recompilation when only one module changes.
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEP)

check:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-$(TARGET) $(SRC) $(LDFLAGS) $(LDLIBS)
	./.check-$(TARGET) --version | grep -Fqx "$$(cat VERSION)"
	./.check-$(TARGET) --help >/dev/null
	rm -f .check-$(TARGET)

# Optional hardware-free screenshot/render smoke test; needs xvfb-run.
check-ui:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-ui tests/ui_preview.c $(TEST_SRC) $(LDFLAGS) $(LDLIBS)
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui ai
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui gaming
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui tuning 999
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui tuning-disabled 999
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui settings 999
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui reference 999
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui navigation
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui desktop 999
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui events 999
	rm -f .check-ui

check-parser:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-ai-parser tests/test_ai_log.c $(TEST_SRC) $(LDFLAGS) $(LDLIBS)
	./.check-ai-parser
	rm -f .check-ai-parser

check-config:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -DAMD_GPU_DIAG_TEST=1 -o .check-config tests/test_config.c $(TEST_SRC) $(LDFLAGS) $(LDLIBS)
	./.check-config
	rm -f .check-config

check-pcie:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-pcie tests/test_pcie.c src/gpu/readback.c src/core/util.c $(LDFLAGS) -lm
	./.check-pcie
	rm -f .check-pcie

check-audit:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -DAMD_GPU_DIAG_TEST=1 -o .check-audit tests/test_audit.c $(TEST_SRC) $(LDFLAGS) $(LDLIBS)
	./.check-audit
	rm -f .check-audit

check-maya-nvme:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-maya-nvme tests/test_maya_nvme.c $(TEST_SRC) $(LDFLAGS) $(LDLIBS)
	./.check-maya-nvme
	rm -f .check-maya-nvme

# Isolated module API tests (no inclusion of the application's main C file).
check-modules:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-modules tests/test_modules.c src/core/util.c src/monitor/nvme.c src/ai/maya_log.c $(LDFLAGS) -lm
	./.check-modules
	rm -f .check-modules

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJ) $(DEP) .check-modules .check-$(TARGET) .check-ui .check-ai-parser .check-pcie .check-config .check-audit .check-maya-nvme .check-settings-module .check-ai-module .check-cpu-module .check-sources-module .check-session-module .check-readback-module .check-control-module .check-ui-layout-module .check-ui-input-module .check-dashboard-module .check-pages-module .check-poll-timing-module .check-events-module .check-monitor-worker-module .check-discovery-worker-module .check-clients-module .check-worker-lifecycle

# Focused tests compile the new modules without pulling in the X11 GUI.
check-settings-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -DAMD_GPU_DIAG_TEST=1 -o .check-settings-module tests/test_settings_module.c src/config/settings.c src/core/util.c
	./.check-settings-module
	rm -f .check-settings-module

check-ai-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-ai-module tests/test_log_parser_module.c src/ai/log_parser.c src/core/util.c -lm
	./.check-ai-module
	rm -f .check-ai-module

check-cpu-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-cpu-module tests/test_cpu_module.c src/monitor/cpu.c src/core/util.c -lm
	./.check-cpu-module
	rm -f .check-cpu-module

# New dev.3 module contract tests, linked without the monolithic GUI source.
check-sources-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-sources-module tests/test_sources_module.c src/ai/http.c src/ai/llama_process.c src/ai/log_parser.c src/ai/log_files.c src/monitor/desktop_probe.c src/monitor/sensors.c src/core/util.c src/config/settings.c $(LDFLAGS) -lm -lcurl
	./.check-sources-module
	rm -f .check-sources-module

# X11-free desktop/session and KMS parsing fixture; no root/GPU writes.
check-session-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-session-module tests/test_session_module.c src/monitor/session.c src/monitor/desktop_probe.c src/core/util.c $(LDFLAGS) -lm
	./.check-session-module
	rm -f .check-session-module

# Independent GPU hwmon readback contract with temporary synthetic files.
check-readback-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-readback-module tests/test_readback_module.c src/gpu/readback.c src/core/util.c $(LDFLAGS) -lm
	./.check-readback-module
	rm -f .check-readback-module

# GPU driver-write fault fixtures, without linking the GUI or changing hardware.
check-control-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-control-module tests/test_control_module.c src/gpu/device.c src/gpu/policy.c src/gpu/tuning.c src/gpu/writes.c src/core/util.c $(LDFLAGS) -lm
	./.check-control-module
	rm -f .check-control-module

# Pure UI geometry tests; no X server or graphics context needed.
check-ui-layout-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-ui-layout-module tests/test_ui_layout_module.c src/ui/layout.c src/gpu_specs.c
	./.check-ui-layout-module
	rm -f .check-ui-layout-module

# Renderer contract links only UI modules; no application controller needed.
check-dashboard-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-dashboard-module tests/test_dashboard_module.c src/ui/ai_dashboard.c src/ui/gaming.c src/ui/status.c src/ui/draw.c src/ui/layout.c src/gpu_specs.c $(LDFLAGS) $(LDLIBS)
	./.check-dashboard-module
	rm -f .check-dashboard-module

# Standalone read-only page API contract without the main application source.
check-pages-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-pages-module tests/test_pages_module.c src/ui/pages_monitor.c src/ui/pages_tuning.c src/ui/pages_settings.c src/ui/draw.c src/ui/layout.c src/ui/status.c src/gpu_specs.c src/gpu/tuning.c src/gpu/writes.c src/gpu/policy.c src/core/util.c $(LDFLAGS) $(LDLIBS)
	./.check-pages-module
	rm -f .check-pages-module

# Time-only monitoring statistics: no GPU hardware, X11, or root required.
check-poll-timing-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-poll-timing-module tests/test_poll_timing_module.c src/app/poll_timing.c
	./.check-poll-timing-module
	rm -f .check-poll-timing-module

# Independent event-ring test; no GUI, sysfs writes or tracefs privileges.
check-events-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-events-module tests/test_events_module.c src/diagnostics/events.c src/core/util.c
	./.check-events-module
	rm -f .check-events-module

# X11 keyboard translation and bounded edit helpers; no running X server needed.
check-ui-input-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-ui-input-module tests/test_ui_input_module.c src/ui/input.c $(LDFLAGS) -lX11
	./.check-ui-input-module
	rm -f .check-ui-input-module

# Threaded telemetry handoff test: no GUI, GPU sysfs, or real network access.
check-monitor-worker-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-monitor-worker-module tests/test_monitor_worker.c src/monitor/worker.c $(LDFLAGS) -pthread
	./.check-monitor-worker-module
	rm -f .check-monitor-worker-module

# Bounded GPU-client/session worker; fake sampler avoids GPU, X11 and /proc access.
check-discovery-worker-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-discovery-worker-module tests/test_discovery_worker.c src/monitor/scan_worker.c $(LDFLAGS) -pthread
	./.check-discovery-worker-module
	rm -f .check-discovery-worker-module

# Independent DRM fdinfo fixture; no running /proc scanner, GPU or X11.
check-clients-module:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-clients-module tests/test_clients_module.c src/monitor/clients.c src/monitor/desktop_probe.c src/monitor/session.c src/core/util.c $(LDFLAGS) -lm
	./.check-clients-module
	rm -f .check-clients-module

# Deterministic worker stop/restart fixture; holds a callback while shutdown
# begins, ensuring queued samples are cancelled without touching GPU sysfs.
check-worker-lifecycle:
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -o .check-worker-lifecycle tests/test_worker_lifecycle.c src/monitor/worker.c src/monitor/scan_worker.c $(LDFLAGS) -pthread
	./.check-worker-lifecycle
	rm -f .check-worker-lifecycle
