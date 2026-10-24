CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2 -std=gnu11 -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wundef
LDFLAGS ?=
LDLIBS ?= -lX11 -lm -lcurl
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

TARGET := amd-gpu-diagnostic
SRC := src/amd-gpu-diagnostic.c src/gpu_specs.c

.PHONY: all clean check check-ui check-parser check-pcie check-config check-audit check-maya-nvme install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

check:
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror -o .check-$(TARGET) $(SRC) $(LDFLAGS) $(LDLIBS)
	./.check-$(TARGET) --version | grep -qx '0.7.0b'
	./.check-$(TARGET) --help >/dev/null
	rm -f .check-$(TARGET)

# Optional hardware-free screenshot/render smoke test; needs xvfb-run.
check-ui:
	$(CC) $(CPPFLAGS) $(CFLAGS) -O0 -Werror -o .check-ui tests/ui_preview.c src/gpu_specs.c $(LDFLAGS) $(LDLIBS)
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui ai
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui gaming
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui tuning 999
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui settings 999
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui desktop 999
	xvfb-run -a -s "-screen 0 1280x900x24" ./.check-ui events 999
	rm -f .check-ui

check-parser:
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror -o .check-ai-parser tests/test_ai_log.c src/gpu_specs.c $(LDFLAGS) $(LDLIBS)
	./.check-ai-parser
	rm -f .check-ai-parser

check-config:
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror -o .check-config tests/test_config.c src/gpu_specs.c $(LDFLAGS) $(LDLIBS)
	./.check-config
	rm -f .check-config

check-pcie:
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror -o .check-pcie tests/test_pcie.c src/gpu_specs.c $(LDFLAGS) $(LDLIBS)
	./.check-pcie
	rm -f .check-pcie

check-audit:
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror -o .check-audit tests/test_audit.c src/gpu_specs.c $(LDFLAGS) $(LDLIBS)
	./.check-audit
	rm -f .check-audit

check-maya-nvme:
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror -o .check-maya-nvme tests/test_maya_nvme.c src/gpu_specs.c $(LDFLAGS) $(LDLIBS)
	./.check-maya-nvme
	rm -f .check-maya-nvme

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) .check-$(TARGET) .check-ui .check-ai-parser .check-pcie .check-config .check-audit .check-maya-nvme
